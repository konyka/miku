#ifndef MIKU_GRACEFUL_H
#define MIKU_GRACEFUL_H

#include "miku_common.h"

typedef void (*miku_reload_fn)(void *ctx);

typedef struct {
    volatile int   running;
    int            drain_timeout_ms;
    miku_reload_fn reload_fn;
    void          *reload_ctx;
} miku_graceful_t;

void miku_graceful_init(miku_graceful_t *g, int drain_timeout_ms);
int  miku_graceful_running(const miku_graceful_t *g);
void miku_graceful_wait(miku_graceful_t *g, void (*on_drain)(void *ctx), void *ctx);
void miku_graceful_cleanup(miku_graceful_t *g);
void miku_graceful_on_reload(miku_graceful_t *g, miku_reload_fn fn, void *ctx);

#endif
