#include "forge_runtime.h"
#include "work_queue.h"
#include "forge/event.h"
#include "forge/platform.h"
#include "forge/thread.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static char *fr_strdup(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

#define MAILBOX_CAP 256

typedef struct fr_mailbox {
    fr_msg_t msgs[MAILBOX_CAP];
    size_t head;
    size_t tail;
    size_t count;
} fr_mailbox_t;

struct fr_coro {
    int id;
    fr_coro_fn fn;
    void *state;
    size_t state_size;
    fr_coro_status_t status;
    int step;
    int on_queue;
    int executing;
    fr_process_t *proc;
    int await_fd;
    uint32_t await_events;
    int await_ready;
    struct fr_coro *next;
};

struct fr_process {
    char *name;
    fr_scheduler_t *sched;
    fr_mailbox_t mailbox;
    fr_coro_t *coros;
    fr_coro_t *coro_tail;
    int next_coro_id;
    fr_coro_fn receive_handler;
    size_t receive_state_size;
    int is_supervisor;
    fr_restart_policy_t restart_policy;
    fr_process_t **children;
    size_t child_count;
    int worker_id;
    fr_mutex_t *lock;
    fr_cond_t *msg_cond;
    struct fr_process *next;
};

struct fr_scheduler {
    fr_process_t *processes;
    int worker_count;
    atomic_int running;
    atomic_int workers_started;
    int stopping;
    size_t runnable;
    unsigned next_queue;
    unsigned next_worker;
    int native_pending;
    fr_thread_t **workers;
    fr_run_queue_t *worker_queues;
    fr_native_queue_t *native_queues;
    fr_event_loop_t *event_loop;
    fr_mutex_t *lock;
    fr_cond_t *idle_cond;
    fr_cond_t *state_cond;
};

static _Thread_local fr_coro_t *tls_current_coro = NULL;
static _Thread_local int tls_worker_id = -1;
static _Atomic(fr_scheduler_t *) g_global_sched = NULL;

fr_coro_t *fr_coro_current(void) {
    return tls_current_coro;
}

fr_scheduler_t *fr_scheduler_global(void) {
    return g_global_sched;
}

void fr_scheduler_set_global(fr_scheduler_t *sched) {
    g_global_sched = sched;
}

int fr_sched_pool_available(void) {
    fr_scheduler_t *sched = g_global_sched;
    return sched && sched->running && sched->workers_started;
}

static int default_worker_count(int n) {
    if (n > 0) return n;
    long cpus = fr_platform_cpu_count();
    return cpus > 0 ? (int)cpus : 4;
}

static int mailbox_push(fr_mailbox_t *mb, fr_msg_t msg) {
    if (mb->count >= MAILBOX_CAP) return 0;
    mb->msgs[mb->tail] = msg;
    mb->tail = (mb->tail + 1) % MAILBOX_CAP;
    mb->count++;
    return 1;
}

static int mailbox_pop(fr_mailbox_t *mb, fr_msg_t *out) {
    if (mb->count == 0) return 0;
    *out = mb->msgs[mb->head];
    mb->head = (mb->head + 1) % MAILBOX_CAP;
    mb->count--;
    return 1;
}

/* Scheduler state and queue publication share this mutex. A running coroutine
 * stays exclusively owned by its worker until the worker commits its result. */
static void enqueue_coro_locked(fr_scheduler_t *sched, fr_coro_t *coro) {
    if (!coro || coro->on_queue || coro->executing) return;
    if (coro->status != FR_CORO_RUNNING) return;
    int wid = coro->proc->worker_id % sched->worker_count;
    if (!fr_run_queue_try_push(&sched->worker_queues[wid], coro)) {
        coro->status = FR_CORO_ERROR;
        return;
    }
    coro->on_queue = 1;
    sched->runnable++;
    fr_cond_signal(sched->idle_cond);
}

static void event_resume_cb(fr_event_loop_t *loop, int fd, uint32_t events, void *userdata) {
    (void)loop;
    (void)fd;
    (void)events;
    fr_coro_t *coro = (fr_coro_t *)userdata;
    if (!coro || !coro->proc || !coro->proc->sched) return;
    fr_scheduler_t *sched = coro->proc->sched;
    fr_mutex_lock(sched->lock);
    if (coro->await_fd >= 0) {
        coro->await_ready = 1;
        if (!coro->executing && coro->status == FR_CORO_WAITING_IO) {
            coro->status = FR_CORO_RUNNING;
            enqueue_coro_locked(sched, coro);
        }
    }
    fr_mutex_unlock(sched->lock);
}

#define FR_REDUCTION_BUDGET 2000

static int sched_any_active(fr_scheduler_t *sched) {
    for (fr_process_t *p = sched->processes; p; p = p->next) {
        for (fr_coro_t *c = p->coros; c; c = c->next) {
            if (c->executing || (c->status != FR_CORO_DONE && c->status != FR_CORO_ERROR))
                return 1;
        }
    }
    return 0;
}

static void scan_enqueue_runnable(fr_scheduler_t *sched) {
    for (fr_process_t *p = sched->processes; p; p = p->next) {
        for (fr_coro_t *c = p->coros; c; c = c->next)
            enqueue_coro_locked(sched, c);
    }
}

typedef struct {
    fr_scheduler_t *sched;
    int wid;
} fr_worker_arg_t;

static void *worker_main(void *arg) {
    fr_worker_arg_t *wa = (fr_worker_arg_t *)arg;
    fr_scheduler_t *sched = wa->sched;
    int wid = wa->wid;
    free(wa);

    int cpus = fr_platform_cpu_count();
    if (cpus > 0) fr_thread_pin_cpu(wid % cpus);
    tls_worker_id = wid;

    for (;;) {
        fr_mutex_lock(sched->lock);
        while (sched->running && sched->runnable == 0)
            fr_cond_wait(sched->idle_cond, sched->lock);
        if (!sched->running) { fr_mutex_unlock(sched->lock); break; }

        fr_coro_t *coro = fr_run_queue_pop(&sched->worker_queues[wid]);
        if (!coro) {
            for (int i = 0; i < sched->worker_count; i++) {
                if (i == wid) continue;
                coro = fr_run_queue_steal(&sched->worker_queues[i], &sched->worker_queues[wid]);
                if (coro) break;
            }
        }
        if (coro) {
            sched->runnable--;
            coro->on_queue = 0;
            coro->executing = 1;
            fr_mutex_unlock(sched->lock);
            fr_coro_status_t status = FR_CORO_RUNNING;
            int budget = FR_REDUCTION_BUDGET;
            tls_current_coro = coro;
            while (budget-- > 0 && sched->running) {
                status = coro->fn(coro, coro->state);
                if (status == FR_CORO_YIELDED) status = FR_CORO_RUNNING;
                if (status != FR_CORO_RUNNING) break;
            }
            tls_current_coro = NULL;
            fr_mutex_lock(sched->lock);
            coro->executing = 0;
            if (status == FR_CORO_WAITING_IO && coro->await_ready)
                status = FR_CORO_RUNNING;
            if (status == FR_CORO_WAITING_RECV) {
                fr_mutex_lock(coro->proc->lock);
                if (coro->proc->mailbox.count > 0) status = FR_CORO_RUNNING;
                fr_mutex_unlock(coro->proc->lock);
            }
            coro->status = status;
            enqueue_coro_locked(sched, coro);
            fr_cond_signal(sched->state_cond);
            fr_mutex_unlock(sched->lock);
            continue;
        }

        void *narg = NULL;
        fr_native_fn nfn = fr_native_queue_pop(&sched->native_queues[wid], &narg);
        if (!nfn) {
            for (int i = 0; i < sched->worker_count; i++) {
                if (i == wid) continue;
                nfn = fr_native_queue_steal(&sched->native_queues[i], &narg);
                if (nfn) break;
            }
        }
        if (nfn) {
            sched->runnable--;
            fr_mutex_unlock(sched->lock);
            nfn(narg);
            fr_mutex_lock(sched->lock);
            sched->native_pending--;
            fr_cond_signal(sched->state_cond);
        }
        fr_mutex_unlock(sched->lock);
    }
    tls_worker_id = -1;
    return NULL;
}

fr_scheduler_t *fr_scheduler_create(int worker_count) {
    fr_scheduler_t *s = (fr_scheduler_t *)calloc(1, sizeof(fr_scheduler_t));
    if (!s) return NULL;
    atomic_init(&s->running, 0);
    atomic_init(&s->workers_started, 0);
    s->worker_count = default_worker_count(worker_count);
    s->event_loop = fr_event_loop_create();
    if (s->event_loop) fr_event_loop_set_cb(s->event_loop, event_resume_cb);
    s->lock = fr_mutex_create();
    s->idle_cond = fr_cond_create();
    s->state_cond = fr_cond_create();
    s->worker_queues = (fr_run_queue_t *)calloc((size_t)s->worker_count, sizeof(fr_run_queue_t));
    s->native_queues = (fr_native_queue_t *)calloc((size_t)s->worker_count, sizeof(fr_native_queue_t));
    s->workers = (fr_thread_t **)calloc((size_t)s->worker_count, sizeof(fr_thread_t *));
    if (!s->event_loop || !s->lock || !s->idle_cond || !s->state_cond ||
        !s->worker_queues || !s->native_queues || !s->workers) {
        fr_scheduler_destroy(s);
        return NULL;
    }
    for (int i = 0; i < s->worker_count; i++) {
        fr_run_queue_init(&s->worker_queues[i]);
        fr_native_queue_init(&s->native_queues[i]);
    }
    return s;
}

void fr_scheduler_destroy(fr_scheduler_t *sched) {
    if (!sched) return;
    fr_scheduler_stop(sched);
    if (sched->worker_queues) {
        for (int i = 0; i < sched->worker_count; i++) {
            fr_run_queue_destroy(&sched->worker_queues[i]);
        }
    }
    if (sched->native_queues) {
        for (int i = 0; i < sched->worker_count; i++) {
            fr_native_queue_destroy(&sched->native_queues[i]);
        }
    }
    fr_process_t *p = sched->processes;
    while (p) {
        fr_process_t *next = p->next;
        fr_process_destroy(p);
        p = next;
    }
    fr_event_loop_destroy(sched->event_loop);
    fr_mutex_destroy(sched->lock);
    fr_cond_destroy(sched->idle_cond);
    fr_cond_destroy(sched->state_cond);
    free(sched->worker_queues);
    free(sched->native_queues);
    free(sched->workers);
    free(sched);
}

static void scheduler_start_workers(fr_scheduler_t *sched) {
    if (!sched || sched->workers_started) return;
    sched->workers_started = 1;
    for (int i = 0; i < sched->worker_count; i++) {
        fr_worker_arg_t *wa = (fr_worker_arg_t *)malloc(sizeof(fr_worker_arg_t));
        if (!wa) continue;
        wa->sched = sched;
        wa->wid = i;
        if (fr_thread_start(&sched->workers[i], worker_main, wa) != 0) free(wa);
    }
}

void fr_scheduler_start(fr_scheduler_t *sched) {
    if (!sched) return;
    sched->running = 1;
    fr_scheduler_set_global(sched);
    fr_mutex_lock(sched->lock);
    scan_enqueue_runnable(sched);
    scheduler_start_workers(sched);
    fr_mutex_unlock(sched->lock);
}

void fr_scheduler_stop(fr_scheduler_t *sched) {
    if (!sched) return;
    fr_mutex_lock(sched->lock);
    while (sched->stopping) fr_cond_wait(sched->idle_cond, sched->lock);
    sched->stopping = 1;
    sched->running = 0;
    fr_cond_broadcast(sched->idle_cond);
    fr_cond_broadcast(sched->state_cond);
    fr_mutex_unlock(sched->lock);
    for (int i = 0; sched->workers && i < sched->worker_count; i++) {
        if (sched->workers[i]) fr_thread_join(sched->workers[i]);
        sched->workers[i] = NULL;
    }
    fr_mutex_lock(sched->lock);
    sched->workers_started = 0;
    sched->stopping = 0;
    fr_cond_broadcast(sched->idle_cond);
    fr_mutex_unlock(sched->lock);
    fr_scheduler_t *expected = sched;
    atomic_compare_exchange_strong(&g_global_sched, &expected, NULL);
}

static int sched_pool_submit(fr_scheduler_t *sched, fr_native_fn fn, void *arg) {
    if (!sched || !fn) return 0;
    fr_mutex_lock(sched->lock);
    int q = (int)(sched->next_queue++ % (unsigned)sched->worker_count);
    int ok = fr_native_queue_try_push(&sched->native_queues[q], fn, arg);
    if (ok) {
        sched->native_pending++;
        sched->runnable++;
        fr_cond_signal(sched->idle_cond);
    }
    fr_mutex_unlock(sched->lock);
    return ok;
}

void fr_sched_pool_submit(fr_scheduler_t *sched, void (*fn)(void *), void *arg) {
    (void)sched_pool_submit(sched, fn, arg);
}

typedef struct {
    fr_sched_native_fn1_t fn;
    int64_t arg;
} sched_native_arg1_t;

typedef struct {
    fr_sched_native_fn2_t fn;
    int64_t id;
    int64_t total;
    fr_mutex_t *lock;
    int *remaining;
    fr_cond_t *done;
} sched_indexed_ctx_t;

static void sched_native_trampoline1(void *p) {
    sched_native_arg1_t *ctx = (sched_native_arg1_t *)p;
    fr_sched_native_fn1_t fn = ctx->fn;
    int64_t arg = ctx->arg;
    free(ctx);
    fn(arg);
}

static void sched_native_trampoline2(void *p) {
    sched_indexed_ctx_t *ctx = (sched_indexed_ctx_t *)p;
    ctx->fn(ctx->id, ctx->total);
    fr_mutex_lock(ctx->lock);
    (*ctx->remaining)--;
    if (*ctx->remaining == 0) fr_cond_broadcast(ctx->done);
    fr_mutex_unlock(ctx->lock);
    free(ctx);
}

int64_t fr_sched_pool_spawn(fr_sched_native_fn1_t fn, int64_t arg) {
    fr_scheduler_t *sched = g_global_sched;
    if (!sched || !sched->running || !fn) return -1;
    sched_native_arg1_t *ctx = (sched_native_arg1_t *)malloc(sizeof(sched_native_arg1_t));
    if (!ctx) return -1;
    ctx->fn = fn;
    ctx->arg = arg;
    if (!sched_pool_submit(sched, sched_native_trampoline1, ctx)) { free(ctx); return -1; }
    return 0;
}

void fr_sched_pool_spawn_indexed(fr_sched_native_fn2_t fn, int64_t count) {
    fr_scheduler_t *sched = g_global_sched;
    if (!sched || !sched->running || !fn || count <= 0) return;
    if (count > sched->worker_count * 64) count = sched->worker_count * 64;

    fr_mutex_t *lock = fr_mutex_create();
    fr_cond_t *done = fr_cond_create();
    int remaining = 0;
    if (!lock || !done) {
        fr_mutex_destroy(lock);
        fr_cond_destroy(done);
        return;
    }

    for (int64_t i = 0; i < count; i++) {
        sched_indexed_ctx_t *ctx = (sched_indexed_ctx_t *)calloc(1, sizeof(sched_indexed_ctx_t));
        if (!ctx) break;
        ctx->fn = fn;
        ctx->id = i;
        ctx->total = count;
        ctx->lock = lock;
        ctx->remaining = &remaining;
        ctx->done = done;
        fr_mutex_lock(lock);
        remaining++;
        if (!sched_pool_submit(sched, sched_native_trampoline2, ctx)) {
            remaining--;
            free(ctx);
            fr_mutex_unlock(lock);
            break;
        }
        fr_mutex_unlock(lock);
    }

    fr_mutex_lock(lock);
    while (remaining > 0) fr_cond_wait(done, lock);
    fr_mutex_unlock(lock);
    fr_mutex_destroy(lock);
    fr_cond_destroy(done);
}

void fr_scheduler_add_process(fr_scheduler_t *sched, fr_process_t *proc) {
    fr_mutex_lock(sched->lock);
    proc->sched = sched;
    proc->worker_id = (int)(sched->next_worker++ % (unsigned)sched->worker_count);
    proc->next = sched->processes;
    sched->processes = proc;
    for (fr_coro_t *c = proc->coros; c; c = c->next) enqueue_coro_locked(sched, c);
    fr_mutex_unlock(sched->lock);
}

fr_event_loop_t *fr_scheduler_event_loop(fr_scheduler_t *sched) {
    return sched ? sched->event_loop : NULL;
}

int fr_scheduler_worker_count(fr_scheduler_t *sched) {
    return sched ? sched->worker_count : 0;
}

static int sched_waiting_io(fr_scheduler_t *sched) {
    for (fr_process_t *p = sched->processes; p; p = p->next) {
        for (fr_coro_t *c = p->coros; c; c = c->next) {
            if (c->status == FR_CORO_WAITING_IO && c->await_fd >= 0 && !c->await_ready)
                return 1;
        }
    }
    return 0;
}

void fr_scheduler_run(fr_scheduler_t *sched) {
    if (!sched) return;
    fr_scheduler_start(sched);
    fr_mutex_lock(sched->lock);
    while (sched->running) {
        if (!sched_any_active(sched) && sched->native_pending == 0) break;
        if (sched_waiting_io(sched)) {
            /* Never hold state across a blocking poll: callbacks and workers
             * need it to publish readiness/completion. Stop latency is bounded. */
            fr_mutex_unlock(sched->lock);
            fr_event_loop_poll(sched->event_loop, 10);
            fr_mutex_lock(sched->lock);
        } else {
            /* Running jobs and mailbox waiters do not need an event-loop poll.
             * Await registration and worker completion wake this predicate. */
            fr_cond_wait(sched->state_cond, sched->lock);
        }
    }
    fr_mutex_unlock(sched->lock);
    fr_scheduler_stop(sched);
}

fr_process_t *fr_process_create(const char *name) {
    fr_process_t *p = (fr_process_t *)calloc(1, sizeof(fr_process_t));
    if (!p) return NULL;
    p->name = fr_strdup(name ? name : "process");
    p->next_coro_id = 1;
    p->lock = fr_mutex_create();
    p->msg_cond = fr_cond_create();
    return p;
}

void fr_process_destroy(fr_process_t *proc) {
    if (!proc) return;
    fr_coro_t *c = proc->coros;
    while (c) {
        fr_coro_t *next = c->next;
        free(c->state);
        free(c);
        c = next;
    }
    for (size_t i = 0; i < proc->mailbox.count; i++) {
        fr_msg_t *m = &proc->mailbox.msgs[(proc->mailbox.head + i) % MAILBOX_CAP];
        if (m->owns_payload && m->payload) free(m->payload);
    }
    fr_mutex_destroy(proc->lock);
    fr_cond_destroy(proc->msg_cond);
    free(proc->children);
    free(proc->name);
    free(proc);
}

fr_scheduler_t *fr_process_scheduler(fr_process_t *proc) {
    return proc->sched;
}

const char *fr_process_name(fr_process_t *proc) {
    return proc ? proc->name : "";
}

void fr_process_set_receive_handler(fr_process_t *proc, fr_coro_fn handler, size_t state_size) {
    proc->receive_handler = handler;
    proc->receive_state_size = state_size;
}

fr_coro_t *fr_coro_spawn(fr_process_t *proc, fr_coro_fn fn, void *init_state, size_t state_size) {
    fr_coro_t *c = (fr_coro_t *)calloc(1, sizeof(fr_coro_t));
    if (!c) return NULL;
    if (!fn) { free(c); return NULL; }
    c->fn = fn;
    c->state_size = state_size;
    c->state = malloc(state_size ? state_size : 1);
    if (!c->state) {
        free(c);
        return NULL;
    }
    if (state_size) memcpy(c->state, init_state, state_size);
    c->status = FR_CORO_RUNNING;
    c->step = 0;
    c->proc = proc;
    c->await_fd = -1;
    if (proc->sched) fr_mutex_lock(proc->sched->lock);
    fr_mutex_lock(proc->lock);
    c->id = proc->next_coro_id++;
    if (!proc->coros) proc->coros = proc->coro_tail = c;
    else { proc->coro_tail->next = c; proc->coro_tail = c; }
    free(init_state);
    fr_mutex_unlock(proc->lock);
    if (proc->sched) {
        enqueue_coro_locked(proc->sched, c);
        fr_mutex_unlock(proc->sched->lock);
    }
    return c;
}

fr_coro_status_t fr_yield(fr_coro_t *coro) {
    (void)coro;
    return FR_CORO_YIELDED;
}

int fr_coro_id(fr_coro_t *coro) {
    return coro ? coro->id : -1;
}

fr_process_t *fr_coro_process(fr_coro_t *coro) {
    return coro ? coro->proc : NULL;
}

int fr_coro_step(fr_coro_t *coro) {
    return coro ? coro->step : 0;
}

void fr_coro_set_step(fr_coro_t *coro, int step) {
    if (coro) coro->step = step;
}

void fr_send(fr_process_t *dst, int tag, int64_t value, void *payload, size_t payload_size) {
    if (!dst) return;
    fr_msg_t msg = { tag, value, payload, payload_size, payload ? 1 : 0, NULL };
    fr_mutex_lock(dst->lock);
    mailbox_push(&dst->mailbox, msg);
    fr_cond_broadcast(dst->msg_cond);
    fr_mutex_unlock(dst->lock);
    if (dst->sched) {
        fr_mutex_lock(dst->sched->lock);
        for (fr_coro_t *c = dst->coros; c; c = c->next) {
            if (!c->executing && c->status == FR_CORO_WAITING_RECV) {
                c->status = FR_CORO_RUNNING;
                enqueue_coro_locked(dst->sched, c);
            }
        }
        fr_mutex_unlock(dst->sched->lock);
    }
}

int fr_try_recv(fr_process_t *self, fr_msg_t *out) {
    if (!self || !out) return 0;
    fr_mutex_lock(self->lock);
    int ok = mailbox_pop(&self->mailbox, out);
    fr_mutex_unlock(self->lock);
    return ok;
}

int fr_recv(fr_process_t *self, fr_msg_t *out) {
    if (!self || !out) return 0;
    fr_mutex_lock(self->lock);
    while (!mailbox_pop(&self->mailbox, out)) {
        fr_cond_wait(self->msg_cond, self->lock);
    }
    fr_mutex_unlock(self->lock);
    return 1;
}

void fr_msg_free_payload(fr_msg_t *msg) {
    if (msg && msg->owns_payload && msg->payload) {
        free(msg->payload);
        msg->payload = NULL;
        msg->owns_payload = 0;
    }
}

int64_t fr_await_fd(fr_coro_t *coro, int64_t fd, uint32_t events) {
    if (!coro) return 1;
    if (!coro->proc || !coro->proc->sched || fd < 0) return -1;
    fr_scheduler_t *sched = coro->proc->sched;
    fr_mutex_lock(sched->lock);
    if (coro->await_ready && coro->await_fd == (int)fd) {
        coro->await_ready = 0;
        coro->await_fd = -1;
        fr_event_loop_del(sched->event_loop, (int)fd);
        fr_mutex_unlock(sched->lock);
        return 1;
    }
    coro->await_fd = (int)fd;
    coro->await_events = events;
    coro->await_ready = 0;
    coro->status = FR_CORO_WAITING_IO;
    int ok = fr_event_loop_add(sched->event_loop, (int)fd, events | FR_EVENT_ONESHOT, coro);
    fr_cond_signal(sched->state_cond);
    if (ok < 0) { coro->await_fd = -1; coro->status = FR_CORO_ERROR; }
    fr_mutex_unlock(sched->lock);
    return ok < 0 ? -1 : 0;
}

fr_process_t *fr_supervisor_create(const char *name, fr_restart_policy_t policy) {
    fr_process_t *p = fr_process_create(name);
    p->is_supervisor = 1;
    p->restart_policy = policy;
    return p;
}

void fr_supervisor_add_child(fr_process_t *supervisor, fr_process_t *child) {
    supervisor->child_count++;
    supervisor->children = (fr_process_t **)realloc(
        supervisor->children, supervisor->child_count * sizeof(fr_process_t *));
    supervisor->children[supervisor->child_count - 1] = child;
}

int fr_event_poll(fr_scheduler_t *sched, int timeout_ms) {
    if (!sched || !sched->event_loop) return -1;
    return fr_event_loop_poll(sched->event_loop, timeout_ms);
}

int64_t fr_event_add_read(fr_scheduler_t *sched, int64_t fd) {
    if (!sched || !sched->event_loop || fd < 0) return -1;
    if (fr_event_loop_add(sched->event_loop, (int)fd, FR_EVENT_READ, NULL) < 0) return -1;
    return fd;
}
