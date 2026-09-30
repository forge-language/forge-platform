#ifndef FORGE_OWNERSHIP_H
#define FORGE_OWNERSHIP_H

#include <stddef.h>
#include <stdint.h>

typedef struct fr_process fr_process_t;

/* Take ownership of a heap string (sets *src to NULL). */
char *fr_own_take(char **src);

/* Send with ownership transfer of payload (receiver must free). */
void fr_send_move(fr_process_t *dst, int tag, char *payload);

/* Clone for immutable sharing across processes. */
char *fr_own_clone(const char *s);

#endif
