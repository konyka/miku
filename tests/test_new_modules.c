#include "miku_test.h"
#include "miku_ratelimit.h"
#include "miku_webhook.h"
#include "miku_seq.h"
#include "miku_incr_sync.h"
#include "miku_offline_push.h"
#include "miku_gzip.h"
#include "miku_cron_tasks.h"
#include "miku_ws_subscription.h"
#include "miku_im_message.h"
#include "miku_mt_pipeline.h"
#include "miku_msg_store.h"
#include "miku_session_cache.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_models.h"
#include <stdlib.h>
#include <string.h>

static int wh_received = 0;
static miku_webhook_event_t wh_last_event = 0;

static void wh_handler(miku_webhook_event_t event, const char *payload, void *ctx) {
    (void)payload; (void)ctx;
    wh_received++;
    wh_last_event = event;
}

static int mt_redis_flush_count = 0;
static int mt_mongo_flush_count = 0;

static void mt_to_redis_cb(const miku_msg_t *msgs, int count, void *ctx) {
    (void)msgs; (void)ctx;
    mt_redis_flush_count += count;
}

static void mt_to_mongo_cb(const miku_msg_t *msgs, int count, void *ctx) {
    (void)msgs; (void)ctx;
    mt_mongo_flush_count += count;
}

void test_ratelimit_basic(void) {
    miku_ratelimit_t *rl = miku_ratelimit_create(60000, 3);
    mk_assert_not_null(rl);

    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "user1"));
    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "user1"));
    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "user1"));
    mk_assert_int_eq(0, miku_ratelimit_allow(rl, "user1"));

    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "user2"));
    mk_assert_int_eq(2, miku_ratelimit_remaining(rl, "user2"));
    mk_assert_int_eq(0, miku_ratelimit_remaining(rl, "user1"));

    miku_ratelimit_destroy(rl);
}

void test_ratelimit_reset(void) {
    miku_ratelimit_t *rl = miku_ratelimit_create(60000, 2);
    mk_assert_not_null(rl);

    miku_ratelimit_allow(rl, "user1");
    miku_ratelimit_allow(rl, "user1");
    mk_assert_int_eq(0, miku_ratelimit_allow(rl, "user1"));

    miku_ratelimit_reset(rl);
    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "user1"));

    miku_ratelimit_destroy(rl);
}

void test_webhook_fire(void) {
    miku_webhook_t *wh = miku_webhook_create();
    mk_assert_not_null(wh);

    miku_webhook_set_handler(wh, wh_handler, NULL);
    miku_webhook_add_url(wh, "http://localhost:8080/callback");

    mk_assert_int_eq(0, wh_received);
    miku_webhook_fire(wh, MK_WH_AFTER_SEND_MSG, "{\"msgID\":\"test\"}");
    mk_assert_int_eq(1, wh_received);
    mk_assert_int_eq((int)MK_WH_AFTER_SEND_MSG, (int)wh_last_event);

    miku_webhook_fire(wh, MK_WH_USER_ONLINE, "{\"userID\":\"u1\"}");
    mk_assert_int_eq(2, wh_received);

    mk_assert_int_eq(0, miku_webhook_remove_url(wh, "http://localhost:8080/callback"));
    mk_assert_int_eq(-1, miku_webhook_remove_url(wh, "http://nope"));

    miku_webhook_destroy(wh);
}

void test_webhook_event_names(void) {
    mk_assert_str_eq("afterSendMsg", miku_webhook_event_name(MK_WH_AFTER_SEND_MSG));
    mk_assert_str_eq("userOnline", miku_webhook_event_name(MK_WH_USER_ONLINE));
    mk_assert_str_eq("msgRevoke", miku_webhook_event_name(MK_WH_MSG_REVOKE));
}

void test_seq_basic(void) {
    miku_seq_t *seq = miku_seq_create();
    mk_assert_not_null(seq);

    mk_assert_long_eq(1, (long)miku_seq_next(seq, "conv_1"));
    mk_assert_long_eq(2, (long)miku_seq_next(seq, "conv_1"));
    mk_assert_long_eq(1, (long)miku_seq_next(seq, "conv_2"));
    mk_assert_long_eq(2, (long)miku_seq_current(seq, "conv_1"));
    mk_assert_long_eq(1, (long)miku_seq_current(seq, "conv_2"));

    miku_seq_set(seq, "conv_1", 100);
    mk_assert_long_eq(100, (long)miku_seq_current(seq, "conv_1"));
    mk_assert_long_eq(101, (long)miku_seq_next(seq, "conv_1"));

    miku_seq_destroy(seq);
}

void test_seq_user_read(void) {
    miku_seq_t *seq = miku_seq_create();
    mk_assert_not_null(seq);

    mk_assert_long_eq(0, (long)miku_seq_get_user_read(seq, "u1", "conv_1"));
    miku_seq_set_user_read(seq, "u1", "conv_1", 5);
    mk_assert_long_eq(5, (long)miku_seq_get_user_read(seq, "u1", "conv_1"));
    mk_assert_long_eq(0, (long)miku_seq_get_user_read(seq, "u2", "conv_1"));

    miku_seq_set_user_read(seq, "u1", "conv_1", 10);
    mk_assert_long_eq(10, (long)miku_seq_get_user_read(seq, "u1", "conv_1"));

    miku_seq_destroy(seq);
}

void test_incr_sync_basic(void) {
    miku_incr_sync_t *is = miku_incr_sync_create();
    mk_assert_not_null(is);

    mk_assert_long_eq(0, (long)miku_incr_version(is, MK_INCR_FRIENDS, "u1"));
    mk_assert_long_eq(1, (long)miku_incr_bump(is, MK_INCR_FRIENDS, "u1"));
    mk_assert_long_eq(2, (long)miku_incr_bump(is, MK_INCR_FRIENDS, "u1"));
    mk_assert_long_eq(2, (long)miku_incr_version(is, MK_INCR_FRIENDS, "u1"));

    mk_assert_long_eq(1, (long)miku_incr_bump(is, MK_INCR_GROUPS, "u1"));
    mk_assert_long_eq(2, (long)miku_incr_version(is, MK_INCR_FRIENDS, "u1"));
    mk_assert_long_eq(1, (long)miku_incr_version(is, MK_INCR_GROUPS, "u1"));

    miku_incr_set_version(is, MK_INCR_FRIENDS, "u1", 100);
    mk_assert_long_eq(100, (long)miku_incr_version(is, MK_INCR_FRIENDS, "u1"));

    miku_incr_sync_destroy(is);
}

void test_incr_sync_changes(void) {
    miku_incr_sync_t *is = miku_incr_sync_create();
    mk_assert_not_null(is);

    miku_incr_bump(is, MK_INCR_BLACKS, "u1");
    miku_incr_bump(is, MK_INCR_BLACKS, "u1");

    char *results = NULL;
    mk_assert_int_eq(0, miku_incr_get_changes(is, MK_INCR_BLACKS, "u1", 0, &results));
    free(results);

    miku_incr_sync_destroy(is);
}

void test_offline_push_basic(void) {
    miku_offline_push_t *op = miku_offline_push_create(MK_PUSH_PROVIDER_DUMMY);
    mk_assert_not_null(op);

    mk_assert_str_eq("DUMMY", miku_offline_push_provider_name(MK_PUSH_PROVIDER_DUMMY));
    mk_assert_str_eq("FCM", miku_offline_push_provider_name(MK_PUSH_PROVIDER_FCM));
    mk_assert_str_eq("Getui", miku_offline_push_provider_name(MK_PUSH_PROVIDER_GETUI));
    mk_assert_str_eq("JPUSH", miku_offline_push_provider_name(MK_PUSH_PROVIDER_JPUSH));

    mk_assert_int_eq(0, miku_offline_push_send(op, "u1", 1, "Hello", "World", NULL));

    miku_offline_push_destroy(op);
}

void test_offline_push_token(void) {
    miku_offline_push_t *op = miku_offline_push_create(MK_PUSH_PROVIDER_FCM);
    mk_assert_not_null(op);

    mk_assert_int_eq(0, miku_offline_push_set_token(op, "u1", 1, "fcm_token_123"));
    mk_assert_int_eq(0, miku_offline_push_send(op, "u1", 1, "Title", "Body", "url"));
    mk_assert_int_eq(-1, miku_offline_push_send(op, "u1", 2, "Title", "Body", NULL));

    mk_assert_int_eq(0, miku_offline_push_del_token(op, "u1", 1));
    mk_assert_int_eq(-1, miku_offline_push_send(op, "u1", 1, "Title", "Body", NULL));

    miku_offline_push_destroy(op);
}

void test_cron_tasks_basic(void) {
    miku_cron_tasks_t *ct = miku_cron_tasks_create();
    mk_assert_not_null(ct);

    mk_assert_int_eq(0, miku_cron_delete_expired_msgs(ct, 30));
    mk_assert_int_eq(0, miku_cron_clear_user_msgs(ct, "u1"));
    mk_assert_int_eq(0, miku_cron_clear_s3_files(ct, 7));

    {
        int64_t t2 = miku_cron_get_last_run(ct, "deleteMsg");
        mk_assert_long_eq(1, (long)(t2 > 0));
    }

    mk_assert_long_eq(0, (long)miku_cron_get_last_run(ct, "nonexistent"));

    miku_cron_tasks_destroy(ct);
}

void test_ws_subscription_basic(void) {
    miku_ws_sub_t *sub = miku_ws_sub_create();
    mk_assert_not_null(sub);

    mk_assert_int_eq(0, miku_ws_sub_subscribe(sub, "u1", "u2"));
    mk_assert_int_eq(1, miku_ws_sub_is_subscribed(sub, "u1", "u2"));
    mk_assert_int_eq(0, miku_ws_sub_is_subscribed(sub, "u1", "u3"));
    mk_assert_int_eq(0, miku_ws_sub_is_subscribed(sub, "u2", "u1"));

    mk_assert_int_eq(0, miku_ws_sub_subscribe(sub, "u1", "u3"));
    mk_assert_int_eq(0, miku_ws_sub_subscribe(sub, "u3", "u1"));

    char *uids[64];
    int n = miku_ws_sub_get_subscribers(sub, "u1", uids, 64);
    mk_assert_int_eq(1, n);

    n = miku_ws_sub_get_subscribers(sub, "u2", uids, 64);
    mk_assert_int_eq(1, n);

    mk_assert_int_eq(0, miku_ws_sub_unsubscribe(sub, "u1", "u2"));
    mk_assert_int_eq(0, miku_ws_sub_is_subscribed(sub, "u1", "u2"));

    mk_assert_int_eq(-1, miku_ws_sub_unsubscribe(sub, "u1", "u2"));

    miku_ws_sub_destroy(sub);
}

void test_gzip_roundtrip(void) {
    const char *data = "Hello, this is a test string for gzip compression in Miku IM Server!";
    size_t data_len = strlen(data);

    char compressed[4096];
    size_t comp_len = sizeof(compressed);
    mk_assert_int_eq(0, miku_gzip_compress(data, data_len, compressed, &comp_len, MK_GZIP_DEFAULT_LEVEL));

    char decompressed[4096];
    size_t decomp_len = sizeof(decompressed);
    mk_assert_int_eq(0, miku_gzip_decompress(compressed, comp_len, decompressed, &decomp_len));

    mk_assert_long_eq((long)data_len, (long)decomp_len);
    mk_assert_int_eq(0, memcmp(data, decompressed, data_len));
}

void test_gzip_detect_encoding(void) {
    mk_assert_int_eq(1, (int)miku_gzip_accepts_encoding("gzip, deflate"));
    mk_assert_int_eq(1, (int)miku_gzip_accepts_encoding("deflate, gzip"));
    mk_assert_int_eq(0, (int)miku_gzip_accepts_encoding("deflate"));
    mk_assert_int_eq(0, (int)miku_gzip_accepts_encoding(NULL));
}

void test_im_message_roundtrip(void) {
    miku_json_val_t *j = miku_json_create_object();
    miku_jss(j, "msgID", "m1");
    miku_jss(j, "clientMsgID", "cm1");
    miku_jss(j, "sendID", "user_a");
    miku_jss(j, "recvID", "user_b");
    miku_jss(j, "groupID", "g1");
    miku_jss(j, "conversationID", "conv_1");
    miku_ji(j, "contentType", 101);
    miku_ji(j, "conversationType", 1);
    miku_jss(j, "content", "hello world");
    miku_jss(j, "senderNickname", "Alice");
    miku_jss(j, "senderFaceURL", "https://face.url");

    miku_im_msg_t msg;
    mk_assert_int_eq(0, miku_im_msg_from_json(&msg, j));
    miku_json_destroy(j);

    mk_assert_str_eq("m1", msg.msg_id);
    mk_assert_str_eq("cm1", msg.client_msg_id);
    mk_assert_str_eq("user_a", msg.send_id);
    mk_assert_str_eq("user_b", msg.recv_id);
    mk_assert_str_eq("g1", msg.group_id);
    mk_assert_str_eq("conv_1", msg.conversation_id);
    mk_assert_int_eq(101, msg.content_type);
    mk_assert_int_eq(1, msg.conversation_type);
    mk_assert_str_eq("hello world", msg.content);
    mk_assert_str_eq("Alice", msg.sender_nickname);

    miku_json_val_t *out = miku_im_msg_to_json(&msg);
    mk_assert_not_null(out);
    mk_assert_str_eq("m1", miku_json_str(miku_json_get(out, "msgID")));
    mk_assert_str_eq("user_a", miku_json_str(miku_json_get(out, "sendID")));
    miku_json_destroy(out);
}

void test_im_message_validate(void) {
    miku_im_msg_t msg;
    miku_im_msg_init(&msg);
    mk_assert_int_eq(-1, miku_im_msg_validate(&msg));

    strncpy(msg.send_id, "user_a", sizeof(msg.send_id) - 1);
    strncpy(msg.recv_id, "user_b", sizeof(msg.recv_id) - 1);
    msg.content_type = MK_IM_MSG_TYPE_TEXT;
    msg.conversation_type = MK_IM_CONV_SINGLE;
    strncpy(msg.content, "hi", sizeof(msg.content) - 1);
    mk_assert_int_eq(0, miku_im_msg_validate(&msg));
}

void test_im_message_generate_id(void) {
    miku_im_msg_t msg;
    miku_im_msg_init(&msg);
    mk_assert_int_eq(0, (int)msg.msg_id[0]);
    miku_im_msg_generate_id(&msg);
    mk_assert_int_ne(0, (int)msg.msg_id[0]);
}

void test_im_ack_roundtrip(void) {
    miku_json_val_t *j = miku_json_create_object();
    miku_ji(j, "type", 1);
    miku_jss(j, "userID", "u1");
    miku_ji(j, "seq", 42);

    miku_im_ack_t ack;
    mk_assert_int_eq(0, miku_im_ack_from_json(&ack, j));
    miku_json_destroy(j);

    mk_assert_int_eq(1, ack.type);
    mk_assert_str_eq("u1", ack.user_id);
    mk_assert_long_eq(42, ack.seq);

    miku_json_val_t *out = miku_im_ack_to_json(&ack);
    mk_assert_not_null(out);
    miku_json_destroy(out);
}

void test_im_ws_hello_parse(void) {
    miku_json_val_t *j = miku_json_create_object();
    miku_ji(j, "type", 1);
    miku_jss(j, "userID", "user_x");
    miku_ji(j, "serverTime", 1000000);

    miku_im_ws_hello_t hello;
    mk_assert_int_eq(0, miku_im_ws_hello_from_json(&hello, j));
    miku_json_destroy(j);

    mk_assert_int_eq(1, hello.type);
    mk_assert_str_eq("user_x", hello.user_id);
    mk_assert_long_eq(1000000, hello.server_time);
}

void test_mt_pipeline_submit_flush(void) {
    miku_mt_pipeline_t *p = miku_mt_pipeline_create();
    mk_assert_not_null(p);
    mk_assert_int_eq(0, miku_mt_pipeline_pending(p));

    mt_redis_flush_count = 0;
    mt_mongo_flush_count = 0;

    miku_mt_pipeline_on_redis(p, mt_to_redis_cb, NULL);
    miku_mt_pipeline_on_mongo(p, mt_to_mongo_cb, NULL);

    miku_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.send_id, "u1", sizeof(msg.send_id) - 1);
    strncpy(msg.recv_id, "u2", sizeof(msg.recv_id) - 1);

    for (int i = 0; i < 5; i++) {
        msg.seq = miku_mt_pipeline_seq_next(p, "conv_1");
        mk_assert_long_eq(i + 1, msg.seq);
        miku_mt_pipeline_submit(p, &msg);
    }
    mk_assert_int_eq(5, miku_mt_pipeline_pending(p));

    miku_mt_pipeline_flush(p);
    mk_assert_int_eq(0, miku_mt_pipeline_pending(p));
    mk_assert_int_eq(5, mt_redis_flush_count);
    mk_assert_int_eq(5, mt_mongo_flush_count);

    miku_mt_pipeline_destroy(p);
}

void test_mt_pipeline_read_seq(void) {
    miku_mt_pipeline_t *p = miku_mt_pipeline_create();
    mk_assert_not_null(p);

    mk_assert_int_eq(0, miku_mt_pipeline_process_read_seq(p, "user1", "conv1", 10));
    mk_assert_long_eq(10, miku_mt_pipeline_get_read_seq(p, "user1", "conv1"));
    mk_assert_long_eq(0, miku_mt_pipeline_get_read_seq(p, "user1", "conv2"));
    mk_assert_long_eq(0, miku_mt_pipeline_get_read_seq(p, "user2", "conv1"));

    miku_mt_pipeline_process_read_seq(p, "user1", "conv1", 20);
    mk_assert_long_eq(20, miku_mt_pipeline_get_read_seq(p, "user1", "conv1"));

    miku_mt_pipeline_destroy(p);
}

void test_msg_store_stub(void) {
    miku_msg_store_t *s = miku_msg_store_create(NULL);
    mk_assert_not_null(s);

    char msg_id[64] = {0};
    int rc = miku_msg_store_insert(s, "conv_1", "user_a", 101, "hello", 1000000, msg_id, sizeof(msg_id));
    mk_assert_int_eq(0, rc);
    mk_assert_int_ne(0, (int)msg_id[0]);

    char *results = NULL;
    rc = miku_msg_store_find_by_conv(s, "conv_1", 0, 100, &results);
    mk_assert_int_eq(0, rc);
    mk_assert_not_null(results);
    mk_assert_str_eq("[]", results);
    free(results);

    char *one = NULL;
    rc = miku_msg_store_find_one(s, msg_id, &one);
    mk_assert_int_eq(0, rc);
    mk_assert_not_null(one);
    free(one);

    rc = miku_msg_store_update_status(s, msg_id, 2);
    mk_assert_int_eq(0, rc);

    rc = miku_msg_store_delete(s, msg_id);
    mk_assert_int_eq(0, rc);

    mk_assert_int_eq(-1, miku_msg_store_insert(s, NULL, "u", 1, "c", 0, NULL, 0));

    miku_msg_store_destroy(s);
}

void test_session_cache_stub(void) {
    miku_session_cache_t *c = miku_session_cache_create(NULL);
    mk_assert_not_null(c);

    mk_assert_int_eq(0, miku_session_set_token(c, "u1", "tok_abc", 1, 3600000));
    mk_assert_int_eq(0, miku_session_validate_token(c, "u1", "tok_abc"));
    mk_assert_int_eq(0, miku_session_remove_token(c, "u1", 1));
    mk_assert_int_eq(0, miku_session_remove_all(c, "u1"));

    mk_assert_int_eq(0, miku_session_set_online(c, "u1", 1, "127.0.0.1:10001"));
    char *platforms = NULL;
    mk_assert_int_eq(0, miku_session_get_online(c, "u1", &platforms));
    mk_assert_not_null(platforms);
    free(platforms);

    mk_assert_int_eq(0, miku_session_set_offline(c, "u1", 1));

    mk_assert_int_eq(-1, miku_session_set_token(c, NULL, "t", 1, 0));

    miku_session_cache_destroy(c);
}

void run_new_module_tests(void) {
    printf("\n── Miku New Module Tests ───────────────────\n\n");
    mk_run_test(test_ratelimit_basic);
    mk_run_test(test_ratelimit_reset);
    mk_run_test(test_webhook_fire);
    mk_run_test(test_webhook_event_names);
    mk_run_test(test_seq_basic);
    mk_run_test(test_seq_user_read);
    mk_run_test(test_incr_sync_basic);
    mk_run_test(test_incr_sync_changes);
    mk_run_test(test_offline_push_basic);
    mk_run_test(test_offline_push_token);
    mk_run_test(test_cron_tasks_basic);
    mk_run_test(test_ws_subscription_basic);
    mk_run_test(test_gzip_roundtrip);
    mk_run_test(test_gzip_detect_encoding);
    mk_run_test(test_im_message_roundtrip);
    mk_run_test(test_im_message_validate);
    mk_run_test(test_im_message_generate_id);
    mk_run_test(test_im_ack_roundtrip);
    mk_run_test(test_im_ws_hello_parse);
    mk_run_test(test_mt_pipeline_submit_flush);
    mk_run_test(test_mt_pipeline_read_seq);
    mk_run_test(test_msg_store_stub);
    mk_run_test(test_session_cache_stub);
}
