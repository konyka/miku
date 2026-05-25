#ifndef MIKU_HTTP_SERVER_H
#define MIKU_HTTP_SERVER_H

#include "miku_common.h"
#include "miku_http.h"
#include "miku_io.h"
#include "miku_stats.h"

typedef enum {
    MK_MW_CONTINUE = 0,
    MK_MW_STOP     = 1
} miku_mw_result_t;

typedef miku_mw_result_t (*miku_http_middleware_fn)(miku_http_request_t *req,
                                                    miku_http_response_t *resp,
                                                    void *ctx);

typedef struct {
    const char       *method;
    const char       *path;
    miku_http_handler_fn handler;
    void             *ctx;
} miku_http_route_t;

typedef struct miku_http_server_s miku_http_server_t;

MIKU_API miku_http_server_t *miku_http_server_create(const char *host, int port);
MIKU_API int  miku_http_server_route(miku_http_server_t *srv, const char *method,
                                      const char *path, miku_http_handler_fn fn, void *ctx);
MIKU_API int  miku_http_server_use(miku_http_server_t *srv, miku_http_middleware_fn mw, void *ctx);
MIKU_API int  miku_http_server_start(miku_http_server_t *srv);
MIKU_API void miku_http_server_stop(miku_http_server_t *srv);
MIKU_API void miku_http_server_destroy(miku_http_server_t *srv);
MIKU_API void miku_http_server_set_stats(miku_http_server_t *srv, miku_stats_t *stats);

#endif
