#include "forge/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#define fr_mkdir(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define fr_mkdir(p) mkdir(p, 0755)
#endif

static int fs_stat(const char *path, struct stat *st) {
    if (!path || !st) return -1;
#if defined(_WIN32)
    return _stat(path, st);
#else
    return stat(path, st);
#endif
}

char *fr_fs_read(const char *path) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int fr_fs_write(const char *path, const char *content) {
    if (!path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (content) {
        size_t n = strlen(content);
        if (fwrite(content, 1, n, f) != n) { fclose(f); return 0; }
    }
    fclose(f);
    return 1;
}

int fr_fs_append(const char *path, const char *content) {
    if (!path) return 0;
    FILE *f = fopen(path, "ab");
    if (!f) return 0;
    if (content) {
        size_t n = strlen(content);
        if (fwrite(content, 1, n, f) != n) { fclose(f); return 0; }
    }
    fclose(f);
    return 1;
}

int fr_fs_exists(const char *path) {
    struct stat st;
    return fs_stat(path, &st) == 0;
}

int fr_fs_remove(const char *path) {
    if (!path) return 0;
    return remove(path) == 0;
}

int64_t fr_fs_size(const char *path) {
    struct stat st;
    if (fs_stat(path, &st) != 0) return -1;
#if defined(_WIN32)
    if ((st.st_mode & _S_IFREG) == 0) return -1;
#else
    if (!S_ISREG(st.st_mode)) return -1;
#endif
    return (int64_t)st.st_size;
}

int fr_fs_is_file(const char *path) {
    struct stat st;
    if (fs_stat(path, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

int fr_fs_is_dir(const char *path) {
    struct stat st;
    if (fs_stat(path, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int fr_fs_mkdir(const char *path) {
    if (!path || !*path) return 0;
    return fr_mkdir(path) == 0 ? 1 : 0;
}

int fr_fs_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return 0;
    return rename(old_path, new_path) == 0 ? 1 : 0;
}

int fr_fs_copy(const char *src, const char *dst) {
    char *data = fr_fs_read(src);
    if (!data) return 0;
    int ok = fr_fs_write(dst, data);
    free(data);
    return ok;
}

static char *fs_list_grow(char *buf, size_t *cap, size_t *len, const char *name) {
    size_t nlen = strlen(name);
    if (*len + nlen + 2 > *cap) {
        size_t ncap = *cap * 2;
        while (*len + nlen + 2 > ncap) ncap *= 2;
        char *n = (char *)realloc(buf, ncap);
        if (!n) { free(buf); return NULL; }
        buf = n;
        *cap = ncap;
    }
    if (*len > 0) {
        buf[*len] = '\n';
        (*len)++;
    }
    memcpy(buf + *len, name, nlen);
    *len += nlen;
    buf[*len] = '\0';
    return buf;
}

char *fr_fs_list_dir(const char *path) {
    if (!path) return NULL;
#if defined(_WIN32)
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    size_t cap = 256, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) { FindClose(h); return NULL; }
    out[0] = '\0';
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        out = fs_list_grow(out, &cap, &len, fd.cFileName);
        if (!out) { FindClose(h); return NULL; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
#else
    DIR *d = opendir(path);
    if (!d) return NULL;
    size_t cap = 256, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) { closedir(d); return NULL; }
    out[0] = '\0';
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        out = fs_list_grow(out, &cap, &len, ent->d_name);
        if (!out) { closedir(d); return NULL; }
    }
    closedir(d);
    return out;
#endif
}
