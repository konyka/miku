#include "miku_msg.h"
#include "miku_hash.h"
#include "miku_uuid.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

/* 2x max msgs for open-addressing load factor ~0.5 */
#define MK_MSG_HASH 131072

struct miku_msg_service_s {
    miku_msg_t msgs[MK_MAX_MSGS];
    int count;
    int64_t seq;
    int32_t sid_hash[MK_MSG_HASH];   /* server_msg_id → msgs[] index */
    int32_t cid_hash[MK_MSG_HASH];   /* client_msg_id → msgs[] index */
    int32_t conv_hash[MK_MSG_HASH];  /* conversation_id → head msg index */
    int32_t conv_next[MK_MAX_MSGS];  /* intrusive list within a conversation */
};

static uint32_t str_slot(const char *s) {
    return (uint32_t)(miku_fnv1a_64(s, strlen(s)) & (MK_MSG_HASH - 1));
}

static void hash_insert_key(int32_t *table, const char *key, int mi,
                            const miku_msg_t *msgs, int which) {
    /* which: 0=server_msg_id, 1=client_msg_id, 2=conversation_id head prepend */
    uint32_t idx = str_slot(key);
    for (int n = 0; n < MK_MSG_HASH; n++) {
        int cur = table[idx];
        if (cur < 0) {
            table[idx] = mi;
            return;
        }
        if (which == 0 && strcmp(msgs[cur].server_msg_id, key) == 0) {
            table[idx] = mi;
            return;
        }
        if (which == 1 && strcmp(msgs[cur].client_msg_id, key) == 0) {
            table[idx] = mi;
            return;
        }
        idx = (idx + 1) & (MK_MSG_HASH - 1);
    }
}

static int hash_find_sid(miku_msg_service_t *svc, const char *sid) {
    uint32_t idx = str_slot(sid);
    for (int n = 0; n < MK_MSG_HASH; n++) {
        int mi = svc->sid_hash[idx];
        if (mi < 0) return -1;
        if (strcmp(svc->msgs[mi].server_msg_id, sid) == 0) return mi;
        idx = (idx + 1) & (MK_MSG_HASH - 1);
    }
    return -1;
}

static int hash_find_cid(miku_msg_service_t *svc, const char *cid) {
    uint32_t idx = str_slot(cid);
    for (int n = 0; n < MK_MSG_HASH; n++) {
        int mi = svc->cid_hash[idx];
        if (mi < 0) return -1;
        if (strcmp(svc->msgs[mi].client_msg_id, cid) == 0) return mi;
        idx = (idx + 1) & (MK_MSG_HASH - 1);
    }
    return -1;
}

static int conv_head(miku_msg_service_t *svc, const char *conv_id) {
    uint32_t idx = str_slot(conv_id);
    for (int n = 0; n < MK_MSG_HASH; n++) {
        int mi = svc->conv_hash[idx];
        if (mi < 0) return -1;
        if (strcmp(svc->msgs[mi].conversation_id, conv_id) == 0) return mi;
        idx = (idx + 1) & (MK_MSG_HASH - 1);
    }
    return -1;
}

static void conv_link(miku_msg_service_t *svc, int mi) {
    const char *cid = svc->msgs[mi].conversation_id;
    if (!cid[0]) {
        svc->conv_next[mi] = -1;
        return;
    }
    uint32_t idx = str_slot(cid);
    for (int n = 0; n < MK_MSG_HASH; n++) {
        int head = svc->conv_hash[idx];
        if (head < 0) {
            svc->conv_hash[idx] = mi;
            svc->conv_next[mi] = -1;
            return;
        }
        if (strcmp(svc->msgs[head].conversation_id, cid) == 0) {
            svc->conv_next[mi] = head;
            svc->conv_hash[idx] = mi;
            return;
        }
        idx = (idx + 1) & (MK_MSG_HASH - 1);
    }
}

static void hash_del_sid(miku_msg_service_t *svc, const char *sid) {
    if (!sid || !sid[0]) return;
    uint32_t idx = str_slot(sid);
    for (int n = 0; n < MK_MSG_HASH; n++) {
        int mi = svc->sid_hash[idx];
        if (mi < 0) return;
        if (strcmp(svc->msgs[mi].server_msg_id, sid) == 0) {
            svc->sid_hash[idx] = -1;
            uint32_t j = (idx + 1) & (MK_MSG_HASH - 1);
            while (svc->sid_hash[j] >= 0) {
                int rem = svc->sid_hash[j];
                svc->sid_hash[j] = -1;
                if (svc->msgs[rem].server_msg_id[0])
                    hash_insert_key(svc->sid_hash, svc->msgs[rem].server_msg_id,
                                    rem, svc->msgs, 0);
                j = (j + 1) & (MK_MSG_HASH - 1);
            }
            return;
        }
        idx = (idx + 1) & (MK_MSG_HASH - 1);
    }
}

static int lower_bound_seq(miku_msg_service_t *svc, int64_t begin) {
    /* msgs[] stays sorted by seq (append-only + memmove deletes). */
    int lo = 0, hi = svc->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (svc->msgs[mid].seq < begin) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void rebuild_indexes(miku_msg_service_t *svc);

static void msg_remove_at(miku_msg_service_t *svc, int mi) {
    if (!svc || mi < 0 || mi >= svc->count) return;
    /* Keep msgs[] seq-ordered (binary pull); rebuild indexes after compact. */
    memmove(&svc->msgs[mi], &svc->msgs[mi + 1],
            (size_t)(svc->count - mi - 1) * sizeof(miku_msg_t));
    svc->count--;
    rebuild_indexes(svc);
}

static void rebuild_indexes(miku_msg_service_t *svc) {
    for (int i = 0; i < MK_MSG_HASH; i++) {
        svc->sid_hash[i] = -1;
        svc->cid_hash[i] = -1;
        svc->conv_hash[i] = -1;
    }
    for (int i = 0; i < MK_MAX_MSGS; i++) svc->conv_next[i] = -1;
    for (int mi = 0; mi < svc->count; mi++) {
        if (svc->msgs[mi].server_msg_id[0])
            hash_insert_key(svc->sid_hash, svc->msgs[mi].server_msg_id, mi, svc->msgs, 0);
        if (svc->msgs[mi].client_msg_id[0])
            hash_insert_key(svc->cid_hash, svc->msgs[mi].client_msg_id, mi, svc->msgs, 1);
        conv_link(svc, mi);
    }
}

miku_msg_service_t *miku_msg_service_create(void) {
    miku_msg_service_t *svc = (miku_msg_service_t *)calloc(1, sizeof(*svc));
    if (svc) {
        for (int i = 0; i < MK_MSG_HASH; i++) {
            svc->sid_hash[i] = -1;
            svc->cid_hash[i] = -1;
            svc->conv_hash[i] = -1;
        }
        for (int i = 0; i < MK_MAX_MSGS; i++) svc->conv_next[i] = -1;
    }
    return svc;
}
void miku_msg_service_destroy(miku_msg_service_t *svc) { free(svc); }

int miku_msg_send(miku_msg_service_t *svc, miku_msg_t *m) {
    if (!svc || !m || svc->count >= MK_MAX_MSGS) return -1;
    if (!m->conversation_id[0]) {
        miku_conversation_id_resolve(m->conversation_id, sizeof(m->conversation_id),
                                     NULL, m->group_id, m->send_id, m->recv_id);
    }
    miku_uuid_generate(m->server_msg_id);
    m->seq = ++svc->seq;
    m->send_time = miku_timestamp_ms();
    m->create_time = m->send_time;
    m->status = 0;
    int mi = svc->count++;
    svc->msgs[mi] = *m;
    if (svc->msgs[mi].server_msg_id[0])
        hash_insert_key(svc->sid_hash, svc->msgs[mi].server_msg_id, mi, svc->msgs, 0);
    if (svc->msgs[mi].client_msg_id[0])
        hash_insert_key(svc->cid_hash, svc->msgs[mi].client_msg_id, mi, svc->msgs, 1);
    conv_link(svc, mi);
    return 0;
}

int miku_msg_get_by_conv(miku_msg_service_t *svc, const char *conv_id,
                          int64_t start, int64_t end, int count,
                          miku_msg_t *out, int max) {
    if (!svc || !conv_id || !out) return 0;
    int n = 0;
    /* Chain is newest-first (prepend on send). */
    for (int mi = conv_head(svc, conv_id); mi >= 0 && n < count && n < max;
         mi = svc->conv_next[mi]) {
        miku_msg_t *m = &svc->msgs[mi];
        if (m->send_time >= start && (end == 0 || m->send_time <= end))
            out[n++] = *m;
    }
    return n;
}

int miku_msg_revoke(miku_msg_service_t *svc, const char *user_id, const char *client_msg_id) {
    if (!svc || !user_id || !client_msg_id) return -1;
    int mi = hash_find_cid(svc, client_msg_id);
    if (mi >= 0 && strcmp(svc->msgs[mi].send_id, user_id) == 0) {
        svc->msgs[mi].status = 2;
        return 0;
    }
    /* Fallback: older duplicate client IDs may not be the hashed slot. */
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->msgs[i].client_msg_id, client_msg_id) == 0 &&
            strcmp(svc->msgs[i].send_id, user_id) == 0) {
            svc->msgs[i].status = 2;
            return 0;
        }
    }
    return -2;
}

int miku_msg_update_delivery(miku_msg_service_t *svc, const char *client_msg_id,
                             int64_t seq, const char *server_msg_id, int64_t send_time) {
    if (!svc || !client_msg_id || !client_msg_id[0]) return -1;
    int mi = hash_find_cid(svc, client_msg_id);
    if (mi < 0) {
        for (int i = svc->count - 1; i >= 0; i--) {
            if (strcmp(svc->msgs[i].client_msg_id, client_msg_id) == 0) {
                mi = i;
                break;
            }
        }
    }
    if (mi < 0) return -1;
    if (seq > 0) svc->msgs[mi].seq = seq;
    if (server_msg_id && server_msg_id[0]) {
        char old_sid[sizeof(svc->msgs[mi].server_msg_id)];
        memcpy(old_sid, svc->msgs[mi].server_msg_id, sizeof(old_sid));
        if (old_sid[0] && strcmp(old_sid, server_msg_id) != 0)
            hash_del_sid(svc, old_sid);
        strncpy(svc->msgs[mi].server_msg_id, server_msg_id,
                sizeof(svc->msgs[mi].server_msg_id) - 1);
        svc->msgs[mi].server_msg_id[sizeof(svc->msgs[mi].server_msg_id) - 1] = '\0';
        if (!old_sid[0] || strcmp(old_sid, server_msg_id) != 0)
            hash_insert_key(svc->sid_hash, svc->msgs[mi].server_msg_id, mi, svc->msgs, 0);
    }
    if (send_time > 0) {
        svc->msgs[mi].send_time = send_time;
        svc->msgs[mi].create_time = send_time;
    }
    return 0;
}


enum {
    MK_MSG_RPC_sendMsg = 0,
    MK_MSG_RPC_getMsgByConv = 1,
    MK_MSG_RPC_revokeMsg = 2,
    MK_MSG_RPC_getServerTime = 3,
    MK_MSG_RPC_getSendMsgStatus = 4,
    MK_MSG_RPC_cleanUpMsg = 5,
    MK_MSG_RPC_deleteMsg = 6,
    MK_MSG_RPC_batchSendMsg = 7,
    MK_MSG_RPC_markMsgAsRead = 8,
    MK_MSG_RPC_getMsgBySeq = 9,
    MK_MSG_RPC_send = 10,
    MK_MSG_RPC_sendSimpleMsg = 11,
    MK_MSG_RPC_sendBusinessNotification = 12,
    MK_MSG_RPC_getMsg = 13,
    MK_MSG_RPC_getNewestSeq = 14,
    MK_MSG_RPC_pullMsgBySeq = 15,
    MK_MSG_RPC_searchMsg = 16,
    MK_MSG_RPC_markMsgsAsRead = 17,
    MK_MSG_RPC_markConversationAsRead = 18,
    MK_MSG_RPC_setConversationHasReadSeq = 19,
    MK_MSG_RPC_getConversationsHasReadAndMaxSeq = 20,
    MK_MSG_RPC_checkMsgIsSendSuccess = 21,
    MK_MSG_RPC_clearConversationMsg = 22,
    MK_MSG_RPC_userClearAllMsg = 23,
    MK_MSG_RPC_deleteMsgPhysical = 24,
    MK_MSG_RPC_deleteMsgPhysicalBySeq = 25,
    MK_MSG_RPC_setMessageReactionExtensions = 26,
    MK_MSG_RPC_getMessageListReactionExtensions = 27,
    MK_MSG_RPC_addMessageReactionExtensions = 28,
    MK_MSG_RPC_deleteMessageReactionExtensions = 29,
    MK_MSG_RPC_COUNT = 30
};

#define MK_MSG_RPC_HASH 64
static const char *const g_msg_rpc_names[MK_MSG_RPC_COUNT] = {
    "sendMsg",
    "getMsgByConv",
    "revokeMsg",
    "getServerTime",
    "getSendMsgStatus",
    "cleanUpMsg",
    "deleteMsg",
    "batchSendMsg",
    "markMsgAsRead",
    "getMsgBySeq",
    "send",
    "sendSimpleMsg",
    "sendBusinessNotification",
    "getMsg",
    "getNewestSeq",
    "pullMsgBySeq",
    "searchMsg",
    "markMsgsAsRead",
    "markConversationAsRead",
    "setConversationHasReadSeq",
    "getConversationsHasReadAndMaxSeq",
    "checkMsgIsSendSuccess",
    "clearConversationMsg",
    "userClearAllMsg",
    "deleteMsgPhysical",
    "deleteMsgPhysicalBySeq",
    "setMessageReactionExtensions",
    "getMessageListReactionExtensions",
    "addMessageReactionExtensions",
    "deleteMessageReactionExtensions"
};

static int16_t g_msg_rpc_hash[MK_MSG_RPC_HASH];
static int g_msg_rpc_ready;

static void msg_rpc_init(void) {
    if (g_msg_rpc_ready) return;
    for (int i = 0; i < MK_MSG_RPC_HASH; i++) g_msg_rpc_hash[i] = -1;
    for (int i = 0; i < MK_MSG_RPC_COUNT; i++) {
        const char *m = g_msg_rpc_names[i];
        uint32_t idx = (uint32_t)(miku_fnv1a_64(m, strlen(m)) & (MK_MSG_RPC_HASH - 1));
        for (int n = 0; n < MK_MSG_RPC_HASH; n++) {
            if (g_msg_rpc_hash[idx] < 0) { g_msg_rpc_hash[idx] = (int16_t)i; break; }
            idx = (idx + 1) & (MK_MSG_RPC_HASH - 1);
        }
    }
    g_msg_rpc_ready = 1;
}

static int msg_rpc_id(const char *method) {
    if (!method) return -1;
    msg_rpc_init();
    uint32_t idx = (uint32_t)(miku_fnv1a_64(method, strlen(method)) & (MK_MSG_RPC_HASH - 1));
    for (int n = 0; n < MK_MSG_RPC_HASH; n++) {
        int id = g_msg_rpc_hash[idx];
        if (id < 0) return -1;
        if (strcmp(g_msg_rpc_names[id], method) == 0) return id;
        idx = (idx + 1) & (MK_MSG_RPC_HASH - 1);
    }
    return -1;
}

void miku_msg_handle_rpc(miku_msg_service_t *svc, const char *method,
                          const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    switch (msg_rpc_id(method)) {
    case MK_MSG_RPC_sendMsg: {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int rc = miku_msg_send(svc, &m);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
        if (rc == 0) {
            miku_jss(resp, "serverMsgID", m.server_msg_id);
            miku_ji(resp, "seq", m.seq);
            miku_ji(resp, "sendTime", m.send_time);
        }
    } break;
    case MK_MSG_RPC_getMsgByConv: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        int64_t start = req ? miku_json_int(miku_json_get(req, "startTime")) : 0;
        int64_t end = req ? miku_json_int(miku_json_get(req, "endTime")) : 0;
        int64_t cnt = req ? miku_json_int(miku_json_get(req, "count")) : 20;
        int max = (cnt > 0 && cnt < MK_MAX_MSGS) ? (int)cnt : 20;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        int n = 0;
        if (cid) {
            for (int mi = conv_head(svc, cid); mi >= 0 && n < max; mi = svc->conv_next[mi]) {
                miku_msg_t *m = &svc->msgs[mi];
                if (m->send_time >= start && (end == 0 || m->send_time <= end)) {
                    miku_json_array_push(arr, miku_msg_to_json(m));
                    n++;
                }
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_revokeMsg: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        int rc = miku_msg_revoke(svc, uid, cmid);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 5001);
    } break;
    case MK_MSG_RPC_getServerTime: {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "serverTime", miku_timestamp_ms());
    } break;
    case MK_MSG_RPC_getSendMsgStatus: {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "status", 1);
    } break;
    case MK_MSG_RPC_cleanUpMsg: {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_deleteMsg: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        int deleted = 0;
        if (uid && uid[0] && cmid && cmid[0]) {
            int mi = hash_find_cid(svc, cmid);
            if (mi < 0 || strcmp(svc->msgs[mi].send_id, uid) != 0) {
                mi = -1;
                for (int i = 0; i < svc->count; i++) {
                    if (strcmp(svc->msgs[i].client_msg_id, cmid) == 0 &&
                        strcmp(svc->msgs[i].send_id, uid) == 0) {
                        mi = i;
                        break;
                    }
                }
            }
            if (mi >= 0) {
                msg_remove_at(svc, mi);
                deleted = 1;
            }
        }
        miku_ji(resp, "errCode", deleted ? 0 : ((uid && uid[0] && cmid && cmid[0]) ? 5001 : 400));
    } break;
    case MK_MSG_RPC_batchSendMsg: {
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } break;
    case MK_MSG_RPC_markMsgAsRead: {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_getMsgBySeq: {
        int64_t seq = req ? miku_json_int(miku_json_get(req, "seq")) : 0;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (seq > 0) {
            int i = lower_bound_seq(svc, seq);
            if (i < svc->count && svc->msgs[i].seq == seq)
                miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[i]));
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_send: {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int rc = miku_msg_send(svc, &m);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
        if (rc == 0) {
            miku_jss(resp, "serverMsgID", m.server_msg_id);
            miku_ji(resp, "seq", (int)m.seq);
            miku_ji(resp, "sendTime", (int)m.send_time);
        }
    } break;
    case MK_MSG_RPC_sendSimpleMsg: {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int rc = miku_msg_send(svc, &m);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
        if (rc == 0) {
            miku_jss(resp, "serverMsgID", m.server_msg_id);
            miku_ji(resp, "seq", m.seq);
            miku_ji(resp, "sendTime", m.send_time);
        }
    } break;
    case MK_MSG_RPC_sendBusinessNotification: {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int rc = miku_msg_send(svc, &m);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
    } break;
    case MK_MSG_RPC_getMsg: {
        const char *smid = req ? miku_json_str(miku_json_get(req, "serverMsgID")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (smid) {
            int mi = hash_find_sid(svc, smid);
            if (mi >= 0)
                miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[mi]));
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_getNewestSeq: {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "seq", (int)svc->seq);
    } break;
    case MK_MSG_RPC_pullMsgBySeq: {
        int64_t begin = req ? miku_json_int(miku_json_get(req, "beginSeq")) : 0;
        int64_t end_seq = req ? miku_json_int(miku_json_get(req, "endSeq")) : 0;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (cid && cid[0]) {
            for (int mi = conv_head(svc, cid); mi >= 0; mi = svc->conv_next[mi]) {
                int64_t s = svc->msgs[mi].seq;
                if (s >= begin && (end_seq == 0 || s <= end_seq))
                    miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[mi]));
            }
        } else {
            int i = lower_bound_seq(svc, begin);
            for (; i < svc->count; i++) {
                int64_t s = svc->msgs[i].seq;
                if (end_seq != 0 && s > end_seq) break;
                miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[i]));
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_searchMsg: {
        const char *keyword = req ? miku_json_str(miku_json_get(req, "keyword")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (keyword && keyword[0]) {
            for (int i = svc->count - 1; i >= 0; i--) {
                if (strstr(svc->msgs[i].content, keyword))
                    miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[i]));
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_markMsgsAsRead: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        (void)cid;
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_markConversationAsRead: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        (void)cid;
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_setConversationHasReadSeq: {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_getConversationsHasReadAndMaxSeq: {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_checkMsgIsSendSuccess: {
        const char *smid = req ? miku_json_str(miku_json_get(req, "serverMsgID")) : NULL;
        int found = (smid && hash_find_sid(svc, smid) >= 0) ? 1 : 0;
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "status", found ? 1 : 0);
    } break;
    case MK_MSG_RPC_clearConversationMsg: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        if (!cid || !cid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int w = 0;
        for (int i = 0; i < svc->count; i++) {
            if (strcmp(svc->msgs[i].conversation_id, cid) == 0) continue;
            svc->msgs[w++] = svc->msgs[i];
        }
        int deleted = svc->count - w;
        svc->count = w;
        rebuild_indexes(svc);
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "deleted", deleted);
    } break;
    case MK_MSG_RPC_userClearAllMsg: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        /* Single-chat only: group msgs are shared history — never hard-delete. */
        int w = 0;
        for (int i = 0; i < svc->count; i++) {
            miku_msg_t *m = &svc->msgs[i];
            if (!m->group_id[0] &&
                (strcmp(m->send_id, uid) == 0 || strcmp(m->recv_id, uid) == 0))
                continue;
            svc->msgs[w++] = *m;
        }
        int deleted = svc->count - w;
        svc->count = w;
        rebuild_indexes(svc);
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "deleted", deleted);
    } break;
    case MK_MSG_RPC_deleteMsgPhysical: {
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        int deleted = 0;
        if (cmid) {
            int mi = hash_find_cid(svc, cmid);
            if (mi < 0) {
                for (int i = 0; i < svc->count; i++) {
                    if (strcmp(svc->msgs[i].client_msg_id, cmid) == 0) { mi = i; break; }
                }
            }
            if (mi >= 0) {
                msg_remove_at(svc, mi);
                deleted = 1;
            }
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } break;
    case MK_MSG_RPC_deleteMsgPhysicalBySeq: {
        int64_t del_seq = req ? miku_json_int(miku_json_get(req, "seq")) : 0;
        int deleted = 0;
        if (del_seq > 0) {
            int i = lower_bound_seq(svc, del_seq);
            if (i < svc->count && svc->msgs[i].seq == del_seq) {
                msg_remove_at(svc, i);
                deleted = 1;
            }
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } break;
    case MK_MSG_RPC_setMessageReactionExtensions: {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_getMessageListReactionExtensions: {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_addMessageReactionExtensions: {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_deleteMessageReactionExtensions: {
        miku_ji(resp, "errCode", 0);
    } break;
    default:
        if (strncmp(method, "setMessage", 10) == 0 ||
            strncmp(method, "getMessage", 10) == 0 ||
            strncmp(method, "addMessage", 10) == 0 ||
            strncmp(method, "deleteMessage", 13) == 0) {
            miku_ji(resp, "errCode", 0);
        } else {
            miku_ji(resp, "errCode", 404);
        }
        break;
    }
}
