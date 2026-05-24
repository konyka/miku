#include "miku_arena.h"
#include <stdlib.h>
#include <string.h>

static miku_arena_block_t *arena_block_new(size_t size) {
    miku_arena_block_t *blk = (miku_arena_block_t *)malloc(sizeof(*blk));
    if (!blk) return NULL;
    blk->base = (uint8_t *)malloc(size);
    if (!blk->base) { free(blk); return NULL; }
    blk->used = 0;
    blk->capacity = size;
    blk->next = NULL;
    return blk;
}

miku_arena_t *miku_arena_create(size_t initial_size) {
    if (initial_size < 4096) initial_size = 4096;
    miku_arena_t *a = (miku_arena_t *)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->first = arena_block_new(initial_size);
    if (!a->first) { free(a); return NULL; }
    a->current = a->first;
    a->default_block_size = initial_size;
    a->total_capacity = initial_size;
    return a;
}

static void *arena_block_alloc(miku_arena_block_t *blk, size_t size) {
    size = MIKU_ROUND_UP(size, sizeof(void *));
    if (blk->used + size > blk->capacity) return NULL;
    void *ptr = blk->base + blk->used;
    blk->used += size;
    return ptr;
}

void *miku_arena_alloc(miku_arena_t *arena, size_t size) {
    if (!arena || size == 0) return NULL;
    void *ptr = arena_block_alloc(arena->current, size);
    if (ptr) {
        arena->total_used += size;
        return ptr;
    }
    size_t blk_size = arena->default_block_size;
    if (size > blk_size) blk_size = MIKU_ROUND_UP(size, 4096);
    miku_arena_block_t *blk = arena_block_new(blk_size);
    if (!blk) return NULL;
    blk->next = arena->first;
    arena->first = blk;
    arena->current = blk;
    arena->total_capacity += blk_size;
    ptr = arena_block_alloc(blk, size);
    arena->total_used += size;
    return ptr;
}

void *miku_arena_alloc_aligned(miku_arena_t *arena, size_t size, size_t alignment) {
    if (!arena || size == 0) return NULL;
    if (alignment == 0) alignment = sizeof(void *);
    size_t total = size + alignment - 1;
    void *raw = miku_arena_alloc(arena, total);
    if (!raw) return NULL;
    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return (void *)aligned;
}

void miku_arena_reset(miku_arena_t *arena) {
    if (!arena) return;
    miku_arena_block_t *blk = arena->first;
    while (blk) {
        blk->used = 0;
        blk = blk->next;
    }
    arena->current = arena->first;
    arena->total_used = 0;
}

void miku_arena_destroy(miku_arena_t *arena) {
    if (!arena) return;
    miku_arena_block_t *blk = arena->first;
    while (blk) {
        miku_arena_block_t *next = blk->next;
        free(blk->base);
        free(blk);
        blk = next;
    }
    free(arena);
}

size_t miku_arena_used(const miku_arena_t *arena) {
    return arena ? arena->total_used : 0;
}

size_t miku_arena_capacity(const miku_arena_t *arena) {
    return arena ? arena->total_capacity : 0;
}
