#ifndef MIKU_OBJECT_STORE_H
#define MIKU_OBJECT_STORE_H

#include "miku_common.h"

typedef struct miku_object_store_s miku_object_store_t;

typedef struct miku_object_meta_s {
    char    name[256];
    char    owner[64];
    char    content_type[64];
    char    upload_id[40];
    int64_t size;
    int64_t created_ms;
    int64_t expire_ms; /* 0 = no absolute expiry; cron still ages by created_ms */
    int     complete;
} miku_object_meta_t;

MIKU_API int miku_object_name_is_safe(const char *name);

MIKU_API miku_object_store_t *miku_object_store_create(void);
MIKU_API void                 miku_object_store_destroy(miku_object_store_t *store);

MIKU_API int miku_object_store_upsert(miku_object_store_t *store,
                                      const miku_object_meta_t *in);
MIKU_API int miku_object_store_get(miku_object_store_t *store, const char *name,
                                   miku_object_meta_t *out);
MIKU_API int miku_object_store_delete(miku_object_store_t *store, const char *name);
/* Remove objects with expire_ms in (0, now) or created_ms < cutoff_ms (cutoff 0 skips age). */
MIKU_API int miku_object_store_purge_expired(miku_object_store_t *store,
                                             int64_t now_ms, int64_t cutoff_ms);
MIKU_API int miku_object_store_count(miku_object_store_t *store);

#endif
