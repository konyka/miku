#include "miku_slab.h"
#include <stdlib.h>
#include <string.h>

miku_slab_t *miku_slab_create(size_t obj_size, size_t capacity) {
    if (obj_size < sizeof(void *)) obj_size = sizeof(void *);
    obj_size = MIKU_ROUND_UP(obj_size, sizeof(void *));

    miku_slab_t *s = (miku_slab_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->obj_size = obj_size;
    s->total_count = capacity;
    s->memory_size = obj_size * capacity;
    s->memory = (uint8_t *)calloc(capacity, obj_size);
    if (!s->memory) { free(s); return NULL; }

    s->free_stack = (void **)malloc(capacity * sizeof(void *));
    if (!s->free_stack) { free(s->memory); free(s); return NULL; }

    for (size_t i = 0; i < capacity; i++) {
        s->free_stack[i] = s->memory + i * obj_size;
    }
    s->free_count = capacity;
    return s;
}

void *miku_slab_alloc(miku_slab_t *slab) {
    if (!slab || slab->free_count == 0) return NULL;
    return slab->free_stack[--slab->free_count];
}

void miku_slab_free(miku_slab_t *slab, void *obj) {
    if (!slab || !obj) return;
    if (!miku_slab_contains(slab, obj)) return;
    slab->free_stack[slab->free_count++] = obj;
}

void miku_slab_destroy(miku_slab_t *slab) {
    if (!slab) return;
    free(slab->free_stack);
    free(slab->memory);
    free(slab);
}

size_t miku_slab_available(const miku_slab_t *slab) {
    return slab ? slab->free_count : 0;
}

bool miku_slab_contains(const miku_slab_t *slab, const void *ptr) {
    if (!slab || !ptr) return false;
    const uint8_t *p = (const uint8_t *)ptr;
    return p >= slab->memory && p < slab->memory + slab->memory_size;
}
