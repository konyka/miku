#include "miku_test.h"
#include "miku_arena.h"
#include "miku_slab.h"
#include "miku_hashmap.h"
#include "miku_string.h"
#include "miku_error.h"
#include "miku_config.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_stats.h"
#include "miku_json_util.h"
#include "miku_uuid.h"
#include "miku_crc32.h"
#include "miku_base64.h"
#include "miku_rbtree.h"
#include "miku_memory.h"

extern void run_runtime_tests(void);
extern void run_protocol_tests(void);
extern void run_storage_tests(void);
extern void run_service_tests(void);
extern void run_benchmarks(void);

void test_arena(void) {
    miku_arena_t *a = miku_arena_create(4096);
    mk_assert_not_null(a);

    void *p1 = miku_arena_alloc(a, 100);
    mk_assert_not_null(p1);
    memset(p1, 0xAA, 100);

    void *p2 = miku_arena_alloc(a, 200);
    mk_assert_not_null(p2);
    mk_assert_ptr_ne(p1, p2);

    mk_assert_int_eq(1, miku_arena_used(a) > 0);
    mk_assert_int_eq(1, miku_arena_capacity(a) >= 4096);

    miku_arena_reset(a);
    mk_assert_int_eq(0, (int)miku_arena_used(a));

    miku_arena_destroy(a);
}

void test_slab(void) {
    miku_slab_t *s = miku_slab_create(64, 16);
    mk_assert_not_null(s);
    mk_assert_int_eq(16, (int)miku_slab_available(s));

    void *objs[16];
    for (int i = 0; i < 16; i++) {
        objs[i] = miku_slab_alloc(s);
        mk_assert_not_null(objs[i]);
    }
    mk_assert_int_eq(0, (int)miku_slab_available(s));
    mk_assert_null(miku_slab_alloc(s));

    miku_slab_free(s, objs[0]);
    mk_assert_int_eq(1, (int)miku_slab_available(s));

    void *p = miku_slab_alloc(s);
    mk_assert_ptr_eq(objs[0], p);

    miku_slab_destroy(s);
}

void test_hashmap(void) {
    miku_hashmap_t *m = miku_hashmap_create(16, free);
    mk_assert_not_null(m);

    mk_assert_int_eq(0, (int)miku_hashmap_size(m));

    miku_hashmap_put(m, "key1", strdup("val1"));
    miku_hashmap_put(m, "key2", strdup("val2"));
    mk_assert_int_eq(2, (int)miku_hashmap_size(m));

    mk_assert_str_eq("val1", (const char *)miku_hashmap_get(m, "key1"));
    mk_assert_str_eq("val2", (const char *)miku_hashmap_get(m, "key2"));
    mk_assert_null(miku_hashmap_get(m, "nonexistent"));

    miku_hashmap_put(m, "key1", strdup("updated"));
    mk_assert_str_eq("updated", (const char *)miku_hashmap_get(m, "key1"));
    mk_assert_int_eq(2, (int)miku_hashmap_size(m));

    miku_hashmap_del(m, "key2");
    mk_assert_null(miku_hashmap_get(m, "key2"));
    mk_assert_int_eq(1, (int)miku_hashmap_size(m));

    miku_hashmap_destroy(m);
}

void test_string(void) {
    miku_string_t *s = miku_str_create("hello");
    mk_assert_not_null(s);
    mk_assert_str_eq("hello", s->data);
    mk_assert_int_eq(5, (int)s->len);

    miku_str_cat(s, " world");
    mk_assert_str_eq("hello world", s->data);

    miku_str_printf(s, " %d", 42);
    mk_assert_str_eq("hello world 42", s->data);

    miku_str_clear(s);
    mk_assert_int_eq(0, (int)s->len);

    miku_str_destroy(s);
}

void test_error(void) {
    miku_error_t ok = miku_error_ok();
    mk_assert_true(miku_error_is_ok(ok));
    mk_assert_int_eq(MK_OK, miku_error_code(ok));

    miku_error_t err = miku_error_new(MK_ERR_MEMORY, "alloc failed for %d bytes", 1024);
    mk_assert_false(miku_error_is_ok(err));
    mk_assert_int_eq(MK_ERR_MEMORY, miku_error_code(err));
    mk_assert_str_eq("alloc failed for 1024 bytes", miku_error_msg(err));
}

void test_config(void) {
    miku_config_t *cfg = miku_config_create();
    mk_assert_not_null(cfg);

    const char *yaml = "listenIP: 0.0.0.0\nport: 10002\ndebug: true\n";
    int rc = miku_config_load_string(cfg, yaml, strlen(yaml));
    mk_assert_int_eq(0, rc);

    mk_assert_str_eq("0.0.0.0", miku_config_get_str(cfg, "listenIP", "default"));
    mk_assert_int_eq(10002, (int)miku_config_get_int(cfg, "port", 0));

    miku_config_destroy(cfg);
}

void test_uuid(void) {
    char u1[37], u2[37];
    miku_uuid_generate(u1);
    miku_uuid_generate(u2);
    mk_assert_int_eq(36, (int)strlen(u1));
    mk_assert_int_eq(4, u1[14] - '0');
    mk_assert(u1[0] != u2[0] || u1[1] != u2[1]);
}

void test_crc32(void) {
    uint32_t crc = miku_crc32((const uint8_t *)"hello", 5);
    mk_assert(crc != 0);

    uint32_t crc2 = miku_crc32_update(0, (const uint8_t *)"hel", 3);
    crc2 = miku_crc32_update(crc2, (const uint8_t *)"lo", 2);
    mk_assert_int_eq((int)crc, (int)crc2);
}

void test_base64(void) {
    const char *input = "Hello, World!";
    char enc[64];
    size_t elen = miku_base64_encode((const uint8_t *)input, strlen(input), enc, sizeof(enc));
    mk_assert(elen > 0);
    mk_assert_str_eq("SGVsbG8sIFdvcmxkIQ==", enc);

    uint8_t dec[64];
    size_t dlen = miku_base64_decode(enc, strlen(enc), dec, sizeof(dec));
    mk_assert_int_eq(13, (int)dlen);
    mk_assert_int_eq(0, memcmp(input, dec, dlen));
}

void test_rbtree(void) {
    miku_rbtree_t tree;
    miku_rbtree_init(&tree, NULL);
    mk_assert_true(miku_rbtree_empty(&tree));
}

void test_memory_pool(void) {
    miku_pool_t *pool = miku_pool_create(4096, 64, 16);
    mk_assert_not_null(pool);

    void *p = miku_pool_alloc(pool, 128);
    mk_assert_not_null(p);

    void *obj = miku_pool_alloc_obj(pool);
    mk_assert_not_null(obj);

    miku_pool_free_obj(pool, obj);
    miku_pool_reset(pool);
    miku_pool_destroy(pool);
}

void test_config_nested(void) {
    miku_config_t *cfg = miku_config_create();
    mk_assert_not_null(cfg);

    const char *yaml =
        "listenIP: 0.0.0.0\n"
        "api:\n"
        "  port: 10002\n"
        "  prometheus:\n"
        "    enable: true\n"
        "rpc:\n"
        "  auth:\n"
        "    port: 10100\n"
        "  user:\n"
        "    port: 10110\n";
    int rc = miku_config_load_string(cfg, yaml, strlen(yaml));
    mk_assert_int_eq(0, rc);

    mk_assert_str_eq("0.0.0.0", miku_config_get_str(cfg, "listenIP", "FAIL"));
    mk_assert_str_eq("10002", miku_config_get_str(cfg, "api.port", "FAIL"));
    mk_assert_str_eq("true", miku_config_get_str(cfg, "api.prometheus.enable", "FAIL"));
    mk_assert_str_eq("10100", miku_config_get_str(cfg, "rpc.auth.port", "FAIL"));
    mk_assert_str_eq("10110", miku_config_get_str(cfg, "rpc.user.port", "FAIL"));
    mk_assert_int_eq(10002, (int)miku_config_get_int(cfg, "api.port", 0));
    mk_assert_int_eq(10100, (int)miku_config_get_int(cfg, "rpc.auth.port", 0));

    miku_config_destroy(cfg);
}

void test_config_defaults(void) {
    miku_config_t *cfg = miku_config_create();
    mk_assert_not_null(cfg);

    mk_assert_int_eq(42, (int)miku_config_get_int(cfg, "nonexistent", 42));
    mk_assert_str_eq("fallback", miku_config_get_str(cfg, "nonexistent", "fallback"));
    mk_assert_null(miku_config_get(cfg, "nonexistent"));

    miku_config_destroy(cfg);
}

void test_config_file_io(void) {
    const char *tmpfile = "/tmp/miku_test_config.yml";
    FILE *f = fopen(tmpfile, "w");
    mk_assert_not_null(f);
    fprintf(f, "key1: value1\nkey2: 42\nnested:\n  child: hello\n");
    fclose(f);

    miku_config_t *cfg = miku_config_create();
    mk_assert_int_eq(0, miku_config_load_file(cfg, tmpfile));
    mk_assert_str_eq("value1", miku_config_get_str(cfg, "key1", "FAIL"));
    mk_assert_int_eq(42, (int)miku_config_get_int(cfg, "key2", 0));
    mk_assert_str_eq("hello", miku_config_get_str(cfg, "nested.child", "FAIL"));

    miku_config_destroy(cfg);
    unlink(tmpfile);
}

void test_service_config(void) {
    miku_service_config_t sc;
    int rc = miku_service_config_load(&sc, "config");
    mk_assert_int_eq(0, rc);

    mk_assert_str_eq("0.0.0.0", sc.listen_ip);
    mk_assert_int_eq(10002, sc.api_port);
    mk_assert_int_eq(10001, sc.ws_port);
    mk_assert_int_eq(10100, sc.rpc_auth_port);
    mk_assert_int_eq(10110, sc.rpc_user_port);
    mk_assert_int_eq(10120, sc.rpc_friend_port);
    mk_assert_int_eq(10150, sc.rpc_group_port);
    mk_assert_int_eq(10180, sc.rpc_conversation_port);
    mk_assert_int_eq(10130, sc.rpc_msg_port);
    mk_assert_int_eq(10200, sc.rpc_third_port);
    mk_assert_str_eq("mongodb://localhost:27017", sc.mongo_uri);
    mk_assert_str_eq("miku", sc.mongo_database);
    mk_assert_int_eq(64, sc.mongo_pool_size);
}

void test_graceful_lifecycle(void) {
    miku_graceful_t g;
    miku_graceful_init(&g, 0);

    mk_assert_int_eq(1, miku_graceful_running(&g));

    g.running = 0;
    mk_assert_int_eq(0, miku_graceful_running(&g));

    miku_graceful_cleanup(&g);
}

void test_stats_basic(void) {
    miku_stats_t s;
    miku_stats_init(&s, "test-svc", 9999);

    mk_assert_str_eq("test-svc", s.service_name);
    mk_assert_int_eq(9999, s.port);
    mk_assert_int_eq(0, (int)miku_atomic_load(&s.requests_total));
    mk_assert_int_eq(0, (int)miku_atomic_load(&s.connections_active));

    miku_stats_request_inc(&s);
    miku_stats_request_inc(&s);
    miku_stats_fail_inc(&s);
    miku_stats_conn_open(&s);
    miku_stats_conn_open(&s);
    miku_stats_conn_close(&s);
    miku_stats_bytes_sent(&s, 1024);
    miku_stats_bytes_recv(&s, 512);

    mk_assert_int_eq(2, (int)miku_atomic_load(&s.requests_total));
    mk_assert_int_eq(1, (int)miku_atomic_load(&s.requests_failed));
    mk_assert_int_eq(1, (int)miku_atomic_load(&s.connections_active));
    mk_assert_int_eq(2, (int)miku_atomic_load(&s.connections_total));
    mk_assert_int_eq(1024, (int)miku_atomic_load(&s.bytes_sent));
    mk_assert_int_eq(512, (int)miku_atomic_load(&s.bytes_recv));
}

void test_stats_snapshot(void) {
    miku_stats_t s;
    miku_stats_init(&s, "snap-test", 8080);
    miku_stats_request_inc(&s);
    miku_stats_conn_open(&s);

    miku_stats_snapshot_t snap;
    miku_stats_snapshot(&s, &snap);

    mk_assert_str_eq("snap-test", snap.service_name);
    mk_assert_int_eq(8080, snap.port);
    mk_assert_int_eq(1, (int)snap.requests_total);
    mk_assert_int_eq(1, (int)snap.connections_active);
    mk_assert(snap.uptime_ms >= 0);
}

void test_stats_uptime(void) {
    miku_stats_t s;
    miku_stats_init(&s, "up", 1);
    int64_t up = miku_stats_uptime_ms(&s);
    mk_assert(up >= 0);
    mk_assert(up < 5000);
}

void test_json_util(void) {
    miku_json_val_t *o = miku_json_create_object();
    miku_ji(o, "code", 42);
    miku_jss(o, "msg", "hello");
    miku_jerr(o, 100, "bad request");

    mk_assert_int_eq(42, miku_json_int(miku_json_get(o, "code")));
    mk_assert_str_eq("hello", miku_json_str(miku_json_get(o, "msg")));
    mk_assert_int_eq(100, miku_json_int(miku_json_get(o, "errCode")));
    mk_assert_str_eq("bad request", miku_json_str(miku_json_get(o, "errMsg")));
    mk_assert_str_eq("", miku_json_str(miku_json_get(o, "errDmg")));
    miku_json_destroy(o);
}

int main(void) {
    printf("── Miku Foundation Tests ───────────────────\n\n");

    mk_run_test(test_arena);
    mk_run_test(test_slab);
    mk_run_test(test_hashmap);
    mk_run_test(test_string);
    mk_run_test(test_error);
    mk_run_test(test_config);
    mk_run_test(test_uuid);
    mk_run_test(test_crc32);
    mk_run_test(test_base64);
    mk_run_test(test_rbtree);
    mk_run_test(test_memory_pool);
    mk_run_test(test_config_nested);
    mk_run_test(test_config_defaults);
    mk_run_test(test_config_file_io);
    mk_run_test(test_service_config);
    mk_run_test(test_graceful_lifecycle);
    mk_run_test(test_stats_basic);
    mk_run_test(test_stats_snapshot);
    mk_run_test(test_stats_uptime);
    mk_run_test(test_json_util);

    run_runtime_tests();
    run_protocol_tests();
    run_storage_tests();
    run_service_tests();
    run_benchmarks();

    return mk_test_summary();
}
