#ifndef MIKU_COROUTINE_H
#define MIKU_COROUTINE_H

#include "miku_common.h"

typedef enum {
    MK_CORO_READY,
    MK_CORO_RUNNING,
    MK_CORO_SUSPENDED,
    MK_CORO_DEAD
} miku_coro_state_t;

typedef struct miku_coro_s miku_coro_t;

typedef void (*miku_coro_fn)(void *arg);

struct miku_coro_s {
    ucontext_t         ctx;
    ucontext_t        *caller;
    miku_coro_fn       fn;
    void              *arg;
    miku_coro_state_t  state;
    int64_t            id;
    uint8_t           *stack;
    size_t             stack_size;
    int                wait_fd;
    int                wait_events;
    int64_t            deadline_ms;
    void              *result;
    miku_coro_t       *next;
};

MIKU_API miku_coro_t *miku_coro_create(miku_coro_fn fn, void *arg, size_t stack_size);
MIKU_API void         miku_coro_resume(miku_coro_t *coro);
MIKU_API void         miku_coro_yield(miku_coro_t *coro);
MIKU_API void         miku_coro_destroy(miku_coro_t *coro);

static inline miku_coro_state_t miku_coro_state(const miku_coro_t *c) {
    return c ? c->state : MK_CORO_DEAD;
}

static inline bool miku_coro_alive(const miku_coro_t *c) {
    return c && c->state != MK_CORO_DEAD;
}

#endif
