#include "forge/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_key_value(const char *json, const char *key, char quote) {
    if (!json || !key) return NULL;
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != quote) return NULL;
    return p + 1;
}

const char *fr_json_get_string(const char *json, const char *key) {
    static char buf[1024];
    const char *start = find_key_value(json, key, '"');
    if (!start) return "";
    const char *end = strchr(start, '"');
    if (!end) return "";
    size_t len = (size_t)(end - start);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    return buf;
}

int64_t fr_json_get_int(const char *json, const char *key) {
    const char *start = find_key_value(json, key, '\0');
    if (!start) {
        start = find_key_value(json, key, '"');
        if (start) return atoll(start);
        return 0;
    }
    return atoll(start);
}

char *fr_json_stringify_str(const char *key, const char *value) {
    char *out = (char *)malloc(1024);
    if (!out) return NULL;
    snprintf(out, 1024, "{\"%s\":\"%s\"}", key ? key : "", value ? value : "");
    return out;
}

char *fr_json_stringify_int(const char *key, int64_t value) {
    char *out = (char *)malloc(256);
    if (!out) return NULL;
    snprintf(out, 256, "{\"%s\":%lld}", key ? key : "", (long long)value);
    return out;
}
