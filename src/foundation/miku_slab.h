#ifndef MIKU_SLAB_H
#define MIKU_SLAB_H

#include "miku_common.h"

typedef struct miku_slab_s {
    void         **free_stack;
    size_t         free_count;
    size_t         total_count;
    size_t         obj_size;
    uint8_t       *memory;
    size_t         memory_size;
} miku_slab_t;

MIKU_API miku_slab_t *miku_slab_create(size_t obj_size, size_t capacity);
MIKU_API void        *miku_slab_alloc(miku_slab_t *slab);
MIKU_API void         miku_slab_free(miku_slab_t *slab, void *obj);
MIKU_API void         miku_slab_destroy(miku_slab_t *slab);
MIKU_API size_t       miku_slab_available(const miku_slab_t *slab);
MIKU_API bool         miku_slab_contains(const miku_slab_t *slab, const void *ptr);

#endif
