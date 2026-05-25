#include "miku_graceful.h"
#include "miku_log.h"
#include <signal.h>
#include <unistd.h>

static miku_graceful_t *g_instance = NULL;

static void handle_shutdown(int sig) {
    (void)sig;
    if (g_instance) g_instance->running = 0;
}

static void handle_reload(int sig) {
    (void)sig;
    if (g_instance && g_instance->reload_fn) {
        g_instance->reload_fn(g_instance->reload_ctx);
    }
}

void miku_graceful_init(miku_graceful_t *g, int drain_timeout_ms) {
    if (!g) return;
    g->running = 1;
    g->drain_timeout_ms = drain_timeout_ms;
    g->reload_fn = NULL;
    g->reload_ctx = NULL;
    g_instance = g;
    signal(SIGTERM, handle_shutdown);
    signal(SIGINT,  handle_shutdown);
    signal(SIGHUP,  handle_reload);
}

int miku_graceful_running(const miku_graceful_t *g) {
    return g ? g->running : 0;
}

void miku_graceful_wait(miku_graceful_t *g, void (*on_drain)(void *ctx), void *ctx) {
    if (!g) return;
    while (g->running) {
        usleep(50000);
    }
    MK_LOG_INFO("Shutdown signal received, draining...");
    if (on_drain) {
        on_drain(ctx);
    }
    if (g->drain_timeout_ms > 0) {
        usleep((useconds_t)g->drain_timeout_ms * 1000);
    }
}

void miku_graceful_cleanup(miku_graceful_t *g) {
    if (!g) return;
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
    signal(SIGHUP,  SIG_IGN);
    g_instance = NULL;
}

void miku_graceful_on_reload(miku_graceful_t *g, miku_reload_fn fn, void *ctx) {
    if (!g) return;
    g->reload_fn = fn;
    g->reload_ctx = ctx;
}
