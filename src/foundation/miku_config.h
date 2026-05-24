#ifndef MIKU_CONFIG_H
#define MIKU_CONFIG_H

#include "miku_common.h"

typedef struct miku_config_s miku_config_t;

MIKU_API miku_config_t *miku_config_create(void);
MIKU_API int            miku_config_load_file(miku_config_t *cfg, const char *path);
MIKU_API int            miku_config_load_string(miku_config_t *cfg, const char *yaml, size_t len);
MIKU_API const char    *miku_config_get(const miku_config_t *cfg, const char *key);
MIKU_API int64_t        miku_config_get_int(const miku_config_t *cfg, const char *key, int64_t def);
MIKU_API const char    *miku_config_get_str(const miku_config_t *cfg, const char *key, const char *def);
MIKU_API void           miku_config_set(miku_config_t *cfg, const char *key, const char *value);
MIKU_API void           miku_config_destroy(miku_config_t *cfg);

#endif
