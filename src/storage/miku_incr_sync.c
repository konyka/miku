#include "miku_incr_sync.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char    owner_id[64];
    int     type;
    int64_t version;
} version_entry_t;

struct miku_incr_sync_s {
    version_entry_t entries[MK_INCR_MAX_VERSIONS];
    int             entry_count;
};

miku_incr_sync_t *miku_incr_sync_create(void) {
    return (miku_incr_sync_t *)calloc(1, sizeof(miku_incr_sync_t));
}

void miku_incr_sync_destroy(miku_incr_sync_t *is) { free(is); }

static version_entry_t *find_entry(miku_incr_sync_t *is, miku_incr_type_t type, const char *owner_id) {
    for (int i = 0; i < is->entry_count; i++) {
        if (is->entries[i].type == (int)type && strcmp(is->entries[i].owner_id, owner_id) == 0)
            return &is->entries[i];
    }
    return NULL;
}

int64_t miku_incr_version(miku_incr_sync_t *is, miku_incr_type_t type, const char *owner_id) {
    if (!is || !owner_id) return 0;
    version_entry_t *e = find_entry(is, type, owner_id);
    return e ? e->version : 0;
}

int64_t miku_incr_bump(miku_incr_sync_t *is, miku_incr_type_t type, const char *owner_id) {
    if (!is || !owner_id) return -1;
    version_entry_t *e = find_entry(is, type, owner_id);
    if (!e) {
        if (is->entry_count >= MK_INCR_MAX_VERSIONS) return -1;
        e = &is->entries[is->entry_count++];
        strncpy(e->owner_id, owner_id, sizeof(e->owner_id) - 1);
        e->type = (int)type;
        e->version = 0;
    }
    return ++e->version;
}

int miku_incr_set_version(miku_incr_sync_t *is, miku_incr_type_t type,
                            const char *owner_id, int64_t version) {
    if (!is || !owner_id) return -1;
    version_entry_t *e = find_entry(is, type, owner_id);
    if (!e) {
        if (is->entry_count >= MK_INCR_MAX_VERSIONS) return -1;
        e = &is->entries[is->entry_count++];
        strncpy(e->owner_id, owner_id, sizeof(e->owner_id) - 1);
        e->type = (int)type;
    }
    e->version = version;
    return 0;
}

int miku_incr_get_changes(miku_incr_sync_t *is, miku_incr_type_t type,
                            const char *owner_id, int64_t since_version,
                            char **results_json) {
    if (!is || !owner_id) return -1;
    (void)since_version;
    if (results_json) *results_json = strdup("[]");
    version_entry_t *e = find_entry(is, type, owner_id);
    if (e && e->version <= since_version) return 0;
    return 0;
}
