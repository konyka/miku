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


void miku_msg_handle_rpc(miku_msg_service_t *svc, const char *method,
                          const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    if (strcmp(method, "sendMsg") == 0) {
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
    } else if (strcmp(method, "getMsgByConv") == 0) {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        int64_t start = req ? miku_json_int(miku_json_get(req, "startTime")) : 0;
        int64_t end = req ? miku_json_int(miku_json_get(req, "endTime")) : 0;
        int64_t cnt = req ? miku_json_int(miku_json_get(req, "count")) : 20;
        miku_msg_t list[16];
        int n = miku_msg_get_by_conv(svc, cid, start, end, (int)cnt, list, 16);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) miku_json_array_push(arr, miku_msg_to_json(&list[i]));
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "revokeMsg") == 0) {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        int rc = miku_msg_revoke(svc, uid, cmid);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 5001);
    } else if (strcmp(method, "getServerTime") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "serverTime", miku_timestamp_ms());
    } else if (strcmp(method, "getSendMsgStatus") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "status", 1);
    } else if (strcmp(method, "cleanUpMsg") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "deleteMsg") == 0) {
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        miku_ji(resp, "errCode", cmid ? 0 : 400);
    } else if (strcmp(method, "batchSendMsg") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } else if (strcmp(method, "markMsgAsRead") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getMsgBySeq") == 0) {
        int64_t seq = req ? miku_json_int(miku_json_get(req, "seq")) : 0;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (seq > 0) {
            int i = lower_bound_seq(svc, seq);
            if (i < svc->count && svc->msgs[i].seq == seq)
                miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[i]));
        }
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "send") == 0) {
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
    } else if (strcmp(method, "sendSimpleMsg") == 0) {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int rc = miku_msg_send(svc, &m);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
        if (rc == 0) {
            miku_jss(resp, "serverMsgID", m.server_msg_id);
            miku_ji(resp, "seq", (int)m.seq);
        }
    } else if (strcmp(method, "sendBusinessNotification") == 0) {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int rc = miku_msg_send(svc, &m);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
    } else if (strcmp(method, "getMsg") == 0) {
        const char *smid = req ? miku_json_str(miku_json_get(req, "serverMsgID")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (smid) {
            int mi = hash_find_sid(svc, smid);
            if (mi >= 0)
                miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[mi]));
        }
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getNewestSeq") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "seq", (int)svc->seq);
    } else if (strcmp(method, "pullMsgBySeq") == 0) {
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
    } else if (strcmp(method, "searchMsg") == 0) {
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
    } else if (strcmp(method, "markMsgsAsRead") == 0) {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        (void)cid;
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "markConversationAsRead") == 0) {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        (void)cid;
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "setConversationHasReadSeq") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getConversationsHasReadAndMaxSeq") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "checkMsgIsSendSuccess") == 0) {
        const char *smid = req ? miku_json_str(miku_json_get(req, "serverMsgID")) : NULL;
        int found = (smid && hash_find_sid(svc, smid) >= 0) ? 1 : 0;
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "status", found ? 1 : 0);
    } else if (strcmp(method, "clearConversationMsg") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "userClearAllMsg") == 0) {
        svc->count = 0;
        rebuild_indexes(svc);
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "deleteMsgPhysical") == 0) {
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
                memmove(&svc->msgs[mi], &svc->msgs[mi + 1],
                        (size_t)(svc->count - mi - 1) * sizeof(miku_msg_t));
                svc->count--;
                rebuild_indexes(svc);
                deleted = 1;
            }
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } else if (strcmp(method, "deleteMsgPhysicalBySeq") == 0) {
        int64_t del_seq = req ? miku_json_int(miku_json_get(req, "seq")) : 0;
        int deleted = 0;
        if (del_seq > 0) {
            int i = lower_bound_seq(svc, del_seq);
            if (i < svc->count && svc->msgs[i].seq == del_seq) {
                memmove(&svc->msgs[i], &svc->msgs[i + 1],
                        (size_t)(svc->count - i - 1) * sizeof(miku_msg_t));
                svc->count--;
                rebuild_indexes(svc);
                deleted = 1;
            }
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } else if (strcmp(method, "setMessageReactionExtensions") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getMessageListReactionExtensions") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "addMessageReactionExtensions") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "deleteMessageReactionExtensions") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strncmp(method, "setMessage", 10) == 0 ||
               strncmp(method, "getMessage", 10) == 0 ||
               strncmp(method, "addMessage", 10) == 0 ||
               strncmp(method, "deleteMessage", 13) == 0) {
        miku_ji(resp, "errCode", 0);
    } else {
        miku_ji(resp, "errCode", 404);
    }
}
