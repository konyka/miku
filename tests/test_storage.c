#include "miku_test.h"
#include "miku_cache.h"
#include "miku_discovery.h"
#include "miku_msg_store.h"
#include <stdlib.h>
#include <string.h>

void test_cache_basic(void) {
    miku_cache_t *c = miku_cache_create(4, NULL);
    mk_assert_not_null(c);
    mk_assert_int_eq(0, (int)miku_cache_size(c));

    miku_cache_put_permanent(c, "a", (void *)1L);
    miku_cache_put_permanent(c, "b", (void *)2L);
    miku_cache_put_permanent(c, "c", (void *)3L);
    mk_assert_int_eq(3, (int)miku_cache_size(c));

    mk_assert_ptr_eq((void *)1L, miku_cache_get(c, "a"));
    mk_assert_ptr_eq((void *)2L, miku_cache_get(c, "b"));
    mk_assert_ptr_eq((void *)3L, miku_cache_get(c, "c"));
    mk_assert_null(miku_cache_get(c, "nonexistent"));

    miku_cache_destroy(c);
}

void test_cache_lru_eviction(void) {
    miku_cache_t *c = miku_cache_create(3, NULL);
    mk_assert_not_null(c);

    miku_cache_put_permanent(c, "a", (void *)1L);
    miku_cache_put_permanent(c, "b", (void *)2L);
    miku_cache_put_permanent(c, "c", (void *)3L);
    mk_assert_int_eq(3, (int)miku_cache_size(c));

    miku_cache_put_permanent(c, "d", (void *)4L);
    mk_assert_int_eq(3, (int)miku_cache_size(c));

    mk_assert_null(miku_cache_get(c, "a"));
    mk_assert_ptr_eq((void *)2L, miku_cache_get(c, "b"));
    mk_assert_ptr_eq((void *)3L, miku_cache_get(c, "c"));
    mk_assert_ptr_eq((void *)4L, miku_cache_get(c, "d"));

    miku_cache_destroy(c);
}

void test_cache_lru_access_order(void) {
    miku_cache_t *c = miku_cache_create(3, NULL);
    mk_assert_not_null(c);

    miku_cache_put_permanent(c, "a", (void *)1L);
    miku_cache_put_permanent(c, "b", (void *)2L);
    miku_cache_put_permanent(c, "c", (void *)3L);

    miku_cache_get(c, "a");
    miku_cache_put_permanent(c, "d", (void *)4L);

    mk_assert_ptr_eq((void *)1L, miku_cache_get(c, "a"));
    mk_assert_null(miku_cache_get(c, "b"));

    miku_cache_destroy(c);
}

void test_cache_ttl_expiry(void) {
    miku_cache_t *c = miku_cache_create(10, NULL);
    mk_assert_not_null(c);

    miku_cache_put(c, "short", (void *)1L, 50);
    miku_cache_put(c, "long", (void *)2L, 60000);

    mk_assert_ptr_eq((void *)1L, miku_cache_get(c, "short"));
    mk_assert_ptr_eq((void *)2L, miku_cache_get(c, "long"));

    usleep(100000);

    mk_assert_null(miku_cache_get(c, "short"));
    mk_assert_ptr_eq((void *)2L, miku_cache_get(c, "long"));

    miku_cache_destroy(c);
}

void test_cache_evict_expired(void) {
    miku_cache_t *c = miku_cache_create(10, NULL);
    mk_assert_not_null(c);

    miku_cache_put(c, "exp1", (void *)1L, 50);
    miku_cache_put(c, "exp2", (void *)2L, 50);
    miku_cache_put_permanent(c, "perm", (void *)3L);
    mk_assert_int_eq(3, (int)miku_cache_size(c));

    usleep(100000);

    size_t evicted = miku_cache_evict_expired(c);
    mk_assert_int_eq(2, (int)evicted);
    mk_assert_int_eq(1, (int)miku_cache_size(c));
    mk_assert_ptr_eq((void *)3L, miku_cache_get(c, "perm"));

    miku_cache_destroy(c);
}

void test_cache_delete(void) {
    miku_cache_t *c = miku_cache_create(10, NULL);
    miku_cache_put_permanent(c, "key", (void *)42L);
    mk_assert_int_eq(1, (int)miku_cache_size(c));

    mk_assert_true(miku_cache_del(c, "key"));
    mk_assert_int_eq(0, (int)miku_cache_size(c));
    mk_assert_null(miku_cache_get(c, "key"));
    mk_assert_false(miku_cache_del(c, "key"));

    miku_cache_destroy(c);
}

void test_cache_clear(void) {
    miku_cache_t *c = miku_cache_create(10, NULL);
    for (int i = 0; i < 8; i++) {
        char key[8];
        snprintf(key, sizeof(key), "k%d", i);
        miku_cache_put_permanent(c, key, (void *)(long)(i + 1));
    }
    mk_assert_int_eq(8, (int)miku_cache_size(c));
    miku_cache_clear(c);
    mk_assert_int_eq(0, (int)miku_cache_size(c));
    miku_cache_destroy(c);
}

void test_discovery_register_resolve(void) {
    miku_discovery_t *d = miku_discovery_create("http://127.0.0.1:2379");
    mk_assert_not_null(d);

    mk_assert_int_eq(0, miku_discovery_register(d, "rpc-auth", "10.0.0.1", 10100, 60));
    mk_assert_int_eq(0, miku_discovery_register(d, "rpc-user", "10.0.0.2", 10110, 60));
    mk_assert_int_eq(0, miku_discovery_register(d, "rpc-auth", "10.0.0.3", 10100, 60));

    miku_service_entry_t entries[8];
    int count = miku_discovery_resolve(d, "rpc-auth", entries, 8);
    mk_assert_int_eq(1, count);
    mk_assert_str_eq("10.0.0.3", entries[0].host);
    mk_assert_int_eq(10100, entries[0].port);

    count = miku_discovery_resolve(d, "rpc-user", entries, 8);
    mk_assert_int_eq(1, count);
    mk_assert_str_eq("10.0.0.2", entries[0].host);

    count = miku_discovery_resolve(d, "nonexistent", entries, 8);
    mk_assert_int_eq(0, count);

    miku_discovery_destroy(d);
}

void test_discovery_deregister(void) {
    miku_discovery_t *d = miku_discovery_create("http://127.0.0.1:2379");
    mk_assert_not_null(d);

    miku_discovery_register(d, "svc1", "host1", 8080, 30);
    miku_discovery_register(d, "svc2", "host2", 8081, 30);

    mk_assert_int_eq(0, miku_discovery_deregister(d, "svc1"));

    miku_service_entry_t entries[4];
    mk_assert_int_eq(0, miku_discovery_resolve(d, "svc1", entries, 4));
    mk_assert_int_eq(1, miku_discovery_resolve(d, "svc2", entries, 4));

    mk_assert_int_eq(-1, miku_discovery_deregister(d, "nonexistent"));
    miku_discovery_destroy(d);
}

static int g_callback_fires;
static int g_last_slot;
static int g_last_count;

static void g_on_overwrite(int slot, int total, void *ctx) {
    (void)ctx;
    g_callback_fires++;
    g_last_slot = slot;
    g_last_count = total;
}

void test_msg_store_overwrite_callback(void) {
    miku_msg_store_t *s = miku_msg_store_create(NULL);
    mk_assert_not_null(s);
    g_callback_fires = 0;
    g_last_slot = -1;
    g_last_count = 0;
    /* Set the callback BEFORE filling — the eviction only happens once
     * the 8192-slot ring is saturated, so the first 8192 inserts must not
     * fire it. */
    miku_msg_store_set_overwrite_cb(s, NULL, NULL);
    /* Fill the ring to capacity; 8192 inserts without firing. */
    char id[64];
    for (int i = 0; i < 8192; i++) {
        snprintf(id, sizeof(id), "k_%d", i);
        int rc = miku_msg_store_insert(s, "conv1", id, 1, "x", i, i, id, sizeof(id));
        mk_assert_int_eq(0, rc);
    }
    mk_assert_int_eq(0, g_callback_fires);
    /* Now install the callback and trigger one more insert. The ring is
     * full, so this insert must overwrite slot 0 and fire the callback. */
    miku_msg_store_set_overwrite_cb(s, g_on_overwrite, NULL);
    snprintf(id, sizeof(id), "k_%d", 8192);
    int rc2 = miku_msg_store_insert(s, "conv1", id, 1, "x", 8192, 8192, id, sizeof(id));
    mk_assert_int_eq(0, rc2);
    mk_assert_int_eq(1, g_callback_fires);
    mk_assert_int_eq(0, g_last_slot);
    mk_assert_int_eq(1, g_last_count);
    /* A second insert triggers a second callback with total=2. */
    snprintf(id, sizeof(id), "k_%d", 8193);
    miku_msg_store_insert(s, "conv1", id, 1, "x", 8193, 8193, id, sizeof(id));
    mk_assert_int_eq(2, g_callback_fires);
    mk_assert_int_eq(2, g_last_count);
    /* Clear callback and verify no further notifications. */
    miku_msg_store_set_overwrite_cb(s, NULL, NULL);
    miku_msg_store_insert(s, "conv1", "k_clear", 1, "x", 9999, 9999, id, sizeof(id));
    mk_assert_int_eq(2, g_callback_fires);
    miku_msg_store_destroy(s);
}

void run_storage_tests(void) {
    printf("── Miku Storage Tests ───────────────────\n\n");

    mk_run_test(test_cache_basic);
    mk_run_test(test_cache_lru_eviction);
    mk_run_test(test_cache_lru_access_order);
    mk_run_test(test_cache_ttl_expiry);
    mk_run_test(test_cache_evict_expired);
    mk_run_test(test_cache_delete);
    mk_run_test(test_cache_clear);
    mk_run_test(test_discovery_register_resolve);
    mk_run_test(test_discovery_deregister);
    mk_run_test(test_msg_store_overwrite_callback);
}
