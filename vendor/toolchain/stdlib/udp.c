#include "forge/udp.h"
#include "forge/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(FORGE_OS_WINDOWS)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

typedef struct {
    char peer[64];
} fr_udp_state_t;

static fr_udp_state_t *udp_state(int64_t sock) {
    static fr_udp_state_t states[256];
    if (sock < 0 || sock >= 256) return &states[0];
    return &states[sock];
}

int64_t fr_udp_bind(int64_t port) {
    fr_platform_init();
    fr_socket_t fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == FR_SOCK_INVALID) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fr_sock_close(fd);
        return -1;
    }
    udp_state((int64_t)fd)->peer[0] = '\0';
    return (int64_t)fd;
}

int64_t fr_udp_send(int64_t sock, const char *host, int64_t port, const char *data) {
    if (sock < 0 || !host || !data) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) return -1;
    ssize_t n = sendto((fr_socket_t)sock, data, (int)strlen(data), 0,
                       (struct sockaddr *)&addr, sizeof(addr));
    return n < 0 ? -1 : (int64_t)n;
}

char *fr_udp_recv(int64_t sock) {
    if (sock < 0) return NULL;
    char buf[65536];
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    ssize_t n = recvfrom((fr_socket_t)sock, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *)&addr, &addrlen);
    if (n < 0) return NULL;
    buf[n] = '\0';
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    snprintf(udp_state(sock)->peer, sizeof(udp_state(sock)->peer), "%s:%d", ip, ntohs(addr.sin_port));
    char *out = (char *)malloc((size_t)n + 1);
    if (!out) return NULL;
    memcpy(out, buf, (size_t)n + 1);
    return out;
}

const char *fr_udp_peer(int64_t sock) {
    return udp_state(sock)->peer;
}

void fr_udp_close(int64_t sock) {
    if (sock >= 0) fr_sock_close(sock);
}
