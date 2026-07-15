#include "miku_msggw_ws_ops.h"
#include "miku_im_message.h"
#include "miku_models.h"
#include "miku_conversation.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_string.h"
#include "miku_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    miku_msggw_t       *gw;
    const miku_im_msg_t *im;
    const char         *payload;
    size_t              payload_len;
    int                 members;
    int                 pushed;
} fanout_ctx_t;

typedef struct {
    miku_conv_service_t *svc;
    const char          *cid;
    const char          *gid;
    const char          *send_id;
    int64_t              send_time;
    const char          *content;
} ws_group_conv_ctx_t;

static void ws_upsert_group_member_conv(const char *user_id, int role, void *v) {
    (void)role;
    ws_group_conv_ctx_t *g = (ws_group_conv_ctx_t *)v;
    if (!g || !user_id) return;
    int bump = (g->send_id && strcmp(user_id, g->send_id) != 0) ? 1 : 0;
    miku_conv_touch_on_send(g->svc, user_id, g->cid, MK_IM_CONV_GROUP,
                            NULL, g->gid, g->send_time, g->content, bump);
}

static void ws_touch_convs_on_send(miku_msggw_ws_ctx_t *gc, const miku_im_msg_t *im,
                                   const char *conv) {
    if (!gc || !gc->conv || !im || !conv || !conv[0] || !im->send_id[0]) return;
    int is_group = (im->conversation_type == MK_IM_CONV_GROUP) || im->group_id[0];
    if (is_group && im->group_id[0]) {
        ws_group_conv_ctx_t gctx = {
            .svc = gc->conv, .cid = conv, .gid = im->group_id,
            .send_id = im->send_id, .send_time = im->send_time,
            .content = im->content,
        };
        if (gc->group)
            miku_group_foreach_member(gc->group, im->group_id,
                                      ws_upsert_group_member_conv, &gctx);
        miku_conv_touch_on_send(gc->conv, im->send_id, conv, MK_IM_CONV_GROUP,
                                NULL, im->group_id, im->send_time, im->content, 0);
        return;
    }
    if (im->recv_id[0]) {
        miku_conv_touch_on_send(gc->conv, im->send_id, conv, MK_IM_CONV_SINGLE,
                                im->recv_id, NULL, im->send_time, im->content, 0);
        if (strcmp(im->recv_id, im->send_id) != 0)
            miku_conv_touch_on_send(gc->conv, im->recv_id, conv, MK_IM_CONV_SINGLE,
                                    im->send_id, NULL, im->send_time, im->content, 1);
    }
}

void miku_msggw_ws_resolve_conv(char *out, size_t out_sz,
                                const char *conversation_id,
                                const char *group_id,
                                const char *send_id,
                                const char *recv_id) {
    miku_conversation_id_resolve(out, out_sz, conversation_id, group_id, send_id, recv_id);
}

static int reply_json(miku_msggw_t *gw, int client_idx, int opcode, const char *json) {
    if (!gw || !json) return -1;
    return miku_msggw_send_op(gw, client_idx, opcode, json, strlen(json));
}

static int push_im_to_user(miku_msggw_t *gw, const char *user_id, const miku_im_msg_t *im) {
    if (!gw || !user_id || !user_id[0] || !im) return 0;
    miku_json_val_t *pj = miku_im_msg_to_json(im);
    if (!pj) return 0;
    int n = 0;
    miku_string_t *ps = miku_json_stringify(pj);
    if (ps && ps->data) {
        n = miku_msggw_send_op_to_user(gw, user_id, MK_WS_OP_PUSH_MSG, ps->data, ps->len);
        MK_LOG_DEBUG("ws_op SEND_MSG push user=%s sessions=%d", user_id, n);
    }
    miku_str_destroy(ps);
    miku_json_destroy(pj);
    return n;
}

static void fill_read_seq_entry(miku_msggw_t *gw, const char *uid, const char *cid,
                                miku_json_val_t *map) {
    if (!gw || !cid || !cid[0] || !map) return;
    int64_t max_seq = 0;
    miku_msggw_peek_max_seq(gw, cid, &max_seq);
    int64_t has_read = (uid && uid[0]) ? miku_msggw_get_user_read(gw, uid, cid) : 0;
    miku_json_val_t *entry = miku_json_create_object();
    miku_ji(entry, "maxSeq", max_seq);
    miku_ji(entry, "hasReadSeq", has_read);
    miku_json_object_set(map, cid, entry);
}

static int fill_peer_from_si_conv(const char *conv, const char *self,
                                  char *peer, size_t peer_sz) {
    if (!conv || !self || !self[0] || !peer || peer_sz == 0 ||
        strncmp(conv, "si_", 3) != 0)
        return -1;
    const char *rest = conv + 3;
    size_t slen = strlen(self);
    size_t rlen = strlen(rest);
    peer[0] = '\0';
    /* si_<self>_<peer> */
    if (rlen > slen + 1 && strncmp(rest, self, slen) == 0 && rest[slen] == '_') {
        strncpy(peer, rest + slen + 1, peer_sz - 1);
        peer[peer_sz - 1] = '\0';
        return peer[0] ? 0 : -1;
    }
    /* si_<peer>_<self> */
    if (rlen > slen + 1 && rest[rlen - slen - 1] == '_' &&
        strcmp(rest + (rlen - slen), self) == 0) {
        size_t plen = rlen - slen - 1;
        if (plen >= peer_sz) return -1;
        memcpy(peer, rest, plen);
        peer[plen] = '\0';
        return 0;
    }
    return -1;
}

static void fanout_one_member(const char *user_id, int role, void *v) {
    (void)role;
    fanout_ctx_t *f = (fanout_ctx_t *)v;
    if (!f || !user_id) return;
    f->members++;
    if (f->im->send_id[0] && strcmp(user_id, f->im->send_id) == 0) return;
    if (!f->payload) return;
    int n = miku_msggw_send_op_to_user(f->gw, user_id, MK_WS_OP_PUSH_MSG,
                                       f->payload, f->payload_len);
    if (n > 0) f->pushed++;
}

static void fanout_send_msg(miku_msggw_ws_ctx_t *gc, const miku_im_msg_t *im) {
    if (!gc || !gc->gw || !im) return;

    int is_group = (im->conversation_type == MK_IM_CONV_GROUP) || im->group_id[0];
    if (is_group && gc->group && im->group_id[0]) {
        miku_json_val_t *pj = miku_im_msg_to_json(im);
        miku_string_t *ps = pj ? miku_json_stringify(pj) : NULL;
        fanout_ctx_t f = {
            .gw = gc->gw,
            .im = im,
            .payload = (ps && ps->data) ? ps->data : NULL,
            .payload_len = (ps && ps->data) ? ps->len : 0,
        };
        miku_group_foreach_member(gc->group, im->group_id, fanout_one_member, &f);
        MK_LOG_DEBUG("ws_op SEND_MSG group=%s members=%d online_pushed=%d",
                     im->group_id, f.members, f.pushed);
        miku_str_destroy(ps);
        miku_json_destroy(pj);
        return;
    }

    if (im->recv_id[0] && strcmp(im->recv_id, im->send_id) != 0)
        push_im_to_user(gc->gw, im->recv_id, im);
}

int miku_msggw_ws_deliver_msg(miku_msggw_ws_ctx_t *gc, miku_im_msg_t *im) {
    if (!gc || !gc->gw || !im) return -1;

    char conv[128];
    miku_msggw_ws_resolve_conv(conv, sizeof(conv),
                               im->conversation_id, im->group_id,
                               im->send_id, im->recv_id);
    if (!im->conversation_id[0])
        strncpy(im->conversation_id, conv, sizeof(im->conversation_id) - 1);
    if (!im->conversation_type) {
        if (im->group_id[0]) im->conversation_type = MK_IM_CONV_GROUP;
        else if (im->recv_id[0]) im->conversation_type = MK_IM_CONV_SINGLE;
    }
    if (im->content_type <= 0)
        im->content_type = MK_IM_MSG_TYPE_TEXT;

    if (miku_im_msg_validate(im) != 0) {
        MK_LOG_WARN("deliver_msg: validate failed send=%s recv=%s group=%s type=%d",
                    im->send_id, im->recv_id, im->group_id, im->content_type);
        return -1;
    }

    /* Single chat blacklist (either direction). */
    if (gc->friend_svc && im->recv_id[0] && !im->group_id[0] &&
        (miku_friend_is_black(gc->friend_svc, im->send_id, im->recv_id) ||
         miku_friend_is_black(gc->friend_svc, im->recv_id, im->send_id))) {
        MK_LOG_WARN("deliver_msg: blocked by blacklist send=%s recv=%s",
                    im->send_id, im->recv_id);
        return -1;
    }

    /* Group chat: sender must be a member when group svc is wired. */
    if (gc->group && im->group_id[0] &&
        !miku_group_is_member(gc->group, im->group_id, im->send_id)) {
        MK_LOG_WARN("deliver_msg: non-member send=%s group=%s",
                    im->send_id, im->group_id);
        return -1;
    }

    miku_im_msg_generate_id(im);

    int64_t seq = 0;
    if (miku_msggw_alloc_seq(gc->gw, conv, &seq) != 0)
        return -1;
    im->seq = seq;
    if (im->send_time <= 0) im->send_time = miku_timestamp_ms();
    if (im->create_time <= 0) im->create_time = im->send_time;

    char store_id[64] = {0};
    if (gc->store) {
        miku_msg_store_insert(gc->store, conv, im->send_id, im->content_type,
                              im->content, im->send_time, im->seq,
                              store_id, sizeof(store_id));
        if (store_id[0])
            strncpy(im->msg_id, store_id, sizeof(im->msg_id) - 1);
    }

    fanout_send_msg(gc, im);
    ws_touch_convs_on_send(gc, im, conv);
    MK_LOG_DEBUG("deliver_msg: conv=%s send=%s recv=%s group=%s seq=%lld",
                 conv, im->send_id, im->recv_id, im->group_id, (long long)im->seq);
    return 0;
}

void miku_msggw_ws_sub_notify(const char *subscriber, const char *payload,
                              size_t len, void *ctx) {
    miku_msggw_t *gw = (miku_msggw_t *)ctx;
    if (gw && subscriber && payload)
        miku_msggw_send_op_to_user(gw, subscriber, MK_WS_OP_SUB_USER_STATUS, payload, len);
}

void miku_msggw_ws_on_presence(const char *user_id, int platform, int online, void *ctx) {
    miku_msggw_ws_ctx_t *gc = (miku_msggw_ws_ctx_t *)ctx;
    if (!gc || !gc->sub || !user_id) return;
    if (online)
        miku_ws_sub_user_online(gc->sub, user_id, platform);
    else
        miku_ws_sub_user_offline(gc->sub, user_id);
}

void miku_msggw_ws_on_opcode(int client_idx, int opcode,
                             const char *payload, size_t len, void *ctx) {
    miku_msggw_ws_ctx_t *gc = (miku_msggw_ws_ctx_t *)ctx;
    if (!gc || !gc->gw) return;

    char uid[64] = {0};
    miku_msggw_get_client_user_id(gc->gw, client_idx, uid, sizeof(uid));

    switch (opcode) {
    case MK_WS_OP_GET_NEWEST_SEQ: {
        char conv[128] = {0};
        if (payload && len > 0) {
            miku_json_val_t *j = miku_json_parse(payload, len);
            if (j) {
                const char *c = miku_json_str(miku_json_get(j, "conversationID"));
                if (c) strncpy(conv, c, sizeof(conv) - 1);
                miku_json_destroy(j);
            }
        }
        int64_t seq = 0;
        miku_msggw_peek_max_seq(gc->gw, conv[0] ? conv : "default", &seq);
        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"errCode\":0,\"conversationID\":\"%s\",\"maxSeq\":%lld}",
                 conv, (long long)seq);
        reply_json(gc->gw, client_idx, opcode, resp);
        MK_LOG_INFO("ws_op[%d]: GET_NEWEST_SEQ client=%d maxSeq=%lld",
                    opcode, client_idx, (long long)seq);
        break;
    }
    case MK_WS_OP_PULL_MSG_BY_SEQ:
    case MK_WS_OP_PULL_MSG: {
        char conv[128] = {0};
        int64_t begin_seq = 0, end_seq = 0;
        if (payload && len > 0) {
            miku_json_val_t *j = miku_json_parse(payload, len);
            if (j) {
                const char *c = miku_json_str(miku_json_get(j, "conversationID"));
                if (c) strncpy(conv, c, sizeof(conv) - 1);
                begin_seq = miku_json_int(miku_json_get(j, "beginSeq"));
                if (begin_seq == 0)
                    begin_seq = miku_json_int(miku_json_get(j, "startSeq"));
                end_seq = miku_json_int(miku_json_get(j, "endSeq"));
                miku_json_destroy(j);
            }
        }
        char *msgs = NULL;
        if (gc->store && conv[0])
            miku_msg_store_find_by_conv(gc->store, conv, begin_seq, end_seq, &msgs);
        if (!msgs) msgs = strdup("[]");
        size_t need = strlen(msgs) + 64;
        char *resp = (char *)malloc(need);
        if (resp) {
            snprintf(resp, need, "{\"errCode\":0,\"msgs\":%s}", msgs);
            reply_json(gc->gw, client_idx, opcode, resp);
            free(resp);
        } else {
            reply_json(gc->gw, client_idx, opcode, "{\"errCode\":0,\"msgs\":[]}");
        }
        free(msgs);
        MK_LOG_INFO("ws_op[%d]: PULL client=%d conv=%s", opcode, client_idx, conv);
        break;
    }
    case MK_WS_OP_SEND_MSG: {
        miku_json_val_t *j = (payload && len > 0) ? miku_json_parse(payload, len) : NULL;
        miku_im_msg_t im;
        miku_im_msg_init(&im);
        int64_t has_read_seq = 0;
        if (j) {
            miku_im_msg_from_json(&im, j);
            has_read_seq = miku_json_int(miku_json_get(j, "hasReadSeq"));
            miku_json_destroy(j);
        }
        /* Always bind sendID to the authenticated WS session user. */
        if (uid[0]) {
            strncpy(im.send_id, uid, sizeof(im.send_id) - 1);
            im.send_id[sizeof(im.send_id) - 1] = '\0';
        }

        char conv[128];
        miku_msggw_ws_resolve_conv(conv, sizeof(conv),
                                   im.conversation_id, im.group_id,
                                   im.send_id, im.recv_id);
        if (!im.conversation_id[0])
            strncpy(im.conversation_id, conv, sizeof(im.conversation_id) - 1);
        if (!im.conversation_type) {
            if (im.group_id[0]) im.conversation_type = MK_IM_CONV_GROUP;
            else if (im.recv_id[0]) im.conversation_type = MK_IM_CONV_SINGLE;
        }

        /* Read receipt: update hasReadSeq, then PUSH to peer(s) without alloc/store. */
        if (im.content_type == MK_IM_MSG_TYPE_READ) {
            int64_t rs = has_read_seq > 0 ? has_read_seq : im.seq;
            if (rs > 0 && uid[0])
                miku_msggw_set_user_read(gc->gw, uid, conv, rs);
            im.seq = rs;
            if (!im.content[0])
                strncpy(im.content, "read", sizeof(im.content) - 1);
            if (!im.group_id[0] && strncmp(conv, "sg_", 3) == 0) {
                strncpy(im.group_id, conv + 3, sizeof(im.group_id) - 1);
                im.conversation_type = MK_IM_CONV_GROUP;
            }
            if (!im.recv_id[0] && !im.group_id[0]) {
                const char *self = uid[0] ? uid : im.send_id;
                fill_peer_from_si_conv(conv, self, im.recv_id, sizeof(im.recv_id));
            }
            if (!im.conversation_type) {
                if (im.group_id[0]) im.conversation_type = MK_IM_CONV_GROUP;
                else if (im.recv_id[0]) im.conversation_type = MK_IM_CONV_SINGLE;
            }
            fanout_send_msg(gc, &im);
            char resp[256];
            snprintf(resp, sizeof(resp),
                     "{\"errCode\":0,\"conversationID\":\"%s\",\"hasReadSeq\":%lld}",
                     conv, (long long)rs);
            reply_json(gc->gw, client_idx, opcode, resp);
            MK_LOG_INFO("ws_op[%d]: SEND_MSG READ client=%d conv=%s hasReadSeq=%lld recv=%s group=%s",
                        opcode, client_idx, conv, (long long)rs, im.recv_id, im.group_id);
            break;
        }

        if (miku_msggw_ws_deliver_msg(gc, &im) != 0) {
            reply_json(gc->gw, client_idx, opcode,
                       "{\"errCode\":1001,\"errMsg\":\"invalid message\"}");
            break;
        }

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"errCode\":0,\"clientMsgID\":\"%s\",\"serverMsgID\":\"%s\","
                 "\"sendTime\":%lld,\"seq\":%lld}",
                 im.client_msg_id, im.msg_id,
                 (long long)im.send_time, (long long)im.seq);
        reply_json(gc->gw, client_idx, opcode, resp);
        MK_LOG_INFO("ws_op[%d]: SEND_MSG client=%d sendID=%s recvID=%s groupID=%s seq=%lld",
                    opcode, client_idx, im.send_id, im.recv_id, im.group_id,
                    (long long)im.seq);
        break;
    }
    case MK_WS_OP_PULL_CONV_LAST_MSG: {
        char conv[128] = {0};
        if (payload && len > 0) {
            miku_json_val_t *j = miku_json_parse(payload, len);
            if (j) {
                const char *c = miku_json_str(miku_json_get(j, "conversationID"));
                if (c) strncpy(conv, c, sizeof(conv) - 1);
                miku_json_destroy(j);
            }
        }
        int64_t max_seq = 0;
        miku_msggw_peek_max_seq(gc->gw, conv[0] ? conv : "default", &max_seq);
        char *msgs = NULL;
        if (gc->store && conv[0] && max_seq > 0)
            miku_msg_store_find_by_conv(gc->store, conv, max_seq, max_seq, &msgs);
        if (!msgs) msgs = strdup("[]");
        size_t need = strlen(msgs) + 64;
        char *resp = (char *)malloc(need);
        if (resp) {
            snprintf(resp, need, "{\"errCode\":0,\"msgs\":%s}", msgs);
            reply_json(gc->gw, client_idx, opcode, resp);
            free(resp);
        }
        free(msgs);
        break;
    }
    case MK_WS_OP_SEND_SIGNAL_MSG:
        reply_json(gc->gw, client_idx, opcode, "{\"errCode\":0}");
        break;
    case MK_WS_OP_GET_CONV_MAX_READ_SEQ: {
        miku_json_val_t *out = miku_json_create_object();
        miku_json_val_t *map = miku_json_create_object();
        miku_ji(out, "errCode", 0);

        miku_json_val_t *j = (payload && len > 0) ? miku_json_parse(payload, len) : NULL;

        if (j) {
            miku_json_val_t *ids = miku_json_get(j, "conversationIDs");
            if (!ids) ids = miku_json_get(j, "conversationIDList");
            if (ids && miku_json_type(ids) == MK_JSON_ARRAY) {
                size_t n = miku_json_size(ids);
                for (size_t i = 0; i < n; i++) {
                    const char *cid = miku_json_str(miku_json_at(ids, i));
                    fill_read_seq_entry(gc->gw, uid, cid, map);
                }
            } else {
                const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
                fill_read_seq_entry(gc->gw, uid, cid, map);
            }
            /* Optional mark-as-read in same round-trip: hasReadSeq + conversationID */
            const char *mark_cid = miku_json_str(miku_json_get(j, "conversationID"));
            int64_t mark_seq = miku_json_int(miku_json_get(j, "hasReadSeq"));
            if (mark_cid && mark_cid[0] && mark_seq > 0 && uid[0]) {
                miku_msggw_set_user_read(gc->gw, uid, mark_cid, mark_seq);
                fill_read_seq_entry(gc->gw, uid, mark_cid, map);
            }
            miku_json_destroy(j);
        }

        miku_json_object_set(out, "maxReadSeqs", map);
        miku_string_t *ps = miku_json_stringify(out);
        if (ps && ps->data)
            reply_json(gc->gw, client_idx, opcode, ps->data);
        else
            reply_json(gc->gw, client_idx, opcode, "{\"errCode\":0,\"maxReadSeqs\":{}}");
        miku_str_destroy(ps);
        miku_json_destroy(out);
        MK_LOG_INFO("ws_op[%d]: GET_CONV_MAX_READ_SEQ client=%d user=%s",
                    opcode, client_idx, uid[0] ? uid : "(anon)");
        break;
    }
    case MK_WS_OP_PUSH_MSG:
    case MK_WS_OP_KICK_ONLINE:
        break;
    case MK_WS_OP_LOGOUT:
        MK_LOG_INFO("ws_op[%d]: LOGOUT client=%d — disconnecting", opcode, client_idx);
        reply_json(gc->gw, client_idx, opcode, "{\"errCode\":0}");
        miku_msggw_disconnect_client(gc->gw, client_idx);
        break;
    case MK_WS_OP_SET_BACKGROUND: {
        int bg = 0;
        if (payload && len > 0) {
            miku_json_val_t *j = miku_json_parse(payload, len);
            if (j) {
                bg = (int)miku_json_int(miku_json_get(j, "isBackground"));
                miku_json_destroy(j);
            }
        }
        miku_msggw_set_background(gc->gw, client_idx, bg != 0);
        reply_json(gc->gw, client_idx, opcode, "{\"errCode\":0}");
        MK_LOG_INFO("ws_op[%d]: SET_BACKGROUND client=%d bg=%d", opcode, client_idx, bg);
        break;
    }
    case MK_WS_OP_SUB_USER_STATUS: {
        miku_json_val_t *j = (payload && len > 0) ? miku_json_parse(payload, len) : NULL;
        const char *subscriber = uid[0] ? uid : "anonymous";
        if (j && gc->sub) {
            const char *target = miku_json_str(miku_json_get(j, "userID"));
            const char *action = miku_json_str(miku_json_get(j, "action"));
            if (target && action) {
                if (strcmp(action, "subscribe") == 0) {
                    miku_ws_sub_subscribe(gc->sub, subscriber, target);
                    MK_LOG_INFO("ws_op[%d]: SUB_USER_STATUS subscribe %s→%s",
                                opcode, subscriber, target);
                } else if (strcmp(action, "unsubscribe") == 0) {
                    miku_ws_sub_unsubscribe(gc->sub, subscriber, target);
                    MK_LOG_INFO("ws_op[%d]: SUB_USER_STATUS unsubscribe %s→%s",
                                opcode, subscriber, target);
                }
            }
            miku_json_destroy(j);
        }
        reply_json(gc->gw, client_idx, opcode, "{\"errCode\":0}");
        break;
    }
    case MK_WS_OP_DATA_ERROR:
        MK_LOG_WARN("ws_op[%d]: DATA_ERROR client=%d", opcode, client_idx);
        break;
    default:
        reply_json(gc->gw, client_idx, opcode, "{\"errCode\":0}");
        MK_LOG_DEBUG("ws_op: unknown opcode=%d client=%d", opcode, client_idx);
        break;
    }
}
