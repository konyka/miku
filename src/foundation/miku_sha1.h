#ifndef MIKU_SHA1_H
#define MIKU_SHA1_H

#include "miku_common.h"

MIKU_API void miku_sha1(uint8_t digest[20], const uint8_t *data, size_t len);

#endif
