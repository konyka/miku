#include "miku_conversation.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

struct miku_conv_service_s {
    miku_conversation_t convs[MK_MAX_CONVS];
    int count;
};

miku_conv_service_t *miku_conv_service_create(void) {
    return (miku_conv_service_t *)calloc(1, sizeof(miku_conv_service_t));
}
void miku_conv_service_destroy(miku_conv_service_t *svc) { free(svc); }

int miku_conv_create(miku_conv_service_t *svc, const miku_conversation_t *c) {
    if (!svc || !c || svc->count >= MK_MAX_CONVS) return -1;
    svc->convs[svc->count++] = *c;
    return 0;
}

int miku_conv_get(miku_conv_service_t *svc, const char *owner, const char *conv_id, miku_conversation_t *out) {
    if (!svc || !owner || !conv_id || !out) return -1;
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->convs[i].owner_user_id, owner) == 0 &&
            strcmp(svc->convs[i].conversation_id, conv_id) == 0) {
            *out = svc->convs[i];
            return 0;
        }
    }
    return -2;
}

int miku_conv_get_all(miku_conv_service_t *svc, const char *owner, miku_conversation_t *out, int max) {
    if (!svc || !owner || !out) return 0;
    int n = 0;
    for (int i = 0; i < svc->count && n < max; i++)
        if (strcmp(svc->convs[i].owner_user_id, owner) == 0) out[n++] = svc->convs[i];
    return n;
}

int miku_conv_update(miku_conv_service_t *svc, const miku_conversation_t *c) {
    if (!svc || !c) return -1;
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->convs[i].owner_user_id, c->owner_user_id) == 0 &&
            strcmp(svc->convs[i].conversation_id, c->conversation_id) == 0) {
            svc->convs[i] = *c;
            return 0;
        }
    }
    return -2;
}


void miku_conv_handle_rpc(miku_conv_service_t *svc, const char *method,
                           const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    if (strcmp(method, "getAllConversations") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_conversation_t list[256];
        int n = miku_conv_get_all(svc, owner, list, 256);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) miku_json_array_push(arr, miku_conversation_to_json(&list[i]));
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getConversation") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        miku_conversation_t c;
        int rc = miku_conv_get(svc, owner, cid, &c);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 4001);
        if (rc == 0) miku_json_object_set(resp, "data", miku_conversation_to_json(&c));
    } else if (strcmp(method, "setConversation") == 0) {
        miku_conversation_t c;
        memset(&c, 0, sizeof(c));
        miku_conversation_from_json(req, &c);
        int rc = miku_conv_update(svc, &c);
        if (rc == -2) rc = miku_conv_create(svc, &c);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
    } else if (strcmp(method, "setConversations") == 0) {
        miku_conversation_t c;
        memset(&c, 0, sizeof(c));
        miku_conversation_from_json(req, &c);
        miku_conv_update(svc, &c);
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "deleteConversation") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getConversationList") == 0 ||
               strcmp(method, "getConversations") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_conversation_t list[256];
        int n = miku_conv_get_all(svc, owner, list, 256);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) miku_json_array_push(arr, miku_conversation_to_json(&list[i]));
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getTotalUnreadMsgCount") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "count", 0);
    } else if (strcmp(method, "setConversationMinSeq") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "markConversationMessageAsRead") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "clearConversationMsg") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "pinConversation") == 0) {
        miku_ji(resp, "errCode", 0);
    } else {
        miku_ji(resp, "errCode", 404);
    }
}
