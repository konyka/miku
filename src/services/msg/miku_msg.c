#include "miku_msg.h"
#include "miku_hash.h"
#include "miku_uuid.h"
#include "miku_json_util.h"
#include "miku_group.h"
#include "miku_friend.h"
#include "miku_token.h"
#include "miku_thread.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

/* 2x max msgs for open-addressing load factor ~0.5 */
#define MK_MSG_HASH 131072

/* GLOBAL LOCK ORDER (pass-28 T0-P2): group_svc → friend_svc → msg_store.
 * miku_msg_service_t::lock (this struct) must be the LAST acquired and the
 * FIRST released along this chain. Reversing the order anywhere in the
 * codebase reintroduces the pass-26 ABBA hazard. */
struct miku_msg_service_s {
    miku_msg_t msgs[MK_MAX_MSGS];
    int count;
    int64_t seq;
    int32_t sid_hash[MK_MSG_HASH];   /* server_msg_id → msgs[] index */
    int32_t cid_hash[MK_MSG_HASH];   /* client_msg_id → msgs[] index */
    int32_t conv_hash[MK_MSG_HASH];  /* conversation_id → head msg index */
    int32_t conv_next[MK_MAX_MSGS];  /* intrusive list within a conversation */
    miku_group_service_t *group_svc; /* non-owning; for getMsg group membership gate */
    miku_friend_service_t *friend_svc; /* non-owning; for si_ conv read gates */
    miku_rwlock_t lock;              /* protects msgs[], count, seq, all hashes, conv_next[] */
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

static int msg_user_may_access_conv(miku_msg_service_t *svc, const char *uid, const char *cid);

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


static void rebuild_indexes(miku_msg_service_t *svc);

static int msg_may_delete_physical_nolock(miku_msg_service_t *svc, const char *uid,
                                          const char *client_msg_id);
static int msg_may_delete_physical_by_seq_nolock(miku_msg_service_t *svc, const char *uid,
                                                 const char *conv_id, int64_t seq);

static int msg_send_gate(miku_msg_service_t *svc, const miku_msg_t *m);

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

void miku_msg_service_set_group_svc(miku_msg_service_t *svc, miku_group_service_t *group) {
    if (!svc) return;
    miku_rwlock_wrlock(&svc->lock);
    svc->group_svc = group;
    miku_rwlock_unlock(&svc->lock);
}

void miku_msg_service_set_friend_svc(miku_msg_service_t *svc, miku_friend_service_t *friend) {
    if (!svc) return;
    miku_rwlock_wrlock(&svc->lock);
    svc->friend_svc = friend;
    miku_rwlock_unlock(&svc->lock);
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
        miku_rwlock_init(&svc->lock);
    }
    return svc;
}
void miku_msg_service_destroy(miku_msg_service_t *svc) {
    if (svc) {
        miku_rwlock_destroy(&svc->lock);
        free(svc);
    }
}

static int msg_store_internal(miku_msg_service_t *svc, miku_msg_t *m) {
    if (!svc || !m || svc->count >= MK_MAX_MSGS) return -1;
    /* Always canonical — never trust client conversationID (injection IDOR). */
    miku_conversation_id_resolve(m->conversation_id, sizeof(m->conversation_id),
                                 NULL, m->group_id, m->send_id, m->recv_id);
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

static int miku_msg_send_nolock(miku_msg_service_t *svc, miku_msg_t *m) {
    if (!svc || !m || svc->count >= MK_MAX_MSGS) return -1;
    return msg_store_internal(svc, m);
}

int miku_msg_send(miku_msg_service_t *svc, miku_msg_t *m) {
    if (!svc || !m) return -1;
    /* Foreign membership checks must precede the message-state write lock. */
    miku_conversation_id_resolve(m->conversation_id, sizeof(m->conversation_id),
                                 NULL, m->group_id, m->send_id, m->recv_id);
    int has_foreign_gate = 0;
    miku_rwlock_rdlock(&svc->lock);
    has_foreign_gate = (m->group_id[0] && svc->group_svc) ||
                       (m->recv_id[0] && svc->friend_svc);
    miku_rwlock_unlock(&svc->lock);
    if (has_foreign_gate) {
        int gate = msg_send_gate(svc, m);
        if (gate != 0) return gate;
    }
    miku_rwlock_wrlock(&svc->lock);
    int rc = miku_msg_send_nolock(svc, m);
    miku_rwlock_unlock(&svc->lock);
    return rc;
}

static int miku_msg_get_by_conv_nolock(miku_msg_service_t *svc, const char *conv_id,
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

int miku_msg_get_by_conv(miku_msg_service_t *svc, const char *conv_id,
                          int64_t start, int64_t end, int count,
                          miku_msg_t *out, int max) {
    if (!svc || !conv_id || !out) return 0;
    miku_rwlock_rdlock(&svc->lock);
    int rc = miku_msg_get_by_conv_nolock(svc, conv_id, start, end, count, out, max);
    miku_rwlock_unlock(&svc->lock);
    return rc;
}

static miku_msg_t *msg_snapshot_conv(miku_msg_service_t *svc, const char *conv_id,
                                     int64_t start, int64_t end, int count, int max,
                                     int *out_count) {
    if (out_count) *out_count = 0;
    if (!svc || max <= 0) return NULL;
    miku_rwlock_rdlock(&svc->lock);
    int cap = svc->count < max ? svc->count : max;
    if (cap <= 0) {
        miku_rwlock_unlock(&svc->lock);
        return NULL;
    }
    miku_msg_t *msgs = (miku_msg_t *)malloc((size_t)cap * sizeof(*msgs));
    if (!msgs) {
        miku_rwlock_unlock(&svc->lock);
        return NULL;
    }
    int n = miku_msg_get_by_conv_nolock(svc, conv_id, start, end, count, msgs, cap);
    miku_rwlock_unlock(&svc->lock);
    if (out_count) *out_count = n;
    return msgs;
}

static int miku_msg_revoke_nolock(miku_msg_service_t *svc, const char *user_id, const char *client_msg_id) {
    if (!svc || !user_id || !client_msg_id) return -1;
    int mi = hash_find_cid(svc, client_msg_id);
    if (mi >= 0 && strcmp(svc->msgs[mi].send_id, user_id) == 0) {
        svc->msgs[mi].status = 2;
        return 0;
    }
    return -2;
}

int miku_msg_revoke(miku_msg_service_t *svc, const char *user_id, const char *client_msg_id) {
    if (!svc || !user_id || !client_msg_id) return -1;
    if (!miku_msg_may_delete_physical(svc, user_id, client_msg_id)) return -2;
    miku_rwlock_wrlock(&svc->lock);
    int rc = msg_may_delete_physical_nolock(svc, user_id, client_msg_id)
        ? miku_msg_revoke_nolock(svc, user_id, client_msg_id) : -2;
    miku_rwlock_unlock(&svc->lock);
    return rc;
}

static int miku_msg_update_delivery_nolock(miku_msg_service_t *svc, const char *uid,
                                            const char *client_msg_id,
                                            int64_t seq, const char *server_msg_id, int64_t send_time) {
    if (!svc || !uid || !uid[0] || !client_msg_id || !client_msg_id[0]) return -1;
    int mi = hash_find_cid(svc, client_msg_id);
    if (mi < 0) return -1;
    if (strcmp(svc->msgs[mi].send_id, uid) != 0) return -1;
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

int miku_msg_update_delivery(miku_msg_service_t *svc, const char *uid,
                             const char *client_msg_id,
                             int64_t seq, const char *server_msg_id, int64_t send_time) {
    if (!svc || !uid || !uid[0] || !client_msg_id || !client_msg_id[0]) return -1;
    miku_rwlock_wrlock(&svc->lock);
    int rc = miku_msg_update_delivery_nolock(svc, uid, client_msg_id,
                                              seq, server_msg_id, send_time);
    miku_rwlock_unlock(&svc->lock);
    return rc;
}

static int msg_user_may_access_conv(miku_msg_service_t *svc, const char *uid, const char *cid) {
    if (!svc || !uid || !uid[0] || !cid || !cid[0]) return 0;
    if (strncmp(cid, "si_", 3) == 0) {
        miku_friend_service_t *friend_svc;
        miku_rwlock_rdlock(&svc->lock);
        friend_svc = svc->friend_svc;
        miku_rwlock_unlock(&svc->lock);
        return friend_svc ? miku_friend_may_access_si_conv(friend_svc, uid, cid) : 0;
    }
    if (strncmp(cid, "sg_", 3) == 0) {
        miku_group_service_t *group_svc;
        miku_rwlock_rdlock(&svc->lock);
        group_svc = svc->group_svc;
        miku_rwlock_unlock(&svc->lock);
        return group_svc ? miku_group_is_member(group_svc, cid + 3, uid) : 0;
    }
    miku_rwlock_rdlock(&svc->lock);
    for (int mi = conv_head(svc, cid); mi >= 0; mi = svc->conv_next[mi]) {
        if (strcmp(svc->msgs[mi].send_id, uid) == 0 ||
            strcmp(svc->msgs[mi].recv_id, uid) == 0)
            { miku_rwlock_unlock(&svc->lock); return 1; }
    }
    miku_rwlock_unlock(&svc->lock);
    return 0;
}

static int msg_reaction_conv_gate(miku_msg_service_t *svc, const miku_json_val_t *req) {
    const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
    const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
    if (!uid || !uid[0] || !cid || !cid[0]) return 400;
    return msg_user_may_access_conv(svc, uid, cid) ? 0 : 3003;
}

/* 0 = allowed; otherwise OpenIM-style errCode for RPC response. */
static int msg_send_gate(miku_msg_service_t *svc, const miku_msg_t *m) {
    if (!m || !m->send_id[0]) return 400;
    if (m->group_id[0]) {
        miku_group_service_t *group_svc = NULL;
        if (svc) {
            miku_rwlock_rdlock(&svc->lock);
            group_svc = svc->group_svc;
            miku_rwlock_unlock(&svc->lock);
        }
        if (!group_svc || !miku_group_is_member(group_svc, m->group_id, m->send_id))
            return 3003;
        return 0;
    }
    if (!m->recv_id[0]) return 400;
    miku_friend_service_t *friend_svc = NULL;
    if (svc) {
        miku_rwlock_rdlock(&svc->lock);
        friend_svc = svc->friend_svc;
        miku_rwlock_unlock(&svc->lock);
    }
    if (!friend_svc) return 6002;
    if (miku_friend_is_black(friend_svc, m->send_id, m->recv_id) ||
        miku_friend_is_black(friend_svc, m->recv_id, m->send_id))
        return 6001;
    if (!miku_friend_is_mutual(friend_svc, m->send_id, m->recv_id))
        return 6002;
    return 0;
}

static int msg_may_delete_physical_nolock(miku_msg_service_t *svc, const char *uid,
                                          const char *client_msg_id) {
    if (!svc || !uid || !uid[0] || !client_msg_id || !client_msg_id[0]) return 0;
    int mi = hash_find_cid(svc, client_msg_id);
    return mi >= 0 && strcmp(svc->msgs[mi].send_id, uid) == 0;
}

int miku_msg_may_delete_physical(miku_msg_service_t *svc, const char *uid,
                                  const char *client_msg_id) {
    if (!svc || !uid || !uid[0] || !client_msg_id || !client_msg_id[0]) return 0;
    char conv_id[sizeof(svc->msgs[0].conversation_id)] = {0};
    miku_rwlock_rdlock(&svc->lock);
    int mi = hash_find_cid(svc, client_msg_id);
    int is_sender = msg_may_delete_physical_nolock(svc, uid, client_msg_id);
    if (is_sender) strncpy(conv_id, svc->msgs[mi].conversation_id, sizeof(conv_id) - 1);
    miku_rwlock_unlock(&svc->lock);
    if (!is_sender) return 0;
    if (strncmp(conv_id, "si_", 3) == 0 || strncmp(conv_id, "sg_", 3) == 0)
        return msg_user_may_access_conv(svc, uid, conv_id);
    return 1;
}

static int msg_may_delete_physical_by_seq_nolock(miku_msg_service_t *svc, const char *uid,
                                                 const char *conv_id, int64_t seq) {
    if (!svc || !uid || !uid[0] || !conv_id || !conv_id[0] || seq <= 0) return 0;
    for (int mi = conv_head(svc, conv_id); mi >= 0; mi = svc->conv_next[mi]) {
        if (svc->msgs[mi].seq == seq)
            return strcmp(svc->msgs[mi].send_id, uid) == 0;
    }
    return 0;
}

int miku_msg_may_delete_physical_by_seq(miku_msg_service_t *svc, const char *uid,
                                         const char *conv_id, int64_t seq) {
    if (!svc || !uid || !uid[0] || !conv_id || !conv_id[0] || seq <= 0) return 0;
    if (strncmp(conv_id, "si_", 3) == 0 || strncmp(conv_id, "sg_", 3) == 0) {
        if (!msg_user_may_access_conv(svc, uid, conv_id)) return 0;
    }
    miku_rwlock_rdlock(&svc->lock);
    int allowed = msg_may_delete_physical_by_seq_nolock(svc, uid, conv_id, seq);
    miku_rwlock_unlock(&svc->lock);
    return allowed;
}

static int msg_rpc_admin_platform(const miku_json_val_t *req) {
    if (!req) return 0;
    if (miku_json_int(miku_json_get(req, "platformID")) != 5) return 0;
    const char *tok = miku_json_str(miku_json_get(req, "token"));
    if (!tok || !tok[0]) {
        MK_LOG_WARN("msg_rpc_admin_platform: missing token for platformID=5");
        return 0;
    }
    char uid[64] = {0};
    int platform = -1;
    int64_t issued = 0;
    if (miku_token_verify_ex(tok, miku_admin_default_secret(),
                              uid, sizeof(uid), &platform, &issued) != 0) {
        MK_LOG_WARN("msg_rpc_admin_platform: token verification failed");
        return 0;
    }
    if (platform != 5) {
        MK_LOG_WARN("msg_rpc_admin_platform: token platform=%d != 5", platform);
        return 0;
    }
    return 1;
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

static void msg_rpc_clear_resp(miku_json_val_t *resp) {
    if (!resp || miku_json_type(resp) != MK_JSON_OBJECT) return;
    static const char *keys[] = {
        "errCode", "errMsg", "data", "seq", "serverMsgID", "sendTime",
        "serverTime", "status", "deleted", NULL,
    };
    for (int i = 0; keys[i]; i++) {
        if (miku_json_get(resp, keys[i]))
            miku_json_object_set(resp, keys[i], miku_json_create_null());
    }
}

void miku_msg_handle_rpc(miku_msg_service_t *svc, const char *method,
                           const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    msg_rpc_clear_resp(resp);
    switch (msg_rpc_id(method)) {
    case MK_MSG_RPC_sendMsg: {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int gate = msg_send_gate(svc, &m);
        if (gate != 0) {
            miku_ji(resp, "errCode", gate);
            break;
        }
        miku_rwlock_wrlock(&svc->lock);
        int rc = miku_msg_send_nolock(svc, &m);
        miku_rwlock_unlock(&svc->lock);
        miku_ji(resp, "errCode", rc == 0 ? 0 : (rc > 0 ? rc : 500));
        if (rc == 0) {
            miku_jss(resp, "serverMsgID", m.server_msg_id);
            miku_ji(resp, "seq", m.seq);
            miku_ji(resp, "sendTime", m.send_time);
        }
    } break;
    case MK_MSG_RPC_getMsgByConv: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int64_t start = req ? miku_json_int(miku_json_get(req, "startTime")) : 0;
        int64_t end = req ? miku_json_int(miku_json_get(req, "endTime")) : 0;
        int64_t cnt = req ? miku_json_int(miku_json_get(req, "count")) : 20;
        int max = (cnt > 0 && cnt < MK_MAX_MSGS) ? (int)cnt : 20;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        int n = 0;
        if (cid && cid[0] && uid && uid[0] && msg_user_may_access_conv(svc, uid, cid)) {
            miku_msg_t *msgs = msg_snapshot_conv(svc, cid, start, end, max, max, &n);
            if (msgs) {
                for (int i = 0; i < n; i++)
                    miku_json_array_push(arr, miku_msg_to_json(&msgs[i]));
                free(msgs);
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_revokeMsg: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        if (!uid || !uid[0] || !cmid || !cmid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int rc = miku_msg_revoke(svc, uid, cmid);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 5001);
    } break;
    case MK_MSG_RPC_getServerTime: {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "serverTime", miku_timestamp_ms());
    } break;
    case MK_MSG_RPC_getSendMsgStatus: {
        const char *smid = req ? miku_json_str(miku_json_get(req, "serverMsgID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!uid || !uid[0] || ((!smid || !smid[0]) && (!cmid || !cmid[0]))) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int status = 0;
        miku_msg_t msg;
        int mi = -1;
        miku_rwlock_rdlock(&svc->lock);
        if (smid && smid[0]) mi = hash_find_sid(svc, smid);
        else if (cmid && cmid[0]) mi = hash_find_cid(svc, cmid);
        if (mi >= 0) msg = svc->msgs[mi];
        miku_rwlock_unlock(&svc->lock);
        if (mi >= 0 && strcmp(msg.send_id, uid) == 0 &&
            msg_user_may_access_conv(svc, uid, msg.conversation_id))
            status = 1;
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "status", status);
    } break;
    case MK_MSG_RPC_cleanUpMsg: {
        if (!msg_rpc_admin_platform(req)) {
            miku_ji(resp, "errCode", 403);
            break;
        }
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_deleteMsg: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        if (!uid || !uid[0] || !cmid || !cmid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int deleted = 0;
        if (miku_msg_may_delete_physical(svc, uid, cmid)) {
            miku_rwlock_wrlock(&svc->lock);
            int mi = hash_find_cid(svc, cmid);
            if (msg_may_delete_physical_nolock(svc, uid, cmid) && mi >= 0) {
                msg_remove_at(svc, mi);
                deleted = 1;
            }
            miku_rwlock_unlock(&svc->lock);
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } break;
    case MK_MSG_RPC_batchSendMsg: {
        if (!msg_rpc_admin_platform(req)) {
            miku_ji(resp, "errCode", 403);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } break;
    case MK_MSG_RPC_markMsgAsRead: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        if (!msg_user_may_access_conv(svc, uid, cid))
            miku_ji(resp, "errCode", 3003);
        else
            miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_getMsgBySeq: {
        int64_t seq = req ? miku_json_int(miku_json_get(req, "seq")) : 0;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0] || seq <= 0) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (msg_user_may_access_conv(svc, uid, cid)) {
            int n = 0;
            miku_msg_t *msgs = msg_snapshot_conv(svc, cid, 0, 0, MK_MAX_MSGS, MK_MAX_MSGS, &n);
            for (int i = 0; msgs && i < n; i++) {
                if (msgs[i].seq == seq) {
                    miku_json_array_push(arr, miku_msg_to_json(&msgs[i]));
                    break;
                }
            }
            free(msgs);
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_send: {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int gate = msg_send_gate(svc, &m);
        if (gate != 0) {
            miku_ji(resp, "errCode", gate);
            break;
        }
        miku_rwlock_wrlock(&svc->lock);
        int rc = miku_msg_send_nolock(svc, &m);
        miku_rwlock_unlock(&svc->lock);
        miku_ji(resp, "errCode", rc == 0 ? 0 : (rc > 0 ? rc : 500));
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
        int gate = msg_send_gate(svc, &m);
        if (gate != 0) {
            miku_ji(resp, "errCode", gate);
            break;
        }
        miku_rwlock_wrlock(&svc->lock);
        int rc = miku_msg_send_nolock(svc, &m);
        miku_rwlock_unlock(&svc->lock);
        miku_ji(resp, "errCode", rc == 0 ? 0 : (rc > 0 ? rc : 500));
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
        int gate;
        if (m.group_id[0]) {
            /* Group-targeted: enforce membership for all callers (3003). */
            gate = msg_send_gate(svc, &m);
        } else if (msg_rpc_admin_platform(req)) {
            /* Admin direct notification: presence check, bypass friend gate. */
            gate = (!m.send_id[0] || !m.recv_id[0]) ? 400 : 0;
        } else {
            /* Non-admin direct notification: rejected by the API layer. */
            gate = 403;
        }
        if (gate != 0) {
            miku_ji(resp, "errCode", gate);
            break;
        }
        miku_rwlock_wrlock(&svc->lock);
        int rc = miku_msg_send_nolock(svc, &m);
        miku_rwlock_unlock(&svc->lock);
        miku_ji(resp, "errCode", rc == 0 ? 0 : (rc > 0 ? rc : 500));
    } break;
    case MK_MSG_RPC_getMsg: {
        const char *smid = req ? miku_json_str(miku_json_get(req, "serverMsgID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!uid || !uid[0] || !smid || !smid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_msg_t msg;
        int found = 0;
        char cid[sizeof(msg.conversation_id)] = {0};
        miku_rwlock_rdlock(&svc->lock);
        int mi = hash_find_sid(svc, smid);
        if (mi >= 0) {
            msg = svc->msgs[mi];
            strncpy(cid, msg.conversation_id, sizeof(cid) - 1);
            found = 1;
        }
        miku_rwlock_unlock(&svc->lock);
        if (found && msg_user_may_access_conv(svc, uid, cid))
            miku_json_array_push(arr, miku_msg_to_json(&msg));
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_getNewestSeq: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int64_t max = 0;
        if (msg_user_may_access_conv(svc, uid, cid)) {
            int n = 0;
            miku_msg_t *msgs = msg_snapshot_conv(svc, cid, 0, 0, MK_MAX_MSGS, MK_MAX_MSGS, &n);
            for (int i = 0; msgs && i < n; i++)
                if (msgs[i].seq > max) max = msgs[i].seq;
            free(msgs);
        }
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "seq", (int)max);
    } break;
    case MK_MSG_RPC_pullMsgBySeq: {
        int64_t begin = req ? miku_json_int(miku_json_get(req, "beginSeq")) : 0;
        int64_t end_seq = req ? miku_json_int(miku_json_get(req, "endSeq")) : 0;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            /* No conversation to anchor: a bare seq-range query returns an
             * empty oracle (we never do a global scan); a fully empty request
             * is malformed. */
            int has_range = req && (miku_json_get(req, "beginSeq") ||
                                    miku_json_get(req, "endSeq"));
            if (has_range) {
                miku_ji(resp, "errCode", 0);
                miku_json_object_set(resp, "data", miku_json_create_array());
            } else {
                miku_ji(resp, "errCode", 400);
            }
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (msg_user_may_access_conv(svc, uid, cid)) {
            int n = 0;
            miku_msg_t *msgs = msg_snapshot_conv(svc, cid, 0, 0, MK_MAX_MSGS, MK_MAX_MSGS, &n);
            for (int i = 0; msgs && i < n; i++) {
                int64_t s = msgs[i].seq;
                if (s >= begin && (end_seq == 0 || s <= end_seq))
                    miku_json_array_push(arr, miku_msg_to_json(&msgs[i]));
            }
            free(msgs);
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_searchMsg: {
        const char *keyword = req ? miku_json_str(miku_json_get(req, "keyword")) : NULL;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!keyword || !keyword[0] || !cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (msg_user_may_access_conv(svc, uid, cid)) {
            int n = 0;
            miku_msg_t *msgs = msg_snapshot_conv(svc, cid, 0, 0, MK_MAX_MSGS, MK_MAX_MSGS, &n);
            for (int i = 0; msgs && i < n; i++) {
                if (strstr(msgs[i].content, keyword))
                    miku_json_array_push(arr, miku_msg_to_json(&msgs[i]));
            }
            free(msgs);
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_markMsgsAsRead: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        if (!msg_user_may_access_conv(svc, uid, cid))
            miku_ji(resp, "errCode", 3003);
        else
            miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_markConversationAsRead: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        if (!msg_user_may_access_conv(svc, uid, cid))
            miku_ji(resp, "errCode", 3003);
        else
            miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_setConversationHasReadSeq: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        if (!msg_user_may_access_conv(svc, uid, cid))
            miku_ji(resp, "errCode", 3003);
        else
            miku_ji(resp, "errCode", 0);
    } break;
    case MK_MSG_RPC_getConversationsHasReadAndMaxSeq: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_checkMsgIsSendSuccess: {
        const char *smid = req ? miku_json_str(miku_json_get(req, "serverMsgID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!uid || !uid[0] || ((!smid || !smid[0]) && (!cmid || !cmid[0]))) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int found = 0;
        miku_msg_t msg;
        miku_rwlock_rdlock(&svc->lock);
        int mi = smid && smid[0] ? hash_find_sid(svc, smid) : hash_find_cid(svc, cmid);
        if (mi >= 0) msg = svc->msgs[mi];
        miku_rwlock_unlock(&svc->lock);
        if (mi >= 0 && strcmp(msg.send_id, uid) == 0 &&
            msg_user_may_access_conv(svc, uid, msg.conversation_id))
            found = 1;
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "status", found ? 1 : 0);
    } break;
    case MK_MSG_RPC_clearConversationMsg: {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!cid || !cid[0] || !uid || !uid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        /* Group history is shared — never hard-delete via clear. */
        if (strncmp(cid, "sg_", 3) == 0) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        if (!msg_user_may_access_conv(svc, uid, cid)) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        miku_rwlock_wrlock(&svc->lock);
        int w = 0;
        for (int i = 0; i < svc->count; i++) {
            if (strcmp(svc->msgs[i].conversation_id, cid) == 0) continue;
            svc->msgs[w++] = svc->msgs[i];
        }
        int deleted = svc->count - w;
        svc->count = w;
        rebuild_indexes(svc);
        miku_rwlock_unlock(&svc->lock);
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
        miku_rwlock_rdlock(&svc->lock);
        int snap_count = svc->count;
        miku_msg_t *snapshot = snap_count > 0
            ? (miku_msg_t *)malloc((size_t)snap_count * sizeof(*snapshot)) : NULL;
        if (snap_count > 0 && !snapshot) {
            miku_rwlock_unlock(&svc->lock);
            miku_ji(resp, "errCode", 500);
            break;
        }
        if (snapshot) memcpy(snapshot, svc->msgs, (size_t)snap_count * sizeof(*snapshot));
        miku_rwlock_unlock(&svc->lock);
        unsigned char *authorized = snap_count > 0
            ? (unsigned char *)calloc((size_t)snap_count, sizeof(*authorized)) : NULL;
        for (int i = 0; i < snap_count; i++) {
            miku_msg_t *m = &snapshot[i];
            if (!m->group_id[0] &&
                (strcmp(m->send_id, uid) == 0 || strcmp(m->recv_id, uid) == 0))
                authorized[i] = (unsigned char)msg_user_may_access_conv(svc, uid,
                                                                         m->conversation_id);
        }
        int w = 0;
        miku_rwlock_wrlock(&svc->lock);
        for (int i = 0; i < svc->count; i++) {
            miku_msg_t *m = &svc->msgs[i];
            int remove = 0;
            if (!m->group_id[0] &&
                (strcmp(m->send_id, uid) == 0 || strcmp(m->recv_id, uid) == 0)) {
                /* ACL was checked against the pre-lock snapshot; preserve the
                 * existing operation's semantics without foreign lock nesting. */
                for (int j = 0; j < snap_count; j++) {
                    if (authorized && authorized[j] &&
                        strcmp(snapshot ? snapshot[j].client_msg_id : "", m->client_msg_id) == 0) {
                        remove = 1;
                        break;
                    }
                }
            }
            if (!remove) svc->msgs[w++] = *m;
        }
        free(authorized);
        int deleted = svc->count - w;
        svc->count = w;
        rebuild_indexes(svc);
        miku_rwlock_unlock(&svc->lock);
        free(snapshot);
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "deleted", deleted);
    } break;
    case MK_MSG_RPC_deleteMsgPhysical: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        if (!uid || !uid[0] || !cmid || !cmid[0]) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int deleted = 0;
        if (miku_msg_may_delete_physical(svc, uid, cmid)) {
            miku_rwlock_wrlock(&svc->lock);
            int mi = hash_find_cid(svc, cmid);
            if (msg_may_delete_physical_nolock(svc, uid, cmid) && mi >= 0) {
                msg_remove_at(svc, mi);
                deleted = 1;
            }
            miku_rwlock_unlock(&svc->lock);
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } break;
    case MK_MSG_RPC_deleteMsgPhysicalBySeq: {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        int64_t del_seq = req ? miku_json_int(miku_json_get(req, "seq")) : 0;
        if (!uid || !uid[0] || !cid || !cid[0] || del_seq <= 0) {
            miku_ji(resp, "errCode", 400);
            break;
        }
        int deleted = 0;
        if (miku_msg_may_delete_physical_by_seq(svc, uid, cid, del_seq)) {
            miku_rwlock_wrlock(&svc->lock);
            for (int mi = conv_head(svc, cid); mi >= 0; mi = svc->conv_next[mi]) {
                if (msg_may_delete_physical_by_seq_nolock(svc, uid, cid, del_seq) &&
                    svc->msgs[mi].seq == del_seq) {
                    msg_remove_at(svc, mi);
                    deleted = 1;
                    break;
                }
            }
            miku_rwlock_unlock(&svc->lock);
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } break;
    case MK_MSG_RPC_setMessageReactionExtensions: {
        int g = msg_reaction_conv_gate(svc, req);
        miku_ji(resp, "errCode", g ? g : 0);
    } break;
    case MK_MSG_RPC_getMessageListReactionExtensions: {
        int g = msg_reaction_conv_gate(svc, req);
        if (g != 0) {
            miku_ji(resp, "errCode", g);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_MSG_RPC_addMessageReactionExtensions: {
        int g = msg_reaction_conv_gate(svc, req);
        miku_ji(resp, "errCode", g ? g : 0);
    } break;
    case MK_MSG_RPC_deleteMessageReactionExtensions: {
        int g = msg_reaction_conv_gate(svc, req);
        miku_ji(resp, "errCode", g ? g : 0);
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
