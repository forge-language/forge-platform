#include "work_queue.h"
#include <stdlib.h>

/* Bound retained memory independently of a queue's peak backlog. */
#define NODE_CACHE_LIMIT 256

void fr_run_queue_init(fr_run_queue_t *q) {
    q->lock = fr_mutex_create();
    q->head = q->tail = q->free_nodes = NULL;
    q->count = q->free_count = 0;
}

void fr_run_queue_destroy(fr_run_queue_t *q) {
    if (!q) return;
    fr_run_node_t *n = q->head;
    while (n) { fr_run_node_t *next = n->next; free(n); n = next; }
    n = q->free_nodes;
    while (n) { fr_run_node_t *next = n->next; free(n); n = next; }
    q->head = q->tail = q->free_nodes = NULL;
    q->count = q->free_count = 0;
    fr_mutex_destroy(q->lock);
    q->lock = NULL;
}

int fr_run_queue_try_push(fr_run_queue_t *q, fr_coro_t *coro) {
    fr_mutex_lock(q->lock);
    fr_run_node_t *node = q->free_nodes;
    if (node) { q->free_nodes = node->next; q->free_count--; }
    else node = (fr_run_node_t *)malloc(sizeof(*node));
    if (!node) { fr_mutex_unlock(q->lock); return 0; }
    node->coro = coro;
    node->prev = q->tail;
    node->next = NULL;
    if (q->tail) q->tail->next = node;
    else q->head = node;
    q->tail = node;
    q->count++;
    fr_mutex_unlock(q->lock);
    return 1;
}

void fr_run_queue_push(fr_run_queue_t *q, fr_coro_t *coro) {
    (void)fr_run_queue_try_push(q, coro);
}

static fr_coro_t *take_run(fr_run_queue_t *q, int steal) {
    fr_mutex_lock(q->lock);
    fr_run_node_t *node = steal ? q->tail : q->head;
    if (!node) { fr_mutex_unlock(q->lock); return NULL; }
    if (node->prev) node->prev->next = node->next;
    else q->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else q->tail = node->prev;
    q->count--;
    fr_coro_t *coro = node->coro;
    if (q->free_count < NODE_CACHE_LIMIT) {
        node->next = q->free_nodes;
        q->free_nodes = node;
        q->free_count++;
        node = NULL;
    }
    fr_mutex_unlock(q->lock);
    if (node) free(node);
    return coro;
}

fr_coro_t *fr_run_queue_pop(fr_run_queue_t *q) { return take_run(q, 0); }
fr_coro_t *fr_run_queue_steal(fr_run_queue_t *victim, fr_run_queue_t *thief) {
    (void)thief;
    return take_run(victim, 1);
}

void fr_native_queue_init(fr_native_queue_t *q) {
    q->lock = fr_mutex_create();
    q->head = q->tail = q->free_nodes = NULL;
    q->count = q->free_count = 0;
}

void fr_native_queue_destroy(fr_native_queue_t *q) {
    if (!q) return;
    fr_native_node_t *n = q->head;
    while (n) { fr_native_node_t *next = n->next; free(n); n = next; }
    n = q->free_nodes;
    while (n) { fr_native_node_t *next = n->next; free(n); n = next; }
    q->head = q->tail = q->free_nodes = NULL;
    q->count = q->free_count = 0;
    fr_mutex_destroy(q->lock);
    q->lock = NULL;
}

int fr_native_queue_try_push(fr_native_queue_t *q, fr_native_fn fn, void *arg) {
    if (!fn) return 0;
    fr_mutex_lock(q->lock);
    fr_native_node_t *node = q->free_nodes;
    if (node) { q->free_nodes = node->next; q->free_count--; }
    else node = (fr_native_node_t *)malloc(sizeof(*node));
    if (!node) { fr_mutex_unlock(q->lock); return 0; }
    node->fn = fn;
    node->arg = arg;
    node->prev = q->tail;
    node->next = NULL;
    if (q->tail) q->tail->next = node;
    else q->head = node;
    q->tail = node;
    q->count++;
    fr_mutex_unlock(q->lock);
    return 1;
}

void fr_native_queue_push(fr_native_queue_t *q, fr_native_fn fn, void *arg) {
    (void)fr_native_queue_try_push(q, fn, arg);
}

static fr_native_fn take_native(fr_native_queue_t *q, void **arg_out, int steal) {
    fr_mutex_lock(q->lock);
    fr_native_node_t *node = steal ? q->tail : q->head;
    if (!node) { fr_mutex_unlock(q->lock); return NULL; }
    if (node->prev) node->prev->next = node->next;
    else q->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else q->tail = node->prev;
    q->count--;
    if (arg_out) *arg_out = node->arg;
    fr_native_fn fn = node->fn;
    if (q->free_count < NODE_CACHE_LIMIT) {
        node->next = q->free_nodes;
        q->free_nodes = node;
        q->free_count++;
        node = NULL;
    }
    fr_mutex_unlock(q->lock);
    if (node) free(node);
    return fn;
}

fr_native_fn fr_native_queue_pop(fr_native_queue_t *q, void **arg_out) {
    return take_native(q, arg_out, 0);
}
fr_native_fn fr_native_queue_steal(fr_native_queue_t *q, void **arg_out) {
    return take_native(q, arg_out, 1);
}
