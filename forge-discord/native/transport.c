#define _GNU_SOURCE
#include "transport.h"
#include "forge/string.h"
#include <curl/curl.h>
#include <curl/websockets.h>
#include <json-c/json.h>
#include <openssl/rand.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utf8.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RESOURCE_COUNT 4096
#define BYTE_LIMIT (4u * 1024u * 1024u)
#define HEADER_LIMIT (64u * 1024u)
typedef struct { unsigned char *data; size_t len, capacity; } Bytes;
typedef struct {
    CURL *curl;
    struct curl_slist *headers;
    curl_mime *mime;
    Bytes body, response, response_headers, message, control;
    long status;
    int kind, message_kind, failed;
    char error[CURL_ERROR_SIZE];
} Connection;
typedef struct { int64_t id; int kind; void *value; } Resource;
static _Thread_local Resource resources[RESOURCE_COUNT];
static _Thread_local int64_t next_id = 1;
static _Thread_local size_t allocated_bytes;
static volatile sig_atomic_t stopping;
static void on_signal(int signal_number) { (void)signal_number; stopping = 1; }

static int64_t keep(int kind, void *value) {
    if (next_id == INT64_MAX) return -1;
    for (size_t i = 0; i < RESOURCE_COUNT; i++) {
        if (!resources[i].value) { resources[i] = (Resource){next_id++, kind, value}; return resources[i].id; }
    }
    return -1;
}
static Resource *find(int64_t id, int kind) {
    if (id <= 0) return NULL;
    for (size_t i = 0; i < RESOURCE_COUNT; i++)
        if (resources[i].id == id && resources[i].kind == kind && resources[i].value) return &resources[i];
    return NULL;
}
static void *get(int64_t id, int kind) { Resource *r = find(id, kind); return r ? r->value : NULL; }
static void forget(int64_t id, int kind) { Resource *r = find(id, kind); if (r) *r = (Resource){0}; }
static int append(Bytes *bytes, const void *data, size_t count, size_t limit) {
    if (count > limit || bytes->len > limit - count) return -1;
    size_t needed = bytes->len + count + 1;
    if (needed > bytes->capacity) {
        size_t capacity = bytes->capacity ? bytes->capacity : 256;
        while (capacity < needed) capacity *= 2;
        if (capacity - bytes->capacity > 64u * 1024u * 1024u - allocated_bytes) return -1;
        unsigned char *grown = realloc(bytes->data, capacity);
        if (!grown) return -1;
        allocated_bytes += capacity - bytes->capacity;
        bytes->data = grown; bytes->capacity = capacity;
    }
    if (count) memcpy(bytes->data + bytes->len, data, count);
    bytes->len += count; bytes->data[bytes->len] = 0;
    return 0;
}
static void clear(Bytes *bytes) { allocated_bytes -= bytes->capacity; free(bytes->data); *bytes = (Bytes){0}; }
static int64_t copy_buffer(const void *data, size_t length) {
    Bytes *bytes = calloc(1, sizeof(*bytes));
    if (!bytes) return -1;
    if (append(bytes, data, length, BYTE_LIMIT) < 0) { clear(bytes); free(bytes); return -1; }
    int64_t id = keep(1, bytes);
    if (id < 0) { clear(bytes); free(bytes); }
    return id;
}
int64_t fr_buffer_new(void) { return copy_buffer("", 0); }
int64_t fr_buffer_from_string(const char *value) { return value ? copy_buffer(value, strlen(value)) : -1; }
int64_t fr_buffer_len(int64_t id) { Bytes *b = get(id, 1); return b ? (int64_t)b->len : -1; }
int64_t fr_buffer_append(int64_t id, int64_t value) {
    Bytes *b = get(id, 1); unsigned char byte = (unsigned char)value;
    return b && value >= 0 && value <= 255 ? append(b, &byte, 1, BYTE_LIMIT) : -1;
}
int64_t fr_buffer_set(int64_t id, int64_t index, int64_t value) {
    Bytes *b = get(id, 1);
    if (!b || index < 0 || (uint64_t)index >= b->len || value < 0 || value > 255) return -1;
    b->data[index] = (unsigned char)value; return 0;
}
int64_t fr_buffer_get(int64_t id, int64_t index) {
    Bytes *b = get(id, 1); return b && index >= 0 && (uint64_t)index < b->len ? b->data[index] : -1;
}
int64_t fr_buffer_free(int64_t id) {
    Bytes *b = get(id, 1); if (!b) return -1;
    clear(b); free(b); forget(id, 1); return 0;
}
int64_t bot_init(void) {
    static int initialized;
    if (initialized) return 1;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 0;
    struct sigaction action = {0}; action.sa_handler = on_signal; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) return 0;
    signal(SIGPIPE, SIG_IGN); initialized = 1; return 1;
}
int64_t bot_signal_stop(void) { return stopping != 0; }
int64_t bot_bit_and(int64_t a, int64_t b) { return a & b; }
int64_t bot_bit_or(int64_t a, int64_t b) { return a | b; }
int64_t bot_bit_not(int64_t a) { return ~a; }
const char *bot_time_iso(int64_t epoch_seconds) {
    time_t seconds = (time_t)epoch_seconds; struct tm utc; char output[32];
    if ((int64_t)seconds != epoch_seconds || !gmtime_r(&seconds, &utc) || !strftime(output, sizeof(output), "%Y-%m-%dT%H:%M:%SZ", &utc)) return "";
    return fr_str_concat(output, "");
}
int64_t fr_monotonic_now_ms(void) {
    struct timespec now; if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
int64_t fr_random_bounded(int64_t bound) {
    if (bound <= 0) return -1;
    uint64_t value, maximum = (uint64_t)bound, threshold = (uint64_t)(-maximum) % maximum;
    do { if (RAND_bytes((unsigned char *)&value, sizeof(value)) != 1) return -1; } while (value < threshold);
    return (int64_t)(value % maximum);
}
static Connection *connection(void) {
    if (!bot_init()) return NULL;
    Connection *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->curl = curl_easy_init();
    if (!c->curl) { free(c); return NULL; }
    curl_easy_setopt(c->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c->curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c->curl, CURLOPT_ERRORBUFFER, c->error);
    const char *ca = getenv("SSL_CERT_FILE");
    if (ca && *ca) curl_easy_setopt(c->curl, CURLOPT_CAINFO, ca);
    return c;
}
static void destroy_connection(Connection *c) {
    curl_mime_free(c->mime); curl_slist_free_all(c->headers); curl_easy_cleanup(c->curl);
    clear(&c->body); clear(&c->response); clear(&c->response_headers); clear(&c->message); clear(&c->control); free(c);
}
static size_t receive_body(char *data, size_t size, size_t count, void *context) {
    Connection *c = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t length = size * count;
    return append(&c->response, data, length, BYTE_LIMIT) == 0 ? length : 0;
}
static size_t receive_headers(char *data, size_t size, size_t count, void *context) {
    Connection *c = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t length = size * count;
    return append(&c->response_headers, data, length, HEADER_LIMIT) == 0 ? length : 0;
}
int64_t fr_net_http_new(const char *method, const char *url) {
    if (!method || !*method || !url || (strncmp(url, "https://", 8) && strncmp(url, "http://", 7))) return -1;
    for (const char *p = method; *p; p++) if (*p < 'A' || *p > 'Z') return -1;
    Connection *c = connection(); if (!c) return -1;
    curl_easy_setopt(c->curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c->curl, CURLOPT_URL, url); curl_easy_setopt(c->curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(c->curl, CURLOPT_WRITEFUNCTION, receive_body); curl_easy_setopt(c->curl, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->curl, CURLOPT_HEADERFUNCTION, receive_headers); curl_easy_setopt(c->curl, CURLOPT_HEADERDATA, c);
    int64_t id = keep(2, c); if (id < 0) destroy_connection(c); return id;
}
int64_t fr_net_http_header(int64_t id, const char *header) {
    Connection *c = get(id, 2);
    if (!c || !header || strlen(header) > 8192 || strchr(header, '\r') || strchr(header, '\n')) return -1;
    struct curl_slist *next = curl_slist_append(c->headers, header); if (!next) return -1;
    c->headers = next; return curl_easy_setopt(c->curl, CURLOPT_HTTPHEADER, next) == CURLE_OK ? 0 : -1;
}
int64_t fr_net_http_body(int64_t id, int64_t buffer) {
    Connection *c = get(id, 2); Bytes *body = get(buffer, 1);
    if (!c || !body || c->mime) return -1;
    clear(&c->body); if (append(&c->body, body->data, body->len, BYTE_LIMIT)) return -1;
    curl_easy_setopt(c->curl, CURLOPT_POSTFIELDS, c->body.data);
    return curl_easy_setopt(c->curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)c->body.len) == CURLE_OK ? 0 : -1;
}
static curl_mimepart *part(Connection *c, const char *field) {
    if (!c || c->body.len || !field || !*field || strlen(field) > 200) return NULL;
    if (!c->mime) c->mime = curl_mime_init(c->curl);
    if (!c->mime) return NULL;
    curl_mimepart *p = curl_mime_addpart(c->mime);
    if (!p || curl_mime_name(p, field) != CURLE_OK) return NULL;
    return p;
}
int64_t fr_net_http_field(int64_t id, const char *field, int64_t buffer) {
    Connection *c = get(id, 2); Bytes *body = get(buffer, 1);
    if (!body) return -1;
    curl_mimepart *p = part(c, field);
    return p && curl_mime_data(p, (const char *)body->data, body->len) == CURLE_OK ? 0 : -1;
}
int64_t fr_net_http_file(int64_t id, const char *field, const char *path, const char *type) {
    Connection *c = get(id, 2); struct stat st;
    if (!path || !type || stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > BYTE_LIMIT) return -1;
    curl_mimepart *p = part(c, field);
    return p && curl_mime_filedata(p, path) == CURLE_OK && curl_mime_type(p, type) == CURLE_OK ? 0 : -1;
}
int64_t fr_net_http_perform(int64_t id, int64_t timeout_ms) {
    Connection *c = get(id, 2); if (!c || timeout_ms <= 0 || timeout_ms > 86400000) return -1;
    clear(&c->response); clear(&c->response_headers); c->status = 0; c->error[0] = 0;
    curl_easy_setopt(c->curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(c->curl, CURLOPT_CONNECTTIMEOUT_MS, (long)timeout_ms);
    if (c->mime) curl_easy_setopt(c->curl, CURLOPT_MIMEPOST, c->mime);
    CURLcode result = curl_easy_perform(c->curl);
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &c->status);
    if (result != CURLE_OK) c->status = 0;
    return result == CURLE_OK ? 0 : -1;
}
int64_t fr_net_http_status(int64_t id) { Connection *c = get(id, 2); return c ? c->status : -1; }
int64_t fr_net_http_response(int64_t id) { Connection *c = get(id, 2); return c ? copy_buffer(c->response.data, c->response.len) : -1; }
const char *fr_net_http_headers(int64_t id) { Connection *c = get(id, 2); return c && c->response_headers.data ? (const char *)c->response_headers.data : ""; }
const char *fr_net_error(int64_t id) { Connection *c = get(id, 2); return c ? c->error : "invalid handle"; }
int64_t fr_net_http_free(int64_t id) { Connection *c = get(id, 2); if (!c) return -1; destroy_connection(c); forget(id, 2); return 0; }

static int wait_socket(Connection *c, short events, int64_t deadline) {
    curl_socket_t socket; if (curl_easy_getinfo(c->curl, CURLINFO_ACTIVESOCKET, &socket) != CURLE_OK || socket == CURL_SOCKET_BAD) return -1;
    for (;;) {
        int64_t remaining = deadline - fr_monotonic_now_ms(); if (remaining <= 0 || stopping) return 0;
        struct pollfd descriptor = { socket, events, 0 };
        int result = poll(&descriptor, 1, (int)remaining);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return result;
        if (descriptor.revents & events) return 1;
        return -1;
    }
}
int64_t fr_net_ws_open(const char *url, const char *headers, int64_t timeout_ms) {
    if (!url || strncmp(url, "wss://", 6) || !headers || *headers || timeout_ms <= 0 || timeout_ms > 86400000) return -1;
    Connection *c = connection(); if (!c) return -1;
    curl_easy_setopt(c->curl, CURLOPT_PROTOCOLS_STR, "wss");
    curl_easy_setopt(c->curl, CURLOPT_URL, url); curl_easy_setopt(c->curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(c->curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    if (curl_easy_perform(c->curl) != CURLE_OK) { destroy_connection(c); return -1; }
    int64_t id = keep(3, c); if (id < 0) destroy_connection(c); return id;
}
int64_t fr_net_ws_send(int64_t id, int64_t buffer, int64_t binary, int64_t timeout_ms) {
    Connection *c = get(id, 3); Bytes *b = get(buffer, 1);
    if (!c || c->failed || !b || timeout_ms <= 0 || timeout_ms > 86400000 || (binary != 0 && binary != 1)) return -1;
    int64_t deadline = fr_monotonic_now_ms() + timeout_ms; size_t offset = 0;
    do {
        if (fr_monotonic_now_ms() >= deadline || stopping) { c->failed = 1; return -1; }
        size_t sent = 0;
        CURLcode result = curl_ws_send(c->curl, b->data + offset, b->len - offset, &sent, 0, binary ? CURLWS_BINARY : CURLWS_TEXT);
        offset += sent;
        if (result == CURLE_AGAIN) { if (wait_socket(c, POLLOUT, deadline) > 0) continue; c->failed = 1; return -1; }
        if (result != CURLE_OK) { c->failed = 1; return -1; }
        if (!sent && offset < b->len && wait_socket(c, POLLOUT, deadline) <= 0) { c->failed = 1; return -1; }
    } while (offset < b->len);
    return 0;
}
int64_t fr_net_ws_recv(int64_t id, int64_t timeout_ms) {
    Connection *c = get(id, 3); if (!c || c->failed || timeout_ms < 0 || timeout_ms > 86400000) return -1;
    int64_t deadline = fr_monotonic_now_ms() + timeout_ms;
    for (unsigned iterations = 0; ; iterations++) {
        if (iterations && (iterations >= 256 || fr_monotonic_now_ms() >= deadline || stopping)) return 0;
        unsigned char bytes[16384]; size_t received = 0; const struct curl_ws_frame *meta = NULL;
        CURLcode result = curl_ws_recv(c->curl, bytes, sizeof(bytes), &received, &meta);
        if (result == CURLE_AGAIN) {
            int wait = wait_socket(c, POLLIN, deadline); if (wait > 0) continue;
            if (wait < 0) c->failed = 1;
            return wait < 0 ? -1 : 0;
        }
        if (result != CURLE_OK || !meta) { c->failed = 1; return -1; }
        int control = meta->flags & (CURLWS_CLOSE | CURLWS_PING | CURLWS_PONG);
        Bytes *destination = control ? &c->control : &c->message;
        if (append(destination, bytes, received, control ? 125 : BYTE_LIMIT)) { c->failed = 1; return -1; }
        if (!control && meta->offset == 0 && c->message_kind == 0) c->message_kind = (meta->flags & CURLWS_BINARY) ? 2 : 1;
        if (meta->bytesleft) continue;
        if (control) {
            if (meta->flags & CURLWS_CLOSE) {
                c->kind = 8; int64_t frame = copy_buffer(c->control.data, c->control.len); clear(&c->control); return frame;
            }
            clear(&c->control); continue;
        }
        if (meta->flags & CURLWS_CONT) continue;
        c->kind = c->message_kind; c->message_kind = 0;
        int64_t frame = copy_buffer(c->message.data, c->message.len); clear(&c->message); return frame;
    }
}
int64_t fr_net_ws_kind(int64_t id) { Connection *c = get(id, 3); return c ? c->kind : -1; }
int64_t fr_net_ws_close(int64_t id) { Connection *c = get(id, 3); if (!c) return -1; destroy_connection(c); forget(id, 3); return 0; }

static struct json_object *parse_json(const char *text) {
    if (!text || strlen(text) > BYTE_LIMIT) return NULL;
    size_t length = strlen(text);
    /* Alpine json-c 0.18 strict parsing rejects literal Unicode. Validate UTF-8
       and feed equivalent escapes while preserving strict JSON syntax checks. */
    char *ascii = malloc(length * 3 + 1); if (!ascii) return NULL;
    char *out = ascii;
    int32_t offset = 0;
    while (offset < (int32_t)length) {
        UChar32 point;
        U8_NEXT(text, offset, (int32_t)length, point);
        if (point < 0) { free(ascii); return NULL; }
        if (point < 128) { *out++ = (char)point; continue; }
        if (point > 0xffff) {
            point -= 0x10000;
            out += snprintf(out, 7, "\\u%04x", 0xd800 + (point >> 10));
            point = 0xdc00 + (point & 0x3ff);
        }
        out += snprintf(out, 7, "\\u%04x", point);
    }
    *out = 0;
    struct json_tokener *tokener = json_tokener_new_ex(64);
    if (!tokener) { free(ascii); return NULL; }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    struct json_object *value = json_tokener_parse_ex(tokener, ascii, (int)(out - ascii) + 1);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    json_tokener_free(tokener); free(ascii);
    if (error != json_tokener_success) { json_object_put(value); return NULL; }
    return value;
}
static struct json_object *path_value(struct json_object *root, const char *path) {
    if (!path) return NULL;
    struct json_object *value = root;
    const char *cursor = path;
    while (*cursor && value) {
        if (*cursor == '.') { cursor++; continue; }
        if (*cursor == '[') {
            char *end; errno = 0; long index = strtol(cursor + 1, &end, 10);
            if (errno || end == cursor + 1 || *end != ']' || index < 0 || json_object_get_type(value) != json_type_array) return NULL;
            value = json_object_array_get_idx(value, (size_t)index); cursor = end + 1;
        } else {
            const char *start = cursor; while (*cursor && *cursor != '.' && *cursor != '[') cursor++;
            size_t length = (size_t)(cursor - start); if (!length || length > 255) return NULL;
            char key[256]; memcpy(key, start, length); key[length] = 0;
            if (json_object_get_type(value) != json_type_object || !json_object_object_get_ex(value, key, &value)) return NULL;
        }
    }
    return value;
}
const char *fr_json_get_path_raw(const char *json, const char *path) {
    struct json_object *root = parse_json(json), *value = path_value(root, path);
    const char *result = value ? fr_str_concat(json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN), "") : "";
    json_object_put(root); return result;
}
const char *fr_json_get_path_string(const char *json, const char *path) {
    struct json_object *root = parse_json(json), *value = path_value(root, path); const char *result = "";
    if (value && json_object_get_type(value) == json_type_string) {
        const char *text = json_object_get_string(value);
        if (strlen(text) == (size_t)json_object_get_string_len(value)) result = fr_str_concat(text, "");
    }
    json_object_put(root); return result;
}
int64_t fr_json_get_path_int(const char *json, const char *path) {
    struct json_object *root = parse_json(json), *value = path_value(root, path);
    int64_t result = value && json_object_get_type(value) == json_type_int ? json_object_get_int64(value) : 0;
    json_object_put(root); return result;
}

const char *bot_read_text(const char *path) {
    if (!path) return "";
    int file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); if (file < 0) return "";
    struct stat st;
    if (fstat(file, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > BYTE_LIMIT) { close(file); return ""; }
    Bytes bytes = {0}; unsigned char chunk[8192]; int okay = 1;
    for (;;) {
        ssize_t count = read(file, chunk, sizeof(chunk));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 || (count > 0 && append(&bytes, chunk, (size_t)count, BYTE_LIMIT))) { okay = 0; break; }
        if (count == 0) break;
    }
    close(file);
    const char *result = okay && (!bytes.len || !memchr(bytes.data, 0, bytes.len)) ? fr_str_concat(bytes.data ? (const char *)bytes.data : "", "") : "";
    clear(&bytes); return result;
}
int64_t bot_write_atomic(const char *path, const char *text) {
    if (!path || !*path || strlen(path) >= PATH_MAX || !text || strlen(text) > BYTE_LIMIT) return 0;
    char copy[PATH_MAX]; strcpy(copy, path);
    int directory = open(path[0] == '/' ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC); if (directory < 0) return 0;
    char *cursor = copy + (path[0] == '/'); char *component = cursor;
    for (; *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = 0;
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) { close(directory); return 0; }
        if (mkdirat(directory, component, 0700) && errno != EEXIST) { close(directory); return 0; }
        int next = openat(directory, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(directory); if (next < 0) return 0; directory = next; component = cursor + 1;
    }
    if (!*component || !strcmp(component, ".") || !strcmp(component, "..") || strlen(component) > 200) { close(directory); return 0; }
    unsigned char random[12]; if (RAND_bytes(random, sizeof(random)) != 1) { close(directory); return 0; }
    char temporary[256]; int offset = snprintf(temporary, sizeof(temporary), ".%s.", component);
    for (size_t i = 0; i < sizeof(random); i++) offset += snprintf(temporary + offset, sizeof(temporary) - (size_t)offset, "%02x", random[i]);
    int file = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file < 0) { close(directory); return 0; }
    size_t length = strlen(text), written = 0; int okay = 1;
    while (written < length) {
        ssize_t count = write(file, text + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { okay = 0; break; }
        written += (size_t)count;
    }
    if (fsync(file)) okay = 0;
    if (close(file)) okay = 0;
    if (okay && renameat(directory, temporary, directory, component)) okay = 0;
    if (okay && fsync(directory)) okay = 0;
    if (!okay) unlinkat(directory, temporary, 0);
    close(directory); return okay;
}
const char *bot_nfkc_lower(const char *text) {
    if (!text || strlen(text) > BYTE_LIMIT) return "";
    UErrorCode status = U_ZERO_ERROR; int32_t input_length = 0;
    u_strFromUTF8(NULL, 0, &input_length, text, -1, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) return "";
    status = U_ZERO_ERROR; UChar *input = malloc(((size_t)input_length + 1) * sizeof(*input)); if (!input) return "";
    u_strFromUTF8(input, input_length + 1, NULL, text, -1, &status);
    const UNormalizer2 *normalizer = unorm2_getNFKCInstance(&status);
    int32_t normalized_length = unorm2_normalize(normalizer, input, input_length, NULL, 0, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) { free(input); return ""; }
    status = U_ZERO_ERROR; UChar *normalized = malloc(((size_t)normalized_length + 1) * sizeof(*normalized));
    if (!normalized) { free(input); return ""; }
    unorm2_normalize(normalizer, input, input_length, normalized, normalized_length + 1, &status); free(input);
    int32_t lowered_length = u_strToLower(NULL, 0, normalized, normalized_length, "en_US", &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) { free(normalized); return ""; }
    status = U_ZERO_ERROR; UChar *lowered = malloc(((size_t)lowered_length + 1) * sizeof(*lowered));
    if (!lowered) { free(normalized); return ""; }
    u_strToLower(lowered, lowered_length + 1, normalized, normalized_length, "en_US", &status); free(normalized);
    int32_t output_length = 0; u_strToUTF8(NULL, 0, &output_length, lowered, lowered_length, &status);
    if ((status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) || output_length < 0 || (uint32_t)output_length > BYTE_LIMIT) { free(lowered); return ""; }
    status = U_ZERO_ERROR; char *output = malloc((size_t)output_length + 1); if (!output) { free(lowered); return ""; }
    u_strToUTF8(output, output_length + 1, NULL, lowered, lowered_length, &status); free(lowered);
    const char *result = U_SUCCESS(status) ? fr_str_concat(output, "") : ""; free(output); return result;
}
