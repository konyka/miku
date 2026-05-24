#ifndef MIKU_ARENA_H
#define MIKU_ARENA_H

#include "miku_common.h"

typedef struct miku_arena_block_s {
    uint8_t                   *base;
    size_t                      used;
    size_t                      capacity;
    struct miku_arena_block_s  *next;
} miku_arena_block_t;

typedef struct {
    miku_arena_block_t *first;
    miku_arena_block_t *current;
    size_t              total_used;
    size_t              total_capacity;
    size_t              default_block_size;
} miku_arena_t;

MIKU_API miku_arena_t *miku_arena_create(size_t initial_size);
MIKU_API void         *miku_arena_alloc(miku_arena_t *arena, size_t size);
MIKU_API void         *miku_arena_alloc_aligned(miku_arena_t *arena, size_t size, size_t alignment);
MIKU_API void          miku_arena_reset(miku_arena_t *arena);
MIKU_API void          miku_arena_destroy(miku_arena_t *arena);
MIKU_API size_t        miku_arena_used(const miku_arena_t *arena);
MIKU_API size_t        miku_arena_capacity(const miku_arena_t *arena);

#endif
