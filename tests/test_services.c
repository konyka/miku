#include "miku_test.h"
#include "miku_models.h"
#include "miku_auth.h"
#include "miku_user.h"
#include "miku_friend.h"
#include "miku_group.h"
#include "miku_conversation.h"
#include "miku_msg.h"
#include "miku_third.h"
#include "miku_json.h"
#include "miku_api.h"
#include "miku_http_server.h"
#include "miku_rpc_server.h"
#include "miku_rpc_client.h"
#include "miku_token.h"
#include "miku_msggateway.h"
#include "miku_msggw_ws_ops.h"
#include "miku_im_message.h"
#include "miku_msg_store.h"
#include "miku_msgtransfer.h"
#include "miku_push.h"
#include "miku_crontask.h"
#include "miku_middleware.h"
#include "miku_json_util.h"
#include "miku_token.h"
#include "miku_websocket.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>

static void test_model_user_json_roundtrip(void) {
    miku_user_t u;
    memset(&u, 0, sizeof(u));
    strncpy(u.user_id, "u1", sizeof(u.user_id) - 1);
    strncpy(u.nickname, "Alice", sizeof(u.nickname) - 1);
    u.gender = 1;

    miku_json_val_t *j = miku_user_to_json(&u);
    mk_assert_not_null(j);

    miku_user_t u2;
    memset(&u2, 0, sizeof(u2));
    int rc = miku_user_from_json(j, &u2);
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("u1", u2.user_id);
    mk_assert_str_eq("Alice", u2.nickname);
    mk_assert_int_eq(1, u2.gender);
    miku_json_destroy(j);
}

static void test_model_msg_json_roundtrip(void) {
    miku_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.send_id, "s1", sizeof(m.send_id) - 1);
    strncpy(m.recv_id, "r1", sizeof(m.recv_id) - 1);
    strncpy(m.content, "hello", sizeof(m.content) - 1);
    m.msg_type = MK_MSG_TYPE_TEXT;
    m.seq = 42;

    miku_json_val_t *j = miku_msg_to_json(&m);
    mk_assert_not_null(j);

    miku_msg_t m2;
    memset(&m2, 0, sizeof(m2));
    int rc = miku_msg_from_json(j, &m2);
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("s1", m2.send_id);
    mk_assert_str_eq("hello", m2.content);
    mk_assert_long_eq(42, (long)m2.seq);
    miku_json_destroy(j);
}

static void test_auth_user_token(void) {
    miku_auth_service_t *svc = miku_auth_service_create();
    mk_assert_not_null(svc);

    char token[512] = {0};
    int rc = miku_auth_user_token(svc, "user1", "openIM123", 1, token, sizeof(token));
    mk_assert_int_eq(0, rc);
    mk_assert(strncmp(token, "miku|user1|", 11) == 0);

    char uid[64] = {0};
    rc = miku_auth_parse_token(svc, token, uid, sizeof(uid));
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("user1", uid);

    miku_auth_service_destroy(svc);
}

static void test_auth_bad_secret(void) {
    miku_auth_service_t *svc = miku_auth_service_create();
    char token[512] = {0};
    int rc = miku_auth_user_token(svc, "user1", "wrongsecret", 1, token, sizeof(token));
    mk_assert_int_eq(-1, rc);
    rc = miku_auth_user_token(svc, "user1", "", 1, token, sizeof(token));
    mk_assert_int_eq(-1, rc);
    miku_auth_service_destroy(svc);
}

static void test_user_register_and_find(void) {
    miku_user_service_t *svc = miku_user_service_create();
    mk_assert_not_null(svc);

    miku_json_val_t *reg = miku_json_create_object();
    miku_json_object_set(reg, "userID", miku_json_create_str("u1"));
    miku_json_object_set(reg, "nickname", miku_json_create_str("Bob"));

    miku_json_val_t *resp = miku_json_create_object();
    miku_user_handle_rpc(svc, "registerUser", reg, resp);
    int64_t err = miku_json_int(miku_json_get(resp, "errCode"));
    mk_assert_int_eq(0, (int)err);

    miku_json_val_t *get = miku_json_create_object();
    miku_json_object_set(get, "userID", miku_json_create_str("u1"));
    miku_json_val_t *resp2 = miku_json_create_object();
    miku_user_handle_rpc(svc, "getUserInfo", get, resp2);
    err = miku_json_int(miku_json_get(resp2, "errCode"));
    mk_assert_int_eq(0, (int)err);

    miku_json_destroy(reg);
    miku_json_destroy(resp);
    miku_json_destroy(get);
    miku_json_destroy(resp2);
    miku_user_service_destroy(svc);
}

static void test_friend_add_and_check(void) {
    miku_friend_service_t *svc = miku_friend_service_create();
    mk_assert_not_null(svc);

    int rc = miku_friend_add(svc, "u1", "u2", "my friend");
    mk_assert_int_eq(0, rc);
    mk_assert_int_eq(-1, miku_friend_add(svc, "u1", "u1", "self"));

    bool is = miku_friend_is_friend(svc, "u1", "u2");
    mk_assert(is);

    is = miku_friend_is_friend(svc, "u1", "u3");
    mk_assert(!is);

    miku_friend_t list[16];
    int n = miku_friend_get_list(svc, "u1", list, 16);
    mk_assert_int_eq(1, n);
    mk_assert_str_eq("u2", list[0].friend_user_id);

    /* API path uses userID + friendUserID (not userID1/userID2) */
    miku_json_val_t *req = miku_json_create_object();
    miku_json_object_set(req, "userID", miku_json_create_str("u1"));
    miku_json_object_set(req, "friendUserID", miku_json_create_str("u2"));
    miku_json_val_t *resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "isFriend", req, resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(resp, "errCode")));
    mk_assert_int_eq(1, (int)miku_json_int(miku_json_get(resp, "isFriend")));
    miku_json_destroy(req);
    miku_json_destroy(resp);

    miku_json_val_t *blk = miku_json_create_object();
    miku_json_object_set(blk, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(blk, "friendUserID", miku_json_create_str("u3"));
    miku_json_val_t *blk_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "addBlack", blk, blk_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(blk_resp, "errCode")));
    mk_assert(miku_friend_is_black(svc, "u1", "u3"));
    miku_json_destroy(blk_resp);
    miku_json_val_t *blk_self = miku_json_create_object();
    miku_json_object_set(blk_self, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(blk_self, "friendUserID", miku_json_create_str("u1"));
    blk_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "addBlack", blk_self, blk_resp);
    mk_assert_int_ne(0, (int)miku_json_int(miku_json_get(blk_resp, "errCode")));
    mk_assert(!miku_friend_is_black(svc, "u1", "u1"));
    miku_json_destroy(blk_self);
    miku_json_destroy(blk_resp);
    blk_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "getBlackList", blk, blk_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(blk_resp, "errCode")));
    mk_assert_int_eq(1, (int)miku_json_size(miku_json_get(blk_resp, "data")));
    miku_json_destroy(blk_resp);
    blk_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "removeBlack", blk, blk_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(blk_resp, "errCode")));
    miku_json_destroy(blk_resp);
    blk_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "getBlackList", blk, blk_resp);
    mk_assert_int_eq(0, (int)miku_json_size(miku_json_get(blk_resp, "data")));
    miku_json_destroy(blk);
    miku_json_destroy(blk_resp);

    miku_friend_service_destroy(svc);
}

static void test_group_create_and_members(void) {
    miku_group_service_t *svc = miku_group_service_create();
    mk_assert_not_null(svc);

    miku_group_t g;
    memset(&g, 0, sizeof(g));
    strncpy(g.group_name, "testgroup", sizeof(g.group_name) - 1);
    int rc = miku_group_create(svc, &g, "owner1");
    mk_assert_int_eq(0, rc);
    mk_assert(strlen(g.group_id) > 0);

    miku_group_t *found = miku_group_find(svc, g.group_id);
    mk_assert_not_null(found);
    mk_assert_str_eq("owner1", found->owner_user_id);
    mk_assert_int_eq(1, found->member_count);

    rc = miku_group_add_member(svc, g.group_id, "member1", 20);
    mk_assert_int_eq(0, rc);
    mk_assert_int_eq(2, miku_group_find(svc, g.group_id)->member_count);

    /* Non-member cannot invite. */
    miku_json_val_t *inv_bad = miku_json_create_object();
    miku_json_object_set(inv_bad, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(inv_bad, "fromUserID", miku_json_create_str("stranger"));
    miku_json_object_set(inv_bad, "userID", miku_json_create_str("member2"));
    miku_json_val_t *inv_bad_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "inviteToGroup", inv_bad, inv_bad_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(inv_bad_resp, "errCode")));
    miku_json_destroy(inv_bad);
    miku_json_destroy(inv_bad_resp);

    miku_json_val_t *inv = miku_json_create_object();
    miku_json_object_set(inv, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(inv, "fromUserID", miku_json_create_str("owner1"));
    miku_json_object_set(inv, "userID", miku_json_create_str("member2"));
    miku_json_val_t *inv_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "inviteToGroup", inv, inv_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(inv_resp, "errCode")));
    mk_assert_int_eq(3, miku_group_find(svc, g.group_id)->member_count);
    miku_json_destroy(inv);
    miku_json_destroy(inv_resp);

    /* Owner cannot quit without transferring first. */
    miku_json_val_t *quit_owner = miku_json_create_object();
    miku_json_object_set(quit_owner, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(quit_owner, "userID", miku_json_create_str("owner1"));
    miku_json_val_t *quit_owner_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "quitGroup", quit_owner, quit_owner_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(quit_owner_resp, "errCode")));
    mk_assert_int_eq(1, miku_group_is_member(svc, g.group_id, "owner1"));
    miku_json_destroy(quit_owner);
    miku_json_destroy(quit_owner_resp);

    /* Admin cannot kick the owner. */
    mk_assert_int_eq(0, miku_group_add_member(svc, g.group_id, "admin1", 60));
    miku_json_val_t *kick_owner = miku_json_create_object();
    miku_json_object_set(kick_owner, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(kick_owner, "opUserID", miku_json_create_str("admin1"));
    miku_json_object_set(kick_owner, "userID", miku_json_create_str("owner1"));
    miku_json_val_t *kick_owner_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "kickGroupMember", kick_owner, kick_owner_resp);
    mk_assert_int_eq(3002, (int)miku_json_int(miku_json_get(kick_owner_resp, "errCode")));
    mk_assert_int_eq(1, miku_group_is_member(svc, g.group_id, "owner1"));
    miku_json_destroy(kick_owner);
    miku_json_destroy(kick_owner_resp);

    /* Peer admin cannot kick another admin (must be strictly higher role). */
    mk_assert_int_eq(0, miku_group_add_member(svc, g.group_id, "admin2", 60));
    miku_json_val_t *kick_peer = miku_json_create_object();
    miku_json_object_set(kick_peer, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(kick_peer, "opUserID", miku_json_create_str("admin1"));
    miku_json_object_set(kick_peer, "userID", miku_json_create_str("admin2"));
    miku_json_val_t *kick_peer_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "kickGroupMember", kick_peer, kick_peer_resp);
    mk_assert_int_eq(3002, (int)miku_json_int(miku_json_get(kick_peer_resp, "errCode")));
    mk_assert_int_eq(1, miku_group_is_member(svc, g.group_id, "admin2"));
    miku_json_destroy(kick_peer);
    miku_json_destroy(kick_peer_resp);
    /* Owner can kick admin. */
    miku_json_val_t *kick_admin = miku_json_create_object();
    miku_json_object_set(kick_admin, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(kick_admin, "opUserID", miku_json_create_str("owner1"));
    miku_json_object_set(kick_admin, "userID", miku_json_create_str("admin2"));
    miku_json_val_t *kick_admin_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "kickGroupMember", kick_admin, kick_admin_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(kick_admin_resp, "errCode")));
    mk_assert_int_eq(0, miku_group_is_member(svc, g.group_id, "admin2"));
    miku_json_destroy(kick_admin);
    miku_json_destroy(kick_admin_resp);

    miku_json_val_t *xfer = miku_json_create_object();
    miku_json_object_set(xfer, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(xfer, "userID", miku_json_create_str("owner1"));
    miku_json_object_set(xfer, "newOwnerUserID", miku_json_create_str("member1"));
    miku_json_val_t *xfer_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "transferGroupOwner", xfer, xfer_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(xfer_resp, "errCode")));
    mk_assert_str_eq("member1", miku_group_find(svc, g.group_id)->owner_user_id);
    miku_json_destroy(xfer);
    miku_json_destroy(xfer_resp);

    /* Non-owner cannot transfer. */
    miku_json_val_t *xfer_bad = miku_json_create_object();
    miku_json_object_set(xfer_bad, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(xfer_bad, "userID", miku_json_create_str("owner1"));
    miku_json_object_set(xfer_bad, "newOwnerUserID", miku_json_create_str("owner1"));
    miku_json_val_t *xfer_bad_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "transferGroupOwner", xfer_bad, xfer_bad_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(xfer_bad_resp, "errCode")));
    miku_json_destroy(xfer_bad);
    miku_json_destroy(xfer_bad_resp);

    miku_group_member_t members[16];
    int n = miku_group_get_members(svc, g.group_id, members, 16);
    mk_assert_int_eq(4, n); /* owner1(now member), member1(owner), member2, admin1 */

    miku_json_val_t *req = miku_json_create_object();
    miku_json_object_set(req, "userID", miku_json_create_str("member1"));
    miku_json_val_t *resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "getJoinedGroupList", req, resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(resp, "errCode")));
    miku_json_val_t *data = miku_json_get(resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(1, (int)miku_json_size(data));
    miku_json_destroy(req);
    miku_json_destroy(resp);

    /* Former owner cannot dismiss; current owner can. */
    miku_json_val_t *dis_bad = miku_json_create_object();
    miku_json_object_set(dis_bad, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(dis_bad, "userID", miku_json_create_str("owner1"));
    miku_json_val_t *dis_bad_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "dismissGroup", dis_bad, dis_bad_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(dis_bad_resp, "errCode")));
    miku_json_destroy(dis_bad);
    miku_json_destroy(dis_bad_resp);

    miku_json_val_t *dis = miku_json_create_object();
    miku_json_object_set(dis, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(dis, "userID", miku_json_create_str("member1"));
    miku_json_val_t *dis_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "dismissGroup", dis, dis_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(dis_resp, "errCode")));
    mk_assert_int_eq(2, miku_group_find(svc, g.group_id)->status);
    mk_assert_int_eq(0, miku_group_find(svc, g.group_id)->member_count);
    mk_assert_int_eq(0, miku_group_get_members(svc, g.group_id, members, 16));

    miku_json_val_t *joined = miku_json_create_object();
    miku_json_object_set(joined, "userID", miku_json_create_str("member1"));
    miku_json_val_t *joined_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "getJoinedGroupList", joined, joined_resp);
    mk_assert_int_eq(0, (int)miku_json_size(miku_json_get(joined_resp, "data")));
    miku_json_destroy(dis);
    miku_json_destroy(dis_resp);
    miku_json_destroy(joined);
    miku_json_destroy(joined_resp);

    /* Cannot re-join a dismissed group or a missing groupID. */
    miku_json_val_t *rejoin = miku_json_create_object();
    miku_json_object_set(rejoin, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(rejoin, "userID", miku_json_create_str("outsider"));
    miku_json_val_t *rejoin_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "joinGroup", rejoin, rejoin_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(rejoin_resp, "errCode")));
    miku_json_destroy(rejoin);
    miku_json_destroy(rejoin_resp);
    miku_json_val_t *ghost = miku_json_create_object();
    miku_json_object_set(ghost, "groupID", miku_json_create_str("no-such-group"));
    miku_json_object_set(ghost, "userID", miku_json_create_str("outsider"));
    miku_json_val_t *ghost_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "joinGroup", ghost, ghost_resp);
    mk_assert_int_eq(3001, (int)miku_json_int(miku_json_get(ghost_resp, "errCode")));
    miku_json_destroy(ghost);
    miku_json_destroy(ghost_resp);

    /* inviteToGroup also rejects missing / dismissed groups. */
    miku_json_val_t *inv_ghost = miku_json_create_object();
    miku_json_object_set(inv_ghost, "groupID", miku_json_create_str("no-such-group"));
    miku_json_object_set(inv_ghost, "fromUserID", miku_json_create_str("outsider"));
    miku_json_object_set(inv_ghost, "userID", miku_json_create_str("victim"));
    miku_json_val_t *inv_ghost_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "inviteToGroup", inv_ghost, inv_ghost_resp);
    mk_assert_int_eq(3001, (int)miku_json_int(miku_json_get(inv_ghost_resp, "errCode")));
    miku_json_destroy(inv_ghost);
    miku_json_destroy(inv_ghost_resp);
    miku_json_val_t *inv_dis = miku_json_create_object();
    miku_json_object_set(inv_dis, "groupID", miku_json_create_str(g.group_id));
    miku_json_object_set(inv_dis, "fromUserID", miku_json_create_str("member1"));
    miku_json_object_set(inv_dis, "userID", miku_json_create_str("victim"));
    miku_json_val_t *inv_dis_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "inviteToGroup", inv_dis, inv_dis_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(inv_dis_resp, "errCode")));
    miku_json_destroy(inv_dis);
    miku_json_destroy(inv_dis_resp);

    miku_group_service_destroy(svc);
}

static void test_conv_create_and_get(void) {
    miku_conv_service_t *svc = miku_conv_service_create();
    mk_assert_not_null(svc);

    miku_conversation_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.owner_user_id, "u1", sizeof(c.owner_user_id) - 1);
    strncpy(c.conversation_id, "conv_1", sizeof(c.conversation_id) - 1);
    c.conversation_type = 1;
    int rc = miku_conv_create(svc, &c);
    mk_assert_int_eq(0, rc);

    miku_conversation_t out;
    rc = miku_conv_get(svc, "u1", "conv_1", &out);
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("conv_1", out.conversation_id);

    miku_conversation_t all[16];
    int n = miku_conv_get_all(svc, "u1", all, 16);
    mk_assert_int_eq(1, n);

    out.unread_count = 5;
    mk_assert_int_eq(0, miku_conv_update(svc, &out));
    miku_json_val_t *req = miku_json_create_object();
    miku_json_object_set(req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(req, "conversationID", miku_json_create_str("conv_1"));
    miku_json_val_t *resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "markConversationMessageAsRead", req, resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(resp, "errCode")));
    mk_assert_int_eq(0, miku_conv_get(svc, "u1", "conv_1", &out));
    mk_assert_int_eq(0, out.unread_count);
    miku_json_destroy(req);
    miku_json_destroy(resp);

    /* API-style setConversation uses userID as owner */
    miku_json_val_t *set = miku_json_create_object();
    miku_json_object_set(set, "userID", miku_json_create_str("u2"));
    miku_json_object_set(set, "conversationID", miku_json_create_str("conv_u2"));
    miku_json_object_set(set, "conversationType", miku_json_create_int(1));
    miku_json_val_t *set_resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "setConversation", set, set_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(set_resp, "errCode")));
    mk_assert_int_eq(0, miku_conv_get(svc, "u2", "conv_u2", &out));
    miku_json_destroy(set);
    miku_json_destroy(set_resp);

    miku_json_val_t *pin = miku_json_create_object();
    miku_json_object_set(pin, "userID", miku_json_create_str("u2"));
    miku_json_object_set(pin, "conversationID", miku_json_create_str("conv_u2"));
    miku_json_object_set(pin, "isPinned", miku_json_create_int(1));
    miku_json_val_t *pin_resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "pinConversation", pin, pin_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(pin_resp, "errCode")));
    miku_json_destroy(pin);
    miku_json_destroy(pin_resp);

    miku_json_val_t *ids_req = miku_json_create_object();
    miku_json_object_set(ids_req, "userID", miku_json_create_str("u2"));
    miku_json_val_t *ids_resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "getPinnedConversationIDs", ids_req, ids_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(ids_resp, "errCode")));
    mk_assert_int_eq(1, (int)miku_json_size(miku_json_get(ids_resp, "data")));
    miku_json_destroy(ids_req);
    miku_json_destroy(ids_resp);

    miku_conv_service_destroy(svc);
}

static void test_msg_send_and_query(void) {
    miku_msg_service_t *svc = miku_msg_service_create();
    miku_friend_service_t *friends = miku_friend_service_create();
    mk_assert_not_null(svc);
    mk_assert_not_null(friends);
    miku_msg_service_set_friend_svc(svc, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "s1", "r1", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "r1", "s1", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "alice", "bob", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "bob", "alice", ""));

    miku_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.send_id, "s1", sizeof(m.send_id) - 1);
    strncpy(m.recv_id, "r1", sizeof(m.recv_id) - 1);
    strncpy(m.content, "hi", sizeof(m.content) - 1);
    m.msg_type = MK_MSG_TYPE_TEXT;
    int rc = miku_msg_send(svc, &m);
    mk_assert_int_eq(0, rc);
    mk_assert(strlen(m.server_msg_id) > 0);
    mk_assert_long_eq(1, (long)m.seq);

    strncpy(m.client_msg_id, "c_upd_1", sizeof(m.client_msg_id) - 1);
    mk_assert_int_eq(0, miku_msg_send(svc, &m));
    mk_assert_str_eq("si_2_r1_s1", m.conversation_id);
    mk_assert_int_eq(0, miku_msg_update_delivery(svc, "c_upd_1", 99, "gw_id", 12345));
    miku_msg_t out[4];
    int n = miku_msg_get_by_conv(svc, "si_2_r1_s1", 0, 0, 10, out, 4);
    mk_assert(n >= 1);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i].client_msg_id, "c_upd_1") == 0) {
            mk_assert_long_eq(99, (long)out[i].seq);
            mk_assert_str_eq("gw_id", out[i].server_msg_id);
            mk_assert_long_eq(12345, (long)out[i].send_time);
            found = 1;
        }
    }
    mk_assert_int_eq(1, found);

    /* Substring userIDs must not leak across conversations. */
    miku_msg_t ice;
    memset(&ice, 0, sizeof(ice));
    strncpy(ice.send_id, "alice", sizeof(ice.send_id) - 1);
    strncpy(ice.recv_id, "bob", sizeof(ice.recv_id) - 1);
    strncpy(ice.content, "a", sizeof(ice.content) - 1);
    ice.msg_type = MK_MSG_TYPE_TEXT;
    mk_assert_int_eq(0, miku_msg_send(svc, &ice));
    mk_assert_str_eq("si_5_alice_bob", ice.conversation_id);
    n = miku_msg_get_by_conv(svc, "si_2_r1_s1", 0, 0, 10, out, 4);
    mk_assert(n >= 1);
    for (int i = 0; i < n; i++)
        mk_assert(strcmp(out[i].send_id, "alice") != 0);
    n = miku_msg_get_by_conv(svc, "si_5_alice_bob", 0, 0, 10, out, 4);
    mk_assert_int_eq(1, n);
    mk_assert_str_eq("alice", out[0].send_id);

    /* Spoofed conversationID must not inject into another chat. */
    miku_msg_t spoof;
    memset(&spoof, 0, sizeof(spoof));
    strncpy(spoof.send_id, "eve", sizeof(spoof.send_id) - 1);
    strncpy(spoof.recv_id, "mallory", sizeof(spoof.recv_id) - 1);
    strncpy(spoof.conversation_id, "si_5_alice_bob", sizeof(spoof.conversation_id) - 1);
    strncpy(spoof.content, "injected", sizeof(spoof.content) - 1);
    spoof.msg_type = MK_MSG_TYPE_TEXT;
    mk_assert_int_eq(0, miku_msg_send(svc, &spoof));
    mk_assert_str_eq("si_3_eve_mallory", spoof.conversation_id);
    n = miku_msg_get_by_conv(svc, "si_5_alice_bob", 0, 0, 10, out, 4);
    mk_assert_int_eq(1, n);
    mk_assert(strcmp(out[0].content, "injected") != 0);
    n = miku_msg_get_by_conv(svc, "si_3_eve_mallory", 0, 0, 10, out, 4);
    mk_assert_int_eq(1, n);
    mk_assert_str_eq("injected", out[0].content);

    miku_msg_t gm;
    memset(&gm, 0, sizeof(gm));
    strncpy(gm.send_id, "owner", sizeof(gm.send_id) - 1);
    strncpy(gm.group_id, "g9", sizeof(gm.group_id) - 1);
    strncpy(gm.content, "ghi", sizeof(gm.content) - 1);
    gm.msg_type = MK_MSG_TYPE_TEXT;
    gm.session_type = 3;
    mk_assert_int_eq(0, miku_msg_send(svc, &gm));
    mk_assert_str_eq("sg_g9", gm.conversation_id);
    n = miku_msg_get_by_conv(svc, "sg_g9", 0, 0, 10, out, 4);
    mk_assert_int_eq(1, n);
    mk_assert_str_eq("g9", out[0].group_id);

    /* Sender's own group message must survive userClearAllMsg. */
    miku_msg_t gm2;
    memset(&gm2, 0, sizeof(gm2));
    strncpy(gm2.send_id, "s1", sizeof(gm2.send_id) - 1);
    strncpy(gm2.group_id, "g9", sizeof(gm2.group_id) - 1);
    strncpy(gm2.content, "from-s1", sizeof(gm2.content) - 1);
    gm2.msg_type = MK_MSG_TYPE_TEXT;
    gm2.session_type = 3;
    mk_assert_int_eq(0, miku_msg_send(svc, &gm2));

    miku_json_val_t *clr = miku_json_create_object();
    miku_json_object_set(clr, "conversationID", miku_json_create_str("si_5_alice_bob"));
    miku_json_object_set(clr, "userID", miku_json_create_str("alice"));
    miku_json_val_t *clr_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "clearConversationMsg", clr, clr_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(clr_resp, "errCode")));
    mk_assert_int_eq(0, miku_msg_get_by_conv(svc, "si_5_alice_bob", 0, 0, 10, out, 4));
    mk_assert(miku_msg_get_by_conv(svc, "si_2_r1_s1", 0, 0, 10, out, 4) >= 1);
    miku_json_destroy(clr);
    miku_json_destroy(clr_resp);

    /* Stranger / group clear refused. */
    miku_json_val_t *clr_bad = miku_json_create_object();
    miku_json_object_set(clr_bad, "conversationID", miku_json_create_str("si_2_r1_s1"));
    miku_json_object_set(clr_bad, "userID", miku_json_create_str("alice"));
    miku_json_val_t *clr_bad_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "clearConversationMsg", clr_bad, clr_bad_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(clr_bad_resp, "errCode")));
    miku_json_destroy(clr_bad);
    miku_json_destroy(clr_bad_resp);

    miku_json_val_t *clr_g = miku_json_create_object();
    miku_json_object_set(clr_g, "conversationID", miku_json_create_str("sg_g9"));
    miku_json_object_set(clr_g, "userID", miku_json_create_str("owner"));
    miku_json_val_t *clr_g_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "clearConversationMsg", clr_g, clr_g_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(clr_g_resp, "errCode")));
    miku_json_destroy(clr_g);
    miku_json_destroy(clr_g_resp);

    miku_json_val_t *uclr = miku_json_create_object();
    miku_json_object_set(uclr, "userID", miku_json_create_str("s1"));
    miku_json_val_t *uclr_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "userClearAllMsg", uclr, uclr_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(uclr_resp, "errCode")));
    mk_assert_int_eq(0, miku_msg_get_by_conv(svc, "si_2_r1_s1", 0, 0, 10, out, 4));
    mk_assert_int_eq(2, miku_msg_get_by_conv(svc, "sg_g9", 0, 0, 10, out, 4));
    miku_json_destroy(uclr);
    miku_json_destroy(uclr_resp);

    miku_friend_service_destroy(friends);
    miku_msg_service_destroy(svc);
}

static void test_msg_get_group_member_gate(void) {
    miku_group_service_t *group = miku_group_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(group);
    mk_assert_not_null(msg);
    miku_msg_service_set_group_svc(msg, group);

    miku_group_t g;
    memset(&g, 0, sizeof(g));
    strncpy(g.group_name, "gate", sizeof(g.group_name) - 1);
    mk_assert_int_eq(0, miku_group_create(group, &g, "owner"));
    mk_assert_int_eq(0, miku_group_add_member(group, g.group_id, "member1", 20));

    miku_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.send_id, "owner", sizeof(m.send_id) - 1);
    strncpy(m.group_id, g.group_id, sizeof(m.group_id) - 1);
    strncpy(m.content, "group secret", sizeof(m.content) - 1);
    m.msg_type = MK_MSG_TYPE_TEXT;
    mk_assert_int_eq(0, miku_msg_send(msg, &m));

    miku_json_val_t *get_req = miku_json_create_object();
    miku_json_object_set(get_req, "serverMsgID", miku_json_create_str(m.server_msg_id));
    miku_json_object_set(get_req, "userID", miku_json_create_str("member1"));
    miku_json_val_t *get_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getMsg", get_req, get_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(get_resp, "errCode")));
    mk_assert_int_eq(1, (int)miku_json_size(miku_json_get(get_resp, "data")));

    miku_json_val_t *get_stranger = miku_json_create_object();
    miku_json_object_set(get_stranger, "serverMsgID", miku_json_create_str(m.server_msg_id));
    miku_json_object_set(get_stranger, "userID", miku_json_create_str("stranger"));
    miku_json_val_t *stranger_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getMsg", get_stranger, stranger_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(stranger_resp, "errCode")));
    mk_assert_int_eq(0, (int)miku_json_size(miku_json_get(stranger_resp, "data")));

    /* Without group_svc wired, getMsg fails closed even for members. */
    miku_msg_service_t *bare = miku_msg_service_create();
    miku_json_val_t *bare_resp = miku_json_create_object();
    miku_msg_handle_rpc(bare, "getMsg", get_req, bare_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(bare_resp, "errCode")));
    mk_assert_int_eq(0, (int)miku_json_size(miku_json_get(bare_resp, "data")));

    miku_json_destroy(get_req);
    miku_json_destroy(get_resp);
    miku_json_destroy(get_stranger);
    miku_json_destroy(stranger_resp);
    miku_json_destroy(bare_resp);
    miku_msg_service_destroy(bare);
    miku_msg_service_destroy(msg);
    miku_group_service_destroy(group);
}

static void test_msg_get_si_mutual_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(friends);
    mk_assert_not_null(msg);
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "a", "b", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "b", "a", ""));

    miku_json_val_t *send_req = miku_json_create_object();
    miku_json_object_set(send_req, "sendID", miku_json_create_str("a"));
    miku_json_object_set(send_req, "recvID", miku_json_create_str("b"));
    miku_json_object_set(send_req, "content", miku_json_create_str("secret"));
    miku_json_object_set(send_req, "clientMsgID", miku_json_create_str("c_si_gate"));
    miku_json_val_t *send_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "send", send_req, send_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(send_resp, "errCode")));
    const char *smid = miku_json_str(miku_json_get(send_resp, "serverMsgID"));

    miku_json_val_t *get_b = miku_json_create_object();
    miku_json_object_set(get_b, "serverMsgID", miku_json_create_str(smid));
    miku_json_object_set(get_b, "userID", miku_json_create_str("b"));
    miku_json_val_t *get_b_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getMsg", get_b, get_b_resp);
    mk_assert_int_eq(1, (int)miku_json_size(miku_json_get(get_b_resp, "data")));

    miku_json_val_t *get_eve = miku_json_create_object();
    miku_json_object_set(get_eve, "serverMsgID", miku_json_create_str(smid));
    miku_json_object_set(get_eve, "userID", miku_json_create_str("eve"));
    miku_json_val_t *get_eve_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getMsg", get_eve, get_eve_resp);
    mk_assert_int_eq(0, (int)miku_json_size(miku_json_get(get_eve_resp, "data")));

    miku_json_destroy(send_req);
    miku_json_destroy(send_resp);
    miku_json_destroy(get_b);
    miku_json_destroy(get_b_resp);
    miku_json_destroy(get_eve);
    miku_json_destroy(get_eve_resp);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_msg_conv_read_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(friends);
    mk_assert_not_null(msg);
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "alice", "bob", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "bob", "alice", ""));

    miku_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.send_id, "alice", sizeof(m.send_id) - 1);
    strncpy(m.recv_id, "bob", sizeof(m.recv_id) - 1);
    strncpy(m.content, "secret dm", sizeof(m.content) - 1);
    m.msg_type = MK_MSG_TYPE_TEXT;
    mk_assert_int_eq(0, miku_msg_send(msg, &m));

    miku_json_val_t *pull_ok = miku_json_create_object();
    miku_json_object_set(pull_ok, "conversationID", miku_json_create_str(m.conversation_id));
    miku_json_object_set(pull_ok, "userID", miku_json_create_str("bob"));
    miku_json_object_set(pull_ok, "beginSeq", miku_json_create_int(1));
    miku_json_val_t *pull_ok_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "pullMsgBySeq", pull_ok, pull_ok_resp);
    mk_assert_int_eq(1, (int)miku_json_size(miku_json_get(pull_ok_resp, "data")));

    miku_json_val_t *pull_bad = miku_json_create_object();
    miku_json_object_set(pull_bad, "conversationID", miku_json_create_str(m.conversation_id));
    miku_json_object_set(pull_bad, "userID", miku_json_create_str("eve"));
    miku_json_object_set(pull_bad, "beginSeq", miku_json_create_int(1));
    miku_json_val_t *pull_bad_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "pullMsgBySeq", pull_bad, pull_bad_resp);
    mk_assert_int_eq(0, (int)miku_json_size(miku_json_get(pull_bad_resp, "data")));

    miku_json_destroy(pull_ok);
    miku_json_destroy(pull_ok_resp);
    miku_json_destroy(pull_bad);
    miku_json_destroy(pull_bad_resp);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_msg_get_send_status_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(friends);
    mk_assert_not_null(msg);
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "s1", "s2", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "s2", "s1", ""));

    miku_json_val_t *send_req = miku_json_create_object();
    miku_json_object_set(send_req, "sendID", miku_json_create_str("s1"));
    miku_json_object_set(send_req, "recvID", miku_json_create_str("s2"));
    miku_json_object_set(send_req, "content", miku_json_create_str("ping"));
    miku_json_object_set(send_req, "clientMsgID", miku_json_create_str("c_gate_1"));
    miku_json_val_t *send_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "send", send_req, send_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(send_resp, "errCode")));
    const char *smid = miku_json_str(miku_json_get(send_resp, "serverMsgID"));

    miku_json_val_t *ok_req = miku_json_create_object();
    miku_json_object_set(ok_req, "serverMsgID", miku_json_create_str(smid));
    miku_json_object_set(ok_req, "userID", miku_json_create_str("s1"));
    miku_json_val_t *ok_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getSendMsgStatus", ok_req, ok_resp);
    mk_assert_int_eq(1, (int)miku_json_int(miku_json_get(ok_resp, "status")));

    miku_json_val_t *chk_req = miku_json_create_object();
    miku_json_object_set(chk_req, "serverMsgID", miku_json_create_str(smid));
    miku_json_object_set(chk_req, "userID", miku_json_create_str("s1"));
    miku_json_val_t *chk_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "checkMsgIsSendSuccess", chk_req, chk_resp);
    mk_assert_int_eq(1, (int)miku_json_int(miku_json_get(chk_resp, "status")));

    mk_assert_int_eq(0, miku_friend_delete(friends, "s1", "s2"));
    mk_assert_int_eq(0, miku_friend_delete(friends, "s2", "s1"));
    miku_json_val_t *lost_req = miku_json_create_object();
    miku_json_object_set(lost_req, "serverMsgID", miku_json_create_str(smid));
    miku_json_object_set(lost_req, "userID", miku_json_create_str("s1"));
    miku_json_val_t *lost_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getSendMsgStatus", lost_req, lost_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(lost_resp, "status")));
    miku_json_destroy(lost_req);
    miku_json_destroy(lost_resp);

    miku_json_val_t *bad_req = miku_json_create_object();
    miku_json_object_set(bad_req, "serverMsgID", miku_json_create_str(smid));
    miku_json_object_set(bad_req, "userID", miku_json_create_str("s2"));
    miku_json_val_t *bad_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getSendMsgStatus", bad_req, bad_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(bad_resp, "status")));

    miku_json_val_t *bad_id_req = miku_json_create_object();
    miku_json_object_set(bad_id_req, "userID", miku_json_create_str("s1"));
    miku_json_val_t *bad_id_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "getSendMsgStatus", bad_id_req, bad_id_resp);
    mk_assert_int_eq(400, (int)miku_json_int(miku_json_get(bad_id_resp, "errCode")));
    miku_json_destroy(bad_id_req);
    miku_json_destroy(bad_id_resp);

    miku_json_destroy(chk_req);
    miku_json_destroy(chk_resp);
    miku_json_destroy(send_req);
    miku_json_destroy(send_resp);
    miku_json_destroy(ok_req);
    miku_json_destroy(ok_resp);
    miku_json_destroy(bad_req);
    miku_json_destroy(bad_resp);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_msg_admin_rpc_gate(void) {
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(msg);

    miku_json_val_t *deny_req = miku_json_create_object();
    miku_json_val_t *deny_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "cleanUpMsg", deny_req, deny_resp);
    mk_assert_int_eq(403, (int)miku_json_int(miku_json_get(deny_resp, "errCode")));
    miku_json_destroy(deny_req);
    miku_json_destroy(deny_resp);

    miku_json_val_t *batch_deny = miku_json_create_object();
    miku_json_val_t *batch_deny_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "batchSendMsg", batch_deny, batch_deny_resp);
    mk_assert_int_eq(403, (int)miku_json_int(miku_json_get(batch_deny_resp, "errCode")));
    miku_json_destroy(batch_deny);
    miku_json_destroy(batch_deny_resp);

    miku_json_val_t *ok_req = miku_json_create_object();
    miku_json_object_set(ok_req, "platformID", miku_json_create_int(5));
    miku_json_val_t *ok_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "cleanUpMsg", ok_req, ok_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(ok_resp, "errCode")));
    miku_json_destroy(ok_req);
    miku_json_destroy(ok_resp);

    miku_json_val_t *batch_ok = miku_json_create_object();
    miku_json_object_set(batch_ok, "platformID", miku_json_create_int(5));
    miku_json_val_t *batch_ok_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "batchSendMsg", batch_ok, batch_ok_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(batch_ok_resp, "errCode")));
    mk_assert_not_null(miku_json_get(batch_ok_resp, "data"));
    miku_json_destroy(batch_ok);
    miku_json_destroy(batch_ok_resp);

    miku_json_val_t *biz_deny = miku_json_create_object();
    miku_json_object_set(biz_deny, "sendID", miku_json_create_str("sys"));
    miku_json_object_set(biz_deny, "recvID", miku_json_create_str("u1"));
    miku_json_val_t *biz_deny_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "sendBusinessNotification", biz_deny, biz_deny_resp);
    mk_assert_int_eq(403, (int)miku_json_int(miku_json_get(biz_deny_resp, "errCode")));
    miku_json_destroy(biz_deny);
    miku_json_destroy(biz_deny_resp);

    miku_msg_service_destroy(msg);
}

static void test_msg_delete_by_seq_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "u1", "u2", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "u2", "u1", ""));

    miku_json_val_t *send_req = miku_json_create_object();
    miku_json_object_set(send_req, "sendID", miku_json_create_str("u1"));
    miku_json_object_set(send_req, "recvID", miku_json_create_str("u2"));
    miku_json_object_set(send_req, "content", miku_json_create_str("x"));
    miku_json_val_t *send_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "send", send_req, send_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(send_resp, "errCode")));
    int64_t seq = miku_json_int(miku_json_get(send_resp, "seq"));
    char conv_id[MK_CONV_ID_LEN];
    miku_conversation_id_resolve(conv_id, sizeof(conv_id), NULL, NULL, "u1", "u2");

    mk_assert_int_eq(0, miku_msg_may_delete_physical_by_seq(msg, "u2", conv_id, seq));
    mk_assert_int_eq(1, miku_msg_may_delete_physical_by_seq(msg, "u1", conv_id, seq));

    miku_json_destroy(send_req);
    miku_json_destroy(send_resp);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_msg_reaction_conv_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "a", "b", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "b", "a", ""));

    char cid[MK_CONV_ID_LEN];
    miku_conversation_id_resolve(cid, sizeof(cid), NULL, NULL, "a", "b");

    miku_json_val_t *ok_req = miku_json_create_object();
    miku_json_object_set(ok_req, "userID", miku_json_create_str("a"));
    miku_json_object_set(ok_req, "conversationID", miku_json_create_str(cid));
    miku_json_val_t *ok_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "setMessageReactionExtensions", ok_req, ok_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(ok_resp, "errCode")));

    miku_json_val_t *bad_req = miku_json_create_object();
    miku_json_object_set(bad_req, "userID", miku_json_create_str("x"));
    miku_json_object_set(bad_req, "conversationID", miku_json_create_str(cid));
    miku_json_val_t *bad_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "addMessageReactionExtensions", bad_req, bad_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(bad_resp, "errCode")));

    miku_json_destroy(ok_req);
    miku_json_destroy(ok_resp);
    miku_json_destroy(bad_req);
    miku_json_destroy(bad_resp);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_msg_revoke_rpc_validation(void) {
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(msg);
    miku_json_val_t *req = miku_json_create_object();
    miku_json_val_t *resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "revokeMsg", req, resp);
    mk_assert_int_eq(400, (int)miku_json_int(miku_json_get(resp, "errCode")));
    miku_json_destroy(req);
    miku_json_destroy(resp);
    miku_msg_service_destroy(msg);
}

static void test_msg_mark_read_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(friends);
    mk_assert_not_null(msg);
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "a", "b", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "b", "a", ""));

    miku_json_val_t *send_req = miku_json_create_object();
    miku_json_object_set(send_req, "sendID", miku_json_create_str("a"));
    miku_json_object_set(send_req, "recvID", miku_json_create_str("b"));
    miku_json_object_set(send_req, "content", miku_json_create_str("hi"));
    miku_json_val_t *send_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "send", send_req, send_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(send_resp, "errCode")));
    char cid[MK_CONV_ID_LEN];
    miku_conversation_id_resolve(cid, sizeof(cid), NULL, NULL, "a", "b");

    miku_json_val_t *ok = miku_json_create_object();
    miku_json_object_set(ok, "conversationID", miku_json_create_str(cid));
    miku_json_object_set(ok, "userID", miku_json_create_str("b"));
    miku_json_val_t *ok_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "markConversationAsRead", ok, ok_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(ok_resp, "errCode")));

    miku_json_val_t *bad = miku_json_create_object();
    miku_json_object_set(bad, "conversationID", miku_json_create_str(cid));
    miku_json_object_set(bad, "userID", miku_json_create_str("x"));
    miku_json_val_t *bad_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "markConversationAsRead", bad, bad_resp);
    mk_assert_int_eq(3003, (int)miku_json_int(miku_json_get(bad_resp, "errCode")));

    miku_json_val_t *mark_msg = miku_json_create_object();
    miku_json_object_set(mark_msg, "conversationID", miku_json_create_str(cid));
    miku_json_object_set(mark_msg, "userID", miku_json_create_str("b"));
    miku_json_val_t *mark_msg_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "markMsgAsRead", mark_msg, mark_msg_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(mark_msg_resp, "errCode")));

    miku_json_destroy(mark_msg);
    miku_json_destroy(mark_msg_resp);
    miku_json_destroy(send_req);
    miku_json_destroy(send_resp);
    miku_json_destroy(ok);
    miku_json_destroy(ok_resp);
    miku_json_destroy(bad);
    miku_json_destroy(bad_resp);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_msg_delete_revoke_conv_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(friends);
    mk_assert_not_null(msg);
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "a", "b", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "b", "a", ""));

    miku_json_val_t *send_req = miku_json_create_object();
    miku_json_object_set(send_req, "sendID", miku_json_create_str("a"));
    miku_json_object_set(send_req, "recvID", miku_json_create_str("b"));
    miku_json_object_set(send_req, "content", miku_json_create_str("delme"));
    miku_json_object_set(send_req, "clientMsgID", miku_json_create_str("c_del_gate"));
    miku_json_val_t *send_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "send", send_req, send_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(send_resp, "errCode")));

    /* Without friend_svc wired, sender cannot delete/revoke (fail-closed). */
    miku_msg_service_t *bare = miku_msg_service_create();
    miku_json_val_t *del_req = miku_json_create_object();
    miku_json_object_set(del_req, "userID", miku_json_create_str("a"));
    miku_json_object_set(del_req, "clientMsgID", miku_json_create_str("c_del_gate"));
    miku_json_val_t *del_resp = miku_json_create_object();
    miku_msg_handle_rpc(bare, "deleteMsg", del_req, del_resp);
    mk_assert_int_eq(5001, (int)miku_json_int(miku_json_get(del_resp, "errCode")));

    miku_json_val_t *rev_req = miku_json_create_object();
    miku_json_object_set(rev_req, "userID", miku_json_create_str("a"));
    miku_json_object_set(rev_req, "clientMsgID", miku_json_create_str("c_del_gate"));
    miku_json_val_t *rev_resp = miku_json_create_object();
    miku_msg_handle_rpc(bare, "revokeMsg", rev_req, rev_resp);
    mk_assert_int_eq(5001, (int)miku_json_int(miku_json_get(rev_resp, "errCode")));

    miku_json_destroy(send_req);
    miku_json_destroy(send_resp);
    miku_json_destroy(del_req);
    miku_json_destroy(del_resp);
    miku_json_destroy(rev_req);
    miku_json_destroy(rev_resp);
    miku_msg_service_destroy(bare);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_msg_send_friend_gate(void) {
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg = miku_msg_service_create();
    mk_assert_not_null(friends);
    mk_assert_not_null(msg);
    miku_msg_service_set_friend_svc(msg, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "s1", "s2", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "s2", "s1", ""));

    miku_json_val_t *ok_req = miku_json_create_object();
    miku_json_object_set(ok_req, "sendID", miku_json_create_str("s1"));
    miku_json_object_set(ok_req, "recvID", miku_json_create_str("s2"));
    miku_json_object_set(ok_req, "content", miku_json_create_str("hi"));
    miku_json_val_t *ok_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "send", ok_req, ok_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(ok_resp, "errCode")));

    miku_msg_service_t *bare = miku_msg_service_create();
    miku_json_val_t *bare_resp = miku_json_create_object();
    miku_msg_handle_rpc(bare, "send", ok_req, bare_resp);
    mk_assert_int_eq(6002, (int)miku_json_int(miku_json_get(bare_resp, "errCode")));

    mk_assert_int_eq(0, miku_friend_add(friends, "s1", "s3", ""));
    miku_json_val_t *one_way = miku_json_create_object();
    miku_json_object_set(one_way, "sendID", miku_json_create_str("s1"));
    miku_json_object_set(one_way, "recvID", miku_json_create_str("s3"));
    miku_json_object_set(one_way, "content", miku_json_create_str("nope"));
    miku_json_val_t *one_way_resp = miku_json_create_object();
    miku_msg_handle_rpc(msg, "send", one_way, one_way_resp);
    mk_assert_int_eq(6002, (int)miku_json_int(miku_json_get(one_way_resp, "errCode")));

    miku_json_destroy(ok_req);
    miku_json_destroy(ok_resp);
    miku_json_destroy(bare_resp);
    miku_json_destroy(one_way);
    miku_json_destroy(one_way_resp);
    miku_msg_service_destroy(bare);
    miku_msg_service_destroy(msg);
    miku_friend_service_destroy(friends);
}

static void test_third_rpc(void) {
    miku_third_service_t *svc = miku_third_service_create();
    mk_assert_not_null(svc);

    miku_json_val_t *resp = miku_json_create_object();
    miku_third_handle_rpc(svc, "getUploadToken", NULL, resp);
    int64_t err = miku_json_int(miku_json_get(resp, "errCode"));
    mk_assert_int_eq(0, (int)err);

    miku_json_destroy(resp);
    miku_third_service_destroy(svc);
}

static void test_api_gateway_e2e(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);

    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19080);
    mk_assert_not_null(srv);

    int rc = miku_api_register_routes(srv, ctx);
    mk_assert_int_eq(0, rc);

    mk_assert(ctx->auth != NULL);
    mk_assert(ctx->user != NULL);
    mk_assert(ctx->friend_svc != NULL);
    mk_assert(ctx->group_svc != NULL);
    mk_assert(ctx->conv != NULL);
    mk_assert(ctx->msg != NULL);
    mk_assert(ctx->third != NULL);

    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static int http_post(const char *host, int port, const char *path, const char *body, char *resp, int resp_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }

    char req[4096];
    int len = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
        path, host, port, strlen(body), body);
    write(fd, req, (size_t)len);

    int total = 0;
    while (total < resp_cap - 1) {
        ssize_t n = read(fd, resp + total, (size_t)(resp_cap - total - 1));
        if (n <= 0) break;
        total += (int)n;
    }
    resp[total] = '\0';
    close(fd);
    return total;
}

static int http_get(const char *host, int port, const char *path, char *resp, int resp_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }

    char req[1024];
    int len = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n", path, host, port);
    write(fd, req, (size_t)len);

    int total = 0;
    while (total < resp_cap - 1) {
        ssize_t nr = read(fd, resp + total, (size_t)(resp_cap - total - 1));
        if (nr <= 0) break;
        total += (int)nr;
    }
    resp[total] = '\0';
    close(fd);
    return total;
}

static void test_rpc_server_e2e(void) {
    miku_user_service_t *svc = miku_user_service_create();
    mk_assert_not_null(svc);

    miku_rpc_server_t *srv = miku_rpc_server_create(svc,
        (miku_rpc_dispatch_fn)miku_user_handle_rpc, 19090);
    mk_assert_not_null(srv);

    int rc = miku_rpc_server_start(srv);
    mk_assert_int_eq(0, rc);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    mk_assert(fd >= 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19090);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    mk_assert_int_eq(0, rc);

    const char *payload = "{\"method\":\"registerUser\",\"userID\":\"u42\",\"nickname\":\"E2E\"}";
    uint32_t plen = (uint32_t)strlen(payload);

    uint8_t hdr[16] = {0};
    hdr[0] = 0x4D; hdr[1] = 0x4B;
    hdr[4] = 1;
    write(fd, hdr, 16);

    uint8_t len_buf[4] = {
        (uint8_t)(plen >> 24), (uint8_t)(plen >> 16),
        (uint8_t)(plen >> 8),  (uint8_t)plen
    };
    write(fd, len_buf, 4);
    write(fd, payload, plen);

    miku_rpc_server_poll(srv, 1000);

    uint8_t resp_len_buf[4] = {0};
    ssize_t rn = read(fd, resp_len_buf, 4);
    mk_assert(rn == 4);
    uint32_t rlen = ((uint32_t)resp_len_buf[0] << 24) | ((uint32_t)resp_len_buf[1] << 16) |
                    ((uint32_t)resp_len_buf[2] << 8)  | (uint32_t)resp_len_buf[3];

    mk_assert(rlen > 0 && rlen < 4096);
    char resp_buf[4096] = {0};
    ssize_t n = read(fd, resp_buf, rlen);
    close(fd);
    mk_assert(n > 0);
    resp_buf[n] = '\0';

    miku_json_val_t *r = miku_json_parse_str(resp_buf);
    mk_assert_not_null(r);
    int64_t err = miku_json_int(miku_json_get(r, "errCode"));
    mk_assert_int_eq(0, (int)err);
    miku_json_destroy(r);

    miku_rpc_server_stop(srv);
    miku_rpc_server_destroy(srv);
    miku_user_service_destroy(svc);
}

static int rpc_call(miku_rpc_server_t *srv, int port, const char *payload,
                    char *resp_buf, size_t resp_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(fd); return -1; }

    uint32_t plen = (uint32_t)strlen(payload);
    uint8_t hdr[16] = {0};
    hdr[0] = 0x4D; hdr[1] = 0x4B;
    hdr[4] = 1;
    write(fd, hdr, 16);
    uint8_t len_buf[4] = {
        (uint8_t)(plen >> 24), (uint8_t)(plen >> 16),
        (uint8_t)(plen >> 8),  (uint8_t)plen
    };
    write(fd, len_buf, 4);
    write(fd, payload, plen);

    if (srv) miku_rpc_server_poll(srv, 1000);

    uint8_t resp_len_buf[4] = {0};
    if (read(fd, resp_len_buf, 4) != 4) { close(fd); return -1; }
    uint32_t rlen = ((uint32_t)resp_len_buf[0] << 24) | ((uint32_t)resp_len_buf[1] << 16) |
                    ((uint32_t)resp_len_buf[2] << 8)  | (uint32_t)resp_len_buf[3];
    if (rlen == 0 || rlen >= resp_cap) { close(fd); return -1; }
    ssize_t n = read(fd, resp_buf, rlen);
    close(fd);
    if (n <= 0) return -1;
    resp_buf[n] = '\0';
    return 0;
}

static void test_rpc_internal_token(void) {
    miku_user_service_t *svc = miku_user_service_create();
    mk_assert_not_null(svc);
    miku_rpc_server_t *srv = miku_rpc_server_create(svc,
        (miku_rpc_dispatch_fn)miku_user_handle_rpc, 19091);
    mk_assert_not_null(srv);
    miku_rpc_server_enable_internal_auth(srv);
    mk_assert_int_eq(0, miku_rpc_server_start(srv));

    char resp[4096] = {0};
    mk_assert_int_eq(0, rpc_call(srv, 19091,
        "{\"method\":\"registerUser\",\"userID\":\"u99\",\"nickname\":\"no token\"}",
        resp, sizeof(resp)));
    miku_json_val_t *r = miku_json_parse_str(resp);
    mk_assert_not_null(r);
    mk_assert_int_eq(401, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(r);

    char with_tok[512];
    mk_assert_int_eq(0, miku_rpc_json_add_internal_token(
        "{\"method\":\"registerUser\",\"userID\":\"u99\",\"nickname\":\"ok\"}",
        with_tok, sizeof(with_tok)));
    mk_assert(strstr(with_tok, "internalToken") != NULL);
    resp[0] = '\0';
    mk_assert_int_eq(0, rpc_call(srv, 19091, with_tok, resp, sizeof(resp)));
    r = miku_json_parse_str(resp);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(r);

    miku_rpc_server_stop(srv);
    miku_rpc_server_destroy(srv);
    miku_user_service_destroy(svc);
}

typedef struct {
    miku_rpc_server_t *srv;
    volatile int       stop;
} rpc_poll_ctx_t;

static void *rpc_poll_thread(void *v) {
    rpc_poll_ctx_t *ctx = (rpc_poll_ctx_t *)v;
    while (!ctx->stop)
        miku_rpc_server_poll(ctx->srv, 50);
    return NULL;
}

static void test_rpc_client_call(void) {
    miku_user_service_t *svc = miku_user_service_create();
    mk_assert_not_null(svc);
    miku_rpc_server_t *srv = miku_rpc_server_create(svc,
        (miku_rpc_dispatch_fn)miku_user_handle_rpc, 19092);
    mk_assert_not_null(srv);
    miku_rpc_server_enable_internal_auth(srv);
    mk_assert_int_eq(0, miku_rpc_server_start(srv));

    rpc_poll_ctx_t pctx = { .srv = srv, .stop = 0 };
    pthread_t tid;
    mk_assert_int_eq(0, pthread_create(&tid, NULL, rpc_poll_thread, &pctx));

    char resp[4096] = {0};
    mk_assert_int_eq(0, miku_rpc_call("127.0.0.1", 19092,
        "{\"method\":\"registerUser\",\"userID\":\"rc1\",\"nickname\":\"via client\"}",
        resp, sizeof(resp), 1));
    miku_json_val_t *r = miku_json_parse_str(resp);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(r);

    pctx.stop = 1;
    pthread_join(tid, NULL);
    miku_rpc_server_stop(srv);
    miku_rpc_server_destroy(srv);
    miku_user_service_destroy(svc);
}

static void test_msggateway_lifecycle(void) {
    miku_msggw_t *gw = miku_msggw_create(19100);
    mk_assert_not_null(gw);

    int rc = miku_msggw_start(gw);
    mk_assert_int_eq(0, rc);
    mk_assert_int_eq(0, miku_msggw_client_count(gw));

    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
}

static void test_msggateway_slot_reuse(void) {
    miku_msggw_t *gw = miku_msggw_create(19101);
    mk_assert_not_null(gw);
    mk_assert_int_eq(0, miku_msggw_start(gw));

    char token[512] = {0};
    mk_assert_int_eq(0, miku_token_create("slot_u", 1, "openIM123", token, sizeof(token)));

    for (int round = 0; round < 3; round++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        mk_assert(fd >= 0);
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(19101);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        mk_assert_int_eq(0, connect(fd, (struct sockaddr *)&addr, sizeof(addr)));

        char req[1024];
        int len = snprintf(req, sizeof(req),
            "GET /ws?token=%s HTTP/1.1\r\n"
            "Host: 127.0.0.1:19101\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n", token);
        write(fd, req, (size_t)len);
        miku_msggw_poll(gw, 300);
        char resp[1024] = {0};
        read(fd, resp, sizeof(resp) - 1);
        mk_assert(strstr(resp, "101") != NULL);
        mk_assert_int_eq(1, miku_msggw_client_count(gw));

        miku_msggw_kick_user(gw, "slot_u", -1);
        mk_assert_int_eq(0, miku_msggw_client_count(gw));
        close(fd);
    }

    /* After 3 connect/kick cycles, online count stays 0 (slots reused). */
    mk_assert_int_eq(0, miku_msggw_client_count(gw));
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
}

static int ws_connect_with_token(int port, const char *token) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    char req[1024];
    int len = snprintf(req, sizeof(req),
        "GET /ws?token=%s HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", token, port);
    write(fd, req, (size_t)len);
    return fd;
}

static void test_msggateway_kick_by_platform(void) {
    miku_msggw_t *gw = miku_msggw_create(19102);
    mk_assert_not_null(gw);
    mk_assert_int_eq(0, miku_msggw_start(gw));

    char tok1[512] = {0}, tok2[512] = {0};
    mk_assert_int_eq(0, miku_token_create("kick_u", 1, "openIM123", tok1, sizeof(tok1)));
    mk_assert_int_eq(0, miku_token_create("kick_u", 2, "openIM123", tok2, sizeof(tok2)));

    int fd1 = ws_connect_with_token(19102, tok1);
    int fd2 = ws_connect_with_token(19102, tok2);
    mk_assert(fd1 >= 0 && fd2 >= 0);
    miku_msggw_poll(gw, 300);
    char resp[1024];
    memset(resp, 0, sizeof(resp));
    read(fd1, resp, sizeof(resp) - 1);
    mk_assert(strstr(resp, "101") != NULL);
    memset(resp, 0, sizeof(resp));
    read(fd2, resp, sizeof(resp) - 1);
    mk_assert(strstr(resp, "101") != NULL);
    mk_assert_int_eq(2, miku_msggw_client_count(gw));

    /* Kick only platform 1 — platform 2 stays online. */
    mk_assert_int_eq(1, miku_msggw_kick_user(gw, "kick_u", 1));
    mk_assert_int_eq(1, miku_msggw_client_count(gw));

    mk_assert_int_eq(1, miku_msggw_kick_user(gw, "kick_u", 2));
    mk_assert_int_eq(0, miku_msggw_client_count(gw));

    close(fd1);
    close(fd2);
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
}

static void test_msgtransfer_queue(void) {
    miku_msgtransfer_t *mt = miku_msgtransfer_create();
    mk_assert_not_null(mt);
    miku_msgtransfer_start(mt);

    mk_assert_int_eq(0, miku_msgtransfer_pending(mt));

    miku_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.send_id, "s1", sizeof(m.send_id) - 1);
    strncpy(m.content, "hello queue", sizeof(m.content) - 1);
    int rc = miku_msgtransfer_enqueue(mt, &m);
    mk_assert_int_eq(0, rc);
    mk_assert_int_eq(1, miku_msgtransfer_pending(mt));

    miku_msg_t out;
    rc = miku_msgtransfer_dequeue(mt, &out);
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("s1", out.send_id);
    mk_assert_str_eq("hello queue", out.content);
    mk_assert_int_eq(0, miku_msgtransfer_pending(mt));
    mk_assert_long_eq(1, (long)miku_msgtransfer_total_processed(mt));

    miku_msgtransfer_stop(mt);
    miku_msgtransfer_destroy(mt);
}

static void test_push_subscribe(void) {
    miku_push_t *p = miku_push_create();
    mk_assert_not_null(p);
    miku_push_start(p);

    mk_assert_int_eq(0, miku_push_online_count(p));

    miku_push_subscribe(p, "u1", 1);
    miku_push_subscribe(p, "u2", 2);
    mk_assert_int_eq(2, miku_push_online_count(p));

    miku_push_to_user(p, "u1", "Test", "Hello");
    mk_assert(miku_push_online_count(p) == 2);

    miku_push_unsubscribe(p, "u1");
    mk_assert_int_eq(1, miku_push_online_count(p));

    miku_push_stop(p);
    miku_push_destroy(p);
}

static int g_cron_ran = 0;
static void cron_test_fn(void *ctx) {
    (void)ctx;
    g_cron_ran++;
}

static void test_crontask_tick(void) {
    miku_crontask_t *ct = miku_crontask_create();
    mk_assert_not_null(ct);
    miku_crontask_start(ct);

    mk_assert_int_eq(0, miku_crontask_task_count(ct));
    g_cron_ran = 0;

    miku_crontask_add(ct, "test_task", cron_test_fn, NULL, 1000);
    mk_assert_int_eq(1, miku_crontask_task_count(ct));

    miku_crontask_tick(ct);
    mk_assert_int_eq(1, g_cron_ran);

    miku_crontask_tick(ct);
    mk_assert_int_eq(1, g_cron_ran);

    miku_crontask_remove(ct, "test_task");
    mk_assert_int_eq(0, miku_crontask_task_count(ct));

    miku_crontask_stop(ct);
    miku_crontask_destroy(ct);
}

static int g_ws_msg_count = 0;
static void ws_msg_cb(const char *user_id, const char *msg, size_t len, void *ctx) {
    (void)user_id; (void)msg; (void)len; (void)ctx;
    g_ws_msg_count++;
}

static void test_msggateway_seq_peek_vs_alloc(void) {
    miku_msggw_t *gw = miku_msggw_create(19211);
    mk_assert_not_null(gw);
    int64_t a = -1, b = -1, c = -1, d = -1;
    mk_assert_int_eq(0, miku_msggw_peek_max_seq(gw, "c1", &a));
    mk_assert_int_eq(0, miku_msggw_peek_max_seq(gw, "c1", &b));
    mk_assert_long_eq((long)a, (long)b);
    mk_assert_int_eq(0, miku_msggw_alloc_seq(gw, "c1", &c));
    mk_assert_long_eq((long)(a + 1), (long)c);
    mk_assert_int_eq(0, miku_msggw_peek_max_seq(gw, "c1", &b));
    mk_assert_long_eq((long)c, (long)b);
    /* Per-conversation: c2 stays at 0 while c1 advanced */
    mk_assert_int_eq(0, miku_msggw_peek_max_seq(gw, "c2", &d));
    mk_assert_long_eq(0, (long)d);
    mk_assert_int_eq(0, miku_msggw_alloc_seq(gw, "c2", &d));
    mk_assert_long_eq(1, (long)d);
    mk_assert_int_eq(0, miku_msggw_peek_max_seq(gw, "c1", &b));
    mk_assert_long_eq((long)c, (long)b);
    miku_msggw_destroy(gw);
}

static void test_msggateway_unwrap_op_data(void) {
    int opcode = 0;
    char *data = NULL;
    size_t len = 0;
    const char *env =
        "{\"reqIdentifier\":1003,\"data\":{\"sendID\":\"u1\",\"recvID\":\"u2\","
        "\"contentType\":101,\"content\":\"hi\"}}";
    mk_assert_int_eq(0, miku_msggw_unwrap_op_data(env, &opcode, &data, &len));
    mk_assert_int_eq(1003, opcode);
    mk_assert_not_null(data);
    mk_assert(strstr(data, "sendID") != NULL);
    mk_assert(strstr(data, "u1") != NULL);

    miku_json_val_t *j = miku_json_parse_str(data);
    mk_assert_not_null(j);
    miku_im_msg_t im;
    mk_assert_int_eq(0, miku_im_msg_from_json(&im, j));
    mk_assert_str_eq("u1", im.send_id);
    mk_assert_str_eq("u2", im.recv_id);
    miku_json_destroy(j);
    free(data);

    mk_assert_int_eq(-1, miku_msggw_unwrap_op_data("{}", &opcode, &data, &len));
}

static void test_op_reply_cb(int client_idx, int opcode, const char *payload, size_t len, void *ctx) {
    (void)payload; (void)len;
    miku_msggw_t *gw = (miku_msggw_t *)ctx;
    if (opcode != MK_WS_OP_GET_NEWEST_SEQ) return;
    int64_t seq = 0;
    miku_msggw_peek_max_seq(gw, "conv_t", &seq);
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"errCode\":0,\"maxSeq\":%lld}", (long long)seq);
    miku_msggw_send_op(gw, client_idx, opcode, resp, strlen(resp));
}

static int ws_client_send_text_masked(int fd, const char *text) {
    size_t tlen = strlen(text);
    uint8_t *payload = (uint8_t *)malloc(tlen);
    if (!payload) return -1;
    memcpy(payload, text, tlen);
    miku_ws_frame_t f;
    memset(&f, 0, sizeof(f));
    f.fin = true;
    f.opcode = MK_WS_TEXT;
    f.masked = true;
    f.masking_key[0] = 0x11;
    f.masking_key[1] = 0x22;
    f.masking_key[2] = 0x33;
    f.masking_key[3] = 0x44;
    f.payload = payload;
    f.payload_len = tlen;
    uint8_t buf[4096];
    size_t out_len = 0;
    int rc = miku_ws_frame_encode(&f, buf, sizeof(buf), &out_len);
    free(payload);
    if (rc != 0) return -1;
    return write(fd, buf, out_len) == (ssize_t)out_len ? 0 : -1;
}

static void test_msggateway_opcode_reply(void) {
    miku_msggw_t *gw = miku_msggw_create(19210);
    mk_assert_not_null(gw);
    mk_assert_int_eq(0, miku_msggw_start(gw));
    miku_msggw_on_opcode(gw, test_op_reply_cb, gw);

    char token[512] = {0};
    mk_assert_int_eq(0, miku_token_create("op_user", 1, "openIM123", token, sizeof(token)));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    mk_assert(fd >= 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19210);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    mk_assert_int_eq(0, connect(fd, (struct sockaddr *)&addr, sizeof(addr)));

    char req[1024];
    int len = snprintf(req, sizeof(req),
        "GET /ws?token=%s HTTP/1.1\r\n"
        "Host: 127.0.0.1:19210\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", token);
    write(fd, req, (size_t)len);
    miku_msggw_poll(gw, 500);

    char hs[4096] = {0};
    ssize_t n = read(fd, hs, sizeof(hs) - 1);
    mk_assert(n > 0);
    mk_assert(strstr(hs, "101") != NULL);

    char uid[64] = {0};
    mk_assert_int_eq(0, miku_msggw_get_client_user_id(gw, 0, uid, sizeof(uid)));
    mk_assert_str_eq("op_user", uid);

    const char *frame =
        "{\"reqIdentifier\":1001,\"data\":{\"conversationID\":\"conv_t\"}}";
    mk_assert_int_eq(0, ws_client_send_text_masked(fd, frame));
    miku_msggw_poll(gw, 500);

    /* Server frames are unmasked; read raw and look for maxSeq in payload bytes */
    uint8_t rbuf[2048];
    n = read(fd, rbuf, sizeof(rbuf));
    mk_assert(n > 0);
    rbuf[n < (ssize_t)sizeof(rbuf) ? n : (ssize_t)sizeof(rbuf) - 1] = '\0';
    mk_assert(strstr((char *)rbuf, "maxSeq") != NULL);
    mk_assert(strstr((char *)rbuf, "1001") != NULL);

    close(fd);
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
}

static void test_msggateway_send_op_to_user(void) {
    miku_msggw_t *gw = miku_msggw_create(19220);
    mk_assert_not_null(gw);
    mk_assert_int_eq(0, miku_msggw_start(gw));

    /* No online sessions → 0 delivered */
    mk_assert_int_eq(0, miku_msggw_send_op_to_user(gw, "nobody", MK_WS_OP_PUSH_MSG, "{}", 2));

    char token[512] = {0};
    mk_assert_int_eq(0, miku_token_create("push_u", 1, "openIM123", token, sizeof(token)));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    mk_assert(fd >= 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19220);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    mk_assert_int_eq(0, connect(fd, (struct sockaddr *)&addr, sizeof(addr)));

    char req[1024];
    int len = snprintf(req, sizeof(req),
        "GET /ws?token=%s HTTP/1.1\r\n"
        "Host: 127.0.0.1:19220\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", token);
    write(fd, req, (size_t)len);
    miku_msggw_poll(gw, 500);
    char hs[2048] = {0};
    ssize_t n = read(fd, hs, sizeof(hs) - 1);
    mk_assert(n > 0);
    mk_assert(strstr(hs, "101") != NULL);

    mk_assert_int_eq(1, miku_msggw_send_op_to_user(gw, "push_u", MK_WS_OP_PUSH_MSG,
                                                    "{\"content\":\"hi\"}", 16));
    uint8_t rbuf[2048];
    n = read(fd, rbuf, sizeof(rbuf) - 1);
    mk_assert(n > 0);
    rbuf[n] = '\0';
    mk_assert(strstr((char *)rbuf, "2001") != NULL);
    mk_assert(strstr((char *)rbuf, "hi") != NULL);

    close(fd);
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
}

static int ws_connect_user(miku_msggw_t *gw, int port, const char *user, int *out_fd) {
    char token[512] = {0};
    if (miku_token_create(user, 1, "openIM123", token, sizeof(token)) != 0) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    char req[1024];
    int len = snprintf(req, sizeof(req),
        "GET /ws?token=%s HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", token, port);
    write(fd, req, (size_t)len);
    miku_msggw_poll(gw, 500);
    char hs[2048] = {0};
    ssize_t n = read(fd, hs, sizeof(hs) - 1);
    if (n <= 0 || !strstr(hs, "101")) { close(fd); return -1; }
    *out_fd = fd;
    return 0;
}

static void test_msggateway_unknown_opcode(void) {
    miku_msggw_t *gw = miku_msggw_create(19211);
    mk_assert_not_null(gw);
    mk_assert_int_eq(0, miku_msggw_start(gw));
    miku_msggw_ws_ctx_t ctx = { .gw = gw, .store = NULL, .sub = NULL, .group = NULL };
    miku_msggw_on_opcode(gw, miku_msggw_ws_on_opcode, &ctx);

    int fd = -1;
    mk_assert_int_eq(0, ws_connect_user(gw, 19211, "bad_op_u", &fd));
    const char *frame = "{\"reqIdentifier\":99999,\"data\":{}}";
    mk_assert_int_eq(0, ws_client_send_text_masked(fd, frame));
    miku_msggw_poll(gw, 500);

    uint8_t rbuf[4096];
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    mk_assert(poll(&pfd, 1, 500) > 0);
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
    mk_assert(n > 0);
    rbuf[n] = '\0';
    mk_assert(strstr((char *)rbuf, "\"errCode\":404") != NULL);

    close(fd);
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
}

static void test_msggateway_read_receipt_fanout(void) {
    miku_msggw_t *gw = miku_msggw_create(19225);
    miku_msg_store_t *store = miku_msg_store_create(NULL);
    miku_friend_service_t *friends = miku_friend_service_create();
    mk_assert_not_null(gw);
    mk_assert_not_null(store);
    mk_assert_not_null(friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "read_a", "read_b", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "read_b", "read_a", ""));
    mk_assert_int_eq(0, miku_msggw_start(gw));
    miku_msggw_ws_ctx_t ctx = { .gw = gw, .store = store, .sub = NULL,
                                  .group = NULL, .friend_svc = friends };
    miku_msggw_on_opcode(gw, miku_msggw_ws_on_opcode, &ctx);

    int fd_a = -1, fd_b = -1;
    mk_assert_int_eq(0, ws_connect_user(gw, 19225, "read_a", &fd_a));
    mk_assert_int_eq(0, ws_connect_user(gw, 19225, "read_b", &fd_b));

    const char *frame =
        "{\"reqIdentifier\":1003,\"data\":{\"sendID\":\"read_a\",\"recvID\":\"read_b\","
        "\"conversationID\":\"si_6_read_a_read_b\",\"contentType\":302,"
        "\"hasReadSeq\":7,\"content\":\"read\"}}";
    mk_assert_int_eq(0, ws_client_send_text_masked(fd_a, frame));
    miku_msggw_poll(gw, 500);

    /* Drain sender ACK; peer frame may embed NUL in WS extended-length header. */
    {
        uint8_t dump[2048];
        struct pollfd pfd = { .fd = fd_a, .events = POLLIN };
        if (poll(&pfd, 1, 50) > 0) (void)read(fd_a, dump, sizeof(dump));
    }
    uint8_t rbuf[4096];
    struct pollfd pb = { .fd = fd_b, .events = POLLIN };
    mk_assert(poll(&pb, 1, 500) > 0);
    ssize_t n = read(fd_b, rbuf, sizeof(rbuf) - 1);
    mk_assert(n > 0);
    const char *json = NULL;
    for (ssize_t i = 0; i < n; i++) {
        if (rbuf[i] == '{') { json = (const char *)(rbuf + i); break; }
    }
    mk_assert_not_null(json);
    mk_assert(strstr(json, "2001") != NULL);
    mk_assert(strstr(json, "hasReadSeq") != NULL);
    mk_assert(strstr(json, "302") != NULL);
    mk_assert_long_eq(7, (long)miku_msggw_get_user_read(gw, "read_a", "si_6_read_a_read_b"));

    /* Stranger cannot forge a READ receipt into someone else's conversation. */
    int fd_x = -1;
    mk_assert_int_eq(0, ws_connect_user(gw, 19225, "read_x", &fd_x));
    const char *bad_frame =
        "{\"reqIdentifier\":1003,\"data\":{\"sendID\":\"read_x\","
        "\"conversationID\":\"si_6_read_a_read_b\",\"contentType\":302,"
        "\"hasReadSeq\":99,\"content\":\"read\"}}";
    mk_assert_int_eq(0, ws_client_send_text_masked(fd_x, bad_frame));
    miku_msggw_poll(gw, 500);
    {
        uint8_t dump[2048];
        struct pollfd pfd = { .fd = fd_x, .events = POLLIN };
        mk_assert(poll(&pfd, 1, 500) > 0);
        ssize_t rn = read(fd_x, dump, sizeof(dump) - 1);
        mk_assert(rn > 0);
        const char *ack = NULL;
        for (ssize_t i = 0; i < rn; i++) {
            if (dump[i] == '{') { ack = (const char *)(dump + i); break; }
        }
        mk_assert_not_null(ack);
        mk_assert(strstr(ack, "\"errCode\":0") != NULL);
        mk_assert(strstr(ack, "\"hasReadSeq\":0") != NULL);
        mk_assert(strstr(ack, "3003") == NULL);
    }
    mk_assert_long_eq(7, (long)miku_msggw_get_user_read(gw, "read_a", "si_6_read_a_read_b"));
    mk_assert_long_eq(0, (long)miku_msggw_get_user_read(gw, "read_x", "si_6_read_a_read_b"));
    close(fd_x);

    close(fd_a);
    close(fd_b);
    miku_msggw_stop(gw);
    miku_msg_store_destroy(store);
    miku_friend_service_destroy(friends);
    miku_msggw_destroy(gw);
}

static void test_msggateway_ws_upgrade(void) {
    miku_msggw_t *gw = miku_msggw_create(19200);
    mk_assert_not_null(gw);
    int rc = miku_msggw_start(gw);
    mk_assert_int_eq(0, rc);

    miku_msggw_on_message(gw, ws_msg_cb, NULL);

    char token[512] = {0};
    mk_assert_int_eq(0, miku_token_create("ws_user", 1, "openIM123", token, sizeof(token)));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    mk_assert(fd >= 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19200);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    mk_assert_int_eq(0, rc);

    char req[1024];
    const char *ws_key = "dGhlIHNhbXBsZSBub25jZQ==";
    int len = snprintf(req, sizeof(req),
        "GET /ws?token=%s HTTP/1.1\r\n"
        "Host: 127.0.0.1:19200\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", token, ws_key);
    write(fd, req, (size_t)len);

    miku_msggw_poll(gw, 500);

    char resp[4096] = {0};
    ssize_t n = read(fd, resp, sizeof(resp) - 1);
    close(fd);
    mk_assert(n > 0);
    resp[n] = '\0';
    mk_assert(strstr(resp, "101") != NULL);
    mk_assert(strstr(resp, "Upgrade") != NULL);

    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
}

static void test_token_revoke_force_logout(void) {
    miku_token_revoke_clear();
    miku_auth_service_t *svc = miku_auth_service_create();
    char token[512] = {0};
    mk_assert_int_eq(0, miku_auth_user_token(svc, "rev_u1", "openIM123", 1, token, sizeof(token)));

    char uid[64] = {0};
    mk_assert_int_eq(0, miku_auth_parse_token(svc, token, uid, sizeof(uid)));
    mk_assert_str_eq("rev_u1", uid);

    mk_assert_int_eq(0, miku_auth_force_logout(svc, "rev_u1", 1));
    mk_assert_int_eq(-1, miku_auth_parse_token(svc, token, uid, sizeof(uid)));

    /* Brief pause so new token issued_at > revoke.since (ms resolution) */
    usleep(2000);
    char token2[512] = {0};
    mk_assert_int_eq(0, miku_auth_user_token(svc, "rev_u1", "openIM123", 1, token2, sizeof(token2)));
    mk_assert_int_eq(0, miku_auth_parse_token(svc, token2, uid, sizeof(uid)));

    miku_token_revoke_clear();
    miku_auth_service_destroy(svc);
}

/* Simulates split-deploy: API revoked locally; gateway must revoke via /internal/kick path. */
static void test_token_revoke_propagates_like_gateway(void) {
    miku_token_revoke_clear();
    char token[512] = {0};
    mk_assert_int_eq(0, miku_token_create("gw_rev", 2, "openIM123", token, sizeof(token)));

    char uid[64] = {0};
    int plat = 0;
    mk_assert_int_eq(0, miku_token_verify_ex(token, "openIM123", uid, sizeof(uid), &plat, NULL));
    mk_assert_str_eq("gw_rev", uid);
    mk_assert_int_eq(2, plat);

    /* What handle_internal_kick does in miku-msggateway after receiving API POST. */
    mk_assert_int_eq(0, miku_token_revoke("gw_rev", 2));
    mk_assert_int_eq(-1, miku_token_verify_ex(token, "openIM123", uid, sizeof(uid), &plat, NULL));

    /* Other platform still valid until revoked. */
    char token_p1[512] = {0};
    mk_assert_int_eq(0, miku_token_create("gw_rev", 1, "openIM123", token_p1, sizeof(token_p1)));
    mk_assert_int_eq(0, miku_token_verify_ex(token_p1, "openIM123", uid, sizeof(uid), &plat, NULL));

    miku_token_revoke_clear();
}

static void test_cross_service_msg_flow(void) {
    miku_user_service_t *user_svc = miku_user_service_create();
    miku_friend_service_t *friends = miku_friend_service_create();
    miku_msg_service_t *msg_svc = miku_msg_service_create();
    miku_conv_service_t *conv_svc = miku_conv_service_create();
    mk_assert_not_null(user_svc);
    mk_assert_not_null(friends);
    mk_assert_not_null(msg_svc);
    mk_assert_not_null(conv_svc);
    miku_msg_service_set_friend_svc(msg_svc, friends);
    mk_assert_int_eq(0, miku_friend_add(friends, "alice", "bob", ""));
    mk_assert_int_eq(0, miku_friend_add(friends, "bob", "alice", ""));

    miku_json_val_t *reg = miku_json_create_object();
    miku_json_object_set(reg, "userID", miku_json_create_str("alice"));
    miku_json_object_set(reg, "nickname", miku_json_create_str("Alice"));
    miku_json_val_t *r = miku_json_create_object();
    miku_user_handle_rpc(user_svc, "registerUser", reg, r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(reg);
    miku_json_destroy(r);

    miku_json_val_t *send = miku_json_create_object();
    miku_json_object_set(send, "sendID", miku_json_create_str("alice"));
    miku_json_object_set(send, "recvID", miku_json_create_str("bob"));
    miku_json_object_set(send, "content", miku_json_create_str("Hello Bob!"));
    miku_json_object_set(send, "contentType", miku_json_create_int(101));
    miku_json_val_t *sr = miku_json_create_object();
    miku_msg_handle_rpc(msg_svc, "sendMsg", send, sr);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(sr, "errCode")));
    mk_assert(miku_json_str(miku_json_get(sr, "serverMsgID")) != NULL);
    miku_json_destroy(send);
    miku_json_destroy(sr);

    miku_conversation_t conv;
    memset(&conv, 0, sizeof(conv));
    snprintf(conv.conversation_id, sizeof(conv.conversation_id), "conv_alice_bob");
    strncpy(conv.owner_user_id, "alice", sizeof(conv.owner_user_id) - 1);
    conv.conversation_type = 1;
    conv.latest_msg_send_time = miku_timestamp_ms();
    int crc = miku_conv_create(conv_svc, &conv);
    mk_assert_int_eq(0, crc);

    miku_conversation_t out;
    crc = miku_conv_get(conv_svc, "alice", "conv_alice_bob", &out);
    mk_assert_int_eq(0, crc);
    mk_assert_str_eq("conv_alice_bob", out.conversation_id);

    miku_user_service_destroy(user_svc);
    miku_friend_service_destroy(friends);
    miku_msg_service_destroy(msg_svc);
    miku_conv_service_destroy(conv_svc);
}

static void test_http_health_endpoint(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);

    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19081);
    mk_assert_not_null(srv);
    miku_http_server_use(srv, miku_mw_stats, &ctx->stats);
    miku_api_register_routes(srv, ctx);

    miku_stats_request_inc(&ctx->stats);
    miku_stats_request_inc(&ctx->stats);

    miku_json_val_t *out = miku_json_create_object();
    miku_ji(out, "status", 0);
    miku_jss(out, "message", "ok");
    miku_string_t *s = miku_json_stringify(out);
    mk_assert(strstr(s->data, "\"status\":0") != NULL);
    mk_assert(strstr(s->data, "\"message\":\"ok\"") != NULL);
    miku_str_destroy(s);
    miku_json_destroy(out);

    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_http_stats_endpoint(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_stats_init(&ctx->stats, "miku-api", 19082);

    miku_stats_request_inc(&ctx->stats);
    miku_stats_request_inc(&ctx->stats);
    miku_stats_request_inc(&ctx->stats);

    miku_stats_snapshot_t snap;
    miku_stats_snapshot(&ctx->stats, &snap);

    mk_assert_int_eq(3, (int)snap.requests_total);
    mk_assert_str_eq("miku-api", snap.service_name);
    mk_assert_int_eq(19082, snap.port);
    mk_assert(snap.uptime_ms >= 0);

    miku_json_val_t *out = miku_json_create_object();
    miku_ji(out, "errCode", 0);
    miku_ji(out, "requestsTotal", snap.requests_total);
    miku_ji(out, "uptimeMs", snap.uptime_ms);
    miku_jss(out, "service", snap.service_name);
    miku_string_t *s = miku_json_stringify(out);
    mk_assert(strstr(s->data, "\"requestsTotal\":3") != NULL);
    mk_assert(strstr(s->data, "\"service\":\"miku-api\"") != NULL);
    miku_str_destroy(s);
    miku_json_destroy(out);

    miku_api_ctx_destroy(ctx);
}

void run_service_tests(void) {
    printf("\n── Miku Service Tests ───────────────────\n\n");

    mk_run_test(test_model_user_json_roundtrip);
    mk_run_test(test_model_msg_json_roundtrip);
    mk_run_test(test_auth_user_token);
    mk_run_test(test_auth_bad_secret);
    mk_run_test(test_token_revoke_force_logout);
    mk_run_test(test_token_revoke_propagates_like_gateway);
    mk_run_test(test_user_register_and_find);
    mk_run_test(test_friend_add_and_check);
    mk_run_test(test_group_create_and_members);
    mk_run_test(test_conv_create_and_get);
    mk_run_test(test_msg_send_and_query);
    mk_run_test(test_msg_get_group_member_gate);
    mk_run_test(test_msg_get_si_mutual_gate);
    mk_run_test(test_msg_conv_read_gate);
    mk_run_test(test_msg_get_send_status_gate);
    mk_run_test(test_msg_admin_rpc_gate);
    mk_run_test(test_msg_delete_by_seq_gate);
    mk_run_test(test_msg_reaction_conv_gate);
    mk_run_test(test_msg_revoke_rpc_validation);
    mk_run_test(test_msg_mark_read_gate);
    mk_run_test(test_msg_delete_revoke_conv_gate);
    mk_run_test(test_msg_send_friend_gate);
    mk_run_test(test_third_rpc);
    mk_run_test(test_api_gateway_e2e);
    mk_run_test(test_rpc_server_e2e);
    mk_run_test(test_rpc_internal_token);
    mk_run_test(test_rpc_client_call);
    mk_run_test(test_msggateway_lifecycle);
    mk_run_test(test_msggateway_slot_reuse);
    mk_run_test(test_msggateway_kick_by_platform);
    mk_run_test(test_msggateway_seq_peek_vs_alloc);
    mk_run_test(test_msggateway_unwrap_op_data);
    mk_run_test(test_msggateway_unknown_opcode);
    mk_run_test(test_msggateway_opcode_reply);
    mk_run_test(test_msggateway_send_op_to_user);
    mk_run_test(test_msggateway_read_receipt_fanout);
    mk_run_test(test_msgtransfer_queue);
    mk_run_test(test_push_subscribe);
    mk_run_test(test_crontask_tick);
    mk_run_test(test_msggateway_ws_upgrade);
    mk_run_test(test_cross_service_msg_flow);
    mk_run_test(test_http_health_endpoint);
    mk_run_test(test_http_stats_endpoint);
}
