#ifndef MIKU_BASE64_H
#define MIKU_BASE64_H

#include "miku_common.h"

MIKU_API size_t miku_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);
MIKU_API size_t miku_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

#endif
