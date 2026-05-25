#ifndef MIKU_STATS_H
#define MIKU_STATS_H

#include "miku_common.h"
#include "miku_atomic.h"
#include <stdint.h>

typedef struct {
    miku_atomic_int64_t requests_total;
    miku_atomic_int64_t requests_failed;
    miku_atomic_int64_t connections_active;
    miku_atomic_int64_t connections_total;
    miku_atomic_int64_t bytes_sent;
    miku_atomic_int64_t bytes_recv;
    int64_t             start_time_ms;
    char                service_name[64];
    int                 port;
} miku_stats_t;

void        miku_stats_init(miku_stats_t *s, const char *service_name, int port);
void        miku_stats_request_inc(miku_stats_t *s);
void        miku_stats_fail_inc(miku_stats_t *s);
void        miku_stats_conn_open(miku_stats_t *s);
void        miku_stats_conn_close(miku_stats_t *s);
void        miku_stats_bytes_sent(miku_stats_t *s, int64_t n);
void        miku_stats_bytes_recv(miku_stats_t *s, int64_t n);
int64_t     miku_stats_uptime_ms(const miku_stats_t *s);

typedef struct {
    int64_t requests_total;
    int64_t requests_failed;
    int64_t connections_active;
    int64_t connections_total;
    int64_t bytes_sent;
    int64_t bytes_recv;
    int64_t uptime_ms;
    char   service_name[64];
    int    port;
} miku_stats_snapshot_t;

void miku_stats_snapshot(const miku_stats_t *s, miku_stats_snapshot_t *out);

#endif
