#include "forge/http.h"
#include "forge/tcp.h"
#include "forge/platform.h"
#include "forge/thread.h"
#include "forge_runtime.h"
#include "forge/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(FORGE_OS_WINDOWS)
#include <errno.h>
#include <sys/socket.h>
#endif

#if defined(FORGE_OS_LINUX)
#include <sys/epoll.h>
#include <unistd.h>
#elif defined(FORGE_OS_MACOS)
#include <sys/event.h>
#endif

typedef struct {
    int64_t sock;
    char method[16];
    char path[512];
    char *body;
} fr_http_req_t;

typedef struct {
    int64_t listen_sock;
    int64_t port;
    char *cached_resp;
    size_t cached_len;
} fr_http_server_t;

static fr_http_req_t g_reqs[128];
static fr_http_server_t g_servers[32];

static int recv_until_headers(int fd, char *buf, size_t cap) {
    size_t len = 0;
    while (len + 1 < cap) {
        ssize_t n = fr_sock_recv(fd, buf + len, cap - len - 1);
        if (n < 0) return -1;
        if (n == 0) break;
        len += (size_t)n;
        buf[len] = '\0';
        if (len >= 4 && buf[len - 4] == '\r' && buf[len - 3] == '\n' &&
            buf[len - 2] == '\r' && buf[len - 1] == '\n') {
            break;
        }
    }
    return (int)len;
}

static int tcp_send_all(int fd, const void *data, size_t len) {
    ssize_t n = fr_sock_send(fd, data, len);
    return n < 0 || (size_t)n != len ? -1 : 0;
}

static void parse_url(const char *url, char *host, size_t hcap, int64_t *port, char *path, size_t pcap) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= hcap) hlen = hcap - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = atoll(colon + 1);
        if (slash) snprintf(path, pcap, "%s", slash);
        else snprintf(path, pcap, "/");
    } else {
        size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
        if (hlen >= hcap) hlen = hcap - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = 80;
        if (slash) snprintf(path, pcap, "%s", slash);
        else snprintf(path, pcap, "/");
    }
}

static char *http_exchange(const char *method, const char *url, const char *body) {
    char host[256], path[512];
    int64_t port = 80;
    parse_url(url, host, sizeof(host), &port, path, sizeof(path));

    int64_t sock = fr_tcp_connect(host, port);
    if (sock < 0) return NULL;

    char header[1024];
    if (body && body[0]) {
        snprintf(header, sizeof(header),
                 "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                 method, path, host, strlen(body));
    } else {
        snprintf(header, sizeof(header),
                 "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 method, path, host);
    }
    fr_tcp_send(sock, header);
    if (body && body[0]) fr_tcp_send(sock, body);

    char *resp = fr_tcp_recv(sock);
    fr_tcp_close(sock);
    if (!resp) return NULL;

    char *body_start = strstr(resp, "\r\n\r\n");
    if (!body_start) return resp;
    body_start += 4;
    char *out = (char *)malloc(strlen(body_start) + 1);
    if (!out) { free(resp); return NULL; }
    strcpy(out, body_start);
    free(resp);
    return out;
}

char *fr_http_get(const char *url) {
    return http_exchange("GET", url, NULL);
}

char *fr_http_post(const char *url, const char *body) {
    return http_exchange("POST", url, body ? body : "");
}

int64_t fr_http_listen(int64_t port) {
    int64_t sock = fr_tcp_listen(port);
    if (sock < 0 || sock >= 32) return -1;
    g_servers[sock].listen_sock = sock;
    g_servers[sock].port = port;
    g_servers[sock].cached_resp = NULL;
    g_servers[sock].cached_len = 0;
    return sock;
}

static void parse_http_request(const char *raw, fr_http_req_t *req) {
    req->method[0] = req->path[0] = '\0';
    req->body = NULL;

    const char *line_end = strstr(raw, "\r\n");
    if (!line_end) return;
    char line[1024];
    size_t ll = (size_t)(line_end - raw);
    if (ll >= sizeof(line)) ll = sizeof(line) - 1;
    memcpy(line, raw, ll);
    line[ll] = '\0';

    sscanf(line, "%15s %511s", req->method, req->path);
    const char *body = strstr(raw, "\r\n\r\n");
    if (body && body[4]) {
        body += 4;
        fr_arena_t *arena = fr_arena_tls();
        req->body = fr_arena_strdup(arena, body);
    }
}

int64_t fr_http_accept(int64_t server) {
    int64_t client = fr_tcp_accept(server);
    if (client < 0 || client >= 128) return -1;

    char buf[4096];
    if (recv_until_headers((int)client, buf, sizeof(buf)) < 0) {
        fr_tcp_close(client);
        return -1;
    }

    g_reqs[client].sock = client;
    parse_http_request(buf, &g_reqs[client]);
    return client;
}

const char *fr_http_req_method(int64_t req) {
    if (req < 0 || req >= 128) return "";
    return g_reqs[req].method;
}

const char *fr_http_req_path(int64_t req) {
    if (req < 0 || req >= 128) return "";
    return g_reqs[req].path;
}

const char *fr_http_req_body(int64_t req) {
    if (req < 0 || req >= 128 || !g_reqs[req].body) return "";
    return g_reqs[req].body;
}

void fr_http_respond(int64_t req, int64_t status, const char *body) {
    if (req < 0 || req >= 128) return;
    const char *text = "OK";
    if (status == 404) text = "Not Found";
    else if (status == 500) text = "Internal Server Error";

    size_t blen = body ? strlen(body) : 0;
    char out[576];
    int hlen = snprintf(out, sizeof(out),
                        "HTTP/1.1 %lld %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                        (long long)status, text, blen);
    if (hlen < 0 || (size_t)hlen >= sizeof(out)) return;

    if (blen > 0 && (size_t)hlen + blen < sizeof(out)) {
        memcpy(out + hlen, body, blen);
        tcp_send_all((int)g_reqs[req].sock, out, (size_t)hlen + blen);
    } else {
        tcp_send_all((int)g_reqs[req].sock, out, (size_t)hlen);
        if (blen > 0) tcp_send_all((int)g_reqs[req].sock, body, blen);
    }
}

void fr_http_close(int64_t req) {
    if (req < 0 || req >= 128) return;
    fr_arena_tls_reset();
    fr_tcp_close(g_reqs[req].sock);
    g_reqs[req].sock = -1;
    g_reqs[req].body = NULL;
}

void fr_http_server_close(int64_t server) {
    if (server >= 0 && server < 32) {
        free(g_servers[server].cached_resp);
        g_servers[server].cached_resp = NULL;
        g_servers[server].cached_len = 0;
    }
    fr_tcp_close(server);
}

void fr_http_prepare(int64_t server, const char *body) {
    if (server < 0 || server >= 32) return;
    free(g_servers[server].cached_resp);
    g_servers[server].cached_resp = NULL;
    g_servers[server].cached_len = 0;

    size_t blen = body ? strlen(body) : 0;
    size_t cap = 128 + blen;
    char *out = (char *)malloc(cap);
    if (!out) return;
    int n = snprintf(out, cap,
                     "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%.*s",
                     blen, (int)blen, body ? body : "");
    if (n <= 0) { free(out); return; }
    g_servers[server].cached_resp = out;
    g_servers[server].cached_len = (size_t)n;
}

void fr_http_serve_prepared(int64_t server) {
    if (server < 0 || server >= 32) return;
    fr_http_server_t *srv = &g_servers[server];
    if (!srv->cached_resp || srv->cached_len == 0) return;

    int client = (int)fr_tcp_accept(server);
    if (client < 0) return;

    char discard[4096];
    fr_sock_recv(client, discard, sizeof(discard));
    tcp_send_all(client, srv->cached_resp, srv->cached_len);
    fr_sock_close(client);
}

static int accept_nonblocking(int listen_fd) {
#if defined(FORGE_OS_LINUX)
    return fr_sock_accept_nb(listen_fd);
#else
    fr_socket_t client = accept((fr_socket_t)listen_fd, NULL, NULL);
    if (client == FR_SOCK_INVALID) return -1;
    fr_sock_set_nonblocking((int)client);
    return (int)client;
#endif
}

static void serve_client(fr_http_server_t *srv, int client) {
    char discard[512];
    fr_sock_recv(client, discard, sizeof(discard));
    fr_sock_send(client, srv->cached_resp, srv->cached_len);
    fr_sock_close(client);
}

typedef struct {
    int client;
    fr_http_server_t *srv;
} http_serve_ctx_t;

static fr_scheduler_t *g_http_sched;

static void http_serve_task(void *arg) {
    http_serve_ctx_t *ctx = (http_serve_ctx_t *)arg;
    if (!ctx) return;
    serve_client(ctx->srv, ctx->client);
    free(ctx);
}

static int http_submit_client(fr_http_server_t *srv, int client) {
    http_serve_ctx_t *ctx = (http_serve_ctx_t *)malloc(sizeof(http_serve_ctx_t));
    if (!ctx) {
        fr_sock_close(client);
        return -1;
    }
    ctx->client = client;
    ctx->srv = srv;
    if (g_http_sched) {
        fr_sched_pool_submit(g_http_sched, http_serve_task, ctx);
        return 0;
    }
    serve_client(srv, client);
    free(ctx);
    return 0;
}

#if !defined(FORGE_OS_LINUX)
static void drain_accept_queue(int listen_fd, fr_http_server_t *srv) {
    for (;;) {
        int client = accept_nonblocking(listen_fd);
        if (client < 0) {
#if defined(FORGE_OS_WINDOWS)
            if (fr_sock_err() == WSAEWOULDBLOCK) break;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
            continue;
        }
        http_submit_client(srv, client);
    }
}
#endif

#if defined(FORGE_OS_LINUX)
static void http_serve_event_loop(int listen_fd, fr_http_server_t *srv) {
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) return;
    fr_sock_set_nonblocking(listen_fd);
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        close(epfd);
        return;
    }
    struct epoll_event events[128];
    for (;;) {
        int n = epoll_wait(epfd, events, 128, -1);
        if (n < 0) continue;
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd != listen_fd) continue;
            for (;;) {
                int client = accept_nonblocking(listen_fd);
                if (client < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    continue;
                }
                http_submit_client(srv, client);
            }
        }
    }
}
#elif defined(FORGE_OS_MACOS)
static void http_serve_event_loop(int listen_fd, fr_http_server_t *srv) {
    int kq = kqueue();
    if (kq < 0) return;
    fr_sock_set_nonblocking(listen_fd);
    struct kevent change;
    EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(kq, &change, 1, NULL, 0, NULL);
    struct kevent events[64];
    for (;;) {
        int n = kevent(kq, NULL, 0, events, 64, NULL);
        if (n < 0) continue;
        for (int i = 0; i < n; i++) {
            if ((int)events[i].ident == listen_fd) drain_accept_queue(listen_fd, srv);
        }
    }
}
#else
static void http_serve_event_loop(int listen_fd, fr_http_server_t *srv) {
    fr_sock_set_nonblocking(listen_fd);
    for (;;) drain_accept_queue(listen_fd, srv);
}
#endif

void fr_http_serve_forever(int64_t server) {
    if (server < 0 || server >= 32) return;
    fr_http_server_t *srv = &g_servers[server];
    if (!srv->cached_resp || srv->cached_len == 0) return;
    http_serve_event_loop((int)server, srv);
}

void fr_http_serve_ok(int64_t server, const char *body) {
    if (server < 0 || server >= 32) return;
    fr_http_server_t *srv = &g_servers[server];
    if (!srv->cached_resp) fr_http_prepare(server, body);
    fr_http_serve_prepared(server);
}

typedef struct {
    int listen_fd;
    fr_http_server_t *srv;
    int worker_id;
} http_worker_ctx_t;

static void *http_worker_main(void *arg) {
    http_worker_ctx_t *ctx = (http_worker_ctx_t *)arg;
    int cpus = fr_platform_cpu_count();
    if (cpus > 0) fr_thread_pin_cpu(ctx->worker_id % cpus);
    http_serve_event_loop(ctx->listen_fd, ctx->srv);
    return NULL;
}

/* Multi-worker REUSEPORT epoll: accept and respond on the same thread. */
void fr_http_serve_mt(int64_t server, int64_t threads) {
    if (server < 0 || server >= 32) return;
    fr_http_server_t *srv = &g_servers[server];
    if (!srv->cached_resp || srv->cached_len == 0) return;

    int cpus = fr_platform_cpu_count();
    if (cpus < 1) cpus = 1;

    int accept_workers = (int)(threads > 0 ? threads : cpus);

    if (accept_workers == 1) {
        http_serve_event_loop((int)server, srv);
        return;
    }

    fr_sock_close((int)server);

    http_worker_ctx_t *ctxs = (http_worker_ctx_t *)calloc((size_t)accept_workers, sizeof(http_worker_ctx_t));
    if (!ctxs) {
        http_serve_event_loop((int)server, srv);
        return;
    }

    int started = 0;
    for (int i = 0; i < accept_workers; i++) {
        int64_t fd = fr_tcp_listen_reuseport(srv->port);
        if (fd < 0) continue;
        ctxs[started].listen_fd = (int)fd;
        ctxs[started].srv = srv;
        ctxs[started].worker_id = started;
        fr_thread_t *tid = NULL;
        if (fr_thread_start(&tid, http_worker_main, &ctxs[started]) != 0) {
            fr_sock_close((int)fd);
            continue;
        }
        fr_thread_detach(tid);
        started++;
    }

    if (started == 0) {
        free(ctxs);
        http_serve_event_loop((int)server, srv);
        return;
    }

    fr_platform_sleep_forever();
}

/* REUSEPORT accept threads + M:N scheduler worker pool for response I/O. */
void fr_http_serve_hybrid(int64_t server, int64_t threads) {
    if (server < 0 || server >= 32) return;
    fr_http_server_t *srv = &g_servers[server];
    if (!srv->cached_resp || srv->cached_len == 0) return;

    int cpus = fr_platform_cpu_count();
    if (cpus < 1) cpus = 1;
    int pool_workers = (int)(threads > 0 ? threads : cpus);
    int accept_workers = cpus;

    g_http_sched = fr_scheduler_create(pool_workers);
    if (!g_http_sched) {
        fr_http_serve_mt(server, threads);
        return;
    }
    fr_scheduler_start(g_http_sched);

    if (accept_workers == 1) {
        http_serve_event_loop((int)server, srv);
        return;
    }

    fr_sock_close((int)server);

    http_worker_ctx_t *ctxs = (http_worker_ctx_t *)calloc((size_t)accept_workers, sizeof(http_worker_ctx_t));
    if (!ctxs) {
        http_serve_event_loop((int)server, srv);
        return;
    }

    int started = 0;
    for (int i = 0; i < accept_workers; i++) {
        int64_t fd = fr_tcp_listen_reuseport(srv->port);
        if (fd < 0) continue;
        ctxs[started].listen_fd = (int)fd;
        ctxs[started].srv = srv;
        ctxs[started].worker_id = started;
        fr_thread_t *tid = NULL;
        if (fr_thread_start(&tid, http_worker_main, &ctxs[started]) != 0) {
            fr_sock_close((int)fd);
            continue;
        }
        fr_thread_detach(tid);
        started++;
    }

    if (started == 0) {
        free(ctxs);
        http_serve_event_loop((int)server, srv);
        return;
    }

    fr_platform_sleep_forever();
}
