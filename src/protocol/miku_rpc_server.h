#ifndef MIKU_RPC_SERVER_H
#define MIKU_RPC_SERVER_H

#include "miku_common.h"
#include "miku_rpc.h"
#include "miku_io.h"
#include "miku_json.h"
#include "miku_stats.h"

typedef void (*miku_rpc_dispatch_fn)(void *svc, const char *method,
                                     const miku_json_val_t *req, miku_json_val_t *resp);

typedef struct {
    void                *svc;
    miku_rpc_dispatch_fn dispatch;
    int                  listen_fd;
    int                  port;
    volatile int         running;
    miku_stats_t        *stats;
    char                *internal_token;
} miku_rpc_server_t;

MIKU_API miku_rpc_server_t *miku_rpc_server_create(void *svc, miku_rpc_dispatch_fn dispatch, int port);
MIKU_API void               miku_rpc_server_destroy(miku_rpc_server_t *srv);
MIKU_API int                miku_rpc_server_start(miku_rpc_server_t *srv);
MIKU_API void               miku_rpc_server_stop(miku_rpc_server_t *srv);
MIKU_API int                miku_rpc_server_poll(miku_rpc_server_t *srv, int timeout_ms);
MIKU_API void               miku_rpc_server_set_stats(miku_rpc_server_t *srv, miku_stats_t *stats);
/* When set, JSON body must include matching internalToken. NULL = open (tests). */
MIKU_API void               miku_rpc_server_set_internal_token(miku_rpc_server_t *srv,
                                                                  const char *token);
MIKU_API void               miku_rpc_server_enable_internal_auth(miku_rpc_server_t *srv);

#endif
