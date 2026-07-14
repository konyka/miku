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


enum {
    MK_CONV_RPC_getAllConversations = 0,
    MK_CONV_RPC_getConversation = 1,
    MK_CONV_RPC_setConversation = 2,
    MK_CONV_RPC_setConversations = 3,
    MK_CONV_RPC_deleteConversation = 4,
    MK_CONV_RPC_getConversationList = 5,
    MK_CONV_RPC_getConversations = 6,
    MK_CONV_RPC_getTotalUnreadMsgCount = 7,
    MK_CONV_RPC_setConversationMinSeq = 8,
    MK_CONV_RPC_markConversationMessageAsRead = 9,
    MK_CONV_RPC_clearConversationMsg = 10,
    MK_CONV_RPC_pinConversation = 11,
    MK_CONV_RPC_deleteConversations = 12,
    MK_CONV_RPC_getFullConversationIDs = 13,
    MK_CONV_RPC_getIncrementalConversation = 14,
    MK_CONV_RPC_getNotNotifyConversationIDs = 15,
    MK_CONV_RPC_getOwnerConversation = 16,
    MK_CONV_RPC_getPinnedConversationIDs = 17,
    MK_CONV_RPC_getSortedConversationList = 18,
    MK_CONV_RPC_updateConversationsByUser = 19,
    MK_CONV_RPC_getActiveConversations = 20,
    MK_CONV_RPC_COUNT = 21
};

#define MK_CONV_RPC_HASH 64
static const char *const g_conv_rpc_names[MK_CONV_RPC_COUNT] = {
    "getAllConversations",
    "getConversation",
    "setConversation",
    "setConversations",
    "deleteConversation",
    "getConversationList",
    "getConversations",
    "getTotalUnreadMsgCount",
    "setConversationMinSeq",
    "markConversationMessageAsRead",
    "clearConversationMsg",
    "pinConversation",
    "deleteConversations",
    "getFullConversationIDs",
    "getIncrementalConversation",
    "getNotNotifyConversationIDs",
    "getOwnerConversation",
    "getPinnedConversationIDs",
    "getSortedConversationList",
    "updateConversationsByUser",
    "getActiveConversations"
};

static int16_t g_conv_rpc_hash[MK_CONV_RPC_HASH];
static int g_conv_rpc_ready;

static void conv_rpc_init(void) {
    if (g_conv_rpc_ready) return;
    for (int i = 0; i < MK_CONV_RPC_HASH; i++) g_conv_rpc_hash[i] = -1;
    for (int i = 0; i < MK_CONV_RPC_COUNT; i++) {
        const char *m = g_conv_rpc_names[i];
        uint32_t idx = (uint32_t)(miku_fnv1a_64(m, strlen(m)) & (MK_CONV_RPC_HASH - 1));
        for (int n = 0; n < MK_CONV_RPC_HASH; n++) {
            if (g_conv_rpc_hash[idx] < 0) { g_conv_rpc_hash[idx] = (int16_t)i; break; }
            idx = (idx + 1) & (MK_CONV_RPC_HASH - 1);
        }
    }
    g_conv_rpc_ready = 1;
}

static int conv_rpc_id(const char *method) {
    if (!method) return -1;
    conv_rpc_init();
    uint32_t idx = (uint32_t)(miku_fnv1a_64(method, strlen(method)) & (MK_CONV_RPC_HASH - 1));
    for (int n = 0; n < MK_CONV_RPC_HASH; n++) {
        int id = g_conv_rpc_hash[idx];
        if (id < 0) return -1;
        if (strcmp(g_conv_rpc_names[id], method) == 0) return id;
        idx = (idx + 1) & (MK_CONV_RPC_HASH - 1);
    }
    return -1;
}


static void conv_owner_to_json_arr(miku_conv_service_t *svc, const char *owner,
                                   miku_json_val_t *arr, int max, int active_only) {
    if (!svc || !owner || !arr || max <= 0) return;
    int n = 0;
    for (int ci = owner_head(svc, owner); ci >= 0 && n < max; ci = svc->owner_next[ci]) {
        miku_conversation_t *c = &svc->convs[ci];
        if (active_only && c->latest_msg_send_time <= 0 && c->unread_count <= 0)
            continue;
        miku_json_array_push(arr, miku_conversation_to_json(c));
        n++;
    }
}

void miku_conv_handle_rpc(miku_conv_service_t *svc, const char *method,
                           const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    switch (conv_rpc_id(method)) {
    case MK_CONV_RPC_getAllConversations:
    {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        int64_t cnt = req ? miku_json_int(miku_json_get(req, "count")) : 0;
        int max = (cnt > 0 && cnt < MK_MAX_CONVS) ? (int)cnt : MK_MAX_CONVS;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (owner) conv_owner_to_json_arr(svc, owner, arr, max, 0);
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getConversation:
    {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        miku_conversation_t c;
        int rc = miku_conv_get(svc, owner, cid, &c);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 4001);
        if (rc == 0) miku_json_object_set(resp, "data", miku_conversation_to_json(&c));
    } break;
    case MK_CONV_RPC_setConversation:
    {
        miku_conversation_t c;
        memset(&c, 0, sizeof(c));
        miku_conversation_from_json(req, &c);
        int rc = miku_conv_update(svc, &c);
        if (rc == -2) rc = miku_conv_create(svc, &c);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
    } break;
    case MK_CONV_RPC_setConversations:
    {
        miku_conversation_t c;
        memset(&c, 0, sizeof(c));
        miku_conversation_from_json(req, &c);
        if (miku_conv_update(svc, &c) == -2)
            miku_conv_create(svc, &c);
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_CONV_RPC_deleteConversation:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_CONV_RPC_getConversationList:
    case MK_CONV_RPC_getConversations:
    {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        if (!owner) owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        int64_t cnt = req ? miku_json_int(miku_json_get(req, "count")) : 0;
        int max = (cnt > 0 && cnt < MK_MAX_CONVS) ? (int)cnt : MK_MAX_CONVS;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (owner) conv_owner_to_json_arr(svc, owner, arr, max, 0);
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getActiveConversations:
    {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        if (!owner) owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        int64_t cnt = req ? miku_json_int(miku_json_get(req, "count")) : 0;
        int max = (cnt > 0 && cnt < MK_MAX_CONVS) ? (int)cnt : MK_MAX_CONVS;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (owner) conv_owner_to_json_arr(svc, owner, arr, max, 1);
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getTotalUnreadMsgCount:
    {
        const char *owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!owner) owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        int64_t total = 0;
        if (owner) {
            for (int ci = owner_head(svc, owner); ci >= 0; ci = svc->owner_next[ci])
                total += svc->convs[ci].unread_count;
        }
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "count", total);
    } break;
    case MK_CONV_RPC_setConversationMinSeq:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_CONV_RPC_markConversationMessageAsRead:
    {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        if (!owner) owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        int rc = -1;
        if (owner && cid) {
            miku_conversation_t c;
            if (miku_conv_get(svc, owner, cid, &c) == 0) {
                c.unread_count = 0;
                rc = miku_conv_update(svc, &c);
            }
        }
        miku_ji(resp, "errCode", rc == 0 ? 0 : 4001);
    } break;
    case MK_CONV_RPC_clearConversationMsg:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_CONV_RPC_pinConversation:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_CONV_RPC_deleteConversations:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_CONV_RPC_getFullConversationIDs:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getIncrementalConversation:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getNotNotifyConversationIDs:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getOwnerConversation:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getPinnedConversationIDs:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_getSortedConversationList:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_CONV_RPC_updateConversationsByUser:
    {
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
    } break;
    default:
        miku_ji(resp, "errCode", 404);
        break;
    }
}

