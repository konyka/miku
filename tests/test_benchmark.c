#include "miku_test.h"
#include "miku_common.h"
#include "miku_json.h"
#include "miku_hashmap.h"
#include "miku_arena.h"
#include "miku_cache.h"
#include "miku_msgtransfer.h"
#include "miku_push.h"
#include "miku_crontask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t bench_now(void) { return miku_timestamp_ms(); }

static void bench_json_parse(void) {
    const char *sample = "{\"userID\":\"u123456789\",\"nickname\":\"BenchmarkUser\","
                         "\"gender\":1,\"phone\":\"+1234567890\",\"email\":\"bench@test.com\","
                         "\"birth\":946684800,\"ex\":\"extended_data_here\"}";
    int64_t start = bench_now();
    int count = 0;
    while (bench_now() - start < 1000) {
        miku_json_val_t *j = miku_json_parse_str(sample);
        miku_json_destroy(j);
        count++;
    }
    printf("  JSON parse:      %d ops/sec\n", count);
    mk_assert(count > 0);
}

static void bench_json_stringify(void) {
    miku_json_val_t *obj = miku_json_create_object();
    miku_json_object_set(obj, "key1", miku_json_create_str("value1"));
    miku_json_object_set(obj, "key2", miku_json_create_int(12345));
    miku_json_object_set(obj, "key3", miku_json_create_bool(true));
    miku_json_object_set(obj, "key4", miku_json_create_str("value4"));
    miku_json_object_set(obj, "key5", miku_json_create_int(67890));

    int64_t start = bench_now();
    int count = 0;
    while (bench_now() - start < 1000) {
        miku_string_t *s = miku_json_stringify(obj);
        miku_str_destroy(s);
        count++;
    }
    printf("  JSON stringify:  %d ops/sec\n", count);
    miku_json_destroy(obj);
    mk_assert(count > 0);
}

static void bench_hashmap(void) {
    miku_hashmap_t *hm = miku_hashmap_create(1024, NULL);
    int64_t start = bench_now();
    int count = 0;
    while (bench_now() - start < 1000) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", count % 1000);
        char val[32];
        snprintf(val, sizeof(val), "val_%d", count % 1000);
        miku_hashmap_put(hm, key, strdup(val));
        count++;
    }
    printf("  HashMap put:     %d ops/sec\n", count);
    miku_hashmap_destroy(hm);
    mk_assert(count > 0);
}

static void bench_cache(void) {
    miku_cache_t *c = miku_cache_create(10000, NULL);
    int64_t start = bench_now();
    int count = 0;
    while (bench_now() - start < 1000) {
        char key[32];
        snprintf(key, sizeof(key), "ck_%d", count % 5000);
        char val[32];
        snprintf(val, sizeof(val), "cv_%d", count % 5000);
        miku_cache_put(c, key, strdup(val), 60000);
        miku_cache_get(c, key);
        count++;
    }
    printf("  Cache set+get:   %d ops/sec\n", count);
    miku_cache_destroy(c);
    mk_assert(count > 0);
}

static void bench_msgtransfer_queue(void) {
    miku_msgtransfer_t *mt = miku_msgtransfer_create();
    miku_msgtransfer_start(mt);
    miku_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.send_id, "s1", sizeof(m.send_id) - 1);
    strncpy(m.content, "bench", sizeof(m.content) - 1);

    int64_t start = bench_now();
    int count = 0;
    while (bench_now() - start < 1000) {
        miku_msgtransfer_enqueue(mt, &m);
        count++;
    }
    printf("  MsgTransfer enq: %d ops/sec\n", count);
    mk_assert(count > 0);

    start = bench_now();
    int dequeued = 0;
    miku_msg_t out;
    while (miku_msgtransfer_dequeue(mt, &out) == 0) dequeued++;
    int64_t elapsed = bench_now() - start;
    if (elapsed == 0) elapsed = 1;
    printf("  MsgTransfer deq: %d ops/sec (%d items)\n", dequeued * 1000 / (int)elapsed, dequeued);

    miku_msgtransfer_stop(mt);
    miku_msgtransfer_destroy(mt);
}

void run_benchmarks(void) {
    printf("\n── Miku Benchmarks (1s each) ──────────────\n\n");
    mk_run_test(bench_json_parse);
    mk_run_test(bench_json_stringify);
    mk_run_test(bench_hashmap);
    mk_run_test(bench_cache);
    mk_run_test(bench_msgtransfer_queue);
    printf("\n");
}
