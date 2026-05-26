#include "miku_seq.h"
#include <stdlib.h>
#include <string.h>

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
    seq_entry_t      entries[MK_SEQ_MAX_CONVERSATIONS];
    int              entry_count;
    user_read_entry_t user_reads[MK_SEQ_MAX_CONVERSATIONS];
    int              user_read_count;
};

miku_seq_t *miku_seq_create(void) {
    return (miku_seq_t *)calloc(1, sizeof(miku_seq_t));
}

void miku_seq_destroy(miku_seq_t *seq) { free(seq); }

static seq_entry_t *find_seq(miku_seq_t *seq, const char *conv_id) {
    for (int i = 0; i < seq->entry_count; i++) {
        if (strcmp(seq->entries[i].conv_id, conv_id) == 0) return &seq->entries[i];
    }
    return NULL;
}

int64_t miku_seq_next(miku_seq_t *seq, const char *conversation_id) {
    if (!seq || !conversation_id) return -1;
    seq_entry_t *e = find_seq(seq, conversation_id);
    if (!e) {
        if (seq->entry_count >= MK_SEQ_MAX_CONVERSATIONS) return -1;
        e = &seq->entries[seq->entry_count++];
        strncpy(e->conv_id, conversation_id, sizeof(e->conv_id) - 1);
        e->current = 0;
    }
    return __sync_add_and_fetch(&e->current, 1);
}

int64_t miku_seq_current(miku_seq_t *seq, const char *conversation_id) {
    if (!seq || !conversation_id) return 0;
    seq_entry_t *e = find_seq(seq, conversation_id);
    return e ? e->current : 0;
}

int miku_seq_set(miku_seq_t *seq, const char *conversation_id, int64_t val) {
    if (!seq || !conversation_id) return -1;
    seq_entry_t *e = find_seq(seq, conversation_id);
    if (!e) {
        if (seq->entry_count >= MK_SEQ_MAX_CONVERSATIONS) return -1;
        e = &seq->entries[seq->entry_count++];
        strncpy(e->conv_id, conversation_id, sizeof(e->conv_id) - 1);
    }
    e->current = val;
    return 0;
}

int miku_seq_set_user_read(miku_seq_t *seq, const char *user_id,
                             const char *conversation_id, int64_t read_seq) {
    if (!seq || !user_id || !conversation_id) return -1;
    for (int i = 0; i < seq->user_read_count; i++) {
        if (strcmp(seq->user_reads[i].user_id, user_id) == 0 &&
            strcmp(seq->user_reads[i].conv_id, conversation_id) == 0) {
            seq->user_reads[i].read_seq = read_seq;
            return 0;
        }
    }
    if (seq->user_read_count >= MK_SEQ_MAX_CONVERSATIONS) return -1;
    user_read_entry_t *ur = &seq->user_reads[seq->user_read_count++];
    strncpy(ur->user_id, user_id, sizeof(ur->user_id) - 1);
    strncpy(ur->conv_id, conversation_id, sizeof(ur->conv_id) - 1);
    ur->read_seq = read_seq;
    return 0;
}

int64_t miku_seq_get_user_read(miku_seq_t *seq, const char *user_id,
                                 const char *conversation_id) {
    if (!seq || !user_id || !conversation_id) return 0;
    for (int i = seq->user_read_count - 1; i >= 0; i--) {
        if (strcmp(seq->user_reads[i].user_id, user_id) == 0 &&
            strcmp(seq->user_reads[i].conv_id, conversation_id) == 0)
            return seq->user_reads[i].read_seq;
    }
    return 0;
}
