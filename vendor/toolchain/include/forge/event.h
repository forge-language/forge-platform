#ifndef FORGE_EVENT_H
#define FORGE_EVENT_H

#include <stdint.h>

typedef struct fr_event_loop fr_event_loop_t;
typedef struct fr_coro fr_coro_t;

typedef void (*fr_event_cb_t)(fr_event_loop_t *loop, int fd, uint32_t events, void *userdata);

fr_event_loop_t *fr_event_loop_create(void);
void fr_event_loop_destroy(fr_event_loop_t *loop);
int fr_event_loop_add(fr_event_loop_t *loop, int fd, uint32_t events, void *userdata);
int fr_event_loop_mod(fr_event_loop_t *loop, int fd, uint32_t events, void *userdata);
int fr_event_loop_del(fr_event_loop_t *loop, int fd);
int fr_event_loop_poll(fr_event_loop_t *loop, int timeout_ms);
void fr_event_loop_set_cb(fr_event_loop_t *loop, fr_event_cb_t cb);

#define FR_EVENT_READ  0x001u
#define FR_EVENT_WRITE 0x002u
#define FR_EVENT_ONESHOT 0x80000000u

#endif
