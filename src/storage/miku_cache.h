#ifndef MIKU_CACHE_H
#define MIKU_CACHE_H

#include "miku_common.h"
#include "miku_hashmap.h"

typedef void (*miku_cache_free_fn)(void *val);

typedef struct miku_cache_s miku_cache_t;
typedef struct miku_cache_entry_s miku_cache_entry_t;

struct miku_cache_entry_s {
    char                   *key;
    void                   *val;
    int64_t                 expire_ms;
    miku_cache_entry_t     *prev;
    miku_cache_entry_t     *next;
};

struct miku_cache_s {
    miku_hashmap_t         *map;
    miku_cache_entry_t      head;
    miku_cache_entry_t      tail;
    size_t                  capacity;
    size_t                  size;
    miku_cache_free_fn      val_free;
};

MIKU_API miku_cache_t *miku_cache_create(size_t capacity, miku_cache_free_fn val_free);
MIKU_API void          miku_cache_destroy(miku_cache_t *cache);
MIKU_API void         *miku_cache_get(miku_cache_t *cache, const char *key);
MIKU_API int           miku_cache_put(miku_cache_t *cache, const char *key, void *val, int64_t ttl_ms);
MIKU_API int           miku_cache_put_permanent(miku_cache_t *cache, const char *key, void *val);
MIKU_API bool          miku_cache_del(miku_cache_t *cache, const char *key);
MIKU_API size_t        miku_cache_size(const miku_cache_t *cache);
MIKU_API size_t        miku_cache_evict_expired(miku_cache_t *cache);
MIKU_API void          miku_cache_clear(miku_cache_t *cache);

#endif
