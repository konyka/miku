#ifndef MIKU_HASH_H
#define MIKU_HASH_H

#include "miku_common.h"
#include <stdint.h>
#include <string.h>

MIKU_API uint64_t miku_fnv1a_64(const void *data, size_t len);

#endif
