#include "forge/thread.h"
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>

struct fr_mutex {
    SRWLOCK lock;
};

struct fr_cond {
    CONDITION_VARIABLE cv;
};

struct fr_thread {
    HANDLE handle;
};

fr_mutex_t *fr_mutex_create(void) {
    fr_mutex_t *m = (fr_mutex_t *)calloc(1, sizeof(fr_mutex_t));
    if (m) InitializeSRWLock(&m->lock);
    return m;
}

void fr_mutex_destroy(fr_mutex_t *m) { free(m); }

void fr_mutex_lock(fr_mutex_t *m) {
    if (m) AcquireSRWLockExclusive(&m->lock);
}

void fr_mutex_unlock(fr_mutex_t *m) {
    if (m) ReleaseSRWLockExclusive(&m->lock);
}

fr_cond_t *fr_cond_create(void) {
    fr_cond_t *c = (fr_cond_t *)calloc(1, sizeof(fr_cond_t));
    if (c) InitializeConditionVariable(&c->cv);
    return c;
}

void fr_cond_destroy(fr_cond_t *c) { free(c); }

void fr_cond_broadcast(fr_cond_t *c) {
    if (c) WakeAllConditionVariable(&c->cv);
}

void fr_cond_wait(fr_cond_t *c, fr_mutex_t *m) {
    if (c && m) SleepConditionVariableSRW(&c->cv, &m->lock, INFINITE, 0);
}

typedef struct {
    fr_thread_fn fn;
    void *arg;
} fr_thread_start_t;

static unsigned __stdcall fr_thread_trampoline(void *p) {
    fr_thread_start_t *st = (fr_thread_start_t *)p;
    fr_thread_fn fn = st->fn;
    void *arg = st->arg;
    free(st);
    fn(arg);
    return 0;
}

int fr_thread_start(fr_thread_t **out, fr_thread_fn fn, void *arg) {
    fr_thread_t *t = (fr_thread_t *)calloc(1, sizeof(fr_thread_t));
    fr_thread_start_t *st = (fr_thread_start_t *)malloc(sizeof(fr_thread_start_t));
    if (!t || !st) { free(t); free(st); return -1; }
    st->fn = fn;
    st->arg = arg;
    t->handle = (HANDLE)_beginthreadex(NULL, 0, fr_thread_trampoline, st, 0, NULL);
    if (!t->handle) { free(t); free(st); return -1; }
    *out = t;
    return 0;
}

void fr_thread_detach(fr_thread_t *t) {
    if (t && t->handle) CloseHandle(t->handle);
    free(t);
}

int fr_thread_join(fr_thread_t *t) {
    if (!t || !t->handle) return -1;
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    free(t);
    return 0;
}

void fr_thread_yield(void) {
    SwitchToThread();
}

void fr_thread_pin_cpu(int cpu) {
    (void)cpu;
}

#else
#include <pthread.h>
#include <sched.h>

struct fr_mutex {
    pthread_mutex_t lock;
};

struct fr_cond {
    pthread_cond_t cv;
};

struct fr_thread {
    pthread_t handle;
};

fr_mutex_t *fr_mutex_create(void) {
    fr_mutex_t *m = (fr_mutex_t *)calloc(1, sizeof(fr_mutex_t));
    if (m) pthread_mutex_init(&m->lock, NULL);
    return m;
}

void fr_mutex_destroy(fr_mutex_t *m) {
    if (!m) return;
    pthread_mutex_destroy(&m->lock);
    free(m);
}

void fr_mutex_lock(fr_mutex_t *m) {
    if (m) pthread_mutex_lock(&m->lock);
}

void fr_mutex_unlock(fr_mutex_t *m) {
    if (m) pthread_mutex_unlock(&m->lock);
}

fr_cond_t *fr_cond_create(void) {
    fr_cond_t *c = (fr_cond_t *)calloc(1, sizeof(fr_cond_t));
    if (c) pthread_cond_init(&c->cv, NULL);
    return c;
}

void fr_cond_destroy(fr_cond_t *c) {
    if (!c) return;
    pthread_cond_destroy(&c->cv);
    free(c);
}

void fr_cond_broadcast(fr_cond_t *c) {
    if (c) pthread_cond_broadcast(&c->cv);
}

void fr_cond_wait(fr_cond_t *c, fr_mutex_t *m) {
    if (c && m) pthread_cond_wait(&c->cv, &m->lock);
}

int fr_thread_start(fr_thread_t **out, fr_thread_fn fn, void *arg) {
    fr_thread_t *t = (fr_thread_t *)calloc(1, sizeof(fr_thread_t));
    if (!t) return -1;
    if (pthread_create(&t->handle, NULL, fn, arg) != 0) {
        free(t);
        return -1;
    }
    *out = t;
    return 0;
}

void fr_thread_detach(fr_thread_t *t) {
    if (!t) return;
    pthread_detach(t->handle);
    free(t);
}

int fr_thread_join(fr_thread_t *t) {
    if (!t) return -1;
    pthread_join(t->handle, NULL);
    free(t);
    return 0;
}

void fr_thread_yield(void) {
    sched_yield();
}

void fr_thread_pin_cpu(int cpu) {
#if defined(FORGE_OS_LINUX)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET((unsigned)(cpu < 0 ? 0 : cpu), &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void)cpu;
#endif
}

#endif
