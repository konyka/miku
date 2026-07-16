#include "miku_middleware.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include "miku_uuid.h"
#include "miku_token.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

miku_mw_result_t miku_mw_cors(miku_http_request_t *req,
                               miku_http_response_t *resp,
                               void *ctx) {
    (void)req; (void)ctx;
    if (!resp) return MK_MW_CONTINUE;
    if (!resp->headers) resp->headers = miku_hashmap_create(8, free);
    miku_hashmap_put(resp->headers, "Access-Control-Allow-Origin", strdup("*"));
    miku_hashmap_put(resp->headers, "Access-Control-Allow-Methods", strdup("POST, GET, OPTIONS"));
    miku_hashmap_put(resp->headers, "Access-Control-Allow-Headers", strdup("Content-Type, token, operationID"));
    if (req && req->method == MK_HTTP_OPTIONS) {
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
    /* Global static counter is intentionally disabled: per-user limiting is
     * handled by miku_ratelimit in API handlers (thread-safe, keyed). */
    if (!cfg || !cfg->enabled) return MK_MW_CONTINUE;
    (void)resp;
    return MK_MW_CONTINUE;
}

miku_mw_result_t miku_mw_logging(miku_http_request_t *req,
                                  miku_http_response_t *resp,
                                  void *ctx) {
    (void)ctx;
    if (!req) return MK_MW_CONTINUE;
    const char *method = "UNKNOWN";
    switch (req->method) {
        case MK_HTTP_GET:    method = "GET"; break;
        case MK_HTTP_POST:   method = "POST"; break;
        case MK_HTTP_PUT:    method = "PUT"; break;
        case MK_HTTP_DELETE: method = "DELETE"; break;
        case MK_HTTP_OPTIONS: method = "OPTIONS"; break;
        default: break;
    }
    MK_LOG_INFO("%s %.*s %d", method, (int)req->path.len, req->path.data, resp ? resp->status : 0);
    return MK_MW_CONTINUE;
}

miku_mw_result_t miku_mw_stats(miku_http_request_t *req,
                                miku_http_response_t *resp,
                                void *ctx) {
    (void)req; (void)resp;
    miku_stats_t *s = (miku_stats_t *)ctx;
    if (s) miku_stats_request_inc(s);
    return MK_MW_CONTINUE;
}

static int path_equals(miku_http_request_t *req, const char *str) {
    size_t slen = strlen(str);
    return req->path.len == slen && strncmp(req->path.data, str, slen) == 0;
}

static int is_public_auth_path(miku_http_request_t *req) {
    return path_equals(req, "/auth/user_token") ||
           path_equals(req, "/auth/admin_token") ||
           path_equals(req, "/auth/parse_token");
}

static void auth_reject(miku_http_response_t *resp, const char *msg) {
    resp->status = 401;
    miku_json_val_t *body = miku_json_create_object();
    miku_json_object_set(body, "errCode", miku_json_create_int(401));
    miku_json_object_set(body, "errMsg", miku_json_create_str(msg));
    miku_json_object_set(body, "errDmg", miku_json_create_str(""));
    miku_string_t *s = miku_json_stringify(body);
    miku_http_response_set_json(resp, s->data);
    miku_str_destroy(s);
    miku_json_destroy(body);
}

miku_mw_result_t miku_mw_auth(miku_http_request_t *req,
                               miku_http_response_t *resp,
                               void *ctx) {
    miku_auth_mw_cfg_t *cfg = (miku_auth_mw_cfg_t *)ctx;
    if (!cfg || !cfg->enabled) return MK_MW_CONTINUE;
    if (!req) return MK_MW_CONTINUE;

    /* Public endpoints — force_logout requires a valid token */
    if (is_public_auth_path(req))      return MK_MW_CONTINUE;
    if (path_equals(req, "/admin/health"))  return MK_MW_CONTINUE;
    if (path_equals(req, "/version"))       return MK_MW_CONTINUE;
    if (path_equals(req, "/admin/metrics")) return MK_MW_CONTINUE;

    const char *token = NULL;
    if (req->headers) {
        token = (const char *)miku_hashmap_get(req->headers, "token");
        if (!token) token = (const char *)miku_hashmap_get(req->headers, "Token");
        if (!token) token = (const char *)miku_hashmap_get(req->headers, "authorization");
    }

    if (!token || !token[0]) {
        auth_reject(resp, "token is missing or invalid");
        return MK_MW_STOP;
    }

    if (strncmp(token, "Bearer ", 7) == 0) token += 7;

    const char *secret = cfg->secret ? cfg->secret : MIKU_TOKEN_DEFAULT_SECRET;
    char uid[128] = {0};
    if (miku_token_verify(token, secret, uid, sizeof(uid)) != 0) {
        auth_reject(resp, "token is missing or invalid");
        return MK_MW_STOP;
    }

    return MK_MW_CONTINUE;
}

miku_mw_result_t miku_mw_request_id(miku_http_request_t *req,
                                      miku_http_response_t *resp,
                                      void *ctx) {
    (void)ctx;
    if (!req || !resp) return MK_MW_CONTINUE;

    char rid[64] = {0};
    if (req->headers) {
        const char *existing = (const char *)miku_hashmap_get(req->headers, "operationid");
        if (!existing) existing = (const char *)miku_hashmap_get(req->headers, "x-request-id");
        if (existing) strncpy(rid, existing, sizeof(rid) - 1);
    }
    if (rid[0] == '\0') miku_uuid_generate(rid);

    if (!resp->headers) resp->headers = miku_hashmap_create(8, free);
    miku_hashmap_put(resp->headers, "X-Request-ID", strdup(rid));

    MK_LOG_INFO("[%s] %.*s", rid, (int)req->path.len, req->path.data);
    return MK_MW_CONTINUE;
}
