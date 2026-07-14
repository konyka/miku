#include "miku_discovery.h"
#include "miku_hash.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

#define MK_DISC_HASH 256

struct miku_discovery_s {
    char                  endpoints[512];
    miku_service_entry_t *entries;
    int                   entry_count;
    int                   entry_cap;
    int16_t              *name_hash; /* -1 empty, else entries[] index */
    int                   hash_cap;
};

static uint32_t name_slot(const char *name, int hash_cap) {
    return (uint32_t)(miku_fnv1a_64(name, strlen(name)) & (uint32_t)(hash_cap - 1));
}

static void disc_hash_insert(miku_discovery_t *d, int ei) {
    uint32_t idx = name_slot(d->entries[ei].name, d->hash_cap);
    for (int n = 0; n < d->hash_cap; n++) {
        if (d->name_hash[idx] < 0) {
            d->name_hash[idx] = (int16_t)ei;
            return;
        }
        idx = (idx + 1) & (uint32_t)(d->hash_cap - 1);
    }
}

static void disc_hash_rebuild(miku_discovery_t *d) {
    for (int i = 0; i < d->hash_cap; i++) d->name_hash[i] = -1;
    for (int i = 0; i < d->entry_count; i++) disc_hash_insert(d, i);
}

static int disc_hash_find(miku_discovery_t *d, const char *name) {
    uint32_t idx = name_slot(name, d->hash_cap);
    for (int n = 0; n < d->hash_cap; n++) {
        int ei = d->name_hash[idx];
        if (ei < 0) return -1;
        if (strcmp(d->entries[ei].name, name) == 0) return ei;
        idx = (idx + 1) & (uint32_t)(d->hash_cap - 1);
    }
    return -1;
}

miku_discovery_t *miku_discovery_create(const char *endpoints) {
    if (!endpoints) return NULL;
    miku_discovery_t *d = (miku_discovery_t *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    strncpy(d->endpoints, endpoints, sizeof(d->endpoints) - 1);
    d->entry_cap = 32;
    d->hash_cap = MK_DISC_HASH;
    d->entries = (miku_service_entry_t *)calloc((size_t)d->entry_cap, sizeof(miku_service_entry_t));
    d->name_hash = (int16_t *)malloc((size_t)d->hash_cap * sizeof(int16_t));
    if (!d->entries || !d->name_hash) {
        free(d->entries);
        free(d->name_hash);
        free(d);
        return NULL;
    }
    for (int i = 0; i < d->hash_cap; i++) d->name_hash[i] = -1;
    return d;
}

void miku_discovery_destroy(miku_discovery_t *d) {
    if (!d) return;
    free(d->entries);
    free(d->name_hash);
    free(d);
}

int miku_discovery_register(miku_discovery_t *d, const char *service_name,
                             const char *host, int port, int64_t ttl_sec) {
    if (!d || !service_name || !host) return -1;
    int ei = disc_hash_find(d, service_name);
    if (ei >= 0) {
        strncpy(d->entries[ei].host, host, sizeof(d->entries[ei].host) - 1);
        d->entries[ei].port = port;
        d->entries[ei].lease_id = ttl_sec;
        MK_LOG_INFO("Service updated: %s -> %s:%d", service_name, host, port);
        return 0;
    }
    if (d->entry_count >= d->entry_cap) {
        int ncap = d->entry_cap * 2;
        miku_service_entry_t *ne = (miku_service_entry_t *)realloc(d->entries,
            (size_t)ncap * sizeof(miku_service_entry_t));
        if (!ne) return -1;
        d->entries = ne;
        d->entry_cap = ncap;
        if (d->entry_count * 2 >= d->hash_cap) {
            int nh = d->hash_cap * 2;
            int16_t *nhash = (int16_t *)malloc((size_t)nh * sizeof(int16_t));
            if (!nhash) return -1;
            free(d->name_hash);
            d->name_hash = nhash;
            d->hash_cap = nh;
            disc_hash_rebuild(d);
        }
    }
    ei = d->entry_count++;
    miku_service_entry_t *e = &d->entries[ei];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, service_name, sizeof(e->name) - 1);
    strncpy(e->host, host, sizeof(e->host) - 1);
    e->port = port;
    e->lease_id = ttl_sec;
    disc_hash_insert(d, ei);
    MK_LOG_INFO("Service registered: %s -> %s:%d", service_name, host, port);
    return 0;
}

int miku_discovery_deregister(miku_discovery_t *d, const char *service_name) {
    if (!d || !service_name) return -1;
    int ei = disc_hash_find(d, service_name);
    if (ei < 0) return -1;
    memmove(&d->entries[ei], &d->entries[ei + 1],
            (size_t)(d->entry_count - ei - 1) * sizeof(miku_service_entry_t));
    d->entry_count--;
    disc_hash_rebuild(d);
    MK_LOG_INFO("Service deregistered: %s", service_name);
    return 0;
}

int miku_discovery_resolve(miku_discovery_t *d, const char *service_name,
                            miku_service_entry_t *entries, int max_entries) {
    if (!d || !service_name || !entries || max_entries <= 0) return 0;
    int ei = disc_hash_find(d, service_name);
    if (ei < 0) return 0;
    entries[0] = d->entries[ei];
    return 1;
}

int miku_discovery_heartbeat(miku_discovery_t *d) {
    if (!d) return -1;
    return 0;
}
