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
#include <string.h>
#include <stdio.h>

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
    mk_assert(strncmp(token, "miku_user1_", 11) == 0);

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

    bool is = miku_friend_is_friend(svc, "u1", "u2");
    mk_assert(is);

    is = miku_friend_is_friend(svc, "u1", "u3");
    mk_assert(!is);

    miku_friend_t list[16];
    int n = miku_friend_get_list(svc, "u1", list, 16);
    mk_assert_int_eq(1, n);
    mk_assert_str_eq("u2", list[0].friend_user_id);

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
    mk_assert_int_eq(2, found->member_count);

    rc = miku_group_add_member(svc, g.group_id, "member1", 20);
    mk_assert_int_eq(0, rc);

    miku_group_member_t members[16];
    int n = miku_group_get_members(svc, g.group_id, members, 16);
    mk_assert_int_eq(2, n);

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

    miku_conv_service_destroy(svc);
}

static void test_msg_send_and_query(void) {
    miku_msg_service_t *svc = miku_msg_service_create();
    mk_assert_not_null(svc);

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

    miku_msg_service_destroy(svc);
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

void run_service_tests(void) {
    printf("\n── Miku Service Tests ───────────────────\n\n");

    mk_run_test(test_model_user_json_roundtrip);
    mk_run_test(test_model_msg_json_roundtrip);
    mk_run_test(test_auth_user_token);
    mk_run_test(test_auth_bad_secret);
    mk_run_test(test_user_register_and_find);
    mk_run_test(test_friend_add_and_check);
    mk_run_test(test_group_create_and_members);
    mk_run_test(test_conv_create_and_get);
    mk_run_test(test_msg_send_and_query);
    mk_run_test(test_third_rpc);
}
