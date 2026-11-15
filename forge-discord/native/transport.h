#ifndef FORGE_DISCORD_TRANSPORT_H
#define FORGE_DISCORD_TRANSPORT_H
#include <stdint.h>
int64_t bot_init(void);
int64_t bot_signal_stop(void);
int64_t bot_write_atomic(const char *path, const char *text);
const char *bot_read_text(const char *path);
const char *bot_nfkc_lower(const char *text);
const char *bot_time_iso(int64_t epoch_seconds);
int64_t bot_bit_and(int64_t a, int64_t b);
int64_t bot_bit_or(int64_t a, int64_t b);
int64_t bot_bit_not(int64_t a);
int64_t fr_buffer_new(void);
int64_t fr_buffer_from_string(const char *value);
int64_t fr_buffer_append(int64_t handle, int64_t value);
int64_t fr_buffer_set(int64_t handle, int64_t index, int64_t value);
int64_t fr_buffer_get(int64_t handle, int64_t index);
int64_t fr_buffer_len(int64_t handle);
int64_t fr_buffer_free(int64_t handle);
int64_t fr_net_http_new(const char *method, const char *url);
int64_t fr_net_http_header(int64_t handle, const char *header);
int64_t fr_net_http_body(int64_t handle, int64_t buffer);
int64_t fr_net_http_file(int64_t handle, const char *field, const char *path, const char *content_type);
int64_t fr_net_http_field(int64_t handle, const char *field, int64_t buffer);
int64_t fr_net_http_perform(int64_t handle, int64_t timeout_ms);
int64_t fr_net_http_status(int64_t handle);
int64_t fr_net_http_response(int64_t handle);
const char *fr_net_http_headers(int64_t handle);
const char *fr_net_error(int64_t handle);
int64_t fr_net_http_free(int64_t handle);
int64_t fr_net_ws_open(const char *url, const char *headers, int64_t timeout_ms);
int64_t fr_net_ws_send(int64_t handle, int64_t buffer, int64_t binary, int64_t timeout_ms);
int64_t fr_net_ws_recv(int64_t handle, int64_t timeout_ms);
int64_t fr_net_ws_kind(int64_t handle);
int64_t fr_net_ws_close(int64_t handle);
const char *fr_json_get_path_raw(const char *json, const char *path);
const char *fr_json_get_path_string(const char *json, const char *path);
int64_t fr_json_get_path_int(const char *json, const char *path);
int64_t fr_monotonic_now_ms(void);
int64_t fr_random_bounded(int64_t bound);
#endif
