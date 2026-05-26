#ifndef MIKU_SEQ_H
#define MIKU_SEQ_H

#include "miku_common.h"

#define MK_SEQ_MAX_CONVERSATIONS 65536

typedef struct miku_seq_s miku_seq_t;

MIKU_API miku_seq_t *miku_seq_create(void);
MIKU_API void        miku_seq_destroy(miku_seq_t *seq);

MIKU_API int64_t miku_seq_next(miku_seq_t *seq, const char *conversation_id);
MIKU_API int64_t miku_seq_current(miku_seq_t *seq, const char *conversation_id);
MIKU_API int     miku_seq_set(miku_seq_t *seq, const char *conversation_id, int64_t val);
MIKU_API int     miku_seq_set_user_read(miku_seq_t *seq, const char *user_id,
                                          const char *conversation_id, int64_t read_seq);
MIKU_API int64_t miku_seq_get_user_read(miku_seq_t *seq, const char *user_id,
                                          const char *conversation_id);

#endif
