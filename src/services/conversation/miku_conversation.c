#include "miku_conversation.h"
#include "miku_hash.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

/* 2x max conversations for open-addressing load factor ~0.5 */
#define MK_CONV_HASH 16384

struct miku_conv_service_s {
    miku_conversation_t convs[MK_MAX_CONVS];
    int count;
    int16_t pair_hash[MK_CONV_HASH];  /* -1 empty, else convs[] index */
    int16_t owner_hash[MK_CONV_HASH]; /* owner → first conv index */
    int16_t owner_next[MK_MAX_CONVS]; /* intrusive list per owner */
};

static uint32_t conv_pair_slot(const char *owner, const char *conv_id) {
    uint64_t a = miku_fnv1a_64(owner, strlen(owner));
    uint64_t b = miku_fnv1a_64(conv_id, strlen(conv_id));
    return (uint32_t)((a ^ (b * 0x9e3779b97f4a7c15ULL)) & (MK_CONV_HASH - 1));
}

static uint32_t owner_slot(const char *owner) {
    return (uint32_t)(miku_fnv1a_64(owner, strlen(owner)) & (MK_CONV_HASH - 1));
}

static void conv_hash_insert(miku_conv_service_t *svc, int ci) {
    uint32_t idx = conv_pair_slot(svc->convs[ci].owner_user_id,
                                  svc->convs[ci].conversation_id);
    for (int n = 0; n < MK_CONV_HASH; n++) {
        if (svc->pair_hash[idx] < 0) {
            svc->pair_hash[idx] = (int16_t)ci;
            return;
        }
        idx = (idx + 1) & (MK_CONV_HASH - 1);
    }
}

static void owner_link(miku_conv_service_t *svc, int ci) {
    const char *owner = svc->convs[ci].owner_user_id;
    uint32_t idx = owner_slot(owner);
    for (int n = 0; n < MK_CONV_HASH; n++) {
        int head = svc->owner_hash[idx];
        if (head < 0) {
            svc->owner_hash[idx] = (int16_t)ci;
            svc->owner_next[ci] = -1;
            return;
        }
        if (strcmp(svc->convs[head].owner_user_id, owner) == 0) {
            svc->owner_next[ci] = (int16_t)head;
            svc->owner_hash[idx] = (int16_t)ci;
            return;
        }
        idx = (idx + 1) & (MK_CONV_HASH - 1);
    }
}

static int owner_head(miku_conv_service_t *svc, const char *owner) {
    uint32_t idx = owner_slot(owner);
    for (int n = 0; n < MK_CONV_HASH; n++) {
        int head = svc->owner_hash[idx];
        if (head < 0) return -1;
        if (strcmp(svc->convs[head].owner_user_id, owner) == 0) return head;
        idx = (idx + 1) & (MK_CONV_HASH - 1);
    }
    return -1;
}

static int conv_hash_find(miku_conv_service_t *svc, const char *owner, const char *conv_id) {
    uint32_t idx = conv_pair_slot(owner, conv_id);
    for (int n = 0; n < MK_CONV_HASH; n++) {
        int ci = svc->pair_hash[idx];
        if (ci < 0) return -1;
        if (strcmp(svc->convs[ci].owner_user_id, owner) == 0 &&
            strcmp(svc->convs[ci].conversation_id, conv_id) == 0)
            return ci;
        idx = (idx + 1) & (MK_CONV_HASH - 1);
    }
    return -1;
}

miku_conv_service_t *miku_conv_service_create(void) {
    miku_conv_service_t *svc = (miku_conv_service_t *)calloc(1, sizeof(*svc));
    if (svc) {
        for (int i = 0; i < MK_CONV_HASH; i++) {
            svc->pair_hash[i] = -1;
            svc->owner_hash[i] = -1;
        }
        for (int i = 0; i < MK_MAX_CONVS; i++) svc->owner_next[i] = -1;
    }
    return svc;
}
void miku_conv_service_destroy(miku_conv_service_t *svc) { free(svc); }

int miku_conv_create(miku_conv_service_t *svc, const miku_conversation_t *c) {
    if (!svc || !c || svc->count >= MK_MAX_CONVS) return -1;
    if (conv_hash_find(svc, c->owner_user_id, c->conversation_id) >= 0) return -2;
    int ci = svc->count++;
    svc->convs[ci] = *c;
    conv_hash_insert(svc, ci);
    owner_link(svc, ci);
    return 0;
}

int miku_conv_get(miku_conv_service_t *svc, const char *owner, const char *conv_id, miku_conversation_t *out) {
    if (!svc || !owner || !conv_id || !out) return -1;
    int ci = conv_hash_find(svc, owner, conv_id);
    if (ci < 0) return -2;
    *out = svc->convs[ci];
    return 0;
}

int miku_conv_get_all(miku_conv_service_t *svc, const char *owner, miku_conversation_t *out, int max) {
    if (!svc || !owner || !out) return 0;
    int n = 0;
    for (int ci = owner_head(svc, owner); ci >= 0 && n < max; ci = svc->owner_next[ci])
        out[n++] = svc->convs[ci];
    return n;
}

int miku_conv_update(miku_conv_service_t *svc, const miku_conversation_t *c) {
    if (!svc || !c) return -1;
    int ci = conv_hash_find(svc, c->owner_user_id, c->conversation_id);
    if (ci < 0) return -2;
    svc->convs[ci] = *c;
    return 0;
}


void miku_conv_handle_rpc(miku_conv_service_t *svc, const char *method,
                           const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    if (strcmp(method, "getAllConversations") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_conversation_t list[16];
        int n = miku_conv_get_all(svc, owner, list, 16);
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
        if (miku_conv_update(svc, &c) == -2)
            miku_conv_create(svc, &c);
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "deleteConversation") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getConversationList") == 0 ||
               strcmp(method, "getConversations") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_conversation_t list[16];
        int n = miku_conv_get_all(svc, owner, list, 16);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) miku_json_array_push(arr, miku_conversation_to_json(&list[i]));
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getTotalUnreadMsgCount") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!owner) owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        int64_t total = 0;
        if (owner) {
            for (int ci = owner_head(svc, owner); ci >= 0; ci = svc->owner_next[ci])
                total += svc->convs[ci].unread_count;
        }
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "count", total);
    } else if (strcmp(method, "setConversationMinSeq") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "markConversationMessageAsRead") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "clearConversationMsg") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "pinConversation") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "deleteConversations") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getFullConversationIDs") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getIncrementalConversation") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getNotNotifyConversationIDs") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getOwnerConversation") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getPinnedConversationIDs") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getSortedConversationList") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "updateConversationsByUser") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        int updated = 0;
        if (owner && cid) {
            int ci = conv_hash_find(svc, owner, cid);
            if (ci >= 0) {
                const char *ex = req ? miku_json_str(miku_json_get(req, "ex")) : NULL;
                if (ex) strncpy(svc->convs[ci].ex, ex, sizeof(svc->convs[ci].ex) - 1);
                updated = 1;
            }
        }
        miku_ji(resp, "errCode", updated ? 0 : 4001);
    } else {
        miku_ji(resp, "errCode", 404);
    }
}
