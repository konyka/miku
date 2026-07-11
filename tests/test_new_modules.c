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
#include "miku_auth.h"
#include "miku_user.h"
#include "miku_friend.h"
#include "miku_group.h"
#include "miku_conversation.h"
#include "miku_msg.h"
#include "miku_third.h"
#include "miku_api.h"
#include "miku_http_server.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

    miku_webhook_wait_idle(wh);
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

    miku_msg_store_t *store = miku_msg_store_create(NULL);
    mk_assert_not_null(store);
    miku_cron_tasks_set_msg_store(ct, store);

    char mid[64] = {0};
    int64_t old_ts = miku_timestamp_ms() - 40LL * 86400000LL;
    mk_assert_int_eq(0, miku_msg_store_insert(store, "c1", "u1", 101, "old", old_ts, 1, mid, sizeof(mid)));
    mk_assert_int_eq(0, miku_msg_store_insert(store, "c1", "u1", 101, "new",
                                               miku_timestamp_ms(), 2, NULL, 0));
    mk_assert_int_eq(2, miku_msg_store_count(store));

    mk_assert_int_eq(0, miku_cron_delete_expired_msgs(ct, 30));
    mk_assert_long_eq(1, (long)miku_cron_total_msgs_deleted(ct));
    mk_assert_int_eq(1, miku_msg_store_count(store));

    mk_assert_int_eq(0, miku_cron_clear_user_msgs(ct, "u1"));
    mk_assert_int_eq(0, miku_msg_store_count(store));
    mk_assert_long_eq(2, (long)miku_cron_total_msgs_deleted(ct));

    mk_assert_int_eq(0, miku_cron_clear_s3_files(ct, 7));

    {
        int64_t t2 = miku_cron_get_last_run(ct, "deleteMsg");
        mk_assert_long_eq(1, (long)(t2 > 0));
    }

    mk_assert_long_eq(0, (long)miku_cron_get_last_run(ct, "nonexistent"));

    miku_cron_tasks_destroy(ct);
    miku_msg_store_destroy(store);
}

static int g_sub_notify_count = 0;
static char g_sub_last_payload[256];

static void test_sub_notify_cb(const char *subscriber, const char *payload, size_t len, void *ctx) {
    (void)subscriber; (void)ctx; (void)len;
    g_sub_notify_count++;
    if (payload) {
        strncpy(g_sub_last_payload, payload, sizeof(g_sub_last_payload) - 1);
        g_sub_last_payload[sizeof(g_sub_last_payload) - 1] = '\0';
    }
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

    /* Re-subscribe u1→u2 and verify online/offline notify */
    mk_assert_int_eq(0, miku_ws_sub_subscribe(sub, "u1", "u2"));
    g_sub_notify_count = 0;
    g_sub_last_payload[0] = '\0';
    miku_ws_sub_set_notify(sub, test_sub_notify_cb, NULL);
    miku_ws_sub_user_online(sub, "u2", 1);
    mk_assert_int_eq(1, g_sub_notify_count);
    mk_assert(strstr(g_sub_last_payload, "online") != NULL);
    miku_ws_sub_user_offline(sub, "u2");
    mk_assert_int_eq(2, g_sub_notify_count);
    mk_assert(strstr(g_sub_last_payload, "offline") != NULL);

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
    int rc = miku_msg_store_insert(s, "conv_1", "user_a", 101, "hello", 1000000, 5, msg_id, sizeof(msg_id));
    mk_assert_int_eq(0, rc);
    mk_assert_int_ne(0, (int)msg_id[0]);
    mk_assert_int_eq(1, miku_msg_store_count(s));

    char *results = NULL;
    rc = miku_msg_store_find_by_conv(s, "conv_1", 0, 100, &results);
    mk_assert_int_eq(0, rc);
    mk_assert_not_null(results);
    mk_assert(strstr(results, msg_id) != NULL);
    mk_assert(strstr(results, "\"seq\":5") != NULL);
    free(results);

    results = NULL;
    rc = miku_msg_store_find_by_conv(s, "conv_1", 6, 100, &results);
    mk_assert_int_eq(0, rc);
    mk_assert_not_null(results);
    mk_assert_str_eq("[]", results);
    free(results);

    char *one = NULL;
    rc = miku_msg_store_find_one(s, msg_id, &one);
    mk_assert_int_eq(0, rc);
    mk_assert_not_null(one);
    mk_assert(strstr(one, "hello") != NULL);
    free(one);

    rc = miku_msg_store_update_status(s, msg_id, 2);
    mk_assert_int_eq(0, rc);

    mk_assert_int_eq(1, miku_msg_store_purge_older_than(s, 2000000));
    mk_assert_int_eq(0, miku_msg_store_count(s));

    rc = miku_msg_store_insert(s, "conv_1", "user_a", 101, "hi", miku_timestamp_ms(), 1, msg_id, sizeof(msg_id));
    mk_assert_int_eq(0, rc);
    rc = miku_msg_store_delete(s, msg_id);
    mk_assert_int_eq(0, rc);
    mk_assert_int_eq(0, miku_msg_store_count(s));

    mk_assert_int_eq(-1, miku_msg_store_insert(s, NULL, "u", 1, "c", 0, 0, NULL, 0));

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

/* ── RPC dispatch coverage tests ────────────────────── */

static void test_dispatch_no_404_helper(void *svc, const char **methods, int count,
                                        void (*handle_rpc)(void *, const char *, const miku_json_val_t *, miku_json_val_t *)) {
    for (int i = 0; i < count; i++) {
        miku_json_val_t *req = miku_json_create_object();
        miku_json_val_t *resp = miku_json_create_object();
        handle_rpc(svc, methods[i], req, resp);
        int64_t ec = miku_json_int(miku_json_get(resp, "errCode"));
        char msg[128];
        snprintf(msg, sizeof(msg), "method %s returned errCode %lld (expected != 404)", methods[i], (long long)ec);
        mk_assert_msg(ec != 404, msg);
    }
}

typedef void (*rpc_fn_generic)(void *, const char *, const miku_json_val_t *, miku_json_val_t *);

static void test_rpc_user_dispatch(void) {
    miku_user_service_t *svc = miku_user_service_create();
    const char *methods[] = {
        "registerUser", "getUserInfo", "updateUserInfo", "updateUserInfoEx",
        "getAllUsersUID", "getUsersOnlineTokenDetail",
        "addNotificationAccount", "updateNotificationAccount", "searchNotificationAccount",
        "setUserClientConfig", "getUserClientConfig", "delUserClientConfig", "pageUserClientConfig",
        "processUserCommand", "processUserCommandAdd", "processUserCommandDelete",
        "processUserCommandUpdate", "processUserCommandGet", "processUserCommandGetAll",
        "getGlobalRecvMessageOpt", "setGlobalRecvMessageOpt", "accountCheck"
    };
    test_dispatch_no_404_helper(svc, methods, 22,
        (rpc_fn_generic)miku_user_handle_rpc);
    miku_user_service_destroy(svc);
}

static void test_rpc_friend_dispatch(void) {
    miku_friend_service_t *svc = miku_friend_service_create();
    const char *methods[] = {
        "addFriend", "deleteFriend", "getFriendList", "isFriend",
        "addBlack", "removeBlack", "getBlackList",
        "getFriendApplyList", "getSelfApplyList", "getDesignatedFriendApply",
        "acceptFriendApply", "refuseFriendApply", "respondFriendApply",
        "importFriend", "syncFriend",
        "getDesignatedFriends", "getFriendIDs", "getFullFriendUserIDs",
        "getIncrementalFriends", "getIncrementalBlacks",
        "getSelfUnhandledApplyCount", "getSpecifiedBlacks", "getSpecifiedFriendsInfo",
        "setFriendRemark", "updateFriends"
    };
    test_dispatch_no_404_helper(svc, methods, 25,
        (rpc_fn_generic)miku_friend_handle_rpc);
    miku_friend_service_destroy(svc);
}

static void test_rpc_group_dispatch(void) {
    miku_group_service_t *svc = miku_group_service_create();
    const char *methods[] = {
        "createGroup", "joinGroup", "quitGroup", "dismissGroup",
        "getGroupInfo", "setGroupInfo", "setGroupInfoEx", "setGroupMemberInfo",
        "getGroupMemberList", "getGroupMemberUserID",
        "getJoinedGroupList", "getGroupApplicationList", "getGroupApplicantList",
        "acceptGroupApplication", "refuseGroupApplication",
        "kickGroupMember", "transferGroupOwner",
        "muteGroup", "cancelMuteGroup", "muteGroupMember", "cancelMuteGroupMember",
        "getFullGroupMemberUserIDs", "getFullJoinGroupIDs", "getGroupAbstractInfo",
        "getGroupApplicationUnhandledCount", "getGroupUsersReqApplicationList",
        "getGroups", "getIncrementalGroupMemberBatch", "getIncrementalGroupMembers",
        "getIncrementalJoinGroups", "getRecvGroupApplicationList",
        "getSpecifiedUserGroupRequestInfo", "getUserReqGroupApplicationList"
    };
    test_dispatch_no_404_helper(svc, methods, 33,
        (rpc_fn_generic)miku_group_handle_rpc);
    miku_group_service_destroy(svc);
}

static void test_rpc_conv_dispatch(void) {
    miku_conv_service_t *svc = miku_conv_service_create();
    const char *methods[] = {
        "getAllConversations", "getConversation", "setConversation",
        "setConversations", "deleteConversation",
        "getConversationList", "getConversations",
        "getTotalUnreadMsgCount",
        "setConversationMinSeq", "markConversationMessageAsRead",
        "clearConversationMsg", "pinConversation",
        "deleteConversations", "getFullConversationIDs",
        "getIncrementalConversation", "getNotNotifyConversationIDs",
        "getOwnerConversation", "getPinnedConversationIDs",
        "getSortedConversationList", "updateConversationsByUser"
    };
    test_dispatch_no_404_helper(svc, methods, 17,
        (rpc_fn_generic)miku_conv_handle_rpc);
    miku_conv_service_destroy(svc);
}

static void test_rpc_msg_dispatch(void) {
    miku_msg_service_t *svc = miku_msg_service_create();
    const char *methods[] = {
        "sendMsg", "getMsgByConv", "revokeMsg", "getServerTime",
        "getSendMsgStatus", "cleanUpMsg", "deleteMsg", "batchSendMsg",
        "markMsgAsRead", "getMsgBySeq",
        "send", "sendSimpleMsg", "sendBusinessNotification",
        "getMsg", "getNewestSeq", "pullMsgBySeq", "searchMsg",
        "markMsgsAsRead", "markConversationAsRead", "setConversationHasReadSeq",
        "getConversationsHasReadAndMaxSeq", "checkMsgIsSendSuccess",
        "clearConversationMsg", "userClearAllMsg",
        "deleteMsgPhysical", "deleteMsgPhysicalBySeq",
        "setMessageReactionExtensions", "getMessageListReactionExtensions",
        "addMessageReactionExtensions", "deleteMessageReactionExtensions"
    };
    test_dispatch_no_404_helper(svc, methods, 30,
        (rpc_fn_generic)miku_msg_handle_rpc);
    miku_msg_service_destroy(svc);
}

static void test_rpc_third_dispatch(void) {
    miku_third_service_t *svc = miku_third_service_create();
    const char *methods[] = {
        "getUploadToken", "getDownloadURL", "accessURL", "deleteObject",
        "initiateMultipartUpload", "completeMultipartUpload",
        "getUploadInfo", "getObjectInfo", "getSignalInvitationInfo",
        "authSign", "completeFormData", "deleteLogs", "fcmUpdateToken",
        "getPrometheus", "initiateFormData", "partLimit", "partSize",
        "searchLogs", "setAppBadge", "uploadLogs"
    };
    test_dispatch_no_404_helper(svc, methods, 20,
        (rpc_fn_generic)miku_third_handle_rpc);
    miku_third_service_destroy(svc);
}

static void test_msg_send_get_search_delete(void) {
    miku_msg_service_t *svc = miku_msg_service_create();

    miku_json_val_t *send_req = miku_json_create_object();
    miku_json_object_set(send_req, "sendID", miku_json_create_str("alice"));
    miku_json_object_set(send_req, "recvID", miku_json_create_str("bob"));
    miku_json_object_set(send_req, "content", miku_json_create_str("hello world"));
    miku_json_object_set(send_req, "msgType", miku_json_create_int(101));
    miku_json_object_set(send_req, "clientMsgID", miku_json_create_str("c_msg_001"));
    miku_json_val_t *send_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "send", send_req, send_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(send_resp, "errCode")));
    const char *smid = miku_json_str(miku_json_get(send_resp, "serverMsgID"));
    mk_assert_not_null(smid);
    mk_assert(strlen(smid) > 0);
    int64_t seq1 = miku_json_int(miku_json_get(send_resp, "seq"));
    mk_assert(seq1 > 0);

    miku_json_val_t *send2_req = miku_json_create_object();
    miku_json_object_set(send2_req, "sendID", miku_json_create_str("alice"));
    miku_json_object_set(send2_req, "recvID", miku_json_create_str("bob"));
    miku_json_object_set(send2_req, "content", miku_json_create_str("second message here"));
    miku_json_object_set(send2_req, "msgType", miku_json_create_int(101));
    miku_json_object_set(send2_req, "clientMsgID", miku_json_create_str("c_msg_002"));
    miku_json_val_t *send2_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "send", send2_req, send2_resp);
    int64_t seq2 = miku_json_int(miku_json_get(send2_resp, "seq"));
    mk_assert(seq2 > seq1);

    miku_json_val_t *get_req = miku_json_create_object();
    miku_json_object_set(get_req, "serverMsgID", miku_json_create_str(smid));
    miku_json_val_t *get_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "getMsg", get_req, get_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(get_resp, "errCode")));
    miku_json_val_t *data = miku_json_get(get_resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(1, (int)miku_json_size(data));

    miku_json_val_t *search_req = miku_json_create_object();
    miku_json_object_set(search_req, "keyword", miku_json_create_str("second"));
    miku_json_val_t *search_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "searchMsg", search_req, search_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(search_resp, "errCode")));
    data = miku_json_get(search_resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(1, (int)miku_json_size(data));

    miku_json_val_t *del_req = miku_json_create_object();
    miku_json_object_set(del_req, "clientMsgID", miku_json_create_str("c_msg_001"));
    miku_json_val_t *del_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "deleteMsgPhysical", del_req, del_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(del_resp, "errCode")));

    miku_json_val_t *get2_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "getMsg", get_req, get2_resp);
    data = miku_json_get(get2_resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(0, (int)miku_json_size(data));

    miku_json_val_t *newest_resp = miku_json_create_object();
    miku_json_val_t *empty = miku_json_create_object();
    miku_msg_handle_rpc(svc, "getNewestSeq", empty, newest_resp);
    mk_assert_int_eq(seq2, miku_json_int(miku_json_get(newest_resp, "seq")));

    miku_json_destroy(send_req); miku_json_destroy(send_resp);
    miku_json_destroy(send2_req); miku_json_destroy(send2_resp);
    miku_json_destroy(get_req); miku_json_destroy(get_resp);
    miku_json_destroy(get2_resp);
    miku_json_destroy(search_req); miku_json_destroy(search_resp);
    miku_json_destroy(del_req); miku_json_destroy(del_resp);
    miku_json_destroy(newest_resp); miku_json_destroy(empty);
    miku_msg_service_destroy(svc);
}

static void test_friend_remark_update_flow(void) {
    miku_friend_service_t *svc = miku_friend_service_create();

    miku_json_val_t *add_req = miku_json_create_object();
    miku_json_object_set(add_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(add_req, "friendUserID", miku_json_create_str("u2"));
    miku_json_val_t *add_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "addFriend", add_req, add_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(add_resp, "errCode")));

    miku_json_val_t *remark_req = miku_json_create_object();
    miku_json_object_set(remark_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(remark_req, "friendUserID", miku_json_create_str("u2"));
    miku_json_object_set(remark_req, "remark", miku_json_create_str("best buddy"));
    miku_json_val_t *remark_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "setFriendRemark", remark_req, remark_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(remark_resp, "errCode")));

    miku_json_val_t *list_req = miku_json_create_object();
    miku_json_object_set(list_req, "userID", miku_json_create_str("u1"));
    miku_json_val_t *list_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "getFriendList", list_req, list_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(list_resp, "errCode")));
    miku_json_val_t *data = miku_json_get(list_resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(1, (int)miku_json_size(data));
    miku_json_val_t *f0 = miku_json_at(data, 0);
    mk_assert_str_eq("best buddy", miku_json_str(miku_json_get(f0, "remark")));

    miku_json_val_t *respond_req = miku_json_create_object();
    miku_json_object_set(respond_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(respond_req, "fromUserID", miku_json_create_str("u3"));
    miku_json_val_t *respond_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "respondFriendApply", respond_req, respond_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(respond_resp, "errCode")));

    miku_json_val_t *list2_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "getFriendList", list_req, list2_resp);
    data = miku_json_get(list2_resp, "data");
    mk_assert_int_eq(2, (int)miku_json_size(data));

    miku_json_destroy(add_req); miku_json_destroy(add_resp);
    miku_json_destroy(remark_req); miku_json_destroy(remark_resp);
    miku_json_destroy(list_req); miku_json_destroy(list_resp);
    miku_json_destroy(list2_resp);
    miku_json_destroy(respond_req); miku_json_destroy(respond_resp);
    miku_friend_service_destroy(svc);
}

static void test_group_create_setinfo_member_flow(void) {
    miku_group_service_t *svc = miku_group_service_create();

    miku_json_val_t *create_req = miku_json_create_object();
    miku_json_object_set(create_req, "groupName", miku_json_create_str("chat room"));
    miku_json_object_set(create_req, "ownerUserID", miku_json_create_str("admin"));
    miku_json_val_t *create_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "createGroup", create_req, create_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(create_resp, "errCode")));
    const char *gid = miku_json_str(miku_json_get(create_resp, "data"));
    mk_assert_not_null(gid);
    mk_assert(strlen(gid) > 0);

    miku_json_val_t *setex_req = miku_json_create_object();
    miku_json_object_set(setex_req, "groupID", miku_json_create_str(gid));
    miku_json_object_set(setex_req, "groupName", miku_json_create_str("updated room"));
    miku_json_object_set(setex_req, "ex", miku_json_create_str("extra data"));
    miku_json_val_t *setex_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "setGroupInfoEx", setex_req, setex_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(setex_resp, "errCode")));

    miku_json_val_t *get_req = miku_json_create_object();
    miku_json_object_set(get_req, "groupID", miku_json_create_str(gid));
    miku_json_val_t *get_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "getGroupInfo", get_req, get_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(get_resp, "errCode")));
    miku_json_val_t *gdata = miku_json_get(get_resp, "data");
    mk_assert_not_null(gdata);
    mk_assert_str_eq("updated room", miku_json_str(miku_json_get(gdata, "groupName")));
    mk_assert_str_eq("extra data", miku_json_str(miku_json_get(gdata, "ex")));

    miku_json_val_t *join_req = miku_json_create_object();
    miku_json_object_set(join_req, "groupID", miku_json_create_str(gid));
    miku_json_object_set(join_req, "userID", miku_json_create_str("member1"));
    miku_json_val_t *join_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "joinGroup", join_req, join_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(join_resp, "errCode")));

    miku_json_val_t *members_req = miku_json_create_object();
    miku_json_object_set(members_req, "groupID", miku_json_create_str(gid));
    miku_json_val_t *members_resp = miku_json_create_object();
    miku_group_handle_rpc(svc, "getGroupMemberUserID", members_req, members_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(members_resp, "errCode")));
    miku_json_val_t *mdata = miku_json_get(members_resp, "data");
    mk_assert_not_null(mdata);
    mk_assert_int_eq(2, (int)miku_json_size(mdata));

    miku_json_destroy(create_req); miku_json_destroy(create_resp);
    miku_json_destroy(setex_req); miku_json_destroy(setex_resp);
    miku_json_destroy(get_req); miku_json_destroy(get_resp);
    miku_json_destroy(join_req); miku_json_destroy(join_resp);
    miku_json_destroy(members_req); miku_json_destroy(members_resp);
    miku_group_service_destroy(svc);
}

static void test_conv_set_update_flow(void) {
    miku_conv_service_t *svc = miku_conv_service_create();

    miku_json_val_t *set_req = miku_json_create_object();
    miku_json_object_set(set_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(set_req, "conversationID", miku_json_create_str("si_u1_u2"));
    miku_json_object_set(set_req, "conversationType", miku_json_create_int(1));
    miku_json_object_set(set_req, "ex", miku_json_create_str("initial ex"));
    miku_json_val_t *set_resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "setConversation", set_req, set_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(set_resp, "errCode")));

    miku_json_val_t *update_req = miku_json_create_object();
    miku_json_object_set(update_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(update_req, "conversationID", miku_json_create_str("si_u1_u2"));
    miku_json_object_set(update_req, "ex", miku_json_create_str("updated ex"));
    miku_json_val_t *update_resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "updateConversationsByUser", update_req, update_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(update_resp, "errCode")));

    miku_json_val_t *get_req = miku_json_create_object();
    miku_json_object_set(get_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(get_req, "conversationID", miku_json_create_str("si_u1_u2"));
    miku_json_val_t *get_resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "getConversation", get_req, get_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(get_resp, "errCode")));
    miku_json_val_t *cdata = miku_json_get(get_resp, "data");
    mk_assert_not_null(cdata);
    mk_assert_str_eq("updated ex", miku_json_str(miku_json_get(cdata, "ex")));

    miku_json_val_t *all_req = miku_json_create_object();
    miku_json_object_set(all_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_val_t *all_resp = miku_json_create_object();
    miku_conv_handle_rpc(svc, "getAllConversations", all_req, all_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(all_resp, "errCode")));
    miku_json_val_t *adata = miku_json_get(all_resp, "data");
    mk_assert_not_null(adata);
    mk_assert_int_eq(1, (int)miku_json_size(adata));

    miku_json_destroy(set_req); miku_json_destroy(set_resp);
    miku_json_destroy(update_req); miku_json_destroy(update_resp);
    miku_json_destroy(get_req); miku_json_destroy(get_resp);
    miku_json_destroy(all_req); miku_json_destroy(all_resp);
    miku_conv_service_destroy(svc);
}

static void test_msg_pull_by_seq_range(void) {
    miku_msg_service_t *svc = miku_msg_service_create();

    for (int i = 0; i < 5; i++) {
        miku_json_val_t *req = miku_json_create_object();
        char content[32];
        snprintf(content, sizeof(content), "msg_%d", i);
        miku_json_object_set(req, "sendID", miku_json_create_str("u1"));
        miku_json_object_set(req, "recvID", miku_json_create_str("u2"));
        miku_json_object_set(req, "content", miku_json_create_str(content));
        miku_json_object_set(req, "msgType", miku_json_create_int(101));
        char cmid[32];
        snprintf(cmid, sizeof(cmid), "c_%d", i);
        miku_json_object_set(req, "clientMsgID", miku_json_create_str(cmid));
        miku_json_val_t *resp = miku_json_create_object();
        miku_msg_handle_rpc(svc, "send", req, resp);
        mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(resp, "errCode")));
        miku_json_destroy(req); miku_json_destroy(resp);
    }

    miku_json_val_t *pull_req = miku_json_create_object();
    miku_json_object_set(pull_req, "beginSeq", miku_json_create_int(2));
    miku_json_object_set(pull_req, "endSeq", miku_json_create_int(4));
    miku_json_val_t *pull_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "pullMsgBySeq", pull_req, pull_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(pull_resp, "errCode")));
    miku_json_val_t *data = miku_json_get(pull_resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(3, (int)miku_json_size(data));

    miku_json_val_t *del_req = miku_json_create_object();
    miku_json_object_set(del_req, "seq", miku_json_create_int(3));
    miku_json_val_t *del_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "deleteMsgPhysicalBySeq", del_req, del_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(del_resp, "errCode")));

    miku_json_val_t *pull2_resp = miku_json_create_object();
    miku_msg_handle_rpc(svc, "pullMsgBySeq", pull_req, pull2_resp);
    data = miku_json_get(pull2_resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(2, (int)miku_json_size(data));

    miku_json_destroy(pull_req); miku_json_destroy(pull_resp);
    miku_json_destroy(pull2_resp);
    miku_json_destroy(del_req); miku_json_destroy(del_resp);
    miku_msg_service_destroy(svc);
}

static void test_friend_designated_friends_flow(void) {
    miku_friend_service_t *svc = miku_friend_service_create();

    miku_json_val_t *add1 = miku_json_create_object();
    miku_json_object_set(add1, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(add1, "friendUserID", miku_json_create_str("f1"));
    miku_json_object_set(add1, "remark", miku_json_create_str("friend one"));
    miku_json_val_t *r1 = miku_json_create_object();
    miku_friend_handle_rpc(svc, "addFriend", add1, r1);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r1, "errCode")));

    miku_json_val_t *add2 = miku_json_create_object();
    miku_json_object_set(add2, "ownerUserID", miku_json_create_str("u1"));
    miku_json_object_set(add2, "friendUserID", miku_json_create_str("f2"));
    miku_json_object_set(add2, "remark", miku_json_create_str("friend two"));
    miku_json_val_t *r2 = miku_json_create_object();
    miku_friend_handle_rpc(svc, "addFriend", add2, r2);

    miku_json_val_t *desig_req = miku_json_create_object();
    miku_json_object_set(desig_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_val_t *ids = miku_json_create_array();
    miku_json_array_push(ids, miku_json_create_str("f1"));
    miku_json_array_push(ids, miku_json_create_str("f2"));
    miku_json_array_push(ids, miku_json_create_str("f99"));
    miku_json_object_set(desig_req, "friendUserIDs", ids);
    miku_json_val_t *desig_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "getDesignatedFriends", desig_req, desig_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(desig_resp, "errCode")));
    miku_json_val_t *data = miku_json_get(desig_resp, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(2, (int)miku_json_size(data));

    miku_json_val_t *info_req = miku_json_create_object();
    miku_json_object_set(info_req, "ownerUserID", miku_json_create_str("u1"));
    miku_json_val_t *ids2 = miku_json_create_array();
    miku_json_array_push(ids2, miku_json_create_str("f1"));
    miku_json_object_set(info_req, "friendUserIDs", ids2);
    miku_json_val_t *info_resp = miku_json_create_object();
    miku_friend_handle_rpc(svc, "getSpecifiedFriendsInfo", info_req, info_resp);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(info_resp, "errCode")));
    data = miku_json_get(info_resp, "data");
    mk_assert_int_eq(1, (int)miku_json_size(data));
    miku_json_val_t *fi = miku_json_at(data, 0);
    mk_assert_str_eq("friend one", miku_json_str(miku_json_get(fi, "remark")));

    miku_json_destroy(add1); miku_json_destroy(r1);
    miku_json_destroy(add2); miku_json_destroy(r2);
    miku_json_destroy(desig_req); miku_json_destroy(desig_resp);
    miku_json_destroy(info_req); miku_json_destroy(info_resp);
    miku_friend_service_destroy(svc);
}

static void *http_server_thread(void *arg) {
    miku_http_server_t *srv = (miku_http_server_t *)arg;
    miku_http_server_start(srv);
    return NULL;
}

static int http_post_to(int port, const char *path, const char *body, char *resp, int cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int retries = 5;
    while (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 && retries-- > 0) {
        usleep(50000);
    }
    if (retries <= 0) { close(fd); return -1; }
    char req[8192];
    int len = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
        path, port, strlen(body), body);
    write(fd, req, (size_t)len);
    int total = 0;
    while (total < cap - 1) {
        ssize_t n = read(fd, resp + total, (size_t)(cap - total - 1));
        if (n <= 0) break;
        total += (int)n;
    }
    resp[total] = '\0';
    close(fd);
    return total;
}

static int http_get_to(int port, const char *path, char *resp, int cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int retries = 5;
    while (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 && retries-- > 0) {
        usleep(50000);
    }
    if (retries <= 0) { close(fd); return -1; }
    char req[1024];
    int len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n", path, port);
    write(fd, req, (size_t)len);
    int total = 0;
    while (total < cap - 1) {
        ssize_t n = read(fd, resp + total, (size_t)(cap - total - 1));
        if (n <= 0) break;
        total += (int)n;
    }
    resp[total] = '\0';
    close(fd);
    return total;
}

static char *extract_json_body(char *http_resp) {
    char *body = strstr(http_resp, "\r\n\r\n");
    return body ? body + 4 : http_resp;
}

static int http_post_with_token(int port, const char *path, const char *token,
                                 const char *body, char *resp, int cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int retries = 5;
    while (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 && retries-- > 0) {
        usleep(50000);
    }
    if (retries <= 0) { close(fd); return -1; }
    char req[8192];
    int len;
    if (token && token[0]) {
        len = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/json\r\n"
            "token: %s\r\nContent-Length: %zu\r\n\r\n%s",
            path, port, token, strlen(body), body);
    } else {
        len = snprintf(req, sizeof(req),
            "POST %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
            path, port, strlen(body), body);
    }
    write(fd, req, (size_t)len);
    int total = 0;
    while (total < cap - 1) {
        ssize_t n = read(fd, resp + total, (size_t)(cap - total - 1));
        if (n <= 0) break;
        total += (int)n;
    }
    resp[total] = '\0';
    close(fd);
    return total;
}

static void test_http_e2e_user_register_and_get(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19777);
    mk_assert_not_null(srv);
    miku_api_register_routes(srv, ctx);

    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_resp[8192] = {0};
    http_post_to(19777, "/auth/user_token",
        "{\"userID\":\"http_u1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_resp, sizeof(auth_resp));
    char *auth_body = extract_json_body(auth_resp);
    miku_json_val_t *auth_r = miku_json_parse_str(auth_body);
    const char *token = auth_r ? miku_json_str(miku_json_get(auth_r, "token")) : NULL;
    mk_assert(token && token[0]);

    char resp[8192] = {0};
    int n = http_post_with_token(19777, "/user/register", token,
        "{\"userID\":\"http_u1\",\"nickname\":\"HTTP Alice\"}", resp, sizeof(resp));
    mk_assert(n > 0);
    char *body = extract_json_body(resp);
    miku_json_val_t *r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(r);

    char resp2[8192] = {0};
    n = http_post_with_token(19777, "/user/get_users_info", token,
        "{\"userIDList\":[\"http_u1\"]}", resp2, sizeof(resp2));
    mk_assert(n > 0);
    body = extract_json_body(resp2);
    r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(r);
    if (auth_r) miku_json_destroy(auth_r);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_http_e2e_auth_token(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19778);
    mk_assert_not_null(srv);
    miku_api_register_routes(srv, ctx);

    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char resp[8192] = {0};
    int n = http_post_to(19778, "/auth/user_token",
        "{\"userID\":\"alice\",\"secret\":\"openIM123\",\"platformID\":1}", resp, sizeof(resp));
    mk_assert(n > 0);
    char *body = extract_json_body(resp);
    miku_json_val_t *r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    const char *token = miku_json_str(miku_json_get(r, "token"));
    mk_assert_not_null(token);
    mk_assert(strncmp(token, "miku|alice|", 11) == 0);
    mk_assert(strchr(token + 5, '|') != NULL);
    miku_json_destroy(r);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_http_e2e_friend_flow(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19779);
    mk_assert_not_null(srv);
    miku_api_register_routes(srv, ctx);

    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_resp[8192] = {0};
    http_post_to(19779, "/auth/user_token",
        "{\"userID\":\"u1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_resp, sizeof(auth_resp));
    miku_json_val_t *ar = miku_json_parse_str(extract_json_body(auth_resp));
    const char *token = ar ? miku_json_str(miku_json_get(ar, "token")) : NULL;
    mk_assert(token && token[0]);

    char resp[8192] = {0};
    http_post_with_token(19779, "/friend/add", token,
        "{\"ownerUserID\":\"u1\",\"friendUserID\":\"u2\",\"remark\":\"test remark\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    miku_json_val_t *r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(r);

    char resp2[8192] = {0};
    http_post_with_token(19779, "/friend/get_friend_list", token,
        "{\"userID\":\"u1\"}", resp2, sizeof(resp2));
    body = extract_json_body(resp2);
    r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_val_t *data = miku_json_get(r, "data");
    mk_assert_not_null(data);
    mk_assert_int_eq(1, (int)miku_json_size(data));
    mk_assert_str_eq("test remark", miku_json_str(miku_json_get(miku_json_at(data, 0), "remark")));
    miku_json_destroy(r);
    if (ar) miku_json_destroy(ar);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_http_e2e_msg_send_and_search(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19780);
    mk_assert_not_null(srv);
    miku_api_register_routes(srv, ctx);

    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_resp[8192] = {0};
    http_post_to(19780, "/auth/user_token",
        "{\"userID\":\"s1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_resp, sizeof(auth_resp));
    miku_json_val_t *ar = miku_json_parse_str(extract_json_body(auth_resp));
    const char *token = ar ? miku_json_str(miku_json_get(ar, "token")) : NULL;
    mk_assert(token && token[0]);

    char resp[8192] = {0};
    http_post_with_token(19780, "/msg/send_msg", token,
        "{\"sendID\":\"s1\",\"recvID\":\"r1\",\"content\":\"e2e test message\",\"msgType\":101,\"clientMsgID\":\"cm_e2e_1\"}",
        resp, sizeof(resp));
    char *body = extract_json_body(resp);
    miku_json_val_t *r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    const char *smid = miku_json_str(miku_json_get(r, "serverMsgID"));
    mk_assert_not_null(smid);
    mk_assert(strlen(smid) > 0);
    miku_json_destroy(r);

    char resp2[8192] = {0};
    http_post_with_token(19780, "/msg/search_msg", token,
        "{\"keyword\":\"e2e test\"}", resp2, sizeof(resp2));
    body = extract_json_body(resp2);
    r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_val_t *data = miku_json_get(r, "data");
    mk_assert_not_null(data);
     mk_assert_int_eq(1, (int)miku_json_size(data));
     miku_json_destroy(r);
     if (ar) miku_json_destroy(ar);

     miku_http_server_stop(srv);
     pthread_join(tid, NULL);
     miku_http_server_destroy(srv);
     miku_api_ctx_destroy(ctx);
}

static int wh_trigger_count;
static miku_webhook_event_t wh_trigger_last_event;
static char wh_trigger_last_payload[1024];

static void wh_trigger_handler(miku_webhook_event_t event, const char *payload, void *ctx) {
    (void)ctx;
    wh_trigger_count++;
    wh_trigger_last_event = event;
    strncpy(wh_trigger_last_payload, payload ? payload : "", sizeof(wh_trigger_last_payload) - 1);
}

static void test_webhook_msg_send_trigger(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    wh_trigger_count = 0;
    wh_trigger_last_payload[0] = '\0';
    miku_webhook_set_handler(ctx->webhook, wh_trigger_handler, NULL);

    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19781);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth1[8192] = {0};
    http_post_to(19781, "/auth/user_token",
        "{\"userID\":\"wh_s1\",\"secret\":\"openIM123\",\"platformID\":1}", auth1, sizeof(auth1));
    miku_json_val_t *ar1 = miku_json_parse_str(extract_json_body(auth1));
    const char *tok1 = ar1 ? miku_json_str(miku_json_get(ar1, "token")) : NULL;

    char resp[8192] = {0};
    http_post_with_token(19781, "/msg/send_msg", tok1,
        "{\"sendID\":\"wh_s1\",\"recvID\":\"wh_r1\",\"content\":\"wh test\",\"msgType\":101,\"clientMsgID\":\"wh_c1\"}",
        resp, sizeof(resp));

    mk_assert_int_eq(1, wh_trigger_count);
    mk_assert(wh_trigger_last_event == MK_WH_AFTER_SEND_MSG);
    mk_assert(strstr(wh_trigger_last_payload, "msgSent") != NULL);
    mk_assert(strstr(wh_trigger_last_payload, "wh_s1") != NULL);
    if (ar1) miku_json_destroy(ar1);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_webhook_friend_add_trigger(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    wh_trigger_count = 0;
    wh_trigger_last_payload[0] = '\0';
    miku_webhook_set_handler(ctx->webhook, wh_trigger_handler, NULL);

    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19782);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth2[8192] = {0};
    http_post_to(19782, "/auth/user_token",
        "{\"userID\":\"wh_u1\",\"secret\":\"openIM123\",\"platformID\":1}", auth2, sizeof(auth2));
    miku_json_val_t *ar2 = miku_json_parse_str(extract_json_body(auth2));
    const char *tok2 = ar2 ? miku_json_str(miku_json_get(ar2, "token")) : NULL;

    char resp[8192] = {0};
    http_post_with_token(19782, "/friend/add", tok2,
        "{\"ownerUserID\":\"wh_u1\",\"friendUserID\":\"wh_u2\"}", resp, sizeof(resp));

    mk_assert_int_eq(1, wh_trigger_count);
    mk_assert(wh_trigger_last_event == MK_WH_AFTER_ADD_FRIEND);
    mk_assert(strstr(wh_trigger_last_payload, "friendAdded") != NULL);
    mk_assert(strstr(wh_trigger_last_payload, "wh_u1") != NULL);
    mk_assert(strstr(wh_trigger_last_payload, "wh_u2") != NULL);
    if (ar2) miku_json_destroy(ar2);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_webhook_group_create_trigger(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    wh_trigger_count = 0;
    wh_trigger_last_payload[0] = '\0';
    miku_webhook_set_handler(ctx->webhook, wh_trigger_handler, NULL);

    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19783);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth3[8192] = {0};
    http_post_to(19783, "/auth/user_token",
        "{\"userID\":\"wh_owner\",\"secret\":\"openIM123\",\"platformID\":1}", auth3, sizeof(auth3));
    miku_json_val_t *ar3 = miku_json_parse_str(extract_json_body(auth3));
    const char *tok3 = ar3 ? miku_json_str(miku_json_get(ar3, "token")) : NULL;

    char resp[8192] = {0};
    int n = http_post_with_token(19783, "/group/create", tok3,
        "{\"groupName\":\"wh group\",\"ownerUserID\":\"wh_owner\"}", resp, sizeof(resp));
    mk_assert(n > 0);
    char *body = extract_json_body(resp);
    miku_json_val_t *r = miku_json_parse_str(body);
    mk_assert_not_null(r);
    mk_assert_int_eq(0, (int)miku_json_int(miku_json_get(r, "errCode")));
    miku_json_destroy(r);

    mk_assert_int_eq(1, wh_trigger_count);
    mk_assert(wh_trigger_last_event == MK_WH_AFTER_CREATE_GROUP);
    mk_assert(strstr(wh_trigger_last_payload, "groupCreated") != NULL);
    mk_assert(strstr(wh_trigger_last_payload, "wh_owner") != NULL);
    if (ar3) miku_json_destroy(ar3);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_ratelimit_per_key(void) {
    miku_ratelimit_t *rl = miku_ratelimit_create(60000, 2);
    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "alice"));
    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "alice"));
    mk_assert_int_eq(0, miku_ratelimit_allow(rl, "alice"));
    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "bob"));
    mk_assert_int_eq(1, miku_ratelimit_allow(rl, "bob"));
    mk_assert_int_eq(0, miku_ratelimit_allow(rl, "bob"));
    miku_ratelimit_destroy(rl);
}

static void test_ratelimit_http_429(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_ratelimit_destroy(ctx->ratelimit);
    ctx->ratelimit = miku_ratelimit_create(60000, 2);

    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19784);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_rl[8192] = {0};
    http_post_to(19784, "/auth/user_token",
        "{\"userID\":\"rl_z\",\"secret\":\"openIM123\",\"platformID\":1}", auth_rl, sizeof(auth_rl));
    miku_json_val_t *ar_rl = miku_json_parse_str(extract_json_body(auth_rl));
    const char *rl_tok = ar_rl ? miku_json_str(miku_json_get(ar_rl, "token")) : NULL;

    for (int i = 0; i < 2; i++) {
        char resp[8192] = {0};
        http_post_with_token(19784, "/friend/add", rl_tok,
            "{\"ownerUserID\":\"rl_z\",\"friendUserID\":\"rl_b\"}", resp, sizeof(resp));
        mk_assert(resp[0] != '\0');
    }

    char resp3[8192] = {0};
    http_post_with_token(19784, "/friend/add", rl_tok,
        "{\"ownerUserID\":\"rl_z\",\"friendUserID\":\"rl_c\"}", resp3, sizeof(resp3));
    char *body3 = extract_json_body(resp3);
    mk_assert(body3 != NULL);
    if (!strstr(body3, "429")) {
        fprintf(stderr, "  expected 429 but got: %.200s\n", body3);
    }
    mk_assert(strstr(body3, "429") != NULL);

    char auth_other[8192] = {0};
    http_post_to(19784, "/auth/user_token",
        "{\"userID\":\"rl_other\",\"secret\":\"openIM123\",\"platformID\":1}", auth_other, sizeof(auth_other));
    miku_json_val_t *ar_other = miku_json_parse_str(extract_json_body(auth_other));
    const char *other_tok = ar_other ? miku_json_str(miku_json_get(ar_other, "token")) : NULL;

    char resp4[8192] = {0};
    http_post_with_token(19784, "/friend/add", other_tok,
        "{\"ownerUserID\":\"rl_other\",\"friendUserID\":\"rl_d\"}", resp4, sizeof(resp4));
    char *body4 = extract_json_body(resp4);
    mk_assert(body4 != NULL);
    mk_assert(strstr(body4, "429") == NULL);
    if (ar_rl) miku_json_destroy(ar_rl);
    if (ar_other) miku_json_destroy(ar_other);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_validation_missing_userID(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19790);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char resp[8192] = {0};
    http_post_to(19790, "/auth/user_token",
        "{\"secret\":\"pass\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":400") != NULL);
    mk_assert(strstr(body, "userID") != NULL);

    char resp2[8192] = {0};
    http_post_to(19790, "/auth/user_token",
        "{\"userID\":\"alice\",\"secret\":\"pass\"}", resp2, sizeof(resp2));
    body = extract_json_body(resp2);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":400") == NULL);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_validation_missing_friend_fields(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19791);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_v[8192] = {0};
    http_post_to(19791, "/auth/user_token",
        "{\"userID\":\"u1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_v, sizeof(auth_v));
    miku_json_val_t *arv = miku_json_parse_str(extract_json_body(auth_v));
    const char *v_tok = arv ? miku_json_str(miku_json_get(arv, "token")) : NULL;

    char resp[8192] = {0};
    http_post_with_token(19791, "/friend/add", v_tok,
        "{\"ownerUserID\":\"u1\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":400") != NULL);
    mk_assert(strstr(body, "friendUserID") != NULL);
    if (arv) miku_json_destroy(arv);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_validation_missing_send_fields(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19792);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_v2[8192] = {0};
    http_post_to(19792, "/auth/user_token",
        "{\"userID\":\"s1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_v2, sizeof(auth_v2));
    miku_json_val_t *arv2 = miku_json_parse_str(extract_json_body(auth_v2));
    const char *v2_tok = arv2 ? miku_json_str(miku_json_get(arv2, "token")) : NULL;

    char resp[8192] = {0};
    http_post_with_token(19792, "/msg/send_msg", v2_tok,
        "{\"sendID\":\"s1\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":400") != NULL);
    mk_assert(strstr(body, "recvID") != NULL);

    char resp2[8192] = {0};
    http_post_with_token(19792, "/msg/send_msg", v2_tok,
        "{\"sendID\":\"s1\",\"recvID\":\"r1\",\"content\":\"hi\"}", resp2, sizeof(resp2));
    body = extract_json_body(resp2);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":400") == NULL);
    if (arv2) miku_json_destroy(arv2);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_validation_valid_request_passes(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19793);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_v3[8192] = {0};
    http_post_to(19793, "/auth/user_token",
        "{\"userID\":\"g1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_v3, sizeof(auth_v3));
    miku_json_val_t *arv3 = miku_json_parse_str(extract_json_body(auth_v3));
    const char *v3_tok = arv3 ? miku_json_str(miku_json_get(arv3, "token")) : NULL;

    char resp[8192] = {0};
    http_post_with_token(19793, "/group/create", v3_tok,
        "{\"ownerUserID\":\"g1\",\"groupName\":\"test group\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":400") == NULL);
    if (arv3) miku_json_destroy(arv3);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_token_auth_required(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19794);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char resp[8192] = {0};
    http_post_to(19794, "/friend/get_friend_list",
        "{\"userID\":\"u1\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":401") != NULL);
    mk_assert(strstr(body, "missing token") != NULL);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_token_invalid_rejected(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19795);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char resp[8192] = {0};
    http_post_with_token(19795, "/user/register", "invalid_token_xyz",
        "{\"userID\":\"tu1\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":401") != NULL);

    char resp2[8192] = {0};
    http_post_with_token(19795, "/user/register", "miku|tampered|1|9999999999|aaaa|00000000000000ff",
        "{\"userID\":\"tu2\"}", resp2, sizeof(resp2));
    body = extract_json_body(resp2);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":401") != NULL);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_token_valid_passes(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19796);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_r[8192] = {0};
    http_post_to(19796, "/auth/user_token",
        "{\"userID\":\"tp1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_r, sizeof(auth_r));
    miku_json_val_t *ar = miku_json_parse_str(extract_json_body(auth_r));
    const char *token = ar ? miku_json_str(miku_json_get(ar, "token")) : NULL;
    mk_assert(token && strncmp(token, "miku|tp1|", 9) == 0);
    mk_assert(strchr(token + 5, '|') != NULL);

    char resp[8192] = {0};
    http_post_with_token(19796, "/user/register", token,
        "{\"userID\":\"tp1\",\"nickname\":\"token test\"}", resp, sizeof(resp));
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":401") == NULL);
    if (ar) miku_json_destroy(ar);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_admin_health(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19797);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char resp[8192] = {0};
    int n = http_get_to(19797, "/admin/health", resp, sizeof(resp));
    mk_assert(n > 0);
    mk_assert(strstr(resp, "200") != NULL);
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"status\":0") != NULL || strstr(body, "\"message\":\"ok\"") != NULL);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_admin_stats(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19798);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char auth_resp[8192] = {0};
    http_post_to(19798, "/auth/user_token",
        "{\"userID\":\"admin1\",\"secret\":\"openIM123\",\"platformID\":1}", auth_resp, sizeof(auth_resp));
    miku_json_val_t *ar = miku_json_parse_str(extract_json_body(auth_resp));
    const char *token = ar ? miku_json_str(miku_json_get(ar, "token")) : NULL;
    mk_assert(token && token[0]);

    char resp[8192] = {0};
    int n = http_post_with_token(19798, "/admin/stats", token, "{}", resp, sizeof(resp));
    mk_assert(n > 0);
    mk_assert(strstr(resp, "200") != NULL);
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "\"errCode\":0") != NULL);
    mk_assert(strstr(body, "uptimeMs") != NULL);
    if (ar) miku_json_destroy(ar);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_admin_metrics(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19799);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char resp[8192] = {0};
    int n = http_get_to(19799, "/admin/metrics", resp, sizeof(resp));
    mk_assert(n > 0);
    mk_assert(strstr(resp, "200") != NULL);
    mk_assert(strstr(resp, "miku_requests_total") != NULL);
    mk_assert(strstr(resp, "# TYPE") != NULL);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

static void test_version_endpoint(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19800);
    miku_api_register_routes(srv, ctx);
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, srv);
    usleep(200000);

    char resp[8192] = {0};
    int n = http_get_to(19800, "/version", resp, sizeof(resp));
    mk_assert(n > 0);
    mk_assert(strstr(resp, "200") != NULL);
    char *body = extract_json_body(resp);
    mk_assert(body != NULL);
    mk_assert(strstr(body, "version") != NULL);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
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
    mk_run_test(test_rpc_user_dispatch);
    mk_run_test(test_rpc_friend_dispatch);
    mk_run_test(test_rpc_group_dispatch);
    mk_run_test(test_rpc_conv_dispatch);
    mk_run_test(test_rpc_msg_dispatch);
    mk_run_test(test_rpc_third_dispatch);

    mk_run_test(test_msg_send_get_search_delete);
    mk_run_test(test_friend_remark_update_flow);
    mk_run_test(test_group_create_setinfo_member_flow);
    mk_run_test(test_conv_set_update_flow);
    mk_run_test(test_msg_pull_by_seq_range);
    mk_run_test(test_friend_designated_friends_flow);

    mk_run_test(test_http_e2e_user_register_and_get);
    mk_run_test(test_http_e2e_auth_token);
    mk_run_test(test_http_e2e_friend_flow);
    mk_run_test(test_http_e2e_msg_send_and_search);

    mk_run_test(test_webhook_msg_send_trigger);
    mk_run_test(test_webhook_friend_add_trigger);
    mk_run_test(test_webhook_group_create_trigger);

    mk_run_test(test_ratelimit_per_key);
    mk_run_test(test_ratelimit_http_429);

    mk_run_test(test_validation_missing_userID);
    mk_run_test(test_validation_missing_friend_fields);
    mk_run_test(test_validation_missing_send_fields);
    mk_run_test(test_validation_valid_request_passes);

    mk_run_test(test_token_auth_required);
    mk_run_test(test_token_invalid_rejected);
    mk_run_test(test_token_valid_passes);

    mk_run_test(test_admin_health);
    mk_run_test(test_admin_stats);
    mk_run_test(test_admin_metrics);
    mk_run_test(test_version_endpoint);
}
