#ifndef MIKU_USER_H
#define MIKU_USER_H

#include "miku_common.h"
#include "miku_models.h"
#include "miku_json.h"

#define MK_MAX_USERS 4096

typedef struct miku_user_service_s miku_user_service_t;

MIKU_API miku_user_service_t *miku_user_service_create(void);
MIKU_API void miku_user_service_destroy(miku_user_service_t *svc);

MIKU_API int miku_user_register(miku_user_service_t *svc, const miku_user_t *user);
MIKU_API miku_user_t *miku_user_find(miku_user_service_t *svc, const char *user_id);
MIKU_API int miku_user_update(miku_user_service_t *svc, const miku_user_t *user);
MIKU_API int miku_user_get_users(miku_user_service_t *svc, const char **user_ids, int count,
                                  miku_user_t *out, int max_out);
MIKU_API int miku_user_count(miku_user_service_t *svc);

MIKU_API void miku_user_handle_rpc(miku_user_service_t *svc, const char *method,
                                    const miku_json_val_t *req_json,
                                    miku_json_val_t *resp_json);

#endif
