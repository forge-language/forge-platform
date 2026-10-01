#ifndef FORGE_STRING_H
#define FORGE_STRING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
/* Opaque, thread-local handles: valid only until fr_str_arena_reset(). Views
 * borrow immutable NUL-terminated bytes; the source must outlive the view.
 * Zero is a failed/empty handle. These are trusted runtime handles, not IDs
 * that can be reconstructed from arbitrary integers. */
int64_t fr_str_view(const char *s);
int64_t fr_str_view_len(int64_t view);
int64_t fr_str_view_at(int64_t view, int64_t index);
/* Arena-backed geometric growth; append returns the same handle or zero on
 * failure. char accepts bytes 1..255 (NUL is rejected). finish copies an
 * immutable snapshot, so further appends cannot modify previously returned
 * strings. Native strings may contain arbitrary nonzero bytes. JavaScript
 * finish requires complete, valid UTF-8 and throws for malformed byte sequences.
 * Builders, snapshots and views are all invalidated by arena reset. */
int64_t fr_str_builder(void);
int64_t fr_str_builder_append(int64_t builder, const char *s);
int64_t fr_str_builder_char(int64_t builder, int64_t ch);
char *fr_str_builder_finish(int64_t builder);

#ifdef __cplusplus
}
#endif

#endif
