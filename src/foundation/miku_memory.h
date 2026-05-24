#ifndef MIKU_MEMORY_H
#define MIKU_MEMORY_H

#include "miku_common.h"
#include "miku_arena.h"
#include "miku_slab.h"

typedef struct {
    miku_arena_t *arena;
    miku_slab_t  *slab;
} miku_pool_t;

MIKU_API miku_pool_t *miku_pool_create(size_t arena_size, size_t slab_obj_size, size_t slab_cap);
MIKU_API void        *miku_pool_alloc(miku_pool_t *pool, size_t size);
MIKU_API void        *miku_pool_alloc_obj(miku_pool_t *pool);
MIKU_API void         miku_pool_free_obj(miku_pool_t *pool, void *obj);
MIKU_API void         miku_pool_reset(miku_pool_t *pool);
MIKU_API void         miku_pool_destroy(miku_pool_t *pool);

#endif
