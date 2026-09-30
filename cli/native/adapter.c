#include "forge/string.h"
#include "forge_web.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
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
