#include "forge/arena.h"
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <pthread.h>
#endif

/* Blocks never move: every allocation remains valid until reset or destroy. */
typedef struct fr_arena_block {
    struct fr_arena_block *next;
    size_t cap;
    size_t pos;
    unsigned char data[];
} fr_arena_block_t;

struct fr_arena {
    fr_arena_block_t *first;
    fr_arena_block_t *current;
};

static _Thread_local fr_arena_t *tls_arena = NULL;
#ifndef _WIN32
static pthread_key_t tls_arena_key;
static pthread_once_t tls_arena_once = PTHREAD_ONCE_INIT;
static int tls_arena_key_ready;
static void tls_arena_cleanup(void *arena) { fr_arena_destroy(arena); }
static void tls_arena_init_key(void) {
    tls_arena_key_ready = pthread_key_create(&tls_arena_key, tls_arena_cleanup) == 0;
}
#endif

static fr_arena_block_t *block_create(size_t cap) {
    if (cap > SIZE_MAX - sizeof(fr_arena_block_t)) return NULL;
    fr_arena_block_t *b = malloc(sizeof(*b) + cap);
    if (!b) return NULL;
    b->next = NULL;
    b->cap = cap;
    b->pos = 0;
    return b;
}

fr_arena_t *fr_arena_create(size_t initial_cap) {
    fr_arena_t *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    if (initial_cap < 4096) initial_cap = 4096;
    a->first = block_create(initial_cap);
    if (!a->first) { free(a); return NULL; }
    a->current = a->first;
    return a;
}

void fr_arena_destroy(fr_arena_t *a) {
    if (!a) return;
    fr_arena_block_t *b = a->first;
    while (b) {
        fr_arena_block_t *next = b->next;
        free(b);
        b = next;
    }
    free(a);
}

void *fr_arena_alloc(fr_arena_t *a, size_t size, size_t align) {
    if (!a || size == 0) return NULL;
    if (align == 0) align = sizeof(void *);
    if ((align & (align - 1)) != 0) return NULL;
    if (align < sizeof(void *)) align = sizeof(void *);
    if (size > SIZE_MAX - (align - 1)) return NULL;
    fr_arena_block_t *b = a->current;
    for (;;) {
        size_t padding = (0 - ((uintptr_t)b->data + b->pos)) & (align - 1);
        size_t available = b->cap - b->pos;
        if (padding <= available && size <= available - padding) {
            unsigned char *out = b->data + b->pos + padding;
            b->pos += padding + size;
            a->current = b;
            return out;
        }
        if (!b->next) {
            size_t cap = b->cap <= SIZE_MAX / 2 ? b->cap * 2 : b->cap;
            size_t need = size + align - 1;
            if (cap < need) cap = need;
            b->next = block_create(cap);
            if (!b->next) return NULL;
        }
        b = b->next;
    }
}

char *fr_arena_strdup(fr_arena_t *a, const char *s) {
    if (!a || !s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = fr_arena_alloc(a, n, 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

void fr_arena_reset(fr_arena_t *a) {
    if (!a) return;
    for (fr_arena_block_t *b = a->first; b; b = b->next) b->pos = 0;
    a->current = a->first;
}

fr_arena_t *fr_arena_tls(void) {
    if (!tls_arena) {
#ifndef _WIN32
        pthread_once(&tls_arena_once, tls_arena_init_key);
        if (!tls_arena_key_ready) return NULL;
#endif
        tls_arena = fr_arena_create(4 * 1024 * 1024);
#ifndef _WIN32
        if (tls_arena && pthread_setspecific(tls_arena_key, tls_arena) != 0) {
            fr_arena_destroy(tls_arena);
            tls_arena = NULL;
        }
#endif
    }
    return tls_arena;
}

void fr_arena_tls_reset(void) {
    if (tls_arena) fr_arena_reset(tls_arena);
}
