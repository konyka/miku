#include "miku_models.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *json_str(const miku_json_val_t *j, const char *key, char *out, size_t cap) {
    out[0] = '\0';
    miku_json_val_t *v = j ? miku_json_get(j, key) : NULL;
    if (v && miku_json_type(v) == MK_JSON_STRING) {
        const char *s = miku_json_str(v);
        if (s) { strncpy(out, s, cap - 1); out[cap - 1] = '\0'; }
    }
    return out;
}

static int64_t json_int(const miku_json_val_t *j, const char *key) {
    miku_json_val_t *v = j ? miku_json_get(j, key) : NULL;
    return v ? miku_json_int(v) : 0;
}

static void set_str(miku_json_val_t *obj, const char *key, const char *val) {
    if (val && val[0]) miku_json_object_set(obj, key, miku_json_create_str(val));
}

static void set_int(miku_json_val_t *obj, const char *key, int64_t val) {
    miku_json_object_set(obj, key, miku_json_create_int(val));
}

miku_json_val_t *miku_user_to_json(const miku_user_t *u) {
    if (!u) return miku_json_create_object();
    miku_json_val_t *j = miku_json_create_object();
    set_str(j, "userID", u->user_id);
    set_str(j, "nickname", u->nickname);
    set_str(j, "faceURL", u->face_url);
    set_int(j, "gender", u->gender);
    set_str(j, "phoneNumber", u->phone_number);
    set_str(j, "email", u->email);
    set_int(j, "birth", u->birth);
    set_str(j, "ex", u->ex);
    set_int(j, "createTime", u->create_time);
    set_int(j, "updateTime", u->update_time);
    set_int(j, "appMgmtLevel", u->app_mgr_level);
    set_int(j, "globalRecvMsgOpt", u->global_recv_msg_opt);
    return j;
}

int miku_user_from_json(const miku_json_val_t *j, miku_user_t *u) {
    if (!j || !u) return -1;
    memset(u, 0, sizeof(*u));
    json_str(j, "userID", u->user_id, sizeof(u->user_id));
    json_str(j, "nickname", u->nickname, sizeof(u->nickname));
    json_str(j, "faceURL", u->face_url, sizeof(u->face_url));
    json_str(j, "phoneNumber", u->phone_number, sizeof(u->phone_number));
    json_str(j, "email", u->email, sizeof(u->email));
    json_str(j, "ex", u->ex, sizeof(u->ex));
    u->gender = (int)json_int(j, "gender");
    u->birth = json_int(j, "birth");
    u->create_time = json_int(j, "createTime");
    u->update_time = json_int(j, "updateTime");
    u->app_mgr_level = (int)json_int(j, "appMgmtLevel");
    u->global_recv_msg_opt = (int)json_int(j, "globalRecvMsgOpt");
    return 0;
}

miku_json_val_t *miku_msg_to_json(const miku_msg_t *m) {
    if (!m) return miku_json_create_object();
    miku_json_val_t *j = miku_json_create_object();
    set_str(j, "serverMsgID", m->server_msg_id);
    set_str(j, "clientMsgID", m->client_msg_id);
    set_str(j, "sendID", m->send_id);
    set_str(j, "recvID", m->recv_id);
    set_str(j, "groupID", m->group_id);
    set_str(j, "conversationID", m->conversation_id);
    set_int(j, "sessionType", m->session_type);
    set_int(j, "contentType", (int64_t)m->msg_type);
    set_str(j, "content", m->content);
    set_int(j, "seq", m->seq);
    set_int(j, "sendTime", m->send_time);
    set_int(j, "createTime", m->create_time);
    set_int(j, "status", m->status);
    set_int(j, "isRead", m->is_read);
    set_str(j, "ex", m->ex);
    return j;
}

int miku_msg_from_json(const miku_json_val_t *j, miku_msg_t *m) {
    if (!j || !m) return -1;
    memset(m, 0, sizeof(*m));
    json_str(j, "serverMsgID", m->server_msg_id, sizeof(m->server_msg_id));
    json_str(j, "clientMsgID", m->client_msg_id, sizeof(m->client_msg_id));
    json_str(j, "sendID", m->send_id, sizeof(m->send_id));
    json_str(j, "recvID", m->recv_id, sizeof(m->recv_id));
    json_str(j, "groupID", m->group_id, sizeof(m->group_id));
    json_str(j, "conversationID", m->conversation_id, sizeof(m->conversation_id));
    json_str(j, "content", m->content, sizeof(m->content));
    json_str(j, "ex", m->ex, sizeof(m->ex));
    m->session_type = (int)json_int(j, "sessionType");
    m->msg_type = (miku_msg_type_t)json_int(j, "contentType");
    if (m->msg_type == 0)
        m->msg_type = (miku_msg_type_t)json_int(j, "msgType");
    m->seq = json_int(j, "seq");
    m->send_time = json_int(j, "sendTime");
    m->create_time = json_int(j, "createTime");
    m->status = (int)json_int(j, "status");
    m->is_read = (int)json_int(j, "isRead");
    return 0;
}

void miku_conversation_id_resolve(char *out, size_t out_sz,
                                  const char *conversation_id,
                                  const char *group_id,
                                  const char *send_id,
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
    if (send_id && send_id[0] && recv_id && recv_id[0]) {
        const char *a = send_id, *b = recv_id;
        if (strcmp(a, b) > 0) { a = recv_id; b = send_id; }
        /* Length-prefix first uid so underscores in userIDs cannot collide. */
        snprintf(out, out_sz, "si_%zu_%s_%s", strlen(a), a, b);
        return;
    }
    if (recv_id && recv_id[0]) {
        strncpy(out, recv_id, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    if (send_id && send_id[0]) {
        strncpy(out, send_id, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    strncpy(out, "default", out_sz - 1);
    out[out_sz - 1] = '\0';
}

int miku_conversation_si_peer(const char *conv, const char *self,
                              char *peer, size_t peer_sz) {
    if (!conv || !self || !self[0] || !peer || peer_sz == 0 ||
        strncmp(conv, "si_", 3) != 0)
        return -1;
    peer[0] = '\0';
    const char *p = conv + 3;
    /* New: si_<len>_<a>_<b> */
    if (p[0] >= '1' && p[0] <= '9') {
        char *end = NULL;
        unsigned long alen = strtoul(p, &end, 10);
        if (!end || *end != '_' || alen == 0 || alen >= MK_USER_ID_LEN) return -1;
        const char *a = end + 1;
        if (strlen(a) <= alen || a[alen] != '_') return -1;
        const char *b = a + alen + 1;
        if (!b[0]) return -1;
        if (strlen(self) == alen && strncmp(a, self, alen) == 0) {
            strncpy(peer, b, peer_sz - 1);
            peer[peer_sz - 1] = '\0';
            return 0;
        }
        if (strcmp(b, self) == 0) {
            if (alen >= peer_sz) return -1;
            memcpy(peer, a, alen);
            peer[alen] = '\0';
            return 0;
        }
        return -1;
    }
    /* Legacy si_<a>_<b> (no leading length) — ambiguous if uids contain '_'. */
    size_t slen = strlen(self);
    size_t rlen = strlen(p);
    if (rlen > slen + 1 && strncmp(p, self, slen) == 0 && p[slen] == '_') {
        strncpy(peer, p + slen + 1, peer_sz - 1);
        peer[peer_sz - 1] = '\0';
        return peer[0] ? 0 : -1;
    }
    if (rlen > slen + 1 && p[rlen - slen - 1] == '_' &&
        strcmp(p + (rlen - slen), self) == 0) {
        size_t plen = rlen - slen - 1;
        if (plen >= peer_sz) return -1;
        memcpy(peer, p, plen);
        peer[plen] = '\0';
        return 0;
    }
    return -1;
}

miku_json_val_t *miku_conversation_to_json(const miku_conversation_t *c) {
    if (!c) return miku_json_create_object();
    miku_json_val_t *j = miku_json_create_object();
    set_str(j, "conversationID", c->conversation_id);
    set_str(j, "ownerUserID", c->owner_user_id);
    set_int(j, "conversationType", c->conversation_type);
    set_str(j, "userID", c->user_id);
    set_str(j, "groupID", c->group_id);
    set_int(j, "recvMsgOpt", c->recv_msg_opt);
    set_int(j, "unreadCount", c->unread_count);
    set_int(j, "latestMsgSendTime", c->latest_msg_send_time);
    set_str(j, "latestMsgContent", c->latest_msg_content);
    set_int(j, "draftTextTime", c->draft_text_time);
    set_str(j, "draftText", c->draft_text);
    set_str(j, "ex", c->ex);
    set_int(j, "isPinned", c->is_pinned);
    set_int(j, "isPrivateChat", c->is_private_chat);
    set_int(j, "burnDuration", c->burn_duration);
    set_int(j, "isNotInGroup", c->is_not_in_group);
    set_int(j, "updateTime", c->update_time);
    return j;
}

int miku_conversation_from_json(const miku_json_val_t *j, miku_conversation_t *c) {
    if (!j || !c) return -1;
    memset(c, 0, sizeof(*c));
    json_str(j, "conversationID", c->conversation_id, sizeof(c->conversation_id));
    json_str(j, "ownerUserID", c->owner_user_id, sizeof(c->owner_user_id));
    json_str(j, "userID", c->user_id, sizeof(c->user_id));
    json_str(j, "groupID", c->group_id, sizeof(c->group_id));
    json_str(j, "latestMsgContent", c->latest_msg_content, sizeof(c->latest_msg_content));
    json_str(j, "draftText", c->draft_text, sizeof(c->draft_text));
    json_str(j, "ex", c->ex, sizeof(c->ex));
    c->conversation_type = (int)json_int(j, "conversationType");
    c->recv_msg_opt = json_int(j, "recvMsgOpt");
    c->unread_count = (int)json_int(j, "unreadCount");
    c->latest_msg_send_time = json_int(j, "latestMsgSendTime");
    c->draft_text_time = json_int(j, "draftTextTime");
    c->is_pinned = (int)json_int(j, "isPinned");
    c->is_private_chat = (int)json_int(j, "isPrivateChat");
    c->burn_duration = (int)json_int(j, "burnDuration");
    c->is_not_in_group = (int)json_int(j, "isNotInGroup");
    c->update_time = json_int(j, "updateTime");
    return 0;
}

miku_json_val_t *miku_group_to_json(const miku_group_t *g) {
    if (!g) return miku_json_create_object();
    miku_json_val_t *j = miku_json_create_object();
    set_str(j, "groupID", g->group_id);
    set_str(j, "groupName", g->group_name);
    set_str(j, "faceURL", g->face_url);
    set_str(j, "ownerUserID", g->owner_user_id);
    set_int(j, "groupType", g->group_type);
    set_int(j, "memberCount", g->member_count);
    set_int(j, "status", g->status);
    set_str(j, "ex", g->ex);
    set_int(j, "createTime", g->create_time);
    return j;
}

miku_json_val_t *miku_group_member_to_json(const miku_group_member_t *gm) {
    if (!gm) return miku_json_create_object();
    miku_json_val_t *j = miku_json_create_object();
    set_str(j, "groupID", gm->group_id);
    set_str(j, "userID", gm->user_id);
    set_int(j, "roleLevel", gm->role_level);
    set_int(j, "joinSource", gm->join_source);
    set_str(j, "operatorID", gm->operator_id);
    set_int(j, "joinTime", gm->join_time);
    set_str(j, "ex", gm->ex);
    return j;
}
