#include "miku_offline_push.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

#define MK_MAX_PUSH_TOKENS 4096

typedef struct {
    char user_id[64];
    int  platform;
    char token[512];
    bool active;
} push_token_t;

struct miku_offline_push_s {
    miku_push_provider_t provider;
    push_token_t         tokens[MK_MAX_PUSH_TOKENS];
    int                  token_count;
    int64_t              total_sent;
};

miku_offline_push_t *miku_offline_push_create(miku_push_provider_t provider) {
    miku_offline_push_t *op = (miku_offline_push_t *)calloc(1, sizeof(*op));
    if (op) op->provider = provider;
    return op;
}

void miku_offline_push_destroy(miku_offline_push_t *op) { free(op); }

int miku_offline_push_send(miku_offline_push_t *op, const char *user_id,
                             int platform, const char *title,
                             const char *content, const char *client_url) {
    if (!op || !user_id) return -1;
    op->total_sent++;

    if (op->provider == MK_PUSH_PROVIDER_DUMMY) {
        MK_LOG_DEBUG("offline_push[DUMMY]: user=%s platform=%d title=%s", user_id, platform, title);
        return 0;
    }

    for (int i = 0; i < op->token_count; i++) {
        if (op->tokens[i].active &&
            strcmp(op->tokens[i].user_id, user_id) == 0 &&
            op->tokens[i].platform == platform) {
            MK_LOG_DEBUG("offline_push[%s]: user=%s token=%s... title=%s body=%s url=%s",
                         miku_offline_push_provider_name(op->provider),
                         user_id, op->tokens[i].token,
                         title ? title : "", content ? content : "",
                         client_url ? client_url : "");
            return 0;
        }
    }
    MK_LOG_DEBUG("offline_push: no token for user=%s platform=%d", user_id, platform);
    return -1;
}

int miku_offline_push_set_token(miku_offline_push_t *op, const char *user_id,
                                  int platform, const char *fcm_token) {
    if (!op || !user_id || !fcm_token) return -1;
    for (int i = 0; i < op->token_count; i++) {
        if (strcmp(op->tokens[i].user_id, user_id) == 0 && op->tokens[i].platform == platform) {
            strncpy(op->tokens[i].token, fcm_token, sizeof(op->tokens[i].token) - 1);
            op->tokens[i].active = true;
            return 0;
        }
    }
    if (op->token_count >= MK_MAX_PUSH_TOKENS) return -1;
    push_token_t *t = &op->tokens[op->token_count++];
    strncpy(t->user_id, user_id, sizeof(t->user_id) - 1);
    t->platform = platform;
    strncpy(t->token, fcm_token, sizeof(t->token) - 1);
    t->active = true;
    return 0;
}

int miku_offline_push_del_token(miku_offline_push_t *op, const char *user_id, int platform) {
    if (!op || !user_id) return -1;
    for (int i = 0; i < op->token_count; i++) {
        if (strcmp(op->tokens[i].user_id, user_id) == 0 && op->tokens[i].platform == platform) {
            op->tokens[i].active = false;
            return 0;
        }
    }
    return -1;
}

const char *miku_offline_push_provider_name(miku_push_provider_t p) {
    switch (p) {
        case MK_PUSH_PROVIDER_FCM:   return "FCM";
        case MK_PUSH_PROVIDER_GETUI: return "Getui";
        case MK_PUSH_PROVIDER_JPUSH: return "JPUSH";
        default:                     return "DUMMY";
    }
}
