#define _POSIX_C_SOURCE 200809L
#include "forge/time.h"
#include "forge/platform.h"
#include <time.h>

#if !defined(FORGE_OS_WINDOWS)
#include <unistd.h>
#endif

int64_t fr_time_now_ms(void) {
#if defined(FORGE_OS_WINDOWS)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (int64_t)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return (int64_t)time(NULL) * 1000;
    }
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void fr_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
#if defined(FORGE_OS_WINDOWS)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}
