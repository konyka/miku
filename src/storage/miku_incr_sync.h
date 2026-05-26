#ifndef MIKU_INCR_SYNC_H
#define MIKU_INCR_SYNC_H

#include "miku_common.h"

#define MK_INCR_MAX_VERSIONS 16384

typedef struct miku_incr_sync_s miku_incr_sync_t;

typedef enum {
    MK_INCR_FRIENDS     = 1,
    MK_INCR_BLACKS      = 2,
    MK_INCR_GROUPS      = 3,
    MK_INCR_GROUP_MEMBERS = 4,
    MK_INCR_CONVERSATIONS = 5,
} miku_incr_type_t;

MIKU_API miku_incr_sync_t *miku_incr_sync_create(void);
MIKU_API void              miku_incr_sync_destroy(miku_incr_sync_t *is);

MIKU_API int64_t miku_incr_version(miku_incr_sync_t *is, miku_incr_type_t type,
                                     const char *owner_id);
MIKU_API int64_t miku_incr_bump(miku_incr_sync_t *is, miku_incr_type_t type,
                                  const char *owner_id);
MIKU_API int     miku_incr_set_version(miku_incr_sync_t *is, miku_incr_type_t type,
                                         const char *owner_id, int64_t version);
MIKU_API int     miku_incr_get_changes(miku_incr_sync_t *is, miku_incr_type_t type,
                                         const char *owner_id, int64_t since_version,
                                         char **results_json);

#endif
