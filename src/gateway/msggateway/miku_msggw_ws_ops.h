#ifndef MIKU_MSGGW_WS_OPS_H
#define MIKU_MSGGW_WS_OPS_H

#include "miku_common.h"
#include "miku_msggateway.h"
#include "miku_ws_subscription.h"
#include "miku_msg_store.h"
#include "miku_group.h"
#include "miku_im_message.h"

typedef struct {
    miku_msggw_t           *gw;
    miku_ws_sub_t          *sub;
    miku_msg_store_t       *store;
    miku_group_service_t   *group;
} miku_msggw_ws_ctx_t;

/*
 * Resolve conversationID:
 *   explicit > sg_<groupID> > si_<min(send,recv)>_<max(send,recv)> > "default"
 */
MIKU_API void miku_msggw_ws_resolve_conv(char *out, size_t out_sz,
                                         const char *conversation_id,
                                         const char *group_id,
                                         const char *send_id,
                                         const char *recv_id);

/* Persist + fan-out (alloc seq, msg_store insert, PUSH_MSG). Updates im in-place. */
MIKU_API int miku_msggw_ws_deliver_msg(miku_msggw_ws_ctx_t *ctx, miku_im_msg_t *im);

MIKU_API void miku_msggw_ws_sub_notify(const char *subscriber, const char *payload,
                                       size_t len, void *ctx);
MIKU_API void miku_msggw_ws_on_presence(const char *user_id, int platform,
                                        int online, void *ctx);
MIKU_API void miku_msggw_ws_on_opcode(int client_idx, int opcode,
                                      const char *payload, size_t len, void *ctx);

#endif
