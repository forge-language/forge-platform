#include "work_queue.h"
#include <stdlib.h>

void fr_run_queue_init(fr_run_queue_t *q) {
    q->lock = fr_mutex_create();
    q->head = q->tail = NULL;
    q->count = 0;
}

void fr_run_queue_destroy(fr_run_queue_t *q) {
    if (!q) return;
    fr_mutex_lock(q->lock);
    fr_run_node_t *n = q->head;
    while (n) {
        fr_run_node_t *next = n->next;
        free(n);
        n = next;
    }
    q->head = q->tail = NULL;
    q->count = 0;
    fr_mutex_unlock(q->lock);
    fr_mutex_destroy(q->lock);
    q->lock = NULL;
}

void fr_run_queue_push(fr_run_queue_t *q, fr_coro_t *coro) {
    fr_run_node_t *node = (fr_run_node_t *)malloc(sizeof(fr_run_node_t));
    if (!node) return;
    node->coro = coro;
    node->prev = NULL;
    node->next = NULL;
    fr_mutex_lock(q->lock);
    node->prev = q->tail;
    if (q->tail) q->tail->next = node;
    else q->head = node;
    q->tail = node;
    q->count++;
    fr_mutex_unlock(q->lock);
}

static fr_coro_t *unlink_run_locked(fr_run_queue_t *q, fr_run_node_t *node) {
    if (!node) return NULL;
    if (node->prev) node->prev->next = node->next;
    else q->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else q->tail = node->prev;
    q->count--;
    fr_coro_t *coro = node->coro;
    free(node);
    return coro;
}

fr_coro_t *fr_run_queue_pop(fr_run_queue_t *q) {
    fr_mutex_lock(q->lock);
    fr_coro_t *coro = unlink_run_locked(q, q->head);
    fr_mutex_unlock(q->lock);
    return coro;
}

fr_coro_t *fr_run_queue_steal(fr_run_queue_t *victim, fr_run_queue_t *thief) {
    (void)thief;
    fr_mutex_lock(victim->lock);
    fr_coro_t *coro = unlink_run_locked(victim, victim->tail);
    fr_mutex_unlock(victim->lock);
    return coro;
}

void fr_native_queue_init(fr_native_queue_t *q) {
    q->lock = fr_mutex_create();
    q->head = q->tail = NULL;
    q->count = 0;
}

void fr_native_queue_destroy(fr_native_queue_t *q) {
    if (!q) return;
    fr_mutex_lock(q->lock);
    fr_native_node_t *n = q->head;
    while (n) {
        fr_native_node_t *next = n->next;
        free(n);
        n = next;
    }
    q->head = q->tail = NULL;
    q->count = 0;
    fr_mutex_unlock(q->lock);
    fr_mutex_destroy(q->lock);
    q->lock = NULL;
}

void fr_native_queue_push(fr_native_queue_t *q, fr_native_fn fn, void *arg) {
    fr_native_node_t *node = (fr_native_node_t *)malloc(sizeof(fr_native_node_t));
    if (!node) return;
    node->fn = fn;
    node->arg = arg;
    node->prev = NULL;
    node->next = NULL;
    fr_mutex_lock(q->lock);
    node->prev = q->tail;
    if (q->tail) q->tail->next = node;
    else q->head = node;
    q->tail = node;
    q->count++;
    fr_mutex_unlock(q->lock);
}

static fr_native_fn unlink_native_locked(fr_native_queue_t *q,
                                         fr_native_node_t *node,
                                         void **arg_out) {
    if (!node) return NULL;
    if (node->prev) node->prev->next = node->next;
    else q->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else q->tail = node->prev;
    q->count--;
    if (arg_out) *arg_out = node->arg;
    fr_native_fn fn = node->fn;
    free(node);
    return fn;
}

fr_native_fn fr_native_queue_pop(fr_native_queue_t *q, void **arg_out) {
    fr_mutex_lock(q->lock);
    fr_native_fn fn = unlink_native_locked(q, q->head, arg_out);
    fr_mutex_unlock(q->lock);
    return fn;
}

fr_native_fn fr_native_queue_steal(fr_native_queue_t *victim, void **arg_out) {
    fr_mutex_lock(victim->lock);
    fr_native_fn fn = unlink_native_locked(victim, victim->tail, arg_out);
    fr_mutex_unlock(victim->lock);
    return fn;
}
