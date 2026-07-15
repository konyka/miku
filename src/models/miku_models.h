#ifndef MIKU_MODELS_H
#define MIKU_MODELS_H

#include "miku_common.h"
#include "miku_string.h"
#include "miku_json.h"

#define MK_USER_ID_LEN    64
#define MK_NICKNAME_LEN   64
#define MK_FACE_URL_LEN   256
#define MK_PHONE_LEN      20
#define MK_EMAIL_LEN      64
#define MK_GROUP_ID_LEN   64
#define MK_MSG_ID_LEN     64
#define MK_CONV_ID_LEN    128
#define MK_TOKEN_LEN      512
#define MK_SECRET_LEN     64
#define MK_EX_LEN         1024

typedef struct {
    char user_id[MK_USER_ID_LEN];
    char nickname[MK_NICKNAME_LEN];
    char face_url[MK_FACE_URL_LEN];
    int  gender;
    char phone_number[MK_PHONE_LEN];
    char email[MK_EMAIL_LEN];
    int64_t birth;
    char ex[MK_EX_LEN];
    int64_t create_time;
    int64_t update_time;
    int  app_mgr_level;
    int  global_recv_msg_opt;
} miku_user_t;

typedef struct {
    char owner_user_id[MK_USER_ID_LEN];
    char friend_user_id[MK_USER_ID_LEN];
    char remark[MK_NICKNAME_LEN];
    int  add_source;
    char ex[MK_EX_LEN];
    int64_t create_time;
} miku_friend_t;

typedef struct {
    char group_id[MK_GROUP_ID_LEN];
    char group_name[MK_NICKNAME_LEN];
    char face_url[MK_FACE_URL_LEN];
    char owner_user_id[MK_USER_ID_LEN];
    int  group_type;
    int  member_count;
    int  status;
    char ex[MK_EX_LEN];
    int64_t create_time;
} miku_group_t;

typedef struct {
    char group_id[MK_GROUP_ID_LEN];
    char user_id[MK_USER_ID_LEN];
    int  role_level;
    int  join_source;
    char operator_id[MK_USER_ID_LEN];
    int64_t join_time;
    char ex[MK_EX_LEN];
} miku_group_member_t;

typedef enum {
    MK_MSG_TYPE_TEXT      = 101,
    MK_MSG_TYPE_PICTURE   = 102,
    MK_MSG_TYPE_VOICE     = 103,
    MK_MSG_TYPE_VIDEO     = 104,
    MK_MSG_TYPE_FILE      = 105,
    MK_MSG_TYPE_AT        = 106,
    MK_MSG_TYPE_LOCATION  = 107,
    MK_MSG_TYPE_CUSTOM    = 108,
    MK_MSG_TYPE_REVOKE    = 109,
    MK_MSG_TYPE_FRIEND    = 114,
    MK_MSG_TYPE_SYSTEM    = 200
} miku_msg_type_t;

typedef struct {
    char server_msg_id[MK_MSG_ID_LEN];
    char client_msg_id[MK_MSG_ID_LEN];
    char send_id[MK_USER_ID_LEN];
    char recv_id[MK_USER_ID_LEN];
    char group_id[MK_GROUP_ID_LEN];
    char conversation_id[MK_CONV_ID_LEN];
    int  session_type;
    miku_msg_type_t msg_type;
    char content[MK_EX_LEN];
    int64_t seq;
    int64_t send_time;
    int64_t create_time;
    int  status;
    int  is_read;
    char ex[MK_EX_LEN];
} miku_msg_t;

typedef struct {
    char conversation_id[MK_CONV_ID_LEN];
    char owner_user_id[MK_USER_ID_LEN];
    int  conversation_type;
    char user_id[MK_USER_ID_LEN];
    char group_id[MK_GROUP_ID_LEN];
    int64_t recv_msg_opt;
    int  unread_count;
    int64_t latest_msg_send_time;
    char latest_msg_content[MK_EX_LEN];
    int64_t draft_text_time;
    char draft_text[MK_EX_LEN];
    char ex[MK_EX_LEN];
    int  is_pinned;
    int  is_private_chat;
    int  burn_duration;
    int  is_not_in_group;
    int64_t update_time;
} miku_conversation_t;

typedef struct {
    char token[MK_TOKEN_LEN];
    char user_id[MK_USER_ID_LEN];
    char platform;
    int64_t expire_at;
} miku_token_info_t;

MIKU_API miku_json_val_t *miku_user_to_json(const miku_user_t *u);
MIKU_API int miku_user_from_json(const miku_json_val_t *j, miku_user_t *u);

MIKU_API miku_json_val_t *miku_msg_to_json(const miku_msg_t *m);
MIKU_API int miku_msg_from_json(const miku_json_val_t *j, miku_msg_t *m);

/* Resolve conversationID: explicit > sg_<groupID> > si_<len>_<a>_<b> > fallback. */
MIKU_API void miku_conversation_id_resolve(char *out, size_t out_sz,
                                           const char *conversation_id,
                                           const char *group_id,
                                           const char *send_id,
                                           const char *recv_id);

/* Fill peer from si_<len>_<a>_<b> when self is a or b. Returns 0 on success. */
MIKU_API int miku_conversation_si_peer(const char *conv, const char *self,
                                       char *peer, size_t peer_sz);

MIKU_API miku_json_val_t *miku_conversation_to_json(const miku_conversation_t *c);
MIKU_API int miku_conversation_from_json(const miku_json_val_t *j, miku_conversation_t *c);

MIKU_API miku_json_val_t *miku_group_to_json(const miku_group_t *g);
MIKU_API miku_json_val_t *miku_group_member_to_json(const miku_group_member_t *gm);

#endif
