#ifndef MIKU_MIDDLEWARE_H
#define MIKU_MIDDLEWARE_H

#include "miku_http_server.h"
#include "miku_http.h"

MIKU_API miku_mw_result_t miku_mw_cors(miku_http_request_t *req,
                                        miku_http_response_t *resp,
                                        void *ctx);

typedef struct {
    int64_t  window_ms;
    int      max_requests;
    int      enabled;
} miku_rate_limit_cfg_t;

MIKU_API miku_mw_result_t miku_mw_rate_limit(miku_http_request_t *req,
                                              miku_http_response_t *resp,
                                              void *ctx);

MIKU_API miku_mw_result_t miku_mw_logging(miku_http_request_t *req,
                                            miku_http_response_t *resp,
                                            void *ctx);

#endif
