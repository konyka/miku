#include "miku_memory.h"
#include <stdlib.h>

miku_pool_t *miku_pool_create(size_t arena_size, size_t slab_obj_size, size_t slab_cap) {
    miku_pool_t *p = (miku_pool_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->arena = miku_arena_create(arena_size);
    if (!p->arena) { free(p); return NULL; }
    if (slab_obj_size > 0 && slab_cap > 0) {
        p->slab = miku_slab_create(slab_obj_size, slab_cap);
        if (!p->slab) { miku_arena_destroy(p->arena); free(p); return NULL; }
    }
    return p;
}

void *miku_pool_alloc(miku_pool_t *pool, size_t size) {
    return pool ? miku_arena_alloc(pool->arena, size) : NULL;
}

void *miku_pool_alloc_obj(miku_pool_t *pool) {
    return (pool && pool->slab) ? miku_slab_alloc(pool->slab) : NULL;
}

void miku_pool_free_obj(miku_pool_t *pool, void *obj) {
    if (pool && pool->slab) miku_slab_free(pool->slab, obj);
}

void miku_pool_reset(miku_pool_t *pool) {
    if (pool) miku_arena_reset(pool->arena);
}

void miku_pool_destroy(miku_pool_t *pool) {
    if (!pool) return;
    if (pool->slab) miku_slab_destroy(pool->slab);
    miku_arena_destroy(pool->arena);
    free(pool);
}
