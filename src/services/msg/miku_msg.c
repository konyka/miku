#include "miku_msg.h"
#include "miku_uuid.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

struct miku_msg_service_s {
    miku_msg_t msgs[MK_MAX_MSGS];
    int count;
    int64_t seq;
};

miku_msg_service_t *miku_msg_service_create(void) {
    return (miku_msg_service_t *)calloc(1, sizeof(miku_msg_service_t));
}
void miku_msg_service_destroy(miku_msg_service_t *svc) { free(svc); }

int miku_msg_send(miku_msg_service_t *svc, miku_msg_t *m) {
    if (!svc || !m || svc->count >= MK_MAX_MSGS) return -1;
    miku_uuid_generate(m->server_msg_id);
    m->seq = ++svc->seq;
    m->send_time = miku_timestamp_ms();
    m->create_time = m->send_time;
    m->status = 0;
    svc->msgs[svc->count++] = *m;
    return 0;
}

int miku_msg_get_by_conv(miku_msg_service_t *svc, const char *conv_id,
                          int64_t start, int64_t end, int count,
                          miku_msg_t *out, int max) {
    if (!svc || !conv_id || !out) return 0;
    int n = 0;
    for (int i = svc->count - 1; i >= 0 && n < count && n < max; i--) {
        miku_msg_t *m = &svc->msgs[i];
        bool match_conv = (strstr(conv_id, m->send_id) != NULL && strstr(conv_id, m->recv_id) != NULL);
        if (match_conv && m->send_time >= start && (end == 0 || m->send_time <= end))
            out[n++] = *m;
    }
    return n;
}

int miku_msg_revoke(miku_msg_service_t *svc, const char *user_id, const char *client_msg_id) {
    if (!svc || !user_id || !client_msg_id) return -1;
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->msgs[i].client_msg_id, client_msg_id) == 0 &&
            strcmp(svc->msgs[i].send_id, user_id) == 0) {
            svc->msgs[i].status = 2;
            return 0;
        }
    }
    return -2;
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
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
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
            for (int i = 0; i < svc->count; i++) {
                if (strcmp(svc->msgs[i].server_msg_id, smid) == 0) {
                    miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[i]));
                    break;
                }
            }
        }
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getNewestSeq") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "seq", (int)svc->seq);
    } else if (strcmp(method, "pullMsgBySeq") == 0) {
        int64_t begin = req ? miku_json_int(miku_json_get(req, "beginSeq")) : 0;
        int64_t end_seq = req ? miku_json_int(miku_json_get(req, "endSeq")) : 0;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < svc->count; i++) {
            if (svc->msgs[i].seq >= begin && (end_seq == 0 || svc->msgs[i].seq <= end_seq))
                miku_json_array_push(arr, miku_msg_to_json(&svc->msgs[i]));
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
        int found = 0;
        if (smid) {
            for (int i = 0; i < svc->count; i++) {
                if (strcmp(svc->msgs[i].server_msg_id, smid) == 0) { found = 1; break; }
            }
        }
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "status", found ? 1 : 0);
    } else if (strcmp(method, "clearConversationMsg") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "userClearAllMsg") == 0) {
        svc->count = 0;
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "deleteMsgPhysical") == 0) {
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        int deleted = 0;
        if (cmid) {
            for (int i = 0; i < svc->count; i++) {
                if (strcmp(svc->msgs[i].client_msg_id, cmid) == 0) {
                    memmove(&svc->msgs[i], &svc->msgs[i+1], (size_t)(svc->count-i-1) * sizeof(miku_msg_t));
                    svc->count--;
                    deleted = 1;
                    break;
                }
            }
        }
        miku_ji(resp, "errCode", deleted ? 0 : 5001);
    } else if (strcmp(method, "deleteMsgPhysicalBySeq") == 0) {
        int64_t del_seq = req ? miku_json_int(miku_json_get(req, "seq")) : 0;
        int deleted = 0;
        if (del_seq > 0) {
            for (int i = 0; i < svc->count; i++) {
                if (svc->msgs[i].seq == del_seq) {
                    memmove(&svc->msgs[i], &svc->msgs[i+1], (size_t)(svc->count-i-1) * sizeof(miku_msg_t));
                    svc->count--;
                    deleted = 1;
                    break;
                }
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
