#define _GNU_SOURCE
#include "../native/transport.h"
#include "forge/string.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    assert(bot_init() == 1);
    int64_t buffer = fr_buffer_from_string("hello");
    assert(buffer > 0 && fr_buffer_len(buffer) == 5);
    assert(fr_buffer_append(buffer, 0) == 0 && fr_buffer_get(buffer, 5) == 0);
    assert(fr_buffer_set(buffer, 0, 255) == 0 && fr_buffer_get(buffer, 0) == 255);
    assert(fr_buffer_get(buffer, -1) == -1 && fr_buffer_set(buffer, 6, 1) == -1);
    assert(fr_buffer_free(buffer) == 0 && fr_buffer_free(buffer) == -1);
    int64_t next = fr_buffer_new(); assert(next > buffer && fr_buffer_get(buffer, 0) == -1); fr_buffer_free(next);
    assert(strcmp(bot_nfkc_lower("ＦＵＣＫ 한글"), "fuck 한글") == 0);
    assert(strcmp(bot_time_iso(0), "1970-01-01T00:00:00Z") == 0);
    assert(bot_bit_and(12, 10) == 8 && bot_bit_or(12, 10) == 14 && bot_bit_not(0) == -1);
    assert(strcmp(fr_json_get_path_string("{\"d\":{\"name\":\"한글\"}}", "d.name"), "한글") == 0);
    assert(fr_json_get_path_int("{\"d\":{\"n\":9007199254740993}}", "d.n") == 9007199254740993LL);
    assert(strcmp(fr_json_get_path_raw("{\"d\":[1,{\"x\":true}]}", "d[1].x"), "true") == 0);
    assert(strcmp(fr_json_get_path_string("{\"x\":\"a\\u0000b\"}", "x"), "") == 0);
    assert(strcmp(fr_json_get_path_raw("{\"x\":1} trailing", "x"), "") == 0);
    assert(strcmp(fr_json_get_path_string("{\"x\":\"✅😀\"}", "x"), "✅😀") == 0);
    assert(strcmp(fr_json_get_path_string("{\"x\":\"\xc0\xaf\"}", "x"), "") == 0);
    assert(fr_random_bounded(0) == -1);
    for (int i = 0; i < 100; i++) { int64_t n = fr_random_bounded(7); assert(n >= 0 && n < 7); }
    char directory[] = "/tmp/forge-transport-XXXXXX"; assert(mkdtemp(directory));
    char path[256]; snprintf(path, sizeof(path), "%s/nested/config.json", directory);
    assert(bot_write_atomic(path, "first") == 1);
    assert(bot_write_atomic(path, "second") == 1);
    assert(strcmp(bot_read_text(path), "second") == 0);
    struct stat st; assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    char link[256], target[256]; snprintf(link, sizeof(link), "%s/link", directory); snprintf(target, sizeof(target), "%s/link/secret", directory);
    assert(symlink("/tmp", link) == 0); assert(bot_write_atomic(target, "blocked") == 0);
    assert(unlink(link) == 0 && unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/nested", directory); assert(rmdir(path) == 0 && rmdir(directory) == 0);
    fr_str_arena_reset();
    puts("PASS buffers, JSON paths, normalization, bits, time and atomic filesystem");
    if (argc < 3) return 0;
    int64_t http = fr_net_http_new("POST", argv[1]); assert(http > 0);
    assert(fr_net_http_header(http, "Authorization: Bot fixture-token") == 0);
    assert(fr_net_http_header(http, "X-Invalid: a\r\nb") == -1);
    buffer = fr_buffer_from_string("{\"ok\":true}"); assert(fr_net_http_body(http, buffer) == 0); fr_buffer_free(buffer);
    assert(fr_net_http_perform(http, 5000) == 0 && fr_net_http_status(http) == 200);
    buffer = fr_net_http_response(http); assert(fr_buffer_len(buffer) == 11); fr_buffer_free(buffer); assert(fr_net_http_free(http) == 0);
    int64_t ws = fr_net_ws_open(argv[2], "", 5000); assert(ws > 0);
    buffer = fr_buffer_from_string("hello"); assert(fr_net_ws_send(ws, buffer, 0, 1000) == 0); fr_buffer_free(buffer);
    int64_t frame = fr_net_ws_recv(ws, 20); assert(frame == 0);
    frame = fr_net_ws_recv(ws, 5000); assert(frame > 0 && fr_net_ws_kind(ws) == 1);
    const char *expected = "split message"; assert(fr_buffer_len(frame) == (int64_t)strlen(expected));
    for (size_t i = 0; i < strlen(expected); i++) assert(fr_buffer_get(frame, (int64_t)i) == expected[i]);
    fr_buffer_free(frame);
    buffer = fr_buffer_from_string("done"); assert(fr_net_ws_send(ws, buffer, 0, 1000) == 0); fr_buffer_free(buffer);
    frame = fr_net_ws_recv(ws, 5000); assert(frame > 0 && fr_net_ws_kind(ws) == 8);
    assert(fr_buffer_get(frame, 0) == 3 && fr_buffer_get(frame, 1) == 232);
    fr_buffer_free(frame); assert(fr_net_ws_close(ws) == 0);
    puts("PASS HTTP request and TLS WebSocket fragmentation, timeout continuation, ping and close");
    return 0;
}
