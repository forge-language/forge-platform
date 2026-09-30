#ifndef FORGE_ARENA_H
#define FORGE_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fr_arena fr_arena_t;

fr_arena_t *fr_arena_create(size_t initial_cap);
void fr_arena_destroy(fr_arena_t *a);
/* Allocations remain valid until reset/destroy, including across growth.
 * align must be a power of two; zero selects pointer alignment. */
void *fr_arena_alloc(fr_arena_t *a, size_t size, size_t align);
char *fr_arena_strdup(fr_arena_t *a, const char *s);
void fr_arena_reset(fr_arena_t *a);

fr_arena_t *fr_arena_tls(void);
void fr_arena_tls_reset(void);

#ifdef __cplusplus
}
#endif

#endif
