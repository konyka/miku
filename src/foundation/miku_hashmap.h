#ifndef MIKU_HASHMAP_H
#define MIKU_HASHMAP_H

#include "miku_common.h"

typedef struct miku_hashmap_s miku_hashmap_t;

MIKU_API miku_hashmap_t *miku_hashmap_create(size_t initial_cap, miku_free_fn val_free);
MIKU_API void            miku_hashmap_destroy(miku_hashmap_t *map);
MIKU_API int             miku_hashmap_put(miku_hashmap_t *map, const char *key, void *val);
MIKU_API void           *miku_hashmap_get(const miku_hashmap_t *map, const char *key);
MIKU_API int             miku_hashmap_del(miku_hashmap_t *map, const char *key);
MIKU_API size_t          miku_hashmap_size(const miku_hashmap_t *map);
MIKU_API void            miku_hashmap_foreach(const miku_hashmap_t *map,
                                               void (*fn)(const char *key, void *val, void *ctx),
                                               void *ctx);

#endif
