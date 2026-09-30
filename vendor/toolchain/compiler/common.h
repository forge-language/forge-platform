#ifndef FORGE_COMMON_H
#define FORGE_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#define FORGE_VERSION "0.1.0"

typedef struct ForgeStr {
    char *data;
    size_t len;
} ForgeStr;

static inline ForgeStr forge_str(const char *s) {
    ForgeStr r = { (char *)s, s ? strlen(s) : 0 };
    return r;
}

static inline bool forge_str_eq(ForgeStr a, ForgeStr b) {
    return a.len == b.len && strncmp(a.data, b.data, a.len) == 0;
}

static inline char *forge_strdup(ForgeStr s) {
    char *out = (char *)malloc(s.len + 1);
    if (!out) return NULL;
    memcpy(out, s.data, s.len);
    out[s.len] = '\0';
    return out;
}

[[noreturn]] static inline void forge_die(const char *msg) {
    fprintf(stderr, "forge: fatal: %s\n", msg);
    exit(1);
}

#endif
