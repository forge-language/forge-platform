#ifndef FORGE_JSON_H
#define FORGE_JSON_H

#include <stdint.h>

const char *fr_json_get_string(const char *json, const char *key);
int64_t fr_json_get_int(const char *json, const char *key);
char *fr_json_stringify_str(const char *key, const char *value);
char *fr_json_stringify_int(const char *key, int64_t value);

#endif
