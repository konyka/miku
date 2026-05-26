#ifndef MIKU_CRON_TASKS_H
#define MIKU_CRON_TASKS_H

#include "miku_common.h"

typedef struct miku_cron_tasks_s miku_cron_tasks_t;

MIKU_API miku_cron_tasks_t *miku_cron_tasks_create(void);
MIKU_API void               miku_cron_tasks_destroy(miku_cron_tasks_t *ct);

MIKU_API int   miku_cron_delete_expired_msgs(miku_cron_tasks_t *ct, int64_t retain_days);
MIKU_API int   miku_cron_clear_user_msgs(miku_cron_tasks_t *ct, const char *user_id);
MIKU_API int   miku_cron_clear_s3_files(miku_cron_tasks_t *ct, int64_t expire_days);
MIKU_API int64_t miku_cron_get_last_run(miku_cron_tasks_t *ct, const char *task_name);

#endif
