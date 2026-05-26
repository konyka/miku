#ifndef MIKU_IM_MESSAGE_H
#define MIKU_IM_MESSAGE_H

#include "miku_common.h"
#include "miku_json.h"

#define MK_IM_MSG_TYPE_TEXT     101
#define MK_IM_MSG_TYPE_IMAGE   102
#define MK_IM_MSG_TYPE_AUDIO   103
#define MK_IM_MSG_TYPE_VIDEO   104
#define MK_IM_MSG_TYPE_FILE    105
#define MK_IM_MSG_TYPE_SYSTEM  200
#define MK_IM_MSG_TYPE_TYPING  301
#define MK_IM_MSG_TYPE_READ    302
#define MK_IM_MSG_TYPE_REVOKE  303

#define MK_IM_CONV_SINGLE  1
#define MK_IM_CONV_GROUP   2
#define MK_IM_CONV_SYSTEM  3

typedef struct {
    char    msg_id[64];
    char    client_msg_id[64];
    char    send_id[64];
    char    recv_id[64];
    char    group_id[64];
    char    conversation_id[128];
    int     content_type;
    int     conversation_type;
    int64_t seq;
    int64_t send_time;
    int64_t create_time;
    int     sender_platform;
    char    content[4096];
    char    sender_nickname[64];
    char    sender_face_url[256];
    int     is_read;
    int     status;
} miku_im_msg_t;

MIKU_API void miku_im_msg_init(miku_im_msg_t *msg);
MIKU_API int  miku_im_msg_from_json(miku_im_msg_t *msg, const miku_json_val_t *j);
MIKU_API miku_json_val_t *miku_im_msg_to_json(const miku_im_msg_t *msg);
MIKU_API int  miku_im_msg_validate(const miku_im_msg_t *msg);
MIKU_API void miku_im_msg_generate_id(miku_im_msg_t *msg);

typedef struct {
    int     type;
    char    user_id[64];
    char    conversation_id[128];
    int64_t seq;
} miku_im_ack_t;

MIKU_API int  miku_im_ack_from_json(miku_im_ack_t *ack, const miku_json_val_t *j);
MIKU_API miku_json_val_t *miku_im_ack_to_json(const miku_im_ack_t *ack);

typedef struct {
    int     type;
    char    user_id[64];
    int64_t server_time;
    int64_t min_seq;
    int64_t max_seq;
} miku_im_ws_hello_t;

MIKU_API int  miku_im_ws_hello_from_json(miku_im_ws_hello_t *hello, const miku_json_val_t *j);

#endif
