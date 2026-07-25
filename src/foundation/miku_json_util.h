#ifndef MIKU_JSON_UTIL_H
#define MIKU_JSON_UTIL_H

#include "miku_json.h"
#include <stdio.h>

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

/* Escape for safe interpolation into a JSON string literal (RFC 8259 §7). */
static inline void miku_json_escape_str(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return;
    size_t pos = 0;
    for (const char *p = in; p && *p && pos + 2 < cap; p++) {
        if (*p == '"' || *p == '\\') {
            if (pos + 2 >= cap) break;
            out[pos++] = '\\';
            out[pos++] = *p;
        } else if ((unsigned char)*p < 0x20) {
            int w = snprintf(out + pos, cap - pos, "\\u%04x", (unsigned)*p);
            if (w <= 0 || pos + (size_t)w >= cap) break;
            pos += (size_t)w;
        } else {
            out[pos++] = *p;
        }
    }
    out[pos] = '\0';
}

#endif
