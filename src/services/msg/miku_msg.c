#include "miku_msg.h"
#include "miku_uuid.h"
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

static void js(miku_json_val_t *o, const char *k, const char *v) { if (v) miku_json_object_set(o, k, miku_json_create_str(v)); }
static void ji(miku_json_val_t *o, const char *k, int64_t v) { miku_json_object_set(o, k, miku_json_create_int(v)); }

void miku_msg_handle_rpc(miku_msg_service_t *svc, const char *method,
                          const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    if (strcmp(method, "sendMsg") == 0) {
        miku_msg_t m;
        memset(&m, 0, sizeof(m));
        miku_msg_from_json(req, &m);
        int rc = miku_msg_send(svc, &m);
        ji(resp, "errCode", rc == 0 ? 0 : 500);
        if (rc == 0) {
            js(resp, "serverMsgID", m.server_msg_id);
            ji(resp, "seq", m.seq);
            ji(resp, "sendTime", m.send_time);
        }
    } else if (strcmp(method, "getMsgByConv") == 0) {
        const char *cid = req ? miku_json_str(miku_json_get(req, "conversationID")) : NULL;
        int64_t start = req ? miku_json_int(miku_json_get(req, "startTime")) : 0;
        int64_t end = req ? miku_json_int(miku_json_get(req, "endTime")) : 0;
        int64_t cnt = req ? miku_json_int(miku_json_get(req, "count")) : 20;
        miku_msg_t list[64];
        int n = miku_msg_get_by_conv(svc, cid, start, end, (int)cnt, list, 64);
        ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) miku_json_array_push(arr, miku_msg_to_json(&list[i]));
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "revokeMsg") == 0) {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        const char *cmid = req ? miku_json_str(miku_json_get(req, "clientMsgID")) : NULL;
        int rc = miku_msg_revoke(svc, uid, cmid);
        ji(resp, "errCode", rc == 0 ? 0 : 5001);
    } else {
        ji(resp, "errCode", 404);
    }
}
