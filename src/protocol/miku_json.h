#ifndef MIKU_JSON_H
#define MIKU_JSON_H

#include "miku_common.h"
#include "miku_string.h"

typedef enum {
    MK_JSON_NULL,
    MK_JSON_BOOL,
    MK_JSON_INT,
    MK_JSON_DOUBLE,
    MK_JSON_STRING,
    MK_JSON_ARRAY,
    MK_JSON_OBJECT
} miku_json_type_t;

typedef struct miku_json_val miku_json_val_t;
typedef struct miku_json_pair miku_json_pair_t;

struct miku_json_val {
    miku_json_type_t type;
    union {
        bool        bool_val;
        int64_t     int_val;
        double      dbl_val;
        char       *str_val;
        struct {
            miku_json_val_t **items;
            size_t            count;
            size_t            capacity;
        } array;
        struct {
            miku_json_pair_t *pairs;
            size_t            count;
            size_t            capacity;
            int32_t          *kh;      /* open-address key → pair index, -1 empty */
            size_t            kh_mask; /* kh length - 1 (power of 2) */
        } object;
    } u;
};

struct miku_json_pair {
    char            *key;
    miku_json_val_t *val;
};

MIKU_API miku_json_val_t *miku_json_parse(const char *json, size_t len);
MIKU_API miku_json_val_t *miku_json_parse_str(const char *json);
MIKU_API void             miku_json_destroy(miku_json_val_t *v);

MIKU_API miku_json_val_t *miku_json_get(const miku_json_val_t *obj, const char *key);
MIKU_API miku_json_val_t *miku_json_at(const miku_json_val_t *arr, size_t index);
MIKU_API size_t           miku_json_size(const miku_json_val_t *v);

MIKU_API miku_json_type_t miku_json_type(const miku_json_val_t *v);
MIKU_API bool             miku_json_bool(const miku_json_val_t *v);
MIKU_API int64_t          miku_json_int(const miku_json_val_t *v);
MIKU_API double           miku_json_dbl(const miku_json_val_t *v);
MIKU_API const char      *miku_json_str(const miku_json_val_t *v);

MIKU_API miku_json_val_t *miku_json_create_null(void);
MIKU_API miku_json_val_t *miku_json_create_bool(bool val);
MIKU_API miku_json_val_t *miku_json_create_int(int64_t val);
MIKU_API miku_json_val_t *miku_json_create_dbl(double val);
MIKU_API miku_json_val_t *miku_json_create_str(const char *val);
MIKU_API miku_json_val_t *miku_json_create_array(void);
MIKU_API miku_json_val_t *miku_json_create_object(void);

MIKU_API int  miku_json_array_push(miku_json_val_t *arr, miku_json_val_t *item);
MIKU_API int  miku_json_object_set(miku_json_val_t *obj, const char *key, miku_json_val_t *val);

MIKU_API miku_string_t   *miku_json_stringify(const miku_json_val_t *v);

#endif
