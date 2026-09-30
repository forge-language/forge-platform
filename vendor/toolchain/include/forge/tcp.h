#ifndef FORGE_TCP_H
#define FORGE_TCP_H

#include <stdint.h>

int64_t fr_tcp_listen(int64_t port);
int64_t fr_tcp_listen_reuseport(int64_t port);
int64_t fr_tcp_accept(int64_t sock);
int64_t fr_tcp_connect(const char *host, int64_t port);
int64_t fr_tcp_send(int64_t sock, const char *data);
char *fr_tcp_recv(int64_t sock);
void fr_tcp_close(int64_t sock);
void fr_net_init(void);

#endif
