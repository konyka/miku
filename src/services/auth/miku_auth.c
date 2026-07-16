#include "miku_auth.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_token.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct miku_auth_service_s {
    int placeholder;
};

miku_auth_service_t *miku_auth_service_create(void) {
    miku_auth_service_t *svc = (miku_auth_service_t *)calloc(1, sizeof(*svc));
    return svc;
}

void miku_auth_service_destroy(miku_auth_service_t *svc) {
    free(svc);
}

int miku_auth_user_token(miku_auth_service_t *svc, const char *user_id,
                           const char *secret, int platform,
                           char *token_out, size_t token_cap) {
    (void)svc;
    if (!user_id || !secret || !token_out) return -1;
    if (platform == 5) return -1;
    if (secret[0] == '\0' || strcmp(secret, MIKU_TOKEN_DEFAULT_SECRET) != 0) {
        MK_LOG_WARN("Auth failed for user %s: bad secret", user_id);
        return -1;
    }
    return miku_token_create(user_id, platform, MIKU_TOKEN_DEFAULT_SECRET,
                             token_out, token_cap);
}

int miku_auth_admin_token(miku_auth_service_t *svc, const char *user_id,
                            const char *secret, char *token_out, size_t token_cap) {
    (void)svc;
    if (!user_id || !secret || !token_out) return -1;
    if (secret[0] == '\0' || strcmp(secret, MIKU_ADMIN_DEFAULT_SECRET) != 0) {
        MK_LOG_WARN("Admin auth failed for user %s: bad secret", user_id);
        return -1;
    }
    return miku_token_create(user_id, 5, MIKU_TOKEN_DEFAULT_SECRET,
                             token_out, token_cap);
}

int miku_auth_parse_token(miku_auth_service_t *svc, const char *token,
                           char *user_id_out, size_t cap) {
    (void)svc;
    return miku_token_verify(token, MIKU_TOKEN_DEFAULT_SECRET, user_id_out, cap);
}

int miku_auth_force_logout(miku_auth_service_t *svc, const char *user_id, int platform) {
    (void)svc;
    if (!user_id || !user_id[0]) return -1;
    return miku_token_revoke(user_id, platform);
}

void miku_auth_handle_rpc(miku_auth_service_t *svc, const miku_rpc_message_t *req,
                           miku_rpc_message_t *resp) {
    if (!svc || !req || !resp) return;
    uint32_t method = req->header.method;
    if (req->payload && req->payload_len > 0) {
        miku_json_val_t *j = miku_json_parse((const char *)req->payload, req->payload_len);
        if (!j) {
            const char *err = "{\"errCode\":400,\"errMsg\":\"invalid JSON\"}";
            miku_rpc_message_set_payload(resp, (const uint8_t *)err, strlen(err));
            return;
        }
        char user_id[64] = {0};
        char secret[64] = {0};
        int64_t platform = 0;

        miku_json_val_t *v;
        v = miku_json_get(j, "userID");
        if (v) { const char *s = miku_json_str(v); if (s) strncpy(user_id, s, sizeof(user_id) - 1); }
        v = miku_json_get(j, "secret");
        if (v) { const char *s = miku_json_str(v); if (s) strncpy(secret, s, sizeof(secret) - 1); }
        v = miku_json_get(j, "platformID");
        if (v) platform = miku_json_int(v);

        if (method == 1) {
            char token[512];
            int rc = miku_auth_user_token(svc, user_id, secret, (int)platform, token, sizeof(token));
            miku_json_val_t *out = miku_json_create_object();
            if (rc == 0) {
                miku_ji(out, "errCode", 0);
                miku_jss(out, "errMsg", "");
                miku_jss(out, "token", token);
                miku_ji(out, "expireTimeSeconds", MIKU_TOKEN_EXPIRY_SECONDS);
            } else {
                miku_ji(out, "errCode", 401);
                miku_jss(out, "errMsg", "authentication failed");
            }
            miku_string_t *s = miku_json_stringify(out);
            miku_rpc_message_set_payload(resp, (const uint8_t *)s->data, s->len);
            miku_str_destroy(s);
            miku_json_destroy(out);
        }
        miku_json_destroy(j);
    }
}
