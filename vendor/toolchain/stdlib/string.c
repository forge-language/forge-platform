#include "forge/string.h"
#include "forge/arena.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fr_str_arena_reset(void) {
    fr_arena_tls_reset();
}

static char *str_alloc(size_t n) {
    return (char *)fr_arena_alloc(fr_arena_tls(), n, 1);
}

int64_t fr_str_len(const char *s) {
    return s ? (int64_t)strlen(s) : 0;
}

char *fr_str_concat(const char *a, const char *b) {
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    char *out = str_alloc(la + lb + 1);
    if (!out) return NULL;
    if (la) memcpy(out, a, la);
    if (lb) memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

int fr_str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

char *fr_str_sub(const char *s, int64_t start, int64_t len) {
    if (!s || start < 0 || len < 0) return NULL;
    size_t slen = strlen(s);
    if ((size_t)start >= slen) {
        char *empty = str_alloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    if ((size_t)(start + len) > slen) len = (int64_t)(slen - (size_t)start);
    char *out = str_alloc((size_t)len + 1);
    if (!out) return NULL;
    memcpy(out, s + start, (size_t)len);
    out[len] = '\0';
    return out;
}

int fr_str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    return strstr(haystack, needle) != NULL;
}

char *fr_str_trim(const char *s) {
    if (!s) return NULL;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) {
        char *empty = str_alloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    const char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    size_t len = (size_t)(end - s + 1);
    char *out = str_alloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

int64_t fr_str_char_at(const char *s, int64_t i) {
    if (!s || i < 0) return -1;
    size_t len = strlen(s);
    if ((size_t)i >= len) return -1;
    return (unsigned char)s[i];
}

char *fr_str_append(const char *s, int64_t ch) {
    size_t len = s ? strlen(s) : 0;
    char *out = str_alloc(len + 2);
    if (!out) return NULL;
    if (len) memcpy(out, s, len);
    out[len] = (char)ch;
    out[len + 1] = '\0';
    return out;
}

char *fr_str_append_str(const char *s, const char *t) {
    return fr_str_concat(s, t);
}

char *fr_str_from_int(int64_t n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return fr_str_concat(buf, "");
}
