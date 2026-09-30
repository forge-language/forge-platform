#ifndef FORGE_UDP_H
#define FORGE_UDP_H

#include <stdint.h>

int64_t fr_udp_bind(int64_t port);
int64_t fr_udp_send(int64_t sock, const char *host, int64_t port, const char *data);
char *fr_udp_recv(int64_t sock);
const char *fr_udp_peer(int64_t sock);
void fr_udp_close(int64_t sock);

#endif
