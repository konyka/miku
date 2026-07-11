#include "miku_seq.h"
#include "miku_hash.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* 2x max conversations for open-addressing load factor ~0.5 */
#define MK_SEQ_HASH 131072

typedef struct {
    char    conv_id[128];
    int64_t current;
} seq_entry_t;

typedef struct {
    char    user_id[64];
    char    conv_id[128];
    int64_t read_seq;
} user_read_entry_t;

struct miku_seq_s {
    seq_entry_t       entries[MK_SEQ_MAX_CONVERSATIONS];
    int               entry_count;
    int               conv_hash[MK_SEQ_HASH]; /* -1 empty, else entries[] index */

    user_read_entry_t user_reads[MK_SEQ_MAX_CONVERSATIONS];
    int               user_read_count;
    int               read_hash[MK_SEQ_HASH];

    pthread_mutex_t   lock;
};

static uint32_t conv_hash_slot(const char *conv_id) {
    return (uint32_t)(miku_fnv1a_64(conv_id, strlen(conv_id)) & (MK_SEQ_HASH - 1));
}

static uint32_t read_hash_slot(const char *user_id, const char *conv_id) {
    uint64_t a = miku_fnv1a_64(user_id, strlen(user_id));
    uint64_t b = miku_fnv1a_64(conv_id, strlen(conv_id));
    return (uint32_t)((a ^ (b * 0x9e3779b97f4a7c15ULL)) & (MK_SEQ_HASH - 1));
}

miku_seq_t *miku_seq_create(void) {
    miku_seq_t *seq = (miku_seq_t *)calloc(1, sizeof(miku_seq_t));
    if (!seq) return NULL;
    for (int i = 0; i < MK_SEQ_HASH; i++) {
        seq->conv_hash[i] = -1;
        seq->read_hash[i] = -1;
    }
    pthread_mutex_init(&seq->lock, NULL);
    return seq;
}

void miku_seq_destroy(miku_seq_t *seq) {
    if (!seq) return;
    pthread_mutex_destroy(&seq->lock);
    free(seq);
}

static seq_entry_t *find_or_create_seq(miku_seq_t *seq, const char *conv_id) {
    uint32_t idx = conv_hash_slot(conv_id);
    for (int n = 0; n < MK_SEQ_HASH; n++) {
        int ei = seq->conv_hash[idx];
        if (ei < 0) {
            if (seq->entry_count >= MK_SEQ_MAX_CONVERSATIONS) return NULL;
            ei = seq->entry_count++;
            seq_entry_t *e = &seq->entries[ei];
            strncpy(e->conv_id, conv_id, sizeof(e->conv_id) - 1);
            e->conv_id[sizeof(e->conv_id) - 1] = '\0';
            e->current = 0;
            seq->conv_hash[idx] = ei;
            return e;
        }
        if (strcmp(seq->entries[ei].conv_id, conv_id) == 0)
            return &seq->entries[ei];
        idx = (idx + 1) & (MK_SEQ_HASH - 1);
    }
    return NULL;
}

static seq_entry_t *find_seq(miku_seq_t *seq, const char *conv_id) {
    uint32_t idx = conv_hash_slot(conv_id);
    for (int n = 0; n < MK_SEQ_HASH; n++) {
        int ei = seq->conv_hash[idx];
        if (ei < 0) return NULL;
        if (strcmp(seq->entries[ei].conv_id, conv_id) == 0)
            return &seq->entries[ei];
        idx = (idx + 1) & (MK_SEQ_HASH - 1);
    }
    return NULL;
}

int64_t miku_seq_next(miku_seq_t *seq, const char *conversation_id) {
    if (!seq || !conversation_id || !conversation_id[0]) return -1;
    pthread_mutex_lock(&seq->lock);
    seq_entry_t *e = find_or_create_seq(seq, conversation_id);
    int64_t v = e ? ++e->current : -1;
    pthread_mutex_unlock(&seq->lock);
    return v;
}

int64_t miku_seq_current(miku_seq_t *seq, const char *conversation_id) {
    if (!seq || !conversation_id) return 0;
    pthread_mutex_lock(&seq->lock);
    seq_entry_t *e = find_seq(seq, conversation_id);
    int64_t v = e ? e->current : 0;
    pthread_mutex_unlock(&seq->lock);
    return v;
}

int miku_seq_set(miku_seq_t *seq, const char *conversation_id, int64_t val) {
    if (!seq || !conversation_id || !conversation_id[0]) return -1;
    pthread_mutex_lock(&seq->lock);
    seq_entry_t *e = find_or_create_seq(seq, conversation_id);
    if (!e) {
        pthread_mutex_unlock(&seq->lock);
        return -1;
    }
    e->current = val;
    pthread_mutex_unlock(&seq->lock);
    return 0;
}

static user_read_entry_t *find_or_create_read(miku_seq_t *seq, const char *user_id,
                                              const char *conversation_id) {
    uint32_t idx = read_hash_slot(user_id, conversation_id);
    for (int n = 0; n < MK_SEQ_HASH; n++) {
        int ei = seq->read_hash[idx];
        if (ei < 0) {
            if (seq->user_read_count >= MK_SEQ_MAX_CONVERSATIONS) return NULL;
            ei = seq->user_read_count++;
            user_read_entry_t *ur = &seq->user_reads[ei];
            strncpy(ur->user_id, user_id, sizeof(ur->user_id) - 1);
            ur->user_id[sizeof(ur->user_id) - 1] = '\0';
            strncpy(ur->conv_id, conversation_id, sizeof(ur->conv_id) - 1);
            ur->conv_id[sizeof(ur->conv_id) - 1] = '\0';
            ur->read_seq = 0;
            seq->read_hash[idx] = ei;
            return ur;
        }
        if (strcmp(seq->user_reads[ei].user_id, user_id) == 0 &&
            strcmp(seq->user_reads[ei].conv_id, conversation_id) == 0)
            return &seq->user_reads[ei];
        idx = (idx + 1) & (MK_SEQ_HASH - 1);
    }
    return NULL;
}

static user_read_entry_t *find_read(miku_seq_t *seq, const char *user_id,
                                    const char *conversation_id) {
    uint32_t idx = read_hash_slot(user_id, conversation_id);
    for (int n = 0; n < MK_SEQ_HASH; n++) {
        int ei = seq->read_hash[idx];
        if (ei < 0) return NULL;
        if (strcmp(seq->user_reads[ei].user_id, user_id) == 0 &&
            strcmp(seq->user_reads[ei].conv_id, conversation_id) == 0)
            return &seq->user_reads[ei];
        idx = (idx + 1) & (MK_SEQ_HASH - 1);
    }
    return NULL;
}

int miku_seq_set_user_read(miku_seq_t *seq, const char *user_id,
                             const char *conversation_id, int64_t read_seq) {
    if (!seq || !user_id || !conversation_id) return -1;
    pthread_mutex_lock(&seq->lock);
    user_read_entry_t *ur = find_or_create_read(seq, user_id, conversation_id);
    if (!ur) {
        pthread_mutex_unlock(&seq->lock);
        return -1;
    }
    ur->read_seq = read_seq;
    pthread_mutex_unlock(&seq->lock);
    return 0;
}

int64_t miku_seq_get_user_read(miku_seq_t *seq, const char *user_id,
                                 const char *conversation_id) {
    if (!seq || !user_id || !conversation_id) return 0;
    pthread_mutex_lock(&seq->lock);
    user_read_entry_t *ur = find_read(seq, user_id, conversation_id);
    int64_t v = ur ? ur->read_seq : 0;
    pthread_mutex_unlock(&seq->lock);
    return v;
}
