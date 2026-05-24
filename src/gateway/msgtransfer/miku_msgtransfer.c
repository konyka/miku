#include "miku_msgtransfer.h"
#include "miku_log.h"
#include <stdlib.h>

struct miku_msgtransfer_s {
    int running;
};

miku_msgtransfer_t *miku_msgtransfer_create(void) {
    return (miku_msgtransfer_t *)calloc(1, sizeof(miku_msgtransfer_t));
}
void miku_msgtransfer_destroy(miku_msgtransfer_t *mt) { free(mt); }

int miku_msgtransfer_start(miku_msgtransfer_t *mt) {
    if (!mt) return -1;
    mt->running = 1;
    MK_LOG_INFO("MsgTransfer: started");
    return 0;
}
int miku_msgtransfer_stop(miku_msgtransfer_t *mt) {
    if (!mt) return -1;
    mt->running = 0;
    MK_LOG_INFO("MsgTransfer: stopped");
    return 0;
}
