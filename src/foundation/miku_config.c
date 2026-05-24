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
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    int rc = miku_config_load_string(cfg, buf, rd);
    free(buf);
    return rc;
}

static void parse_simple_line(miku_config_t *cfg, char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == '\0' || *line == '\n') return;

    char *colon = strchr(line, ':');
    if (!colon) return;
    *colon = '\0';
    char *val = colon + 1;
    while (*val == ' ') val++;

    size_t vlen = strlen(val);
    while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r' || val[vlen-1] == ' '))
        val[--vlen] = '\0';

    if (line[0] && val[0])
        miku_config_set(cfg, line, val);
}

int miku_config_load_string(miku_config_t *cfg, const char *yaml, size_t len) {
    if (!cfg || !yaml) return -1;
    char *copy = (char *)malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, yaml, len);
    copy[len] = '\0';
    char *saveptr;
    char *line = strtok_r(copy, "\n", &saveptr);
    while (line) {
        parse_simple_line(cfg, line);
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
