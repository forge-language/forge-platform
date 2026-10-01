#ifndef FORGE_WORK_QUEUE_H
#define FORGE_WORK_QUEUE_H

#include "forge/thread.h"
#include <stddef.h>

typedef struct fr_coro fr_coro_t;

typedef struct fr_run_node {
    fr_coro_t *coro;
    struct fr_run_node *prev;
    struct fr_run_node *next;
} fr_run_node_t;

typedef struct {
    fr_mutex_t *lock;
    fr_run_node_t *head;
    fr_run_node_t *tail;
    size_t count;
    fr_run_node_t *free_nodes;
    size_t free_count;
} fr_run_queue_t;

void fr_run_queue_init(fr_run_queue_t *q);
void fr_run_queue_destroy(fr_run_queue_t *q);
int fr_run_queue_try_push(fr_run_queue_t *q, fr_coro_t *coro);
void fr_run_queue_push(fr_run_queue_t *q, fr_coro_t *coro);
fr_coro_t *fr_run_queue_pop(fr_run_queue_t *q);
fr_coro_t *fr_run_queue_steal(fr_run_queue_t *victim, fr_run_queue_t *thief);

typedef void (*fr_native_fn)(void *arg);

typedef struct fr_native_node {
    fr_native_fn fn;
    void *arg;
    struct fr_native_node *prev;
    struct fr_native_node *next;
} fr_native_node_t;

typedef struct {
    fr_mutex_t *lock;
    fr_native_node_t *head;
    fr_native_node_t *tail;
    size_t count;
    fr_native_node_t *free_nodes;
    size_t free_count;
} fr_native_queue_t;

void fr_native_queue_init(fr_native_queue_t *q);
void fr_native_queue_destroy(fr_native_queue_t *q);
int fr_native_queue_try_push(fr_native_queue_t *q, fr_native_fn fn, void *arg);
void fr_native_queue_push(fr_native_queue_t *q, fr_native_fn fn, void *arg);
fr_native_fn fr_native_queue_pop(fr_native_queue_t *q, void **arg_out);
fr_native_fn fr_native_queue_steal(fr_native_queue_t *victim, void **arg_out);

#endif
