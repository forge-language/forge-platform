#ifndef FORGE_THREADING_H
#define FORGE_THREADING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t fr_threading_cpu_count(void);
void fr_threading_yield(void);
void fr_threading_pin(int64_t cpu);
int64_t fr_threading_self(void);
int64_t fr_threading_worker_id(void);

int64_t fr_threading_mutex_create(void);
void fr_threading_mutex_lock(int64_t handle);
void fr_threading_mutex_unlock(int64_t handle);
void fr_threading_mutex_destroy(int64_t handle);

typedef int64_t (*fr_threading_fn1_t)(int64_t);
typedef int64_t (*fr_threading_fn2_t)(int64_t, int64_t);

int64_t fr_threading_spawn(fr_threading_fn1_t fn, int64_t arg);
void fr_threading_spawn_indexed(fr_threading_fn2_t fn, int64_t count);
int64_t fr_threading_join(int64_t handle);
void fr_threading_join_all(void);

/* When a hybrid scheduler is running, spawn routes to its worker pool. */
int fr_threading_use_scheduler_pool(void);

#ifdef __cplusplus
}
#endif

#endif
