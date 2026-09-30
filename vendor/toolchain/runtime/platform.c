#define _GNU_SOURCE
#define _GNU_SOURCE
#include "forge/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(FORGE_OS_WINDOWS)
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

static int g_net_inited = 0;

void fr_platform_init(void) {
#if defined(FORGE_OS_WINDOWS)
    if (!g_net_inited) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) g_net_inited = 1;
    }
#else
    (void)g_net_inited;
#endif
}

void fr_platform_shutdown(void) {
#if defined(FORGE_OS_WINDOWS)
    if (g_net_inited) {
        WSACleanup();
        g_net_inited = 0;
    }
#endif
}

int fr_platform_cpu_count(void) {
#if defined(FORGE_OS_WINDOWS)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    int n = (int)info.dwNumberOfProcessors;
    return n > 0 ? n : 4;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 4;
#endif
}

void fr_platform_sleep_forever(void) {
#if defined(FORGE_OS_WINDOWS)
    for (;;) Sleep(INFINITE);
#else
    for (;;) pause();
#endif
}

ssize_t fr_sock_send(int fd, const void *buf, size_t len) {
#if defined(FORGE_OS_WINDOWS)
    return send((SOCKET)fd, (const char *)buf, (int)len, 0);
#else
    ssize_t sent = 0;
    const char *p = (const char *)buf;
    while ((size_t)sent < len) {
        ssize_t n = send(fd, p + sent, len - (size_t)sent, MSG_NOSIGNAL);
        if (n <= 0) return n;
        sent += n;
    }
    return sent;
#endif
}

ssize_t fr_sock_recv(int fd, void *buf, size_t len) {
#if defined(FORGE_OS_WINDOWS)
    return recv((SOCKET)fd, (char *)buf, (int)len, 0);
#else
    return recv(fd, buf, len, 0);
#endif
}

int fr_sock_set_tcp_nodelay(int fd) {
    int yes = 1;
#if defined(FORGE_OS_WINDOWS)
    return setsockopt((SOCKET)fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
#else
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#endif
}

int fr_sock_set_nonblocking(int fd) {
#if defined(FORGE_OS_WINDOWS)
    u_long mode = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int fr_sock_accept_nb(int listen_fd) {
#if defined(FORGE_OS_LINUX)
    return (int)accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    fr_socket_t client = accept((fr_socket_t)listen_fd, NULL, NULL);
    if (client == FR_SOCK_INVALID) return -1;
    fr_sock_set_nonblocking((int)client);
    return (int)client;
#endif
}

void fr_sock_close(int fd) {
    if (fd < 0) return;
#if defined(FORGE_OS_WINDOWS)
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
}

int fr_path_exists(const char *path) {
    if (!path) return 0;
#if defined(FORGE_OS_WINDOWS)
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, R_OK) == 0;
#endif
}

void fr_path_normalize(char *path) {
    if (!path) return;
#if defined(FORGE_OS_WINDOWS)
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
#endif
}

int fr_make_temp_path(char *buf, size_t cap, const char *prefix, const char *suffix) {
    if (!buf || cap == 0) return -1;
#if defined(FORGE_OS_WINDOWS)
    char temp_dir[MAX_PATH];
    if (GetTempPathA(sizeof(temp_dir), temp_dir) == 0) return -1;
    if (GetTempFileNameA(temp_dir, prefix ? prefix : "frg", 0, buf) == 0) return -1;
    if (suffix && suffix[0]) {
        size_t n = strlen(buf);
        if (n + strlen(suffix) + 1 > cap) return -1;
        strcat(buf, suffix);
    }
    return 0;
#else
    snprintf(buf, cap, "/tmp/%s-XXXXXX", prefix ? prefix : "forge");
    int fd = mkstemp(buf);
    if (fd < 0) return -1;
    close(fd);
    if (suffix && suffix[0]) {
        size_t n = strlen(buf);
        if (n + strlen(suffix) + 1 > cap) return -1;
        strcat(buf, suffix);
    }
    return 0;
#endif
}
