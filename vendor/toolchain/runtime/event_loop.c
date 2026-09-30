#include "forge/event.h"
#include "forge/platform.h"
#include <stdlib.h>
#include <string.h>

#if defined(FORGE_OS_LINUX)
#include <sys/epoll.h>
#include <unistd.h>
#elif defined(FORGE_OS_MACOS)
#include <sys/event.h>
#include <unistd.h>
#else
#include <errno.h>
#if !defined(FORGE_OS_WINDOWS)
#include <sys/select.h>
#include <unistd.h>
#endif
#endif

#define FR_EVENT_ONESHOT 0x80000000u

struct fr_event_entry {
    int fd;
    uint32_t events;
    void *userdata;
    int active;
};

struct fr_event_loop {
#if defined(FORGE_OS_LINUX)
    int epfd;
#elif defined(FORGE_OS_MACOS)
    int kqfd;
#else
    struct fr_event_entry entries[128];
    int entry_count;
#endif
    fr_event_cb_t cb;
};

fr_event_loop_t *fr_event_loop_create(void) {
    fr_event_loop_t *loop = (fr_event_loop_t *)calloc(1, sizeof(fr_event_loop_t));
    if (!loop) return NULL;
#if defined(FORGE_OS_LINUX)
    loop->epfd = epoll_create1(0);
    if (loop->epfd < 0) { free(loop); return NULL; }
#elif defined(FORGE_OS_MACOS)
    loop->kqfd = kqueue();
    if (loop->kqfd < 0) { free(loop); return NULL; }
#endif
    return loop;
}

void fr_event_loop_destroy(fr_event_loop_t *loop) {
    if (!loop) return;
#if defined(FORGE_OS_LINUX)
    if (loop->epfd >= 0) close(loop->epfd);
#elif defined(FORGE_OS_MACOS)
    if (loop->kqfd >= 0) close(loop->kqfd);
#endif
    free(loop);
}

void fr_event_loop_set_cb(fr_event_loop_t *loop, fr_event_cb_t cb) {
    if (loop) loop->cb = cb;
}

static struct fr_event_entry *find_entry(fr_event_loop_t *loop, int fd) {
#if defined(FORGE_OS_LINUX) || defined(FORGE_OS_MACOS)
    (void)loop; (void)fd;
    return NULL;
#else
    for (int i = 0; i < loop->entry_count; i++) {
        if (loop->entries[i].active && loop->entries[i].fd == fd) {
            return &loop->entries[i];
        }
    }
    return NULL;
#endif
}

int fr_event_loop_add(fr_event_loop_t *loop, int fd, uint32_t events, void *userdata) {
    if (!loop || fd < 0) return -1;
    uint32_t oneshot = events & FR_EVENT_ONESHOT;
    events &= ~FR_EVENT_ONESHOT;
#if defined(FORGE_OS_LINUX)
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = 0;
    if (events & FR_EVENT_READ) ev.events |= EPOLLIN;
    if (events & FR_EVENT_WRITE) ev.events |= EPOLLOUT;
    if (oneshot) ev.events |= EPOLLONESHOT;
    ev.data.ptr = userdata;
    return epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev);
#elif defined(FORGE_OS_MACOS)
    struct kevent changes[2];
    int n = 0;
    if (events & FR_EVENT_READ) {
        EV_SET(&changes[n++], fd, EVFILT_READ, EV_ADD | (oneshot ? EV_ONESHOT : 0), 0, 0, userdata);
    }
    if (events & FR_EVENT_WRITE) {
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD | (oneshot ? EV_ONESHOT : 0), 0, 0, userdata);
    }
    return kevent(loop->kqfd, changes, n, NULL, 0, NULL);
#else
    if (loop->entry_count >= 128) return -1;
    struct fr_event_entry *e = find_entry(loop, fd);
    if (!e) e = &loop->entries[loop->entry_count++];
    e->fd = fd;
    e->events = events | (oneshot ? FR_EVENT_ONESHOT : 0);
    e->userdata = userdata;
    e->active = 1;
    return 0;
#endif
}

int fr_event_loop_mod(fr_event_loop_t *loop, int fd, uint32_t events, void *userdata) {
    fr_event_loop_del(loop, fd);
    return fr_event_loop_add(loop, fd, events, userdata);
}

int fr_event_loop_del(fr_event_loop_t *loop, int fd) {
    if (!loop || fd < 0) return -1;
#if defined(FORGE_OS_LINUX)
    return epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL);
#elif defined(FORGE_OS_MACOS)
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(loop->kqfd, changes, 2, NULL, 0, NULL);
    return 0;
#else
    struct fr_event_entry *e = find_entry(loop, fd);
    if (e) e->active = 0;
    return 0;
#endif
}

int fr_event_loop_poll(fr_event_loop_t *loop, int timeout_ms) {
    if (!loop) return -1;
#if defined(FORGE_OS_LINUX)
    struct epoll_event events[64];
    int n = epoll_wait(loop->epfd, events, 64, timeout_ms);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        if (loop->cb) loop->cb(loop, 0, events[i].events, events[i].data.ptr);
    }
    return n;
#elif defined(FORGE_OS_MACOS)
    struct kevent events[64];
    struct timespec ts;
    struct timespec *tp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        tp = &ts;
    }
    int n = kevent(loop->kqfd, NULL, 0, events, 64, tp);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        uint32_t ev = 0;
        if (events[i].filter == EVFILT_READ) ev |= FR_EVENT_READ;
        if (events[i].filter == EVFILT_WRITE) ev |= FR_EVENT_WRITE;
        if (loop->cb) loop->cb(loop, (int)events[i].ident, ev, events[i].udata);
    }
    return n;
#else
#if defined(FORGE_OS_WINDOWS)
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    int maxfd = 0;
    for (int i = 0; i < loop->entry_count; i++) {
        if (!loop->entries[i].active) continue;
        int fd = loop->entries[i].fd;
        if (fd > maxfd) maxfd = fd;
        if (loop->entries[i].events & FR_EVENT_READ) FD_SET(fd, &rfds);
        if (loop->entries[i].events & FR_EVENT_WRITE) FD_SET(fd, &wfds);
    }
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int n = select(maxfd + 1, &rfds, &wfds, NULL, tvp);
    if (n < 0) return -1;
    int fired = 0;
    for (int i = 0; i < loop->entry_count; i++) {
        if (!loop->entries[i].active) continue;
        int fd = loop->entries[i].fd;
        uint32_t ev = 0;
        if (FD_ISSET(fd, &rfds)) ev |= FR_EVENT_READ;
        if (FD_ISSET(fd, &wfds)) ev |= FR_EVENT_WRITE;
        if (!ev) continue;
        if (loop->cb) loop->cb(loop, fd, ev, loop->entries[i].userdata);
        if (loop->entries[i].events & FR_EVENT_ONESHOT) loop->entries[i].active = 0;
        fired++;
    }
    return fired;
#else
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    int maxfd = 0;
    for (int i = 0; i < loop->entry_count; i++) {
        if (!loop->entries[i].active) continue;
        int fd = loop->entries[i].fd;
        if (fd > maxfd) maxfd = fd;
        if (loop->entries[i].events & FR_EVENT_READ) FD_SET(fd, &rfds);
        if (loop->entries[i].events & FR_EVENT_WRITE) FD_SET(fd, &wfds);
    }
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int n = select(maxfd + 1, &rfds, &wfds, NULL, tvp);
    if (n < 0) return -1;
    int fired = 0;
    for (int i = 0; i < loop->entry_count; i++) {
        if (!loop->entries[i].active) continue;
        int fd = loop->entries[i].fd;
        uint32_t ev = 0;
        if (FD_ISSET(fd, &rfds)) ev |= FR_EVENT_READ;
        if (FD_ISSET(fd, &wfds)) ev |= FR_EVENT_WRITE;
        if (!ev) continue;
        if (loop->cb) loop->cb(loop, fd, ev, loop->entries[i].userdata);
        if (loop->entries[i].events & FR_EVENT_ONESHOT) loop->entries[i].active = 0;
        fired++;
    }
    return fired;
#endif
#endif
}
