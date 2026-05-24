#ifndef MIKU_UUID_H
#define MIKU_UUID_H

#include "miku_common.h"

MIKU_API void miku_uuid_generate(char out[37]);
MIKU_API void miku_uuid_generate_bytes(uint8_t out[16]);

#endif
