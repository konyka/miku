#include "miku_test.h"
#include "miku_ratelimit.h"
#include "miku_webhook.h"
#include "miku_seq.h"
#include "miku_incr_sync.h"
#include "miku_offline_push.h"
#include "miku_gzip.h"
#include "miku_cron_tasks.h"
#include "miku_ws_subscription.h"
#include <stdlib.h>
#include <string.h>

static int wh_received = 0;
static miku_webhook_event_t wh_last_event = 0;

static void wh_handler(miku_webhook_event_t event, const char *payload, void *ctx) {
    (void)payload; (void)ctx;
    wh_received++;
    wh_last_event = event;
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
}
