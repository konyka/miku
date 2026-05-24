#ifndef MIKU_CHANNEL_H
#define MIKU_CHANNEL_H

#include "miku_common.h"

typedef struct miku_channel_s miku_channel_t;

MIKU_API miku_channel_t *miku_channel_create(size_t capacity);
MIKU_API int     miku_channel_send(miku_channel_t *ch, void *val);
MIKU_API void   *miku_channel_recv(miku_channel_t *ch);
MIKU_API size_t  miku_channel_len(const miku_channel_t *ch);
MIKU_API bool    miku_channel_closed(const miku_channel_t *ch);
MIKU_API void    miku_channel_close(miku_channel_t *ch);
MIKU_API void    miku_channel_destroy(miku_channel_t *ch);

#endif
