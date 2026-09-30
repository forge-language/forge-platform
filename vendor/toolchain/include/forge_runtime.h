#ifndef FORGE_RUNTIME_H
#define FORGE_RUNTIME_H

#include "forge/io.h"
#include <stddef.h>
#include <stdint.h>

typedef struct fr_msg fr_msg_t;
typedef struct fr_process fr_process_t;
typedef struct fr_coro fr_coro_t;
typedef struct fr_scheduler fr_scheduler_t;
typedef struct fr_event_loop fr_event_loop_t;

typedef enum {
    FR_CORO_RUNNING = 0,
    FR_CORO_YIELDED,
    FR_CORO_WAITING_RECV,
    FR_CORO_WAITING_IO,
    FR_CORO_DONE,
    FR_CORO_ERROR
} fr_coro_status_t;

typedef enum {
    FR_RESTART_CORO = 0,
    FR_RESTART_PROCESS,
    FR_RESTART_ALL
} fr_restart_policy_t;

typedef fr_coro_status_t (*fr_coro_fn)(fr_coro_t *coro, void *state);

struct fr_msg {
    int tag;
    int64_t value;
    void *payload;
    size_t payload_size;
    int owns_payload;
    fr_process_t *from;
};

/* worker_count: 0 = auto (CPU count), 1 = single-threaded cooperative */
fr_scheduler_t *fr_scheduler_create(int worker_count);
void fr_scheduler_destroy(fr_scheduler_t *sched);
void fr_scheduler_add_process(fr_scheduler_t *sched, fr_process_t *proc);
void fr_scheduler_start(fr_scheduler_t *sched);
void fr_scheduler_stop(fr_scheduler_t *sched);
void fr_scheduler_run(fr_scheduler_t *sched);
fr_event_loop_t *fr_scheduler_event_loop(fr_scheduler_t *sched);
int fr_scheduler_worker_count(fr_scheduler_t *sched);
fr_scheduler_t *fr_scheduler_global(void);
void fr_scheduler_set_global(fr_scheduler_t *sched);

/* Native OS-thread work on the scheduler worker pool (hybrid mode). */
typedef int64_t (*fr_sched_native_fn1_t)(int64_t);
typedef int64_t (*fr_sched_native_fn2_t)(int64_t, int64_t);
int64_t fr_sched_pool_spawn(fr_sched_native_fn1_t fn, int64_t arg);
void fr_sched_pool_spawn_indexed(fr_sched_native_fn2_t fn, int64_t count);
int fr_sched_pool_available(void);
void fr_sched_pool_submit(fr_scheduler_t *sched, void (*fn)(void *), void *arg);

fr_process_t *fr_process_create(const char *name);
void fr_process_destroy(fr_process_t *proc);
fr_scheduler_t *fr_process_scheduler(fr_process_t *proc);
const char *fr_process_name(fr_process_t *proc);
void fr_process_set_receive_handler(fr_process_t *proc, fr_coro_fn handler, size_t state_size);

fr_coro_t *fr_coro_spawn(fr_process_t *proc, fr_coro_fn fn, void *init_state, size_t state_size);
fr_coro_status_t fr_yield(fr_coro_t *coro);
int fr_coro_id(fr_coro_t *coro);
fr_process_t *fr_coro_process(fr_coro_t *coro);
int fr_coro_step(fr_coro_t *coro);
void fr_coro_set_step(fr_coro_t *coro, int step);
fr_coro_t *fr_coro_current(void);

void fr_send(fr_process_t *dst, int tag, int64_t value, void *payload, size_t payload_size);
int fr_try_recv(fr_process_t *self, fr_msg_t *out);
int fr_recv(fr_process_t *self, fr_msg_t *out);
void fr_msg_free_payload(fr_msg_t *msg);

/* await I/O in coroutine — returns 0 until ready, then 1 */
int64_t fr_await_fd(fr_coro_t *coro, int64_t fd, uint32_t events);

fr_process_t *fr_supervisor_create(const char *name, fr_restart_policy_t policy);
void fr_supervisor_add_child(fr_process_t *supervisor, fr_process_t *child);

int fr_event_poll(fr_scheduler_t *sched, int timeout_ms);
int64_t fr_event_add_read(fr_scheduler_t *sched, int64_t fd);

#endif
