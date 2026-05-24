#include "miku_push.h"
#include "miku_log.h"
#include <stdlib.h>

struct miku_push_s {
    int running;
};

miku_push_t *miku_push_create(void) {
    return (miku_push_t *)calloc(1, sizeof(miku_push_t));
}
void miku_push_destroy(miku_push_t *p) { free(p); }

int miku_push_start(miku_push_t *p) {
    if (!p) return -1;
    p->running = 1;
    MK_LOG_INFO("Push: started");
    return 0;
}
int miku_push_stop(miku_push_t *p) {
    if (!p) return -1;
    p->running = 0;
    MK_LOG_INFO("Push: stopped");
    return 0;
}
