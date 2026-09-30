#ifndef FORGE_STRING_H
#define FORGE_STRING_H

#include <stdint.h>

int64_t fr_str_len(const char *s);
char *fr_str_concat(const char *a, const char *b);
int fr_str_eq(const char *a, const char *b);
char *fr_str_sub(const char *s, int64_t start, int64_t len);
int fr_str_contains(const char *haystack, const char *needle);
char *fr_str_trim(const char *s);
int64_t fr_str_char_at(const char *s, int64_t i);
char *fr_str_append(const char *s, int64_t ch);
char *fr_str_append_str(const char *s, const char *t);
char *fr_str_from_int(int64_t n);
void fr_str_arena_reset(void);

#endif
