#ifndef MIKU_CRC32_H
#define MIKU_CRC32_H

#include "miku_common.h"

MIKU_API uint32_t miku_crc32(const uint8_t *data, size_t len);
MIKU_API uint32_t miku_crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#endif
