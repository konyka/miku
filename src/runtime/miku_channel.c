#include "miku_channel.h"
#include "miku_thread.h"
#include <stdlib.h>

struct miku_channel_s {
    void         **buf;
    size_t         cap;
    size_t         head;
    size_t         tail;
    size_t         count;
    bool           closed;
    miku_mutex_t   lock;
    miku_cond_t    send_cond;
    miku_cond_t    recv_cond;
};

miku_channel_t *miku_channel_create(size_t capacity) {
    miku_channel_t *ch = (miku_channel_t *)calloc(1, sizeof(*ch));
    if (!ch) return NULL;

    ch->cap = capacity > 0 ? capacity : 1;
    ch->buf = (void **)calloc(ch->cap, sizeof(void *));
    if (!ch->buf) { free(ch); return NULL; }

    miku_mutex_init(&ch->lock);
    miku_cond_init(&ch->send_cond);
    miku_cond_init(&ch->recv_cond);
    return ch;
}

int miku_channel_send(miku_channel_t *ch, void *val) {
    if (!ch || ch->closed) return -1;
    miku_mutex_lock(&ch->lock);
    while (ch->count >= ch->cap && !ch->closed)
        miku_cond_wait(&ch->send_cond, &ch->lock);
    if (ch->closed) { miku_mutex_unlock(&ch->lock); return -1; }
    ch->buf[ch->tail] = val;
    ch->tail = (ch->tail + 1) % ch->cap;
    ch->count++;
    miku_cond_signal(&ch->recv_cond);
    miku_mutex_unlock(&ch->lock);
    return 0;
}

void *miku_channel_recv(miku_channel_t *ch) {
    if (!ch) return NULL;
    miku_mutex_lock(&ch->lock);
    while (ch->count == 0 && !ch->closed)
        miku_cond_wait(&ch->recv_cond, &ch->lock);
    if (ch->count == 0) { miku_mutex_unlock(&ch->lock); return NULL; }
    void *val = ch->buf[ch->head];
    ch->buf[ch->head] = NULL;
    ch->head = (ch->head + 1) % ch->cap;
    ch->count--;
    miku_cond_signal(&ch->send_cond);
    miku_mutex_unlock(&ch->lock);
    return val;
}

size_t miku_channel_len(const miku_channel_t *ch) {
    return ch ? ch->count : 0;
}

bool miku_channel_closed(const miku_channel_t *ch) {
    return ch ? ch->closed : true;
}

void miku_channel_close(miku_channel_t *ch) {
    if (!ch) return;
    miku_mutex_lock(&ch->lock);
    ch->closed = true;
    miku_cond_broadcast(&ch->send_cond);
    miku_cond_broadcast(&ch->recv_cond);
    miku_mutex_unlock(&ch->lock);
}

void miku_channel_destroy(miku_channel_t *ch) {
    if (!ch) return;
    miku_channel_close(ch);
    free(ch->buf);
    miku_mutex_destroy(&ch->lock);
    miku_cond_destroy(&ch->send_cond);
    miku_cond_destroy(&ch->recv_cond);
    free(ch);
}
