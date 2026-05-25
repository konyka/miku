#include "miku_middleware.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include <string.h>
#include <stdio.h>

miku_mw_result_t miku_mw_cors(miku_http_request_t *req,
                               miku_http_response_t *resp,
                               void *ctx) {
    (void)req; (void)ctx;
    if (!resp) return MK_MW_CONTINUE;
    if (!resp->headers) resp->headers = miku_hashmap_create(8, NULL);
    miku_hashmap_put(resp->headers, "Access-Control-Allow-Origin", "*");
    miku_hashmap_put(resp->headers, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    miku_hashmap_put(resp->headers, "Access-Control-Allow-Headers", "Content-Type, token, operationID");
    if (req->method == MK_HTTP_OPTIONS) {
        resp->status = 204;
        return MK_MW_STOP;
    }
    return MK_MW_CONTINUE;
}

miku_mw_result_t miku_mw_rate_limit(miku_http_request_t *req,
                                     miku_http_response_t *resp,
                                     void *ctx) {
    (void)req;
    miku_rate_limit_cfg_t *cfg = (miku_rate_limit_cfg_t *)ctx;
    if (!cfg || !cfg->enabled) return MK_MW_CONTINUE;
    static int64_t window_start = 0;
    static int request_count = 0;
    int64_t now = miku_timestamp_ms();
    if (window_start == 0 || (now - window_start) >= cfg->window_ms) {
        window_start = now;
        request_count = 0;
    }
    request_count++;
    if (request_count > cfg->max_requests) {
        resp->status = 429;
        miku_json_val_t *body = miku_json_create_object();
        miku_json_object_set(body, "errCode", miku_json_create_int(429));
        miku_json_object_set(body, "errMsg", miku_json_create_str("rate limit exceeded"));
        miku_string_t *s = miku_json_stringify(body);
        miku_http_response_set_json(resp, s->data);
        miku_str_destroy(s);
        miku_json_destroy(body);
        return MK_MW_STOP;
    }
    return MK_MW_CONTINUE;
}

miku_mw_result_t miku_mw_logging(miku_http_request_t *req,
                                  miku_http_response_t *resp,
                                  void *ctx) {
    (void)ctx;
    if (!req) return MK_MW_CONTINUE;
    MK_LOG_INFO("%.*s %.*s", (int)req->path.len, req->path.data, 0, "");
    (void)resp;
    return MK_MW_CONTINUE;
}
