#include "miku_im_message.h"
#include "miku_json_util.h"
#include "miku_uuid.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

void miku_im_msg_init(miku_im_msg_t *msg) {
    if (!msg) return;
    memset(msg, 0, sizeof(*msg));
}

int miku_im_msg_from_json(miku_im_msg_t *msg, const miku_json_val_t *j) {
    if (!msg || !j) return -1;
    miku_im_msg_init(msg);

    const char *v;
    int64_t iv;

    v = miku_json_str(miku_json_get(j, "msgID"));
    if (v) strncpy(msg->msg_id, v, sizeof(msg->msg_id) - 1);

    v = miku_json_str(miku_json_get(j, "clientMsgID"));
    if (v) strncpy(msg->client_msg_id, v, sizeof(msg->client_msg_id) - 1);

    v = miku_json_str(miku_json_get(j, "sendID"));
    if (v) strncpy(msg->send_id, v, sizeof(msg->send_id) - 1);

    v = miku_json_str(miku_json_get(j, "recvID"));
    if (v) strncpy(msg->recv_id, v, sizeof(msg->recv_id) - 1);

    v = miku_json_str(miku_json_get(j, "groupID"));
    if (v) strncpy(msg->group_id, v, sizeof(msg->group_id) - 1);

    v = miku_json_str(miku_json_get(j, "conversationID"));
    if (v) strncpy(msg->conversation_id, v, sizeof(msg->conversation_id) - 1);

    iv = miku_json_int(miku_json_get(j, "contentType"));
    msg->content_type = (int)iv;

    iv = miku_json_int(miku_json_get(j, "conversationType"));
    if (iv == 0) {
        iv = miku_json_int(miku_json_get(j, "sessionType"));
        /* OpenIM sessionType: 1=single, 2=group, 3=superGroup */
        if (iv == 3) iv = MK_IM_CONV_GROUP;
    }
    msg->conversation_type = (int)iv;

    iv = miku_json_int(miku_json_get(j, "seq"));
    msg->seq = iv;

    iv = miku_json_int(miku_json_get(j, "sendTime"));
    msg->send_time = iv;

    iv = miku_json_int(miku_json_get(j, "senderPlatformID"));
    msg->sender_platform = (int)iv;

    v = miku_json_str(miku_json_get(j, "content"));
    if (v) strncpy(msg->content, v, sizeof(msg->content) - 1);

    v = miku_json_str(miku_json_get(j, "senderNickname"));
    if (v) strncpy(msg->sender_nickname, v, sizeof(msg->sender_nickname) - 1);

    v = miku_json_str(miku_json_get(j, "senderFaceURL"));
    if (v) strncpy(msg->sender_face_url, v, sizeof(msg->sender_face_url) - 1);

    return 0;
}

miku_json_val_t *miku_im_msg_to_json(const miku_im_msg_t *msg) {
    if (!msg) return NULL;
    miku_json_val_t *j = miku_json_create_object();
    if (!j) return NULL;

    if (msg->msg_id[0])      miku_jss(j, "msgID", msg->msg_id);
    if (msg->client_msg_id[0]) miku_jss(j, "clientMsgID", msg->client_msg_id);
    if (msg->send_id[0])     miku_jss(j, "sendID", msg->send_id);
    if (msg->recv_id[0])     miku_jss(j, "recvID", msg->recv_id);
    if (msg->group_id[0])    miku_jss(j, "groupID", msg->group_id);
    if (msg->conversation_id[0]) miku_jss(j, "conversationID", msg->conversation_id);
    miku_ji(j, "contentType", msg->content_type);
    miku_ji(j, "conversationType", msg->conversation_type);
    miku_ji(j, "seq", (int)msg->seq);
    miku_ji(j, "sendTime", (int)msg->send_time);
    miku_ji(j, "createTime", (int)msg->create_time);
    miku_ji(j, "senderPlatformID", msg->sender_platform);
    if (msg->content[0])     miku_jss(j, "content", msg->content);
    if (msg->sender_nickname[0]) miku_jss(j, "senderNickname", msg->sender_nickname);
    if (msg->sender_face_url[0]) miku_jss(j, "senderFaceURL", msg->sender_face_url);
    miku_ji(j, "isRead", msg->is_read);
    miku_ji(j, "status", msg->status);
    return j;
}

int miku_im_msg_validate(const miku_im_msg_t *msg) {
    if (!msg) return -1;
    if (msg->send_id[0] == '\0') return -1;
    if (msg->content_type <= 0) return -1;
    if (msg->content[0] == '\0') return -1;
    if (msg->conversation_type == MK_IM_CONV_SINGLE && msg->recv_id[0] == '\0') return -1;
    if (msg->conversation_type == MK_IM_CONV_GROUP && msg->group_id[0] == '\0') return -1;
    return 0;
}

void miku_im_msg_generate_id(miku_im_msg_t *msg) {
    if (!msg) return;
    if (msg->msg_id[0] == '\0') {
        miku_uuid_generate(msg->msg_id);
    }
    if (msg->client_msg_id[0] == '\0') {
        miku_uuid_generate(msg->client_msg_id);
    }
    if (msg->send_time == 0) {
        msg->send_time = miku_timestamp_ms();
    }
    if (msg->create_time == 0) {
        msg->create_time = msg->send_time;
    }
}

int miku_im_ack_from_json(miku_im_ack_t *ack, const miku_json_val_t *j) {
    if (!ack || !j) return -1;
    memset(ack, 0, sizeof(*ack));
    ack->type = (int)miku_json_int(miku_json_get(j, "type"));
    const char *v = miku_json_str(miku_json_get(j, "userID"));
    if (v) strncpy(ack->user_id, v, sizeof(ack->user_id) - 1);
    v = miku_json_str(miku_json_get(j, "conversationID"));
    if (v) strncpy(ack->conversation_id, v, sizeof(ack->conversation_id) - 1);
    ack->seq = miku_json_int(miku_json_get(j, "seq"));
    return 0;
}

miku_json_val_t *miku_im_ack_to_json(const miku_im_ack_t *ack) {
    if (!ack) return NULL;
    miku_json_val_t *j = miku_json_create_object();
    miku_ji(j, "type", ack->type);
    if (ack->user_id[0]) miku_jss(j, "userID", ack->user_id);
    if (ack->conversation_id[0]) miku_jss(j, "conversationID", ack->conversation_id);
    miku_ji(j, "seq", (int)ack->seq);
    return j;
}

int miku_im_ws_hello_from_json(miku_im_ws_hello_t *hello, const miku_json_val_t *j) {
    if (!hello || !j) return -1;
    memset(hello, 0, sizeof(*hello));
    hello->type = (int)miku_json_int(miku_json_get(j, "type"));
    const char *v = miku_json_str(miku_json_get(j, "userID"));
    if (v) strncpy(hello->user_id, v, sizeof(hello->user_id) - 1);
    hello->server_time = miku_json_int(miku_json_get(j, "serverTime"));
    return 0;
}
