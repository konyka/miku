#ifndef MIKU_THIRD_H
#define MIKU_THIRD_H

#include "miku_common.h"
#include "miku_json.h"

typedef struct miku_third_service_s miku_third_service_t;

MIKU_API miku_third_service_t *miku_third_service_create(void);
MIKU_API void miku_third_service_destroy(miku_third_service_t *svc);

MIKU_API void miku_third_handle_rpc(miku_third_service_t *svc, const char *method,
                                     const miku_json_val_t *req, miku_json_val_t *resp);
#endif
