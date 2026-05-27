#include "miku_auth.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_uuid.h"
#include "miku_hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define AUTH_SECRET "openIM123"
#define TOKEN_EXPIRY_SECONDS 86400

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

static uint64_t compute_sig(const char *uid, int platform, int64_t ts, const char *nonce) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%d:%lld:%s:%s", uid, platform, (long long)ts, nonce, AUTH_SECRET);
    return miku_fnv1a_64(buf, strlen(buf));
}

int miku_auth_user_token(miku_auth_service_t *svc, const char *user_id,
                           const char *secret, int platform,
                           char *token_out, size_t token_cap) {
    (void)svc;
    if (!user_id || !secret || !token_out) return -1;
    if (strcmp(secret, AUTH_SECRET) != 0 && strlen(secret) > 0) {
        MK_LOG_WARN("Auth failed for user %s: bad secret", user_id);
        return -1;
    }
    char nonce[64];
    miku_uuid_generate(nonce);
    char nonce16[17] = {0};
    memcpy(nonce16, nonce, 16);
    int64_t ts = (int64_t)time(NULL);
    uint64_t sig = compute_sig(user_id, platform, ts, nonce16);
    snprintf(token_out, token_cap, "miku|%s|%d|%lld|%s|%016llx",
             user_id, platform, (long long)ts, nonce16, (unsigned long long)sig);
    return 0;
}

int miku_auth_parse_token(miku_auth_service_t *svc, const char *token,
                           char *user_id_out, size_t cap) {
    (void)svc;
    if (!token || !user_id_out || !cap) return -1;
    if (strncmp(token, "miku|", 5) != 0) return -1;

    const char *p = token + 5;
    const char *sep1 = strchr(p, '|');
    if (!sep1) return -1;
    size_t uid_len = (size_t)(sep1 - p);
    if (uid_len >= cap) uid_len = cap - 1;
    memcpy(user_id_out, p, uid_len);
    user_id_out[uid_len] = '\0';

    int platform = 0;
    if (sscanf(sep1 + 1, "%d", &platform) != 1) return -1;

    const char *sep2 = strchr(sep1 + 1, '|');
    if (!sep2) return -1;
    int64_t ts = 0;
    if (sscanf(sep2 + 1, "%lld", (long long *)&ts) != 1) return -1;

    const char *sep3 = strchr(sep2 + 1, '|');
    if (!sep3) return -1;
    char nonce17[17] = {0};
    const char *sep4 = strchr(sep3 + 1, '|');
    if (!sep4) return -1;
    size_t nonce_len = (size_t)(sep4 - sep3 - 1);
    if (nonce_len > 16) nonce_len = 16;
    memcpy(nonce17, sep3 + 1, nonce_len);

    unsigned long long sig_from_token = 0;
    if (sscanf(sep4 + 1, "%llx", &sig_from_token) != 1) return -1;

    uint64_t expected_sig = compute_sig(user_id_out, platform, ts, nonce17);
    if ((unsigned long long)expected_sig != sig_from_token) return -1;

    int64_t now = (int64_t)time(NULL);
    if (now - ts > TOKEN_EXPIRY_SECONDS) return -1;

    return 0;
}

int miku_auth_force_logout(miku_auth_service_t *svc, const char *user_id, int platform) {
    (void)svc; (void)user_id; (void)platform;
    return 0;
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
                miku_ji(out, "expireTimeSeconds", 86400);
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
