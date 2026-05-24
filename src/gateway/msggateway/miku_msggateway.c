#include "miku_msggateway.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

struct miku_msggw_s {
    int port;
    int running;
};

miku_msggw_t *miku_msggw_create(int port) {
    miku_msggw_t *gw = (miku_msggw_t *)calloc(1, sizeof(*gw));
    if (gw) gw->port = port;
    return gw;
}
void miku_msggw_destroy(miku_msggw_t *gw) { free(gw); }

int miku_msggw_start(miku_msggw_t *gw) {
    if (!gw) return -1;
    gw->running = 1;
    MK_LOG_INFO("MsgGateway: listening on :%d (ws)", gw->port);
    /* TODO: integrate miku_websocket_server + epoll event loop */
    return 0;
}
int miku_msggw_stop(miku_msggw_t *gw) {
    if (!gw) return -1;
    gw->running = 0;
    MK_LOG_INFO("MsgGateway: stopped");
    return 0;
}
