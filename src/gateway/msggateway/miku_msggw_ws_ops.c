#include "miku_msggw_ws_ops.h"
#include "miku_im_message.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_string.h"
#include "miku_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MK_WS_FANOUT_MAX 512

void miku_msggw_ws_resolve_conv(char *out, size_t out_sz,
                                const char *conversation_id,
                                const char *group_id,
                                const char *recv_id) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (conversation_id && conversation_id[0]) {
        strncpy(out, conversation_id, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    if (group_id && group_id[0]) {
        snprintf(out, out_sz, "sg_%s", group_id);
        return;
    }
    if (recv_id && recv_id[0]) {
        strncpy(out, recv_id, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    strncpy(out, "default", out_sz - 1);
    out[out_sz - 1] = '\0';
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

static void fanout_send_msg(miku_msggw_ws_ctx_t *gc, const miku_im_msg_t *im) {
    if (!gc || !gc->gw || !im) return;

    int is_group = (im->conversation_type == MK_IM_CONV_GROUP) || im->group_id[0];
    if (is_group && gc->group && im->group_id[0]) {
        miku_group_member_t members[MK_WS_FANOUT_MAX];
        int n = miku_group_get_members(gc->group, im->group_id, members, MK_WS_FANOUT_MAX);
        int pushed = 0;
        for (int i = 0; i < n; i++) {
            if (im->send_id[0] && strcmp(members[i].user_id, im->send_id) == 0)
                continue;
            if (push_im_to_user(gc->gw, members[i].user_id, im) > 0)
                pushed++;
        }
        MK_LOG_DEBUG("ws_op SEND_MSG group=%s members=%d online_pushed=%d",
                     im->group_id, n, pushed);
        return;
    }

    if (im->recv_id[0] && strcmp(im->recv_id, im->send_id) != 0)
        push_im_to_user(gc->gw, im->recv_id, im);
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
            char *tmp = strndup(payload, len);
            miku_json_val_t *j = tmp ? miku_json_parse_str(tmp) : NULL;
            free(tmp);
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
            char *tmp = strndup(payload, len);
            miku_json_val_t *j = tmp ? miku_json_parse_str(tmp) : NULL;
            free(tmp);
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
        char *tmp = payload && len > 0 ? strndup(payload, len) : NULL;
        miku_json_val_t *j = tmp ? miku_json_parse_str(tmp) : NULL;
        free(tmp);
        miku_im_msg_t im;
        miku_im_msg_init(&im);
        int64_t has_read_seq = 0;
        if (j) {
            miku_im_msg_from_json(&im, j);
            has_read_seq = miku_json_int(miku_json_get(j, "hasReadSeq"));
            miku_json_destroy(j);
        }
        if (!im.send_id[0] && uid[0])
            strncpy(im.send_id, uid, sizeof(im.send_id) - 1);

        char conv[128];
        miku_msggw_ws_resolve_conv(conv, sizeof(conv),
                                   im.conversation_id, im.group_id, im.recv_id);
        if (!im.conversation_id[0])
            strncpy(im.conversation_id, conv, sizeof(im.conversation_id) - 1);
        if (!im.conversation_type) {
            if (im.group_id[0]) im.conversation_type = MK_IM_CONV_GROUP;
            else if (im.recv_id[0]) im.conversation_type = MK_IM_CONV_SINGLE;
        }

        /* Read receipt: update hasReadSeq without allocating chat seq / fan-out. */
        if (im.content_type == MK_IM_MSG_TYPE_READ) {
            int64_t rs = has_read_seq > 0 ? has_read_seq : im.seq;
            if (rs > 0 && uid[0])
                miku_msggw_set_user_read(gc->gw, uid, conv, rs);
            char resp[256];
            snprintf(resp, sizeof(resp),
                     "{\"errCode\":0,\"conversationID\":\"%s\",\"hasReadSeq\":%lld}",
                     conv, (long long)rs);
            reply_json(gc->gw, client_idx, opcode, resp);
            MK_LOG_INFO("ws_op[%d]: SEND_MSG READ client=%d conv=%s hasReadSeq=%lld",
                        opcode, client_idx, conv, (long long)rs);
            break;
        }

        miku_im_msg_generate_id(&im);

        int64_t seq = 0;
        miku_msggw_alloc_seq(gc->gw, conv, &seq);
        im.seq = seq;
        if (im.send_time <= 0) im.send_time = miku_timestamp_ms();

        char store_id[64] = {0};
        if (gc->store) {
            miku_msg_store_insert(gc->store, conv, im.send_id, im.content_type,
                                  im.content, im.send_time, im.seq,
                                  store_id, sizeof(store_id));
            if (store_id[0])
                strncpy(im.msg_id, store_id, sizeof(im.msg_id) - 1);
        }

        fanout_send_msg(gc, &im);

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
            char *tmp = strndup(payload, len);
            miku_json_val_t *j = tmp ? miku_json_parse_str(tmp) : NULL;
            free(tmp);
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

        char *tmp = payload && len > 0 ? strndup(payload, len) : NULL;
        miku_json_val_t *j = tmp ? miku_json_parse_str(tmp) : NULL;
        free(tmp);

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
            char *tmp = strndup(payload, len);
            miku_json_val_t *j = tmp ? miku_json_parse_str(tmp) : NULL;
            free(tmp);
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
        char *tmp = payload && len > 0 ? strndup(payload, len) : NULL;
        miku_json_val_t *j = tmp ? miku_json_parse_str(tmp) : NULL;
        free(tmp);
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
