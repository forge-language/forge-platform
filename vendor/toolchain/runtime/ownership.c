#include "forge/ownership.h"
#include "forge_runtime.h"
#include <stdlib.h>
#include <string.h>

char *fr_own_take(char **src) {
    if (!src) return NULL;
    char *out = *src;
    *src = NULL;
    return out;
}

void fr_send_move(fr_process_t *dst, int tag, char *payload) {
    size_t n = payload ? strlen(payload) + 1 : 0;
    fr_send(dst, tag, 0, payload, n);
}

char *fr_own_clone(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}
