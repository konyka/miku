#ifndef MIKU_MSGGATEWAY_H
#define MIKU_MSGGATEWAY_H

#include "miku_common.h"
#include "miku_websocket.h"

#define MK_GW_MAX_CLIENTS 4096

#define MK_WS_OP_GET_NEWEST_SEQ       1001
#define MK_WS_OP_PULL_MSG_BY_SEQ      1002
#define MK_WS_OP_SEND_MSG             1003
#define MK_WS_OP_SEND_SIGNAL_MSG      1004
#define MK_WS_OP_PULL_MSG             1005
#define MK_WS_OP_GET_CONV_MAX_READ_SEQ 1006
#define MK_WS_OP_PULL_CONV_LAST_MSG   1007
#define MK_WS_OP_PUSH_MSG             2001
#define MK_WS_OP_KICK_ONLINE          2002
#define MK_WS_OP_LOGOUT               2003
#define MK_WS_OP_SET_BACKGROUND       2004
#define MK_WS_OP_SUB_USER_STATUS      2005
#define MK_WS_OP_DATA_ERROR           3001

typedef void (*miku_msggw_on_msg_fn)(const char *user_id, const char *msg, size_t len, void *ctx);
typedef void (*miku_msggw_on_op_fn)(int client_idx, int opcode, const char *payload, size_t len, void *ctx);
/* online=1 after handshake; online=0 before session teardown */
typedef void (*miku_msggw_on_presence_fn)(const char *user_id, int platform, int online, void *ctx);

typedef struct miku_msggw_client_s {
    int            fd;
    char           user_id[64];
    char           conn_id[64];
    int            platform;
    int64_t        connect_time;
    bool           online;
    bool           upgraded;
    bool           is_background;
    char           sdk_type[16];
    char           sdk_version[16];
} miku_msggw_client_t;

typedef struct miku_msggw_s miku_msggw_t;

MIKU_API miku_msggw_t *miku_msggw_create(int port);
MIKU_API void miku_msggw_destroy(miku_msggw_t *gw);
MIKU_API int  miku_msggw_start(miku_msggw_t *gw);
MIKU_API int  miku_msggw_stop(miku_msggw_t *gw);

MIKU_API int  miku_msggw_client_count(miku_msggw_t *gw);
MIKU_API int  miku_msggw_broadcast(miku_msggw_t *gw, const char *msg, size_t len);
MIKU_API int  miku_msggw_send_to_user(miku_msggw_t *gw, const char *user_id,
                                       const char *msg, size_t len);
MIKU_API int  miku_msggw_send_op_to_user(miku_msggw_t *gw, const char *user_id,
                                           int opcode, const char *payload, size_t len);
MIKU_API int  miku_msggw_kick_user(miku_msggw_t *gw, const char *user_id);

MIKU_API void miku_msggw_on_message(miku_msggw_t *gw, miku_msggw_on_msg_fn fn, void *ctx);
MIKU_API void miku_msggw_on_opcode(miku_msggw_t *gw, miku_msggw_on_op_fn fn, void *ctx);
MIKU_API void miku_msggw_on_presence(miku_msggw_t *gw, miku_msggw_on_presence_fn fn, void *ctx);
MIKU_API int  miku_msggw_poll(miku_msggw_t *gw, int timeout_ms);
MIKU_API int  miku_msggw_send_op(miku_msggw_t *gw, int client_idx, int opcode,
                                   const char *payload, size_t len);
MIKU_API int  miku_msggw_broadcast_op(miku_msggw_t *gw, int opcode,
                                        const char *payload, size_t len);
MIKU_API int  miku_msggw_peek_max_seq(miku_msggw_t *gw, const char *conversation_id,
                                        int64_t *seq);
MIKU_API int  miku_msggw_alloc_seq(miku_msggw_t *gw, const char *conversation_id,
                                     int64_t *seq);
/* Deprecated alias of alloc_seq — prefer peek_max_seq for reads. */
MIKU_API int  miku_msggw_get_seq(miku_msggw_t *gw, const char *conversation_id, int64_t *seq);
MIKU_API int  miku_msggw_set_user_read(miku_msggw_t *gw, const char *user_id,
                                         const char *conversation_id, int64_t read_seq);
MIKU_API int64_t miku_msggw_get_user_read(miku_msggw_t *gw, const char *user_id,
                                            const char *conversation_id);
MIKU_API int  miku_msggw_set_background(miku_msggw_t *gw, int client_idx, bool background);
MIKU_API int  miku_msggw_disconnect_client(miku_msggw_t *gw, int client_idx);
MIKU_API int  miku_msggw_get_client_user_id(miku_msggw_t *gw, int client_idx,
                                             char *out, size_t out_cap);

/* Unwrap {"reqIdentifier":N,"data":...} → opcode + data JSON (caller frees *out_data). */
MIKU_API int  miku_msggw_unwrap_op_data(const char *envelope_json, int *out_opcode,
                                          char **out_data, size_t *out_len);

#endif
