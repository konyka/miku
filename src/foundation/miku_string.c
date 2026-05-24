#include "miku_string.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static int str_ensure_cap(miku_string_t *s, size_t needed) {
    if (s->cap >= needed) return 0;
    size_t newcap = s->cap ? s->cap * 2 : 64;
    while (newcap < needed) newcap *= 2;
    char *nd = (char *)realloc(s->data, newcap);
    if (!nd) return -1;
    s->data = nd;
    s->cap = newcap;
    return 0;
}

miku_string_t *miku_str_create(const char *init) {
    size_t len = init ? strlen(init) : 0;
    miku_string_t *s = miku_str_create_empty(len + 1);
    if (!s) return NULL;
    if (len > 0) {
        memcpy(s->data, init, len);
        s->len = len;
        s->data[s->len] = '\0';
    }
    return s;
}

miku_string_t *miku_str_create_empty(size_t cap) {
    if (cap < 16) cap = 16;
    miku_string_t *s = (miku_string_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->data = (char *)malloc(cap);
    if (!s->data) { free(s); return NULL; }
    s->data[0] = '\0';
    s->len = 0;
    s->cap = cap;
    return s;
}

void miku_str_destroy(miku_string_t *s) {
    if (!s) return;
    free(s->data);
    free(s);
}

miku_string_t *miku_str_cat(miku_string_t *s, const char *str) {
    if (!s || !str) return s;
    return miku_str_cat_len(s, str, strlen(str));
}

miku_string_t *miku_str_cat_len(miku_string_t *s, const char *str, size_t len) {
    if (!s || !str || len == 0) return s;
    if (str_ensure_cap(s, s->len + len + 1) != 0) return NULL;
    memcpy(s->data + s->len, str, len);
    s->len += len;
    s->data[s->len] = '\0';
    return s;
}

miku_string_t *miku_str_printf(miku_string_t *s, const char *fmt, ...) {
    if (!s || !fmt) return s;
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return NULL;
    if (str_ensure_cap(s, s->len + (size_t)needed + 1) != 0) return NULL;
    va_start(ap, fmt);
    vsnprintf(s->data + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    s->len += (size_t)needed;
    s->data[s->len] = '\0';
    return s;
}

void miku_str_clear(miku_string_t *s) {
    if (!s) return;
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

miku_str_t miku_str_view(const miku_string_t *s) {
    if (!s || !s->data) return (miku_str_t){NULL, 0};
    return (miku_str_t){s->data, s->len};
}
