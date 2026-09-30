#include "forge_runtime.h"
#include "work_queue.h"
#include "forge/event.h"
#include "forge/platform.h"
#include "forge/thread.h"
#include <stdlib.h>
#include <string.h>

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
    int running;
    int done;
    int workers_started;
    int native_pending;
    fr_thread_t **workers;
    fr_run_queue_t *worker_queues;
    fr_native_queue_t *native_queues;
    fr_event_loop_t *event_loop;
    fr_mutex_t *lock;
    fr_cond_t *idle_cond;
};

static _Thread_local fr_coro_t *tls_current_coro = NULL;
static _Thread_local int tls_worker_id = -1;
static fr_scheduler_t *g_global_sched = NULL;

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
    return g_global_sched && g_global_sched->running && g_global_sched->workers_started;
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

static void enqueue_coro(fr_scheduler_t *sched, fr_coro_t *coro) {
    if (!coro || coro->on_queue) return;
    if (coro->status == FR_CORO_DONE || coro->status == FR_CORO_ERROR) return;
    coro->on_queue = 1;
    int wid = coro->proc->worker_id % sched->worker_count;
    fr_run_queue_push(&sched->worker_queues[wid], coro);
    fr_cond_broadcast(sched->idle_cond);
}

static void event_resume_cb(fr_event_loop_t *loop, int fd, uint32_t events, void *userdata) {
    (void)loop;
    (void)fd;
    (void)events;
    fr_coro_t *coro = (fr_coro_t *)userdata;
    if (!coro) return;
    coro->await_ready = 1;
    coro->status = FR_CORO_RUNNING;
    if (coro->proc && coro->proc->sched) {
        enqueue_coro(coro->proc->sched, coro);
    }
}

#define FR_REDUCTION_BUDGET 2000

static int run_coro_step(fr_coro_t *c) {
    tls_current_coro = c;
    fr_coro_status_t st = c->fn(c, c->state);
    tls_current_coro = NULL;
    c->status = st;
    if (st == FR_CORO_YIELDED) {
        c->status = FR_CORO_RUNNING;
        return 1;
    }
    if (st == FR_CORO_WAITING_RECV || st == FR_CORO_WAITING_IO) return 0;
    return st == FR_CORO_DONE || st == FR_CORO_ERROR ? 1 : 1;
}

static int process_has_active(fr_process_t *p) {
    for (fr_coro_t *c = p->coros; c; c = c->next) {
        if (c->status != FR_CORO_DONE && c->status != FR_CORO_ERROR) return 1;
    }
    return 0;
}

static int sched_any_active(fr_scheduler_t *sched) {
    for (fr_process_t *p = sched->processes; p; p = p->next) {
        if (process_has_active(p)) return 1;
    }
    return 0;
}

static void scan_enqueue_runnable(fr_scheduler_t *sched) {
    for (fr_process_t *p = sched->processes; p; p = p->next) {
        fr_mutex_lock(p->lock);
        for (fr_coro_t *c = p->coros; c; c = c->next) {
            if (c->status == FR_CORO_RUNNING) {
                enqueue_coro(sched, c);
            }
        }
        fr_mutex_unlock(p->lock);
    }
}

typedef struct {
    fr_scheduler_t *sched;
    int wid;
} fr_worker_arg_t;

static int sched_has_work(fr_scheduler_t *sched) {
    if (sched->native_pending > 0) return 1;
    for (int i = 0; i < sched->worker_count; i++) {
        if (sched->worker_queues[i].count > 0) return 1;
        if (sched->native_queues[i].count > 0) return 1;
    }
    return sched_any_active(sched);
}

static void run_native_task(fr_native_fn fn, void *arg) {
    if (fn) fn(arg);
}

static void *worker_main(void *arg) {
    fr_worker_arg_t *wa = (fr_worker_arg_t *)arg;
    fr_scheduler_t *sched = wa->sched;
    int wid = wa->wid;
    free(wa);

    int cpus = fr_platform_cpu_count();
    if (cpus > 0) fr_thread_pin_cpu(wid % cpus);
    tls_worker_id = wid;

    while (sched->running) {
        fr_coro_t *coro = fr_run_queue_pop(&sched->worker_queues[wid]);
        if (!coro) {
            for (int i = 0; i < sched->worker_count; i++) {
                if (i == wid) continue;
                coro = fr_run_queue_steal(&sched->worker_queues[i], &sched->worker_queues[wid]);
                if (coro) break;
            }
        }

        if (coro) {
            coro->on_queue = 0;
            int budget = FR_REDUCTION_BUDGET;
            while (budget-- > 0) {
                if (coro->status != FR_CORO_RUNNING) break;
                run_coro_step(coro);
                if (coro->status == FR_CORO_WAITING_IO || coro->status == FR_CORO_WAITING_RECV) break;
                if (coro->status == FR_CORO_DONE || coro->status == FR_CORO_ERROR) break;
            }
            if (coro->status == FR_CORO_RUNNING) {
                enqueue_coro(sched, coro);
            }
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
            fr_mutex_lock(sched->lock);
            if (sched->native_pending > 0) sched->native_pending--;
            fr_mutex_unlock(sched->lock);
            run_native_task(nfn, narg);
            continue;
        }

        if (!sched_has_work(sched)) {
            if (!sched->running) break;
            fr_mutex_lock(sched->lock);
            if (!sched_has_work(sched) && !sched->running) {
                fr_mutex_unlock(sched->lock);
                break;
            }
            fr_cond_wait(sched->idle_cond, sched->lock);
            fr_mutex_unlock(sched->lock);
            continue;
        }
        fr_thread_yield();
    }
    tls_worker_id = -1;
    return NULL;
}

fr_scheduler_t *fr_scheduler_create(int worker_count) {
    fr_scheduler_t *s = (fr_scheduler_t *)calloc(1, sizeof(fr_scheduler_t));
    if (!s) return NULL;
    s->worker_count = default_worker_count(worker_count);
    s->event_loop = fr_event_loop_create();
    if (s->event_loop) fr_event_loop_set_cb(s->event_loop, event_resume_cb);
    s->lock = fr_mutex_create();
    s->idle_cond = fr_cond_create();
    s->worker_queues = (fr_run_queue_t *)calloc((size_t)s->worker_count, sizeof(fr_run_queue_t));
    s->native_queues = (fr_native_queue_t *)calloc((size_t)s->worker_count, sizeof(fr_native_queue_t));
    s->workers = (fr_thread_t **)calloc((size_t)s->worker_count, sizeof(fr_thread_t *));
    if (!s->worker_queues || !s->native_queues || !s->workers) {
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
    sched->running = 0;
    fr_cond_broadcast(sched->idle_cond);
    if (sched->workers) {
        for (int i = 0; i < sched->worker_count; i++) {
            if (sched->workers[i]) fr_thread_join(sched->workers[i]);
        }
    }
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
        fr_thread_start(&sched->workers[i], worker_main, wa);
    }
}

void fr_scheduler_start(fr_scheduler_t *sched) {
    if (!sched) return;
    sched->running = 1;
    sched->done = 0;
    fr_scheduler_set_global(sched);
    scan_enqueue_runnable(sched);
    scheduler_start_workers(sched);
}

void fr_scheduler_stop(fr_scheduler_t *sched) {
    if (!sched) return;
    sched->running = 0;
    fr_cond_broadcast(sched->idle_cond);
    for (int i = 0; i < sched->worker_count; i++) {
        if (sched->workers[i]) fr_thread_join(sched->workers[i]);
        sched->workers[i] = NULL;
    }
    sched->workers_started = 0;
    if (g_global_sched == sched) g_global_sched = NULL;
}

void fr_sched_pool_submit(fr_scheduler_t *sched, void (*fn)(void *), void *arg) {
    if (!sched || !fn) return;
    static int next_q;
    int q = next_q++ % sched->worker_count;
    fr_mutex_lock(sched->lock);
    sched->native_pending++;
    fr_mutex_unlock(sched->lock);
    fr_native_queue_push(&sched->native_queues[q], fn, arg);
    fr_cond_broadcast(sched->idle_cond);
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
    fr_sched_pool_submit(sched, sched_native_trampoline1, ctx);
    return 0;
}

void fr_sched_pool_spawn_indexed(fr_sched_native_fn2_t fn, int64_t count) {
    fr_scheduler_t *sched = g_global_sched;
    if (!sched || !sched->running || !fn || count <= 0) return;
    if (count > sched->worker_count * 64) count = sched->worker_count * 64;

    fr_mutex_t *lock = fr_mutex_create();
    fr_cond_t *done = fr_cond_create();
    int remaining = (int)count;
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
        fr_sched_pool_submit(sched, sched_native_trampoline2, ctx);
    }

    fr_mutex_lock(lock);
    while (remaining > 0) fr_cond_wait(done, lock);
    fr_mutex_unlock(lock);
    fr_mutex_destroy(lock);
    fr_cond_destroy(done);
}

void fr_scheduler_add_process(fr_scheduler_t *sched, fr_process_t *proc) {
    static int next_worker = 0;
    proc->sched = sched;
    proc->worker_id = next_worker++ % sched->worker_count;
    fr_mutex_lock(sched->lock);
    proc->next = sched->processes;
    sched->processes = proc;
    fr_mutex_unlock(sched->lock);
}

fr_event_loop_t *fr_scheduler_event_loop(fr_scheduler_t *sched) {
    return sched ? sched->event_loop : NULL;
}

int fr_scheduler_worker_count(fr_scheduler_t *sched) {
    return sched ? sched->worker_count : 0;
}

void fr_scheduler_run(fr_scheduler_t *sched) {
    if (!sched) return;
    fr_scheduler_start(sched);
    fr_mutex_lock(sched->lock);
    while (!sched->done) {
        fr_event_loop_poll(sched->event_loop, 1);
        if (!sched_has_work(sched)) sched->done = 1;
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
    c->id = proc->next_coro_id++;
    c->fn = fn;
    c->state_size = state_size;
    c->state = malloc(state_size);
    if (!c->state) {
        free(c);
        return NULL;
    }
    memcpy(c->state, init_state, state_size);
    c->status = FR_CORO_RUNNING;
    c->step = 0;
    c->proc = proc;
    c->await_fd = -1;
    if (!proc->coros) proc->coros = proc->coro_tail = c;
    else { proc->coro_tail->next = c; proc->coro_tail = c; }
    free(init_state);
    if (proc->sched) enqueue_coro(proc->sched, c);
    return c;
}

fr_coro_status_t fr_yield(fr_coro_t *coro) {
    coro->status = FR_CORO_YIELDED;
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
    if (dst->sched) fr_cond_broadcast(dst->sched->idle_cond);
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
    if (coro->await_ready && coro->await_fd == (int)fd) {
        coro->await_ready = 0;
        coro->await_fd = -1;
        fr_event_loop_del(coro->proc->sched->event_loop, (int)fd);
        return 1;
    }
    coro->await_fd = (int)fd;
    coro->await_events = events;
    coro->await_ready = 0;
    coro->status = FR_CORO_WAITING_IO;
    fr_event_loop_add(coro->proc->sched->event_loop, (int)fd, events | FR_EVENT_ONESHOT, coro);
    return 0;
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
