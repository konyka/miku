#ifndef MIKU_GZIP_H
#define MIKU_GZIP_H

#include "miku_common.h"

#define MK_GZIP_NO_COMPRESSION   (-1)
#define MK_GZIP_DEFAULT_LEVEL    (0)
#define MK_GZIP_BEST_COMPRESSION (1)
#define MK_GZIP_BEST_SPEED       (2)

MIKU_API int  miku_gzip_compress(const char *in, size_t in_len,
                                   char *out, size_t *out_len, int level);
MIKU_API int  miku_gzip_decompress(const char *in, size_t in_len,
                                     char *out, size_t *out_len);
MIKU_API bool miku_gzip_accepts_encoding(const char *accept_encoding);

#endif
