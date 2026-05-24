#ifndef MIKU_MONGO_H
#define MIKU_MONGO_H

#include "miku_common.h"

typedef struct miku_mongo_s miku_mongo_t;

typedef struct {
    char  *key;
    int    type;
    union {
        char       *str_val;
        int64_t     int_val;
        double      dbl_val;
        bool        bool_val;
    } u;
} miku_mongo_field_t;

typedef struct {
    miku_mongo_field_t *fields;
    size_t              field_count;
} miku_mongo_doc_t;

MIKU_API miku_mongo_t *miku_mongo_create(const char *uri, const char *db);
MIKU_API void          miku_mongo_destroy(miku_mongo_t *m);

MIKU_API int   miku_mongo_connect(miku_mongo_t *m);
MIKU_API void  miku_mongo_disconnect(miku_mongo_t *m);
MIKU_API bool  miku_mongo_is_connected(const miku_mongo_t *m);

MIKU_API int   miku_mongo_insert(miku_mongo_t *m, const char *collection,
                                  const char *json_doc);
MIKU_API int   miku_mongo_find_one(miku_mongo_t *m, const char *collection,
                                    const char *json_filter, char **result);
MIKU_API int   miku_mongo_update(miku_mongo_t *m, const char *collection,
                                  const char *json_filter, const char *json_update,
                                  bool upsert);
MIKU_API int   miku_mongo_delete(miku_mongo_t *m, const char *collection,
                                  const char *json_filter);

#endif
