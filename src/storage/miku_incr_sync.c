#include "miku_incr_sync.h"
#include "miku_hash.h"
#include <stdlib.h>
#include <string.h>

#define MK_INCR_HASH 32768

typedef struct {
    char    owner_id[64];
    int     type;
    int64_t version;
} version_entry_t;

struct miku_incr_sync_s {
    version_entry_t entries[MK_INCR_MAX_VERSIONS];
    int             entry_count;
    int16_t         pair_hash[MK_INCR_HASH]; /* -1 empty, else entries[] index */
};

static uint32_t incr_pair_slot(miku_incr_type_t type, const char *owner_id) {
    uint64_t a = miku_fnv1a_64(owner_id, strlen(owner_id));
    uint64_t b = (uint64_t)(unsigned)type * 0x9e3779b97f4a7c15ULL;
    return (uint32_t)((a ^ b) & (MK_INCR_HASH - 1));
}

static void incr_hash_insert(miku_incr_sync_t *is, int ei) {
    uint32_t idx = incr_pair_slot((miku_incr_type_t)is->entries[ei].type,
                                  is->entries[ei].owner_id);
    for (int n = 0; n < MK_INCR_HASH; n++) {
        if (is->pair_hash[idx] < 0) {
            is->pair_hash[idx] = (int16_t)ei;
            return;
        }
        idx = (idx + 1) & (MK_INCR_HASH - 1);
    }
}

static version_entry_t *find_entry(miku_incr_sync_t *is, miku_incr_type_t type, const char *owner_id) {
    uint32_t idx = incr_pair_slot(type, owner_id);
    for (int n = 0; n < MK_INCR_HASH; n++) {
        int ei = is->pair_hash[idx];
        if (ei < 0) return NULL;
        if (is->entries[ei].type == (int)type &&
            strcmp(is->entries[ei].owner_id, owner_id) == 0)
            return &is->entries[ei];
        idx = (idx + 1) & (MK_INCR_HASH - 1);
    }
    return NULL;
}

miku_incr_sync_t *miku_incr_sync_create(void) {
    miku_incr_sync_t *is = (miku_incr_sync_t *)calloc(1, sizeof(*is));
    if (is) {
        for (int i = 0; i < MK_INCR_HASH; i++) is->pair_hash[i] = -1;
    }
    return is;
}

void miku_incr_sync_destroy(miku_incr_sync_t *is) { free(is); }

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
        int ei = is->entry_count++;
        e = &is->entries[ei];
        memset(e, 0, sizeof(*e));
        strncpy(e->owner_id, owner_id, sizeof(e->owner_id) - 1);
        e->type = (int)type;
        e->version = 0;
        incr_hash_insert(is, ei);
    }
    return ++e->version;
}

int miku_incr_set_version(miku_incr_sync_t *is, miku_incr_type_t type,
                            const char *owner_id, int64_t version) {
    if (!is || !owner_id) return -1;
    version_entry_t *e = find_entry(is, type, owner_id);
    if (!e) {
        if (is->entry_count >= MK_INCR_MAX_VERSIONS) return -1;
        int ei = is->entry_count++;
        e = &is->entries[ei];
        memset(e, 0, sizeof(*e));
        strncpy(e->owner_id, owner_id, sizeof(e->owner_id) - 1);
        e->type = (int)type;
        incr_hash_insert(is, ei);
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
