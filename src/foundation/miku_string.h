#ifndef MIKU_STRING_H
#define MIKU_STRING_H

#include "miku_common.h"

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} miku_string_t;

MIKU_API miku_string_t *miku_str_create(const char *init);
MIKU_API miku_string_t *miku_str_create_empty(size_t cap);
MIKU_API void           miku_str_destroy(miku_string_t *s);
MIKU_API miku_string_t *miku_str_cat(miku_string_t *s, const char *str);
MIKU_API miku_string_t *miku_str_cat_len(miku_string_t *s, const char *str, size_t len);
MIKU_API miku_string_t *miku_str_printf(miku_string_t *s, const char *fmt, ...) MIKU_FORMAT(2, 3);
MIKU_API void           miku_str_clear(miku_string_t *s);
MIKU_API miku_str_t     miku_str_view(const miku_string_t *s);

#endif
