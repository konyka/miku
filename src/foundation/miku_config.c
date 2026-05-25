#include "miku_config.h"
#include "miku_hashmap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct miku_config_s {
    miku_hashmap_t *values;
};

miku_config_t *miku_config_create(void) {
    miku_config_t *cfg = (miku_config_t *)calloc(1, sizeof(*cfg));
    if (!cfg) return NULL;
    cfg->values = miku_hashmap_create(64, free);
    if (!cfg->values) { free(cfg); return NULL; }
    return cfg;
}

int miku_config_load_file(miku_config_t *cfg, const char *path) {
    if (!cfg || !path) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long szl = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t sz = (size_t)szl;
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    int rc = miku_config_load_string(cfg, buf, rd);
    free(buf);
    return rc;
}

/* Count leading spaces (treated as 2-space indent per YAML convention) */
static int count_indent(const char *line) {
    int n = 0;
    while (*line == ' ') { n++; line++; }
    if (*line == '\t') return -1; /* tabs not supported */
    return n / 2;
}

/* Strip leading whitespace, return pointer into original buffer */
static char *strip_left(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Strip trailing whitespace in-place */
static void strip_right(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' ' || s[len-1] == '\t'))
        s[--len] = '\0';
}

int miku_config_load_string(miku_config_t *cfg, const char *yaml, size_t len) {
    if (!cfg || !yaml) return -1;
    char *copy = (char *)malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, yaml, len);
    copy[len] = '\0';

    /* Indentation stack for dot-path keys: stack[0].stack[1].stack[2]... */
    char *stack[32] = {0};
    int   depth = 0;

    char *saveptr;
    char *line = strtok_r(copy, "\n", &saveptr);
    while (line) {
        char *stripped = strip_left(line);
        /* Skip empty lines and comments */
        if (*stripped == '#' || *stripped == '\0' || *stripped == '\r') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        int indent = count_indent(line);
        if (indent < 0) indent = 0;
        if (indent > 31) indent = 31;

        /* Find the colon separator */
        char *colon = strchr(stripped, ':');
        if (!colon) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Extract key */
        *colon = '\0';
        char *key = stripped;
        strip_right(key);

        /* Extract value (after colon) */
        char *val = colon + 1;
        while (*val == ' ') val++;
        strip_right(val);

        /* Update depth stack */
        depth = indent;
        stack[depth] = key;

        if (val[0]) {
            /* Strip surrounding quotes from value */
            size_t vlen = strlen(val);
            if (vlen >= 2 && ((val[0] == '"' && val[vlen-1] == '"') ||
                              (val[0] == '\'' && val[vlen-1] == '\''))) {
                val[vlen-1] = '\0';
                val++;
            }

            /* Leaf node: build dot-path */
            char fullpath[512] = {0};
            size_t pos = 0;
            for (int i = 0; i <= depth; i++) {
                if (i > 0 && pos < sizeof(fullpath) - 1) fullpath[pos++] = '.';
                size_t klen = strlen(stack[i]);
                if (pos + klen >= sizeof(fullpath) - 1) break;
                memcpy(fullpath + pos, stack[i], klen);
                pos += klen;
            }
            fullpath[pos] = '\0';
            if (fullpath[0])
                miku_config_set(cfg, fullpath, val);
        }
        /* else: parent node, just pushed onto stack, no value to set */

        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(copy);
    return 0;
}

const char *miku_config_get(const miku_config_t *cfg, const char *key) {
    return cfg ? (const char *)miku_hashmap_get(cfg->values, key) : NULL;
}

int64_t miku_config_get_int(const miku_config_t *cfg, const char *key, int64_t def) {
    const char *val = miku_config_get(cfg, key);
    return val ? atoll(val) : def;
}

const char *miku_config_get_str(const miku_config_t *cfg, const char *key, const char *def) {
    const char *val = miku_config_get(cfg, key);
    return val ? val : def;
}

void miku_config_set(miku_config_t *cfg, const char *key, const char *value) {
    if (!cfg || !key) return;
    char *vcopy = strdup(value);
    miku_hashmap_put(cfg->values, key, vcopy);
}

void miku_config_destroy(miku_config_t *cfg) {
    if (!cfg) return;
    miku_hashmap_destroy(cfg->values);
    free(cfg);
}
