#include "forge/string.h"
#include "forge_web.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PM_HASH_MAX_BYTES (512ULL * 1024ULL * 1024ULL)
#define PM_HASH_MAX_ENTRIES 100000ULL
#define PM_HASH_MAX_NAME_BYTES (16ULL * 1024ULL * 1024ULL)
#define PM_HASH_MAX_DEPTH 64

const char *pm_executable(const char *command) {
  char candidate[4096], resolved[4096];
  if (!command || !*command)
    return "";
  if (strchr(command, '/'))
    return realpath(command, resolved) && access(resolved, X_OK) == 0
               ? fr_str_concat(resolved, "") : "";
  const char *paths = getenv("PATH");
  if (!paths)
    return "";
  for (const char *start = paths;;) {
    const char *end = strchr(start, ':');
    size_t length = end ? (size_t)(end - start) : strlen(start);
    if (length < sizeof candidate &&
        snprintf(candidate, sizeof candidate, "%.*s/%s",
                 length ? (int)length : 1, length ? start : ".", command) < (int)sizeof candidate &&
        access(candidate, X_OK) == 0 && realpath(candidate, resolved))
      return fr_str_concat(resolved, "");
    if (!end)
      return "";
    start = end + 1;
  }
}

struct pm_hash_state {
  EVP_MD_CTX *digest;
  int64_t excludes;
  int64_t suffixes;
  uint64_t bytes;
  uint64_t entries;
  uint64_t name_bytes;
};

static int pm_hash_update(struct pm_hash_state *state, const void *data,
                          size_t size) {
  return EVP_DigestUpdate(state->digest, data, size) == 1;
}

static int pm_hash_text(struct pm_hash_state *state, const char *text) {
  uint64_t size = (uint64_t)strlen(text);
  return pm_hash_update(state, &size, sizeof size) &&
         pm_hash_update(state, text, (size_t)size);
}

static int pm_hash_list_contains(int64_t values, const char *text) {
  int64_t count = fw_count(values);
  for (int64_t i = 0; i < count; i++)
    if (strcmp(fw_text(fw_at(values, i)), text) == 0)
      return 1;
  return 0;
}

static int pm_hash_selected(struct pm_hash_state *state, const char *name) {
  int64_t count = fw_count(state->suffixes);
  if (count == 0)
    return 1;
  size_t name_size = strlen(name);
  for (int64_t i = 0; i < count; i++) {
    const char *suffix = fw_text(fw_at(state->suffixes, i));
    size_t suffix_size = strlen(suffix);
    if (suffix_size <= name_size &&
        strcmp(name + name_size - suffix_size, suffix) == 0)
      return 1;
  }
  return 0;
}

static int pm_name_compare(const void *left, const void *right) {
  return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static void pm_free_names(char **names, size_t count) {
  for (size_t i = 0; i < count; i++)
    free(names[i]);
  free(names);
}

static int pm_hash_path(struct pm_hash_state *state, const char *path,
                        const char *relative, unsigned depth) {
  struct stat metadata;
  if (depth > PM_HASH_MAX_DEPTH || lstat(path, &metadata) != 0 ||
      S_ISLNK(metadata.st_mode))
    return 0;
  if (++state->entries > PM_HASH_MAX_ENTRIES)
    return 0;
  if (S_ISREG(metadata.st_mode)) {
    const char *name = strrchr(relative, '/');
    name = name ? name + 1 : relative;
    if (!pm_hash_selected(state, name))
      return 1;
    if ((uint64_t)metadata.st_size > PM_HASH_MAX_BYTES - state->bytes ||
        !pm_hash_text(state, "file") || !pm_hash_text(state, relative))
      return 0;
    uint32_t permissions = (uint32_t)(metadata.st_mode & 07777);
    if (!pm_hash_update(state, &permissions, sizeof permissions))
      return 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
      return 0;
    unsigned char buffer[65536];
    int ok = 1;
    for (;;) {
      ssize_t got = read(fd, buffer, sizeof buffer);
      if (got < 0 && errno == EINTR)
        continue;
      if (got < 0) {
        ok = 0;
        break;
      }
      if (got == 0)
        break;
      if ((uint64_t)got > PM_HASH_MAX_BYTES - state->bytes) {
        ok = 0;
        break;
      }
      state->bytes += (uint64_t)got;
      if (!pm_hash_update(state, buffer, (size_t)got)) {
        ok = 0;
        break;
      }
    }
    if (close(fd) != 0)
      ok = 0;
    return ok;
  }
  if (!S_ISDIR(metadata.st_mode))
    return 0;
  DIR *directory = opendir(path);
  if (!directory)
    return 0;
  char **names = NULL;
  size_t count = 0, capacity = 0;
  int ok = 1;
  errno = 0;
  for (struct dirent *entry = readdir(directory); entry;
       entry = readdir(directory)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
        pm_hash_list_contains(state->excludes, entry->d_name))
      continue;
    size_t size = strlen(entry->d_name) + 1;
    if (state->name_bytes + size > PM_HASH_MAX_NAME_BYTES) {
      ok = 0;
      break;
    }
    if (count == capacity) {
      size_t next = capacity ? capacity * 2 : 32;
      char **grown = realloc(names, next * sizeof(*names));
      if (!grown) {
        ok = 0;
        break;
      }
      names = grown;
      capacity = next;
    }
    names[count] = strdup(entry->d_name);
    if (!names[count]) {
      ok = 0;
      break;
    }
    state->name_bytes += size;
    count++;
  }
  if (errno != 0 || closedir(directory) != 0)
    ok = 0;
  if (ok)
    qsort(names, count, sizeof(*names), pm_name_compare);
  for (size_t i = 0; ok && i < count; i++) {
    char child[4096], child_relative[4096];
    int path_size = snprintf(child, sizeof child, "%s/%s", path, names[i]);
    int relative_size = snprintf(child_relative, sizeof child_relative, "%s/%s",
                                 relative, names[i]);
    if (path_size < 0 || path_size >= (int)sizeof child || relative_size < 0 ||
        relative_size >= (int)sizeof child_relative ||
        !pm_hash_path(state, child, child_relative, depth + 1))
      ok = 0;
  }
  pm_free_names(names, count);
  return ok;
}

const char *pm_sha256(const char *text) {
  EVP_MD_CTX *digest = EVP_MD_CTX_new();
  unsigned char bytes[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  char hex[EVP_MAX_MD_SIZE * 2 + 1];
  if (!digest || EVP_DigestInit_ex(digest, EVP_sha256(), NULL) != 1 ||
      EVP_DigestUpdate(digest, text, strlen(text)) != 1 ||
      EVP_DigestFinal_ex(digest, bytes, &size) != 1) {
    EVP_MD_CTX_free(digest);
    return "";
  }
  EVP_MD_CTX_free(digest);
  for (unsigned int i = 0; i < size; i++)
    snprintf(hex + i * 2, 3, "%02x", bytes[i]);
  hex[size * 2] = 0;
  return fr_str_concat(hex, "");
}

const char *pm_hash(int64_t paths, int64_t excludes, int64_t suffixes) {
  int64_t count = fw_count(paths);
  if (count <= 0 || count > 256 || fw_count(excludes) > 64 ||
      fw_count(suffixes) > 64)
    return "";
  struct pm_hash_state state = {.digest = EVP_MD_CTX_new(),
                                .excludes = excludes,
                                .suffixes = suffixes};
  int ok = state.digest &&
           EVP_DigestInit_ex(state.digest, EVP_sha256(), NULL) == 1;
  for (int64_t i = 0; ok && i < count; i++) {
    const char *path = fw_text(fw_at(paths, i));
    ok = *path && pm_hash_text(&state, "root") && pm_hash_text(&state, path) &&
         pm_hash_path(&state, path, path, 0);
  }
  unsigned char bytes[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  if (!ok || EVP_DigestFinal_ex(state.digest, bytes, &size) != 1) {
    EVP_MD_CTX_free(state.digest);
    return "";
  }
  EVP_MD_CTX_free(state.digest);
  char hex[EVP_MAX_MD_SIZE * 2 + 1];
  for (unsigned int i = 0; i < size; i++)
    snprintf(hex + i * 2, 3, "%02x", bytes[i]);
  hex[size * 2] = 0;
  return fr_str_concat(hex, "");
}

int64_t pm_mkdir(const char *path) {
  char *s = strdup(path);
  if (!s)
    return 0;
  for (char *q = s + 1; *q; q++)
    if (*q == '/') {
      *q = 0;
      if (mkdir(s, 0700) && errno != EEXIST) {
        free(s);
        return 0;
      }
      *q = '/';
    }
  int ok = mkdir(s, 0700) == 0 || errno == EEXIST;
  free(s);
  return ok;
}
int64_t pm_write(const char *path, const char *text) {
  char temp[4096];
  if (snprintf(temp, sizeof temp, "%s.XXXXXX", path) >= (int)sizeof temp)
    return 0;
  int fd = mkstemp(temp);
  if (fd < 0)
    return 0;
  size_t n = strlen(text), done = 0;
  int ok = 1;
  while (done < n) {
    ssize_t r = write(fd, text + done, n - done);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0) {
      ok = 0;
      break;
    }
    done += (size_t)r;
  }
  if (fsync(fd))
    ok = 0;
  if (close(fd))
    ok = 0;
  if (ok && rename(temp, path))
    ok = 0;
  if (!ok)
    unlink(temp);
  return ok;
}
int64_t pm_exists(const char *path) {
  struct stat s;
  return stat(path, &s) == 0;
}
int64_t pm_rename(const char *a, const char *b) { return rename(a, b) == 0; }
const char *pm_cwd(void) {
  char p[4096];
  return getcwd(p, sizeof p) ? fr_str_concat(p, "") : "";
}
int64_t pm_run(int64_t args, const char *directory) {
  int64_t result = fw_object();
  fw_set(result, "status", fw_number(-1));
  fw_set(result, "output", fw_string(""));
  size_t n = (size_t)fw_count(args);
  if (!n || n > 1024)
    return result;
  char **argv = calloc(n + 1, sizeof(char *));
  if (!argv)
    return result;
  for (size_t i = 0; i < n; i++)
    argv[i] = (char *)fw_text(fw_at(args, (int64_t)i));
  int pipes[2];
  if (pipe(pipes)) {
    free(argv);
    return result;
  }
  pid_t child = fork();
  if (child == 0) {
    setpgid(0, 0);
    close(pipes[0]);
    dup2(pipes[1], STDOUT_FILENO);
    close(pipes[1]);
    int null = open("/dev/null", O_RDONLY);
    if (null >= 0) {
      dup2(null, STDIN_FILENO);
      close(null);
    }
    setenv("GIT_TERMINAL_PROMPT", "0", 1);
    setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
    setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
    setenv("GIT_MASTER", "1", 1);
    if (directory && *directory && chdir(directory))
      _exit(126);
    execvp(argv[0], argv);
    _exit(127);
  }
  free(argv);
  close(pipes[1]);
  if (child < 0) {
    close(pipes[0]);
    return result;
  }
  setpgid(child, child);
  fcntl(pipes[0], F_SETFL, O_NONBLOCK);
  char *output = calloc(1024 * 1024 + 1, 1);
  size_t used = 0;
  int status = 0, finished = 0, expired = 0;
  time_t started = time(NULL);
  while (!finished) {
    char b[4096];
    ssize_t got;
    while ((got = read(pipes[0], b, sizeof b)) > 0) {
      size_t copy = (size_t)got;
      if (copy > 1024 * 1024 - used)
        copy = 1024 * 1024 - used;
      if (output && copy)
        memcpy(output + used, b, copy);
      used += copy;
    }
    pid_t done = waitpid(child, &status, WNOHANG);
    if (done == child) {
      finished = 1;
      continue;
    }
    if (done < 0 && errno != EINTR) {
      expired = 1;
      break;
    }
    if (time(NULL) - started >= 600) {
      expired = 1;
      kill(-child, SIGKILL);
      waitpid(child, &status, 0);
      break;
    }
    struct pollfd p = {pipes[0], POLLIN, 0};
    poll(&p, 1, 50);
  }
  char b[4096];
  ssize_t got;
  while ((got = read(pipes[0], b, sizeof b)) > 0) {
    size_t copy = (size_t)got;
    if (copy > 1024 * 1024 - used)
      copy = 1024 * 1024 - used;
    if (output && copy)
      memcpy(output + used, b, copy);
    used += copy;
  }
  close(pipes[0]);
  fw_set(result, "status",
         fw_number(expired             ? 124
                   : WIFEXITED(status) ? WEXITSTATUS(status)
                                       : 128));
  if (output) {
    output[used] = 0;
    fw_set(result, "output", fw_string(output));
    free(output);
  }
  return result;
}
