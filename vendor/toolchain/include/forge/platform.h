#ifndef FORGE_PLATFORM_H
#define FORGE_PLATFORM_H

#if !defined(_WIN32)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define FORGE_OS_WINDOWS 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET fr_socket_t;
#define FR_SOCK_INVALID ((fr_socket_t)INVALID_SOCKET)
#define fr_sock_err() WSAGetLastError()
#else
#define FORGE_OS_POSIX 1
#include <unistd.h>
typedef int fr_socket_t;
#define FR_SOCK_INVALID (-1)
#define fr_sock_err() errno
#include <errno.h>
#endif

#if defined(__APPLE__)
#define FORGE_OS_MACOS 1
#endif

#if defined(__linux__)
#define FORGE_OS_LINUX 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

void fr_platform_init(void);
void fr_platform_shutdown(void);
int fr_platform_cpu_count(void);
void fr_platform_sleep_forever(void);

ssize_t fr_sock_send(int fd, const void *buf, size_t len);
ssize_t fr_sock_recv(int fd, void *buf, size_t len);
int fr_sock_set_tcp_nodelay(int fd);
int fr_sock_set_nonblocking(int fd);
int fr_sock_accept_nb(int listen_fd);
void fr_sock_close(int fd);

int fr_path_exists(const char *path);
int fr_make_temp_path(char *buf, size_t cap, const char *prefix, const char *suffix);
void fr_path_normalize(char *path);

#ifdef __cplusplus
}
#endif

#endif
