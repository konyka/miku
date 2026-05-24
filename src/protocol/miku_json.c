#include "miku_json.h"
#include "miku_string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    const char *src;
    size_t     len;
    size_t     pos;
} json_parser_t;

static void skip_ws(json_parser_t *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p->pos++; continue; }
        break;
    }
}

static char peek(json_parser_t *p) {
    skip_ws(p);
    return (p->pos < p->len) ? p->src[p->pos] : '\0';
}

static char next(json_parser_t *p) {
    skip_ws(p);
    return (p->pos < p->len) ? p->src[p->pos++] : '\0';
}

static miku_json_val_t *parse_value(json_parser_t *p);

static char *decode_string(json_parser_t *p) {
    if (next(p) != '"') return NULL;
    size_t start = p->pos;
    size_t esc_count = 0;
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == '"') break;
        if (c == '\\') { p->pos++; esc_count++; }
        p->pos++;
    }
    if (p->pos >= p->len) return NULL;
    size_t raw_len = p->pos - start;
    char *result = (char *)malloc(raw_len + 1);
    if (!result) return NULL;
    if (esc_count == 0) {
        memcpy(result, p->src + start, raw_len);
        result[raw_len] = '\0';
    } else {
        size_t j = 0;
        for (size_t i = start; i < p->pos; i++) {
            if (p->src[i] == '\\' && i + 1 < p->pos) {
                i++;
                switch (p->src[i]) {
                    case '"':  result[j++] = '"'; break;
                    case '\\': result[j++] = '\\'; break;
                    case '/':  result[j++] = '/'; break;
                    case 'b':  result[j++] = '\b'; break;
                    case 'f':  result[j++] = '\f'; break;
                    case 'n':  result[j++] = '\n'; break;
                    case 'r':  result[j++] = '\r'; break;
                    case 't':  result[j++] = '\t'; break;
                    default:   result[j++] = p->src[i]; break;
                }
            } else {
                result[j++] = p->src[i];
            }
        }
        result[j] = '\0';
    }
    p->pos++;
    return result;
}

static miku_json_val_t *parse_string(json_parser_t *p) {
    char *s = decode_string(p);
    if (!s) return NULL;
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (!v) { free(s); return NULL; }
    v->type = MK_JSON_STRING;
    v->u.str_val = s;
    return v;
}

static miku_json_val_t *parse_number(json_parser_t *p) {
    size_t start = p->pos;
    bool is_float = false;
    if (p->pos < p->len && p->src[p->pos] == '-') p->pos++;
    while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    if (p->pos < p->len && p->src[p->pos] == '.') {
        is_float = true;
        p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    }
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        is_float = true;
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    }

    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (!v) return NULL;

    char buf[64];
    size_t nlen = p->pos - start;
    if (nlen >= sizeof(buf)) nlen = sizeof(buf) - 1;
    memcpy(buf, p->src + start, nlen);
    buf[nlen] = '\0';

    if (is_float) {
        v->type = MK_JSON_DOUBLE;
        v->u.dbl_val = strtod(buf, NULL);
    } else {
        v->type = MK_JSON_INT;
        v->u.int_val = strtoll(buf, NULL, 10);
    }
    return v;
}

static miku_json_val_t *parse_array(json_parser_t *p) {
    if (next(p) != '[') return NULL;
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = MK_JSON_ARRAY;
    v->u.array.capacity = 8;
    v->u.array.items = (miku_json_val_t **)malloc(v->u.array.capacity * sizeof(miku_json_val_t *));
    if (!v->u.array.items) { free(v); return NULL; }

    if (peek(p) == ']') { next(p); return v; }

    while (true) {
        miku_json_val_t *item = parse_value(p);
        if (!item) { miku_json_destroy(v); return NULL; }
        if (v->u.array.count >= v->u.array.capacity) {
            v->u.array.capacity *= 2;
            v->u.array.items = (miku_json_val_t **)realloc(v->u.array.items,
                v->u.array.capacity * sizeof(miku_json_val_t *));
        }
        v->u.array.items[v->u.array.count++] = item;
        char c = peek(p);
        if (c == ',') { next(p); continue; }
        if (c == ']') { next(p); break; }
        miku_json_destroy(v);
        return NULL;
    }
    return v;
}

static miku_json_val_t *parse_object(json_parser_t *p) {
    if (next(p) != '{') return NULL;
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = MK_JSON_OBJECT;
    v->u.object.capacity = 8;
    v->u.object.pairs = (miku_json_pair_t *)malloc(v->u.object.capacity * sizeof(miku_json_pair_t));
    if (!v->u.object.pairs) { free(v); return NULL; }

    if (peek(p) == '}') { next(p); return v; }

    while (true) {
        char *key = decode_string(p);
        if (!key) { miku_json_destroy(v); return NULL; }
        if (next(p) != ':') { free(key); miku_json_destroy(v); return NULL; }
        miku_json_val_t *val = parse_value(p);
        if (!val) { free(key); miku_json_destroy(v); return NULL; }

        if (v->u.object.count >= v->u.object.capacity) {
            v->u.object.capacity *= 2;
            v->u.object.pairs = (miku_json_pair_t *)realloc(v->u.object.pairs,
                v->u.object.capacity * sizeof(miku_json_pair_t));
        }
        v->u.object.pairs[v->u.object.count].key = key;
        v->u.object.pairs[v->u.object.count].val = val;
        v->u.object.count++;

        char c = peek(p);
        if (c == ',') { next(p); continue; }
        if (c == '}') { next(p); break; }
        miku_json_destroy(v);
        return NULL;
    }
    return v;
}

static miku_json_val_t *parse_value(json_parser_t *p) {
    char c = peek(p);
    switch (c) {
        case '"': return parse_string(p);
        case '{': return parse_object(p);
        case '[': return parse_array(p);
        case 't': case 'f': {
            if (p->pos + 4 <= p->len && memcmp(p->src + p->pos, "true", 4) == 0) {
                p->pos += 4;
                miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
                if (v) { v->type = MK_JSON_BOOL; v->u.bool_val = true; }
                return v;
            }
            if (p->pos + 5 <= p->len && memcmp(p->src + p->pos, "false", 5) == 0) {
                p->pos += 5;
                miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
                if (v) { v->type = MK_JSON_BOOL; v->u.bool_val = false; }
                return v;
            }
            return NULL;
        }
        case 'n': {
            if (p->pos + 4 <= p->len && memcmp(p->src + p->pos, "null", 4) == 0) {
                p->pos += 4;
                return miku_json_create_null();
            }
            return NULL;
        }
        case '-': case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': case '8': case '9':
            return parse_number(p);
        default:
            return NULL;
    }
}

miku_json_val_t *miku_json_parse(const char *json, size_t len) {
    if (!json) return NULL;
    json_parser_t p = { json, len, 0 };
    miku_json_val_t *v = parse_value(&p);
    return v;
}

miku_json_val_t *miku_json_parse_str(const char *json) {
    return miku_json_parse(json, json ? strlen(json) : 0);
}

void miku_json_destroy(miku_json_val_t *v) {
    if (!v) return;
    switch (v->type) {
        case MK_JSON_STRING:
            free(v->u.str_val);
            break;
        case MK_JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++)
                miku_json_destroy(v->u.array.items[i]);
            free(v->u.array.items);
            break;
        case MK_JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.pairs[i].key);
                miku_json_destroy(v->u.object.pairs[i].val);
            }
            free(v->u.object.pairs);
            break;
        default:
            break;
    }
    free(v);
}

miku_json_val_t *miku_json_get(const miku_json_val_t *obj, const char *key) {
    if (!obj || obj->type != MK_JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.pairs[i].key, key) == 0)
            return obj->u.object.pairs[i].val;
    }
    return NULL;
}

miku_json_val_t *miku_json_at(const miku_json_val_t *arr, size_t index) {
    if (!arr || arr->type != MK_JSON_ARRAY || index >= arr->u.array.count) return NULL;
    return arr->u.array.items[index];
}

size_t miku_json_size(const miku_json_val_t *v) {
    if (!v) return 0;
    if (v->type == MK_JSON_ARRAY) return v->u.array.count;
    if (v->type == MK_JSON_OBJECT) return v->u.object.count;
    return 0;
}

miku_json_type_t miku_json_type(const miku_json_val_t *v) {
    return v ? v->type : MK_JSON_NULL;
}

bool miku_json_bool(const miku_json_val_t *v) {
    return (v && v->type == MK_JSON_BOOL) ? v->u.bool_val : false;
}

int64_t miku_json_int(const miku_json_val_t *v) {
    return (v && v->type == MK_JSON_INT) ? v->u.int_val : 0;
}

double miku_json_dbl(const miku_json_val_t *v) {
    return (v && v->type == MK_JSON_DOUBLE) ? v->u.dbl_val : 0.0;
}

const char *miku_json_str(const miku_json_val_t *v) {
    return (v && v->type == MK_JSON_STRING) ? v->u.str_val : NULL;
}

miku_json_val_t *miku_json_create_null(void) {
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (v) v->type = MK_JSON_NULL;
    return v;
}

miku_json_val_t *miku_json_create_bool(bool val) {
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (v) { v->type = MK_JSON_BOOL; v->u.bool_val = val; }
    return v;
}

miku_json_val_t *miku_json_create_int(int64_t val) {
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (v) { v->type = MK_JSON_INT; v->u.int_val = val; }
    return v;
}

miku_json_val_t *miku_json_create_dbl(double val) {
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (v) { v->type = MK_JSON_DOUBLE; v->u.dbl_val = val; }
    return v;
}

miku_json_val_t *miku_json_create_str(const char *val) {
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (v) { v->type = MK_JSON_STRING; v->u.str_val = strdup(val ? val : ""); }
    return v;
}

miku_json_val_t *miku_json_create_array(void) {
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = MK_JSON_ARRAY;
    v->u.array.capacity = 8;
    v->u.array.items = (miku_json_val_t **)malloc(8 * sizeof(miku_json_val_t *));
    if (!v->u.array.items) { free(v); return NULL; }
    return v;
}

miku_json_val_t *miku_json_create_object(void) {
    miku_json_val_t *v = (miku_json_val_t *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = MK_JSON_OBJECT;
    v->u.object.capacity = 8;
    v->u.object.pairs = (miku_json_pair_t *)malloc(8 * sizeof(miku_json_pair_t));
    if (!v->u.object.pairs) { free(v); return NULL; }
    return v;
}

int miku_json_array_push(miku_json_val_t *arr, miku_json_val_t *item) {
    if (!arr || arr->type != MK_JSON_ARRAY) return -1;
    if (arr->u.array.count >= arr->u.array.capacity) {
        arr->u.array.capacity *= 2;
        arr->u.array.items = (miku_json_val_t **)realloc(arr->u.array.items,
            arr->u.array.capacity * sizeof(miku_json_val_t *));
        if (!arr->u.array.items) return -1;
    }
    arr->u.array.items[arr->u.array.count++] = item;
    return 0;
}

int miku_json_object_set(miku_json_val_t *obj, const char *key, miku_json_val_t *val) {
    if (!obj || obj->type != MK_JSON_OBJECT || !key) return -1;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.pairs[i].key, key) == 0) {
            miku_json_destroy(obj->u.object.pairs[i].val);
            obj->u.object.pairs[i].val = val;
            return 0;
        }
    }
    if (obj->u.object.count >= obj->u.object.capacity) {
        obj->u.object.capacity *= 2;
        obj->u.object.pairs = (miku_json_pair_t *)realloc(obj->u.object.pairs,
            obj->u.object.capacity * sizeof(miku_json_pair_t));
        if (!obj->u.object.pairs) return -1;
    }
    obj->u.object.pairs[obj->u.object.count].key = strdup(key);
    obj->u.object.pairs[obj->u.object.count].val = val;
    obj->u.object.count++;
    return 0;
}

static void stringify_val(miku_string_t *s, const miku_json_val_t *v) {
    if (!v) { miku_str_cat(s, "null"); return; }
    switch (v->type) {
        case MK_JSON_NULL:
            miku_str_cat(s, "null");
            break;
        case MK_JSON_BOOL:
            miku_str_cat(s, v->u.bool_val ? "true" : "false");
            break;
        case MK_JSON_INT:
            miku_str_printf(s, "%lld", (long long)v->u.int_val);
            break;
        case MK_JSON_DOUBLE: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", v->u.dbl_val);
            miku_str_cat(s, buf);
            break;
        }
        case MK_JSON_STRING: {
            miku_str_cat(s, "\"");
            if (v->u.str_val) {
                const char *p = v->u.str_val;
                while (*p) {
                    switch (*p) {
                        case '"':  miku_str_cat(s, "\\\""); break;
                        case '\\': miku_str_cat(s, "\\\\"); break;
                        case '\n': miku_str_cat(s, "\\n"); break;
                        case '\r': miku_str_cat(s, "\\r"); break;
                        case '\t': miku_str_cat(s, "\\t"); break;
                        case '\b': miku_str_cat(s, "\\b"); break;
                        case '\f': miku_str_cat(s, "\\f"); break;
                        default:   miku_str_cat_len(s, p, 1); break;
                    }
                    p++;
                }
            }
            miku_str_cat(s, "\"");
            break;
        }
        case MK_JSON_ARRAY:
            miku_str_cat(s, "[");
            for (size_t i = 0; i < v->u.array.count; i++) {
                if (i > 0) miku_str_cat(s, ",");
                stringify_val(s, v->u.array.items[i]);
            }
            miku_str_cat(s, "]");
            break;
        case MK_JSON_OBJECT:
            miku_str_cat(s, "{");
            for (size_t i = 0; i < v->u.object.count; i++) {
                if (i > 0) miku_str_cat(s, ",");
                miku_str_printf(s, "\"%s\":", v->u.object.pairs[i].key);
                stringify_val(s, v->u.object.pairs[i].val);
            }
            miku_str_cat(s, "}");
            break;
    }
}

miku_string_t *miku_json_stringify(const miku_json_val_t *v) {
    miku_string_t *s = miku_str_create_empty(256);
    if (!s) return NULL;
    stringify_val(s, v);
    return s;
}
