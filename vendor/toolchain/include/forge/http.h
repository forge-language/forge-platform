#ifndef FORGE_HTTP_H
#define FORGE_HTTP_H

#include <stdint.h>

char *fr_http_get(const char *url);
char *fr_http_post(const char *url, const char *body);
int64_t fr_http_listen(int64_t port);
int64_t fr_http_accept(int64_t server);
const char *fr_http_req_method(int64_t req);
const char *fr_http_req_path(int64_t req);
const char *fr_http_req_body(int64_t req);
void fr_http_respond(int64_t req, int64_t status, const char *body);
void fr_http_close(int64_t req);
void fr_http_server_close(int64_t server);
void fr_http_prepare(int64_t server, const char *body);
void fr_http_serve_prepared(int64_t server);
void fr_http_serve_forever(int64_t server);
void fr_http_serve_mt(int64_t server, int64_t threads);
void fr_http_serve_hybrid(int64_t server, int64_t threads);
void fr_http_serve_ok(int64_t server, const char *body);

#endif
