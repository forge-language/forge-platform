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

/* Reject links and special files in downloaded source and Git metadata.
 * Build products are checked separately; they are not source checkout inputs. */
static int safe_tree_walk(const char *path, int depth, size_t *entries) {
  struct stat info;
  if (depth > 64 || ++*entries > 100000 || lstat(path, &info) || S_ISLNK(info.st_mode)) return 0;
  if (S_ISREG(info.st_mode)) return info.st_nlink == 1;
  if (!S_ISDIR(info.st_mode)) return 0;
  DIR *dir = opendir(path);
  if (!dir) return 0;
  int ok = 1; struct dirent *entry;
  while (ok && (entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    if (depth == 0 && (!strcmp(entry->d_name, "build") || !strcmp(entry->d_name, "node_modules"))) {
      char generated[4096]; struct stat output;
      if (snprintf(generated, sizeof generated, "%s/%s", path, entry->d_name) >= (int)sizeof generated || lstat(generated, &output) || !S_ISDIR(output.st_mode)) ok = 0;
      continue;
    }
    char child[4096];
    if (snprintf(child, sizeof child, "%s/%s", path, entry->d_name) >= (int)sizeof child || !safe_tree_walk(child, depth + 1, entries)) ok = 0;
  }
  closedir(dir); return ok;
}
int64_t pm_safe_checkout_tree(const char *path) {
  if (!path || !*path || strlen(path) >= 4000) return 0;
  char component[4096]; size_t length = strlen(path);
  memcpy(component, path, length + 1);
  for (size_t i = 0; i <= length; i++) if (component[i] == '/' || component[i] == 0) {
    char saved = component[i]; component[i] = 0; struct stat info;
    if (*component && (lstat(component, &info) || !S_ISDIR(info.st_mode))) return 0;
    component[i] = saved;
  }
  size_t entries = 0; return safe_tree_walk(path, 0, &entries);
}
int64_t pm_safe_git_checkout_tree(const char *path) {
  if (!pm_safe_checkout_tree(path)) return 0;
  char metadata[4096]; struct stat info;
  snprintf(metadata, sizeof metadata, "%s/.git", path);
  if (lstat(metadata, &info) || !S_ISDIR(info.st_mode)) return 0;
  const char *redirects[] = {"objects/info/alternates", "info/grafts", "refs/replace", "commondir"};
  for (size_t i = 0; i < sizeof redirects / sizeof redirects[0]; i++) {
    snprintf(metadata, sizeof metadata, "%s/.git/%s", path, redirects[i]);
    if (lstat(metadata, &info) == 0 || errno != ENOENT) return 0;
  }
  return 1;
}

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
  int root_excludes_only;
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
        ((!state->root_excludes_only || depth == 0) && pm_hash_list_contains(state->excludes, entry->d_name)))
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

const char *pm_tree_digest(const char *path, int64_t excludes) {
  struct pm_hash_state state = {.digest = EVP_MD_CTX_new(), .excludes = excludes, .root_excludes_only = 1};
  unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned int size = 0;
  int ok = state.digest && EVP_DigestInit_ex(state.digest, EVP_sha256(), NULL) == 1 && pm_hash_path(&state, path, ".", 0) && EVP_DigestFinal_ex(state.digest, bytes, &size) == 1;
  EVP_MD_CTX_free(state.digest); if (!ok) return "";
  char hex[EVP_MAX_MD_SIZE * 2 + 1];
  for (unsigned i = 0; i < size; i++) snprintf(hex + i * 2, 3, "%02x", bytes[i]);
  hex[size * 2] = 0; return fr_str_concat(hex, "");
}
const char *pm_file_sha256(const char *path) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC); struct stat info;
  if (fd < 0) return "";
  if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size > PM_HASH_MAX_BYTES || info.st_nlink != 1) { close(fd); return ""; }
  EVP_MD_CTX *digest = EVP_MD_CTX_new(); unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned int size = 0;
  int ok = digest && EVP_DigestInit_ex(digest, EVP_sha256(), NULL) == 1; char buffer[65536]; ssize_t got;
  while (ok && (got = read(fd, buffer, sizeof buffer)) != 0) {
    if (got < 0) { if (errno == EINTR) continue; ok = 0; break; }
    ok = EVP_DigestUpdate(digest, buffer, (size_t)got) == 1;
  }
  if (ok) ok = EVP_DigestFinal_ex(digest, bytes, &size) == 1;
  EVP_MD_CTX_free(digest); close(fd); if (!ok) return "";
  char hex[EVP_MAX_MD_SIZE * 2 + 1];
  for (unsigned i = 0; i < size; i++) snprintf(hex + i * 2, 3, "%02x", bytes[i]);
  hex[size * 2] = 0; return fr_str_concat(hex, "");
}

int64_t pm_mkdir_new(const char *path) {
  return mkdir(path, 0700) == 0;
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
    /* Git routing variables must not redirect package verification outside its checkout. */
    if (!strcmp(argv[0], "git")) {
      const char *routing[] = {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_COMMON_DIR", "GIT_CONFIG", "GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS"};
      for (size_t i = 0; i < sizeof routing / sizeof routing[0]; i++) unsetenv(routing[i]);
    }
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

/* Bounded tar filesystem extraction. Transport and release policy are in Forge. */
#include <locale.h>
#include <archive.h>
#include <archive_entry.h>
#define PM_ARCHIVE_MAX_BYTES (256ULL * 1024ULL * 1024ULL)
#define PM_ARCHIVE_MAX_ENTRIES 10000
#define PM_ARCHIVE_MAX_NAMES (4ULL * 1024ULL * 1024ULL)

static int pm_archive_path(const char *path) {
  if (!path || !*path || path[0] == '/' || strlen(path) >= 4096 || strchr(path, '\\')) return 0;
  int depth = 0;
  for (const char *part = path; *part;) {
    const char *slash = strchr(part, '/');
    size_t n = slash ? (size_t)(slash - part) : strlen(part);
    if (!n || n > 255 || (n == 1 && part[0] == '.') ||
        (n == 2 && memcmp(part, "..", 2) == 0) ||
        (n == 4 && memcmp(part, ".git", 4) == 0) ||
        (n == 15 && memcmp(part, ".forge-artifact", 15) == 0) ||
        (n == 20 && memcmp(part, ".forge-source.tar.gz", 20) == 0) || ++depth > 64) return 0;
    if (!slash) break;
    part = slash + 1;
  }
  return 1;
}
static int pm_archive_parent(int root, char *name, const char **leaf) {
  int parent = dup(root);
  if (parent < 0) return -1;
  char *part = name;
  char *slash;
  while ((slash = strchr(part, '/'))) {
    *slash = 0;
    if (mkdirat(parent, part, 0700) < 0 && errno != EEXIST) { close(parent); return -1; }
    int child = openat(parent, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    if (child < 0) return -1;
    parent = child; part = slash + 1;
  }
  *leaf = part;
  return parent;
}
int64_t pm_archive_extract(const char *file, const char *directory) {
  if (!file || !directory || mkdir(directory, 0700) < 0) return 0;
  int root = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (root < 0) return 0;
  /* PAX paths are UTF-8. A scoped thread locale keeps their bytes intact
     even when the caller starts in the C locale; do not change global locale. */
  locale_t filename_locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
  if (!filename_locale) { close(root); return 0; }
  locale_t previous_locale = uselocale(filename_locale);
  if (!previous_locale) { freelocale(filename_locale); close(root); return 0; }
  struct archive *archive = archive_read_new();
  char **seen = calloc(PM_ARCHIVE_MAX_ENTRIES, sizeof *seen);
  char prefix[256] = {0};
  uint64_t bytes = 0, name_bytes = 0;
  int count = 0, ok = 0, status;
  struct archive_entry *entry;
  if (!archive || !seen) goto done;
  archive_read_support_filter_gzip(archive);
  archive_read_support_format_tar(archive);
  if (archive_read_open_filename(archive, file, 65536) != ARCHIVE_OK) goto done;
  while ((status = archive_read_next_header(archive, &entry)) == ARCHIVE_OK) {
    const char *original = archive_entry_pathname(entry);
    if (archive_filter_code(archive, 0) != ARCHIVE_FILTER_GZIP || !pm_archive_path(original) ||
        archive_entry_symlink(entry) || archive_entry_hardlink(entry) ||
        (archive_entry_filetype(entry) != AE_IFDIR && archive_entry_filetype(entry) != AE_IFREG) ||
        count >= PM_ARCHIVE_MAX_ENTRIES) goto done;
    char name[4096]; strcpy(name, original);
    size_t length = strlen(name);
    if (length && name[length-1] == '/') name[--length] = 0;
    if (!length) goto done;
    name_bytes += length;
    if (name_bytes > PM_ARCHIVE_MAX_NAMES) goto done;
    for (int i = 0; i < count; ++i) if (!strcmp(name, seen[i])) goto done;
    seen[count] = strdup(name); if (!seen[count]) goto done; count++;
    char *slash = strchr(name, '/');
    size_t prefix_length = slash ? (size_t)(slash - name) : strlen(name);
    if (!*prefix) { memcpy(prefix, name, prefix_length); prefix[prefix_length] = 0; }
    if (strlen(prefix) != prefix_length || memcmp(prefix, name, prefix_length)) goto done;
    if (!slash) {
      if (archive_entry_filetype(entry) != AE_IFDIR || archive_read_data_skip(archive) != ARCHIVE_OK) goto done;
      continue;
    }
    /* Generated directories are excluded from source integrity; archives must
       not pre-populate them as executable source inputs. */
    const char *source = slash + 1;
    if (!strcmp(source, "build") || !strncmp(source, "build/", 6) ||
        !strcmp(source, "node_modules") || !strncmp(source, "node_modules/", 13)) goto done;
    const char *leaf;
    int parent = pm_archive_parent(root, slash + 1, &leaf);
    if (parent < 0) goto done;
    if (archive_entry_filetype(entry) == AE_IFDIR) {
      if (mkdirat(parent, leaf, 0700) < 0 && errno != EEXIST) { close(parent); goto done; }
      int child = openat(parent, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      close(parent); if (child < 0) goto done; close(child);
      if (archive_read_data_skip(archive) != ARCHIVE_OK) goto done;
    } else {
      int64_t size = archive_entry_size(entry);
      if (size < 0 || (uint64_t)size > PM_ARCHIVE_MAX_BYTES - bytes) { close(parent); goto done; }
      int fd = openat(parent, leaf, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                      (archive_entry_perm(entry) & 0111) ? 0700 : 0600);
      close(parent); if (fd < 0) goto done;
      uint64_t written = 0; char buffer[65536]; la_ssize_t got;
      while ((got = archive_read_data(archive, buffer, sizeof buffer)) > 0) {
        if ((uint64_t)got > PM_ARCHIVE_MAX_BYTES - bytes || (uint64_t)got > (uint64_t)size - written) { close(fd); goto done; }
        size_t offset = 0;
        while (offset < (size_t)got) {
          ssize_t n = write(fd, buffer + offset, (size_t)got - offset);
          if (n < 0 && errno == EINTR) continue;
          if (n <= 0) { close(fd); goto done; }
          offset += (size_t)n;
        }
        bytes += (uint64_t)got; written += (uint64_t)got;
      }
      if (got < 0 || written != (uint64_t)size || fsync(fd) < 0) { close(fd); goto done; }
      if (close(fd) < 0) goto done;
    }
  }
  ok = status == ARCHIVE_EOF && count > 0;
done:
  if (archive) archive_read_free(archive);
  for (int i = 0; i < count; ++i) free(seen[i]);
  free(seen); close(root);
  uselocale(previous_locale); freelocale(filename_locale);
  return ok;
}
static int pm_remove_at(int parent, const char *name, int depth) {
  struct stat info;
  if (depth > 66) return 0;
  if (fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) < 0) return errno == ENOENT;
  if (!S_ISDIR(info.st_mode)) return unlinkat(parent, name, 0) == 0;
  int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return 0;
  DIR *dir = fdopendir(fd); if (!dir) { close(fd); return 0; }
  int ok = 1; struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    if (!pm_remove_at(fd, entry->d_name, depth + 1)) ok = 0;
  }
  closedir(dir);
  return ok && unlinkat(parent, name, AT_REMOVEDIR) == 0;
}
int64_t pm_remove_tree(const char *path) {
  if (!path || !*path || !strcmp(path, ".") || !strcmp(path, "/")) return 0;
  return pm_remove_at(AT_FDCWD, path, 0);
}
int64_t pm_file_size(const char *path) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return -1;
  struct stat info;
  int ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
  close(fd); return ok ? (int64_t)info.st_size : -1;
}
