#include "miku_crontask.h"
#include "miku_log.h"
#include <stdlib.h>

struct miku_crontask_s {
    int running;
};

miku_crontask_t *miku_crontask_create(void) {
    return (miku_crontask_t *)calloc(1, sizeof(miku_crontask_t));
}
void miku_crontask_destroy(miku_crontask_t *ct) { free(ct); }

int miku_crontask_start(miku_crontask_t *ct) {
    if (!ct) return -1;
    ct->running = 1;
    MK_LOG_INFO("Crontask: started");
    return 0;
}
int miku_crontask_stop(miku_crontask_t *ct) {
    if (!ct) return -1;
    ct->running = 0;
    MK_LOG_INFO("Crontask: stopped");
    return 0;
}
