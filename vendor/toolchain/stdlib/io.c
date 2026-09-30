#include "forge/io.h"
#include "forge/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

static char *io_alloc(size_t n) {
    char *p = (char *)fr_arena_alloc(fr_arena_tls(), n, 1);
    if (p) p[0] = '\0';
    return p;
}

static char *io_strdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *out = io_alloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static char *io_trim_newline(const char *s) {
    if (!s) return io_strdup("");
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) n--;
    char *out = io_alloc(n + 1);
    if (!out) return NULL;
    if (n > 0) memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

void fr_print_int(int64_t value) {
    printf("%lld", (long long)value);
}

void fr_print_str(const char *value) {
    if (value) fputs(value, stdout);
    fflush(stdout);
}

void fr_println(void) {
    putchar('\n');
    fflush(stdout);
}

void fr_print(const char *value) {
    if (value) fputs(value, stdout);
    fflush(stdout);
}

void fr_eprint(const char *value) {
    if (value) fputs(value, stderr);
}

void fr_eprintln(const char *value) {
    if (value) fputs(value, stderr);
    fputc('\n', stderr);
}

void fr_io_eprint_int(int64_t value) {
    fprintf(stderr, "%lld", (long long)value);
}

void fr_io_flush(void) {
    fflush(stdout);
}

void fr_io_flush_err(void) {
    fflush(stderr);
}

char *fr_io_read_line(void) {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return io_strdup("");
    return io_trim_newline(buf);
}

int64_t fr_io_read_char(void) {
    int ch = fgetc(stdin);
    if (ch == EOF) return -1;
    return (int64_t)ch;
}

char *fr_io_read_stdin(void) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return io_strdup("");
    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), stdin)) {
        size_t n = strlen(chunk);
        if (len + n + 1 > cap) {
            cap *= 2;
            while (len + n + 1 > cap) cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                return io_strdup("");
            }
            buf = nb;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    buf[len] = '\0';
    char *out = io_strdup(buf);
    free(buf);
    return out;
}

char *fr_io_prompt(const char *msg) {
    if (msg) fputs(msg, stdout);
    fflush(stdout);
    return fr_io_read_line();
}

int64_t fr_io_write_fd(int64_t fd, const char *text) {
    if (!text) text = "";
    size_t n = strlen(text);
#if defined(_WIN32)
    int wrote = _write((int)fd, text, (unsigned)n);
#else
    ssize_t wrote = write((int)fd, text, n);
#endif
    if (wrote < 0) return -1;
    return (int64_t)wrote;
}

char *fr_io_read_fd(int64_t fd, int64_t max_bytes) {
    if (max_bytes <= 0) max_bytes = 4096;
    if (max_bytes > 1024 * 1024) max_bytes = 1024 * 1024;
    char *buf = (char *)malloc((size_t)max_bytes + 1);
    if (!buf) return io_strdup("");
#if defined(_WIN32)
    int n = (int)_read((int)fd, buf, (unsigned)max_bytes);
#else
    ssize_t n = read((int)fd, buf, (size_t)max_bytes);
#endif
    if (n <= 0) {
        free(buf);
        return io_strdup("");
    }
    buf[n] = '\0';
    char *out = io_strdup(buf);
    free(buf);
    return out;
}

int64_t fr_io_stdin_fd(void) { return 0; }
int64_t fr_io_stdout_fd(void) { return 1; }
int64_t fr_io_stderr_fd(void) { return 2; }
