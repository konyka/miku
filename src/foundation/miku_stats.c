#include "miku_stats.h"
#include "miku_atomic.h"
#include <string.h>
#include <time.h>

void miku_stats_init(miku_stats_t *s, const char *service_name, int port) {
    memset(s, 0, sizeof(*s));
    strncpy(s->service_name, service_name, sizeof(s->service_name) - 1);
    s->port = port;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    s->start_time_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void miku_stats_request_inc(miku_stats_t *s) {
    if (s) miku_atomic_fetch_add(&s->requests_total, 1);
}

void miku_stats_fail_inc(miku_stats_t *s) {
    if (s) miku_atomic_fetch_add(&s->requests_failed, 1);
}

void miku_stats_conn_open(miku_stats_t *s) {
    if (!s) return;
    miku_atomic_fetch_add(&s->connections_active, 1);
    miku_atomic_fetch_add(&s->connections_total, 1);
}

void miku_stats_conn_close(miku_stats_t *s) {
    if (s) miku_atomic_fetch_sub(&s->connections_active, 1);
}

void miku_stats_bytes_sent(miku_stats_t *s, int64_t n) {
    if (s) miku_atomic_fetch_add(&s->bytes_sent, n);
}

void miku_stats_bytes_recv(miku_stats_t *s, int64_t n) {
    if (s) miku_atomic_fetch_add(&s->bytes_recv, n);
}

int64_t miku_stats_uptime_ms(const miku_stats_t *s) {
    if (!s) return 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return now - s->start_time_ms;
}

void miku_stats_snapshot(const miku_stats_t *s, miku_stats_snapshot_t *out) {
    if (!s || !out) return;
    out->requests_total     = (int64_t)miku_atomic_load(&s->requests_total);
    out->requests_failed    = (int64_t)miku_atomic_load(&s->requests_failed);
    out->connections_active = (int64_t)miku_atomic_load(&s->connections_active);
    out->connections_total  = (int64_t)miku_atomic_load(&s->connections_total);
    out->bytes_sent         = (int64_t)miku_atomic_load(&s->bytes_sent);
    out->bytes_recv         = (int64_t)miku_atomic_load(&s->bytes_recv);
    out->uptime_ms          = miku_stats_uptime_ms(s);
    strncpy(out->service_name, s->service_name, sizeof(out->service_name) - 1);
    out->port = s->port;
}
