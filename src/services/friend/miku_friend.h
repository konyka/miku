#ifndef MIKU_FRIEND_H
#define MIKU_FRIEND_H

#include "miku_common.h"
#include "miku_models.h"
#include "miku_json.h"

#define MK_MAX_FRIENDS 8192

typedef struct miku_friend_service_s miku_friend_service_t;

MIKU_API miku_friend_service_t *miku_friend_service_create(void);
MIKU_API void miku_friend_service_destroy(miku_friend_service_t *svc);

MIKU_API int miku_friend_add(miku_friend_service_t *svc, const char *owner, const char *friend_uid, const char *remark);
MIKU_API int miku_friend_delete(miku_friend_service_t *svc, const char *owner, const char *friend_uid);
MIKU_API int miku_friend_get_list(miku_friend_service_t *svc, const char *owner, miku_friend_t *out, int max);
MIKU_API bool miku_friend_is_friend(miku_friend_service_t *svc, const char *uid1, const char *uid2);

MIKU_API void miku_friend_handle_rpc(miku_friend_service_t *svc, const char *method,
                                      const miku_json_val_t *req, miku_json_val_t *resp);

#endif
