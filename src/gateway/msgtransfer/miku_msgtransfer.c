#include "miku_msgtransfer.h"
#include "miku_log.h"
#include "miku_spinlock.h"
#include <stdlib.h>
#include <string.h>

struct miku_msgtransfer_s {
    miku_msg_t    queue[MK_MT_QUEUE_SIZE];
    int           head;
    int           tail;
    int           count;
    int64_t       total_processed;
    int           running;
    miku_spinlock_t lock;
};

miku_msgtransfer_t *miku_msgtransfer_create(void) {
    return (miku_msgtransfer_t *)calloc(1, sizeof(miku_msgtransfer_t));
}
void miku_msgtransfer_destroy(miku_msgtransfer_t *mt) { free(mt); }

int miku_msgtransfer_start(miku_msgtransfer_t *mt) {
    if (!mt) return -1;
    mt->running = 1;
    MK_LOG_INFO("MsgTransfer: started (queue cap: %d)", MK_MT_QUEUE_SIZE);
    return 0;
}
int miku_msgtransfer_stop(miku_msgtransfer_t *mt) {
    if (!mt) return -1;
    mt->running = 0;
    MK_LOG_INFO("MsgTransfer: stopped (processed: %ld, pending: %d)",
                (long)mt->total_processed, mt->count);
    return 0;
}

int miku_msgtransfer_enqueue(miku_msgtransfer_t *mt, const miku_msg_t *msg) {
    if (!mt || !msg) return -1;
    miku_spinlock_lock(&mt->lock);
    if (mt->count >= MK_MT_QUEUE_SIZE) {
        miku_spinlock_unlock(&mt->lock);
        return -1;
    }
    mt->queue[mt->tail] = *msg;
    mt->tail = (mt->tail + 1) % MK_MT_QUEUE_SIZE;
    mt->count++;
    miku_spinlock_unlock(&mt->lock);
    return 0;
}

int miku_msgtransfer_dequeue(miku_msgtransfer_t *mt, miku_msg_t *out) {
    if (!mt || !out) return -1;
    miku_spinlock_lock(&mt->lock);
    if (mt->count == 0) {
        miku_spinlock_unlock(&mt->lock);
        return -1;
    }
    *out = mt->queue[mt->head];
    mt->head = (mt->head + 1) % MK_MT_QUEUE_SIZE;
    mt->count--;
    mt->total_processed++;
    miku_spinlock_unlock(&mt->lock);
    return 0;
}

int miku_msgtransfer_pending(miku_msgtransfer_t *mt) {
    if (!mt) return 0;
    return mt->count;
}

int64_t miku_msgtransfer_total_processed(miku_msgtransfer_t *mt) {
    if (!mt) return 0;
    return mt->total_processed;
}
