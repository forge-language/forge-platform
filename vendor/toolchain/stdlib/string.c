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

typedef struct { const char *data; size_t len; } str_view_t;
typedef struct { char *data; size_t len, cap; } str_builder_t;

int64_t fr_str_view(const char *s) {
    str_view_t *view = fr_arena_alloc(fr_arena_tls(), sizeof(*view), 0);
    if (!view) return 0;
    view->data = s;
    view->len = s ? strlen(s) : 0;
    return (int64_t)(intptr_t)view;
}

int64_t fr_str_view_len(int64_t handle) {
    const str_view_t *view = (const str_view_t *)(intptr_t)handle;
    return view ? (int64_t)view->len : 0;
}

int64_t fr_str_view_at(int64_t handle, int64_t index) {
    const str_view_t *view = (const str_view_t *)(intptr_t)handle;
    if (!view || index < 0 || (uint64_t)index >= view->len) return -1;
    return (unsigned char)view->data[index];
}

int64_t fr_str_builder(void) {
    str_builder_t *builder = fr_arena_alloc(fr_arena_tls(), sizeof(*builder), 0);
    if (!builder) return 0;
    memset(builder, 0, sizeof(*builder));
    return (int64_t)(intptr_t)builder;
}

static int builder_reserve(str_builder_t *builder, size_t extra) {
    if (!builder || extra > SIZE_MAX - builder->len - 1) return 0;
    size_t needed = builder->len + extra + 1;
    if (needed <= builder->cap) return 1;
    size_t cap = builder->cap ? builder->cap : 64;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) { cap = needed; break; }
        cap *= 2;
    }
    char *data = str_alloc(cap);
    if (!data) return 0;
    if (builder->len) memcpy(data, builder->data, builder->len);
    data[builder->len] = '\0';
    builder->data = data;
    builder->cap = cap;
    return 1;
}

int64_t fr_str_builder_append(int64_t handle, const char *s) {
    str_builder_t *builder = (str_builder_t *)(intptr_t)handle;
    size_t len = s ? strlen(s) : 0;
    if (!builder_reserve(builder, len)) return 0;
    if (len) memmove(builder->data + builder->len, s, len);
    builder->len += len;
    builder->data[builder->len] = '\0';
    return handle;
}

int64_t fr_str_builder_char(int64_t handle, int64_t ch) {
    str_builder_t *builder = (str_builder_t *)(intptr_t)handle;
    /* These APIs build NUL-terminated byte strings. Never introduce a hidden
     * suffix by appending NUL, or silently narrow an out-of-range byte. */
    if (ch <= 0 || ch > 255 || !builder_reserve(builder, 1)) return 0;
    builder->data[builder->len++] = (char)ch;
    builder->data[builder->len] = '\0';
    return handle;
}

char *fr_str_builder_finish(int64_t handle) {
    const str_builder_t *builder = (const str_builder_t *)(intptr_t)handle;
    if (!builder) return NULL;
    char *snapshot = str_alloc(builder->len + 1);
    if (!snapshot) return NULL;
    if (builder->len) memcpy(snapshot, builder->data, builder->len);
    snapshot[builder->len] = '\0';
    return snapshot;
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
    if ((uint64_t)len > slen - (size_t)start) len = (int64_t)(slen - (size_t)start);
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
