#ifndef MIKU_GROUP_H
#define MIKU_GROUP_H

#include "miku_common.h"
#include "miku_models.h"
#include "miku_json.h"

#define MK_MAX_GROUPS 2048
#define MK_MAX_MEMBERS 16384

typedef struct miku_group_service_s miku_group_service_t;

MIKU_API miku_group_service_t *miku_group_service_create(void);
MIKU_API void miku_group_service_destroy(miku_group_service_t *svc);

MIKU_API int miku_group_create(miku_group_service_t *svc, miku_group_t *g, const char *owner_uid);
MIKU_API miku_group_t *miku_group_find(miku_group_service_t *svc, const char *group_id);
MIKU_API int miku_group_add_member(miku_group_service_t *svc, const char *group_id, const char *user_id, int role);
MIKU_API int miku_group_remove_member(miku_group_service_t *svc, const char *group_id, const char *user_id);
MIKU_API int miku_group_get_members(miku_group_service_t *svc, const char *group_id, miku_group_member_t *out, int max);
/* Visit every member of group_id (no copy cap). Returns visited count. */
typedef void (*miku_group_member_fn)(const char *user_id, int role, void *ctx);
MIKU_API int miku_group_foreach_member(miku_group_service_t *svc, const char *group_id,
                                       miku_group_member_fn fn, void *ctx);
/* 1 if user is in group, else 0. */
MIKU_API int miku_group_is_member(miku_group_service_t *svc, const char *group_id,
                                  const char *user_id);
/* Member role_level, or -1 if not a member. Owner=100, admin>=60, member=20. */
MIKU_API int miku_group_member_role(miku_group_service_t *svc, const char *group_id,
                                    const char *user_id);

MIKU_API void miku_group_handle_rpc(miku_group_service_t *svc, const char *method,
                                     const miku_json_val_t *req, miku_json_val_t *resp);
#endif
