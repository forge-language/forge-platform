#include "forge/threading.h"
#include "forge/platform.h"
#include "forge/thread.h"
#include "forge_runtime.h"
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

#define THREAD_MAX_HANDLES 256
#define THREAD_MAX_MUTEXES 64

typedef struct {
    fr_thread_t *thread;
    int in_use;
} thread_slot_t;

static thread_slot_t g_threads[THREAD_MAX_HANDLES];
static fr_mutex_t *g_mutexes[THREAD_MAX_MUTEXES];
static fr_mutex_t *g_registry_lock;
static int64_t g_next_thread_id;
static fr_mutex_t *g_id_lock;

#if defined(_WIN32)
static __declspec(thread) int64_t tls_worker_id = -1;
#else
static __thread int64_t tls_worker_id = -1;
#endif

static void registry_init(void) {
    if (!g_registry_lock) {
        g_registry_lock = fr_mutex_create();
        g_id_lock = fr_mutex_create();
        memset(g_threads, 0, sizeof(g_threads));
        memset(g_mutexes, 0, sizeof(g_mutexes));
    }
}

int64_t fr_threading_cpu_count(void) {
    return (int64_t)fr_platform_cpu_count();
}

void fr_threading_yield(void) {
    fr_thread_yield();
}

void fr_threading_pin(int64_t cpu) {
    fr_thread_pin_cpu((int)cpu);
}

int64_t fr_threading_self(void) {
#if defined(_WIN32)
    return (int64_t)(uintptr_t)GetCurrentThreadId();
#elif defined(FORGE_OS_LINUX)
    return (int64_t)(uintptr_t)pthread_self();
#else
    registry_init();
    fr_mutex_lock(g_id_lock);
    static __thread int64_t cached = 0;
    if (cached == 0) cached = ++g_next_thread_id;
    int64_t id = cached;
    fr_mutex_unlock(g_id_lock);
    return id;
#endif
}

int64_t fr_threading_worker_id(void) {
    return tls_worker_id;
}

int64_t fr_threading_mutex_create(void) {
    registry_init();
    fr_mutex_lock(g_registry_lock);
    int64_t handle = -1;
    for (int i = 0; i < THREAD_MAX_MUTEXES; i++) {
        if (!g_mutexes[i]) {
            g_mutexes[i] = fr_mutex_create();
            if (g_mutexes[i]) handle = i;
            break;
        }
    }
    fr_mutex_unlock(g_registry_lock);
    return handle;
}

void fr_threading_mutex_lock(int64_t handle) {
    if (handle < 0 || handle >= THREAD_MAX_MUTEXES) return;
    if (g_mutexes[handle]) fr_mutex_lock(g_mutexes[handle]);
}

void fr_threading_mutex_unlock(int64_t handle) {
    if (handle < 0 || handle >= THREAD_MAX_MUTEXES) return;
    if (g_mutexes[handle]) fr_mutex_unlock(g_mutexes[handle]);
}

void fr_threading_mutex_destroy(int64_t handle) {
    if (handle < 0 || handle >= THREAD_MAX_MUTEXES) return;
    registry_init();
    fr_mutex_lock(g_registry_lock);
    if (g_mutexes[handle]) {
        fr_mutex_destroy(g_mutexes[handle]);
        g_mutexes[handle] = NULL;
    }
    fr_mutex_unlock(g_registry_lock);
}

typedef struct {
    fr_threading_fn1_t fn;
    int64_t arg;
} thread_arg1_t;

typedef struct {
    fr_threading_fn2_t fn;
    int64_t id;
    int64_t total;
} thread_arg2_t;

static void *spawn_trampoline(void *p) {
    thread_arg1_t *a = (thread_arg1_t *)p;
    fr_threading_fn1_t fn = a->fn;
    int64_t arg = a->arg;
    free(a);
    fn(arg);
    return NULL;
}

static void *indexed_trampoline(void *p) {
    thread_arg2_t *a = (thread_arg2_t *)p;
    fr_threading_fn2_t fn = a->fn;
    int64_t id = a->id;
    int64_t total = a->total;
    free(a);
    tls_worker_id = id;
    fn(id, total);
    tls_worker_id = -1;
    return NULL;
}

static int64_t alloc_thread_slot(fr_thread_t *t) {
    registry_init();
    fr_mutex_lock(g_registry_lock);
    int64_t handle = -1;
    for (int i = 0; i < THREAD_MAX_HANDLES; i++) {
        if (!g_threads[i].in_use) {
            g_threads[i].thread = t;
            g_threads[i].in_use = 1;
            handle = i;
            break;
        }
    }
    fr_mutex_unlock(g_registry_lock);
    return handle;
}

static fr_thread_t *take_thread_slot(int64_t handle) {
    if (handle < 0 || handle >= THREAD_MAX_HANDLES) return NULL;
    registry_init();
    fr_mutex_lock(g_registry_lock);
    fr_thread_t *t = NULL;
    if (g_threads[handle].in_use) {
        t = g_threads[handle].thread;
        g_threads[handle].thread = NULL;
        g_threads[handle].in_use = 0;
    }
    fr_mutex_unlock(g_registry_lock);
    return t;
}

int fr_threading_use_scheduler_pool(void) {
    return fr_sched_pool_available();
}

int64_t fr_threading_spawn(fr_threading_fn1_t fn, int64_t arg) {
    if (!fn) return -1;
    if (fr_sched_pool_available()) return fr_sched_pool_spawn((fr_sched_native_fn1_t)fn, arg);
    thread_arg1_t *ctx = (thread_arg1_t *)malloc(sizeof(thread_arg1_t));
    if (!ctx) return -1;
    ctx->fn = fn;
    ctx->arg = arg;
    fr_thread_t *t = NULL;
    if (fr_thread_start(&t, spawn_trampoline, ctx) != 0) {
        free(ctx);
        return -1;
    }
    int64_t handle = alloc_thread_slot(t);
    if (handle < 0) {
        fr_thread_detach(t);
        return -1;
    }
    return handle;
}

void fr_threading_spawn_indexed(fr_threading_fn2_t fn, int64_t count) {
    if (!fn || count <= 0) return;
    if (fr_sched_pool_available()) {
        fr_sched_pool_spawn_indexed((fr_sched_native_fn2_t)fn, count);
        return;
    }
    if (count > THREAD_MAX_HANDLES) count = THREAD_MAX_HANDLES;

    fr_thread_t **threads = (fr_thread_t **)calloc((size_t)count, sizeof(fr_thread_t *));
    if (!threads) return;

    int started = 0;
    for (int64_t i = 0; i < count; i++) {
        thread_arg2_t *ctx = (thread_arg2_t *)malloc(sizeof(thread_arg2_t));
        if (!ctx) break;
        ctx->fn = fn;
        ctx->id = i;
        ctx->total = count;
        if (fr_thread_start(&threads[started], indexed_trampoline, ctx) != 0) {
            free(ctx);
            break;
        }
        started++;
    }

    for (int i = 0; i < started; i++) {
        if (threads[i]) fr_thread_join(threads[i]);
    }
    free(threads);
}

int64_t fr_threading_join(int64_t handle) {
    fr_thread_t *t = take_thread_slot(handle);
    if (!t) return -1;
    return fr_thread_join(t) == 0 ? 0 : -1;
}

void fr_threading_join_all(void) {
    registry_init();
    fr_mutex_lock(g_registry_lock);
    for (int i = 0; i < THREAD_MAX_HANDLES; i++) {
        if (g_threads[i].in_use && g_threads[i].thread) {
            fr_thread_t *t = g_threads[i].thread;
            g_threads[i].thread = NULL;
            g_threads[i].in_use = 0;
            fr_mutex_unlock(g_registry_lock);
            fr_thread_join(t);
            fr_mutex_lock(g_registry_lock);
        }
    }
    fr_mutex_unlock(g_registry_lock);
}
