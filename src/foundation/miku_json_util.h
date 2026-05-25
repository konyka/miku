#ifndef MIKU_JSON_UTIL_H
#define MIKU_JSON_UTIL_H

#include "miku_json.h"

static inline void miku_ji(miku_json_val_t *o, const char *k, int64_t v) {
    miku_json_object_set(o, k, miku_json_create_int(v));
}

static inline void miku_jss(miku_json_val_t *o, const char *k, const char *v) {
    if (v) miku_json_object_set(o, k, miku_json_create_str(v));
}

static inline void miku_jerr(miku_json_val_t *o, int64_t code, const char *msg) {
    miku_ji(o, "errCode", code);
    miku_jss(o, "errMsg", msg);
    miku_jss(o, "errDmg", "");
}

#endif
