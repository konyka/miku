#include "miku_cache.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

static void unlink_entry(miku_cache_t *cache, miku_cache_entry_t *e) {
    e->prev->next = e->next;
    e->next->prev = e->prev;
}

static void push_front(miku_cache_t *cache, miku_cache_entry_t *e) {
    e->next = cache->head.next;
    e->prev = &cache->head;
    cache->head.next->prev = e;
    cache->head.next = e;
}

static void move_to_front(miku_cache_t *cache, miku_cache_entry_t *e) {
    unlink_entry(cache, e);
    push_front(cache, e);
}

static void free_entry(miku_cache_t *cache, miku_cache_entry_t *e) {
    if (!e || e == &cache->head || e == &cache->tail) return;
    if (cache->val_free && e->val) cache->val_free(e->val);
    free(e->key);
    free(e);
}

miku_cache_t *miku_cache_create(size_t capacity, miku_cache_free_fn val_free) {
    miku_cache_t *c = (miku_cache_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->map = miku_hashmap_create(capacity > 0 ? capacity : 64, NULL);
    if (!c->map) { free(c); return NULL; }
    c->capacity = capacity > 0 ? capacity : 1024;
    c->val_free = val_free;
    c->head.next = &c->tail;
    c->tail.prev = &c->head;
    return c;
}

void miku_cache_destroy(miku_cache_t *cache) {
    if (!cache) return;
    miku_cache_clear(cache);
    miku_hashmap_destroy(cache->map);
    free(cache);
}

void *miku_cache_get(miku_cache_t *cache, const char *key) {
    if (!cache || !key) return NULL;
    miku_cache_entry_t *e = (miku_cache_entry_t *)miku_hashmap_get(cache->map, key);
    if (!e) return NULL;
    if (e->expire_ms > 0 && e->expire_ms < miku_timestamp_ms()) {
        unlink_entry(cache, e);
        miku_hashmap_del(cache->map, key);
        free_entry(cache, e);
        cache->size--;
        return NULL;
    }
    move_to_front(cache, e);
    return e->val;
}

static void evict_lru(miku_cache_t *cache) {
    while (cache->size >= cache->capacity) {
        miku_cache_entry_t *victim = cache->tail.prev;
        if (victim == &cache->head) break;
        unlink_entry(cache, victim);
        miku_hashmap_del(cache->map, victim->key);
        free_entry(cache, victim);
        cache->size--;
    }
}

int miku_cache_put(miku_cache_t *cache, const char *key, void *val, int64_t ttl_ms) {
    if (!cache || !key) return -1;

    miku_cache_entry_t *existing = (miku_cache_entry_t *)miku_hashmap_get(cache->map, key);
    if (existing) {
        if (cache->val_free && existing->val) cache->val_free(existing->val);
        existing->val = val;
        existing->expire_ms = ttl_ms > 0 ? miku_timestamp_ms() + ttl_ms : 0;
        move_to_front(cache, existing);
        return 0;
    }

    evict_lru(cache);

    miku_cache_entry_t *e = (miku_cache_entry_t *)calloc(1, sizeof(*e));
    if (!e) return -1;
    e->key = strdup(key);
    e->val = val;
    e->expire_ms = ttl_ms > 0 ? miku_timestamp_ms() + ttl_ms : 0;
    push_front(cache, e);
    miku_hashmap_put(cache->map, key, e);
    cache->size++;
    return 0;
}

int miku_cache_put_permanent(miku_cache_t *cache, const char *key, void *val) {
    return miku_cache_put(cache, key, val, 0);
}

bool miku_cache_del(miku_cache_t *cache, const char *key) {
    if (!cache || !key) return false;
    miku_cache_entry_t *e = (miku_cache_entry_t *)miku_hashmap_get(cache->map, key);
    if (!e) return false;
    unlink_entry(cache, e);
    miku_hashmap_del(cache->map, key);
    e->val = NULL;
    free_entry(cache, e);
    cache->size--;
    return true;
}

size_t miku_cache_size(const miku_cache_t *cache) {
    return cache ? cache->size : 0;
}

size_t miku_cache_evict_expired(miku_cache_t *cache) {
    if (!cache) return 0;
    int64_t now = miku_timestamp_ms();
    size_t evicted = 0;
    miku_cache_entry_t *e = cache->tail.prev;
    while (e != &cache->head) {
        miku_cache_entry_t *prev = e->prev;
        if (e->expire_ms > 0 && e->expire_ms < now) {
            unlink_entry(cache, e);
            miku_hashmap_del(cache->map, e->key);
            free_entry(cache, e);
            cache->size--;
            evicted++;
        }
        e = prev;
    }
    return evicted;
}

void miku_cache_clear(miku_cache_t *cache) {
    if (!cache) return;
    miku_cache_entry_t *e = cache->head.next;
    while (e != &cache->tail) {
        miku_cache_entry_t *next = e->next;
        free_entry(cache, e);
        e = next;
    }
    miku_hashmap_destroy(cache->map);
    cache->map = miku_hashmap_create(cache->capacity, NULL);
    cache->head.next = &cache->tail;
    cache->tail.prev = &cache->head;
    cache->size = 0;
}
