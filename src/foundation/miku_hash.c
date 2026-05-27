#include "miku_hash.h"

uint64_t miku_fnv1a_64(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
