#include "miku_msggateway.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

struct miku_msggw_s {
    int                  port;
    int                  listen_fd;
    int                  running;
    miku_msggw_client_t  clients[MK_GW_MAX_CLIENTS];
    int                  client_count;
};

miku_msggw_t *miku_msggw_create(int port) {
    miku_msggw_t *gw = (miku_msggw_t *)calloc(1, sizeof(*gw));
    if (gw) gw->port = port;
    return gw;
}

void miku_msggw_destroy(miku_msggw_t *gw) { free(gw); }

int miku_msggw_start(miku_msggw_t *gw) {
    if (!gw) return -1;

    gw->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (gw->listen_fd < 0) {
        MK_LOG_ERROR("MsgGateway socket() failed: %s", strerror(errno));
        return -1;
    }
    int opt = 1;
    setsockopt(gw->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)gw->port);

    if (bind(gw->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        MK_LOG_ERROR("MsgGateway bind(:%d) failed: %s", gw->port, strerror(errno));
        close(gw->listen_fd);
        return -1;
    }
    if (listen(gw->listen_fd, 128) < 0) {
        MK_LOG_ERROR("MsgGateway listen() failed: %s", strerror(errno));
        close(gw->listen_fd);
        return -1;
    }

    gw->running = 1;
    MK_LOG_INFO("MsgGateway: listening on :%d (ws)", gw->port);
    return 0;
}

int miku_msggw_stop(miku_msggw_t *gw) {
    if (!gw) return -1;
    gw->running = 0;
    if (gw->listen_fd >= 0) {
        close(gw->listen_fd);
        gw->listen_fd = -1;
    }
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].fd >= 0)
            close(gw->clients[i].fd);
    }
    gw->client_count = 0;
    MK_LOG_INFO("MsgGateway: stopped");
    return 0;
}

int miku_msggw_client_count(miku_msggw_t *gw) {
    if (!gw) return 0;
    int n = 0;
    for (int i = 0; i < gw->client_count; i++)
        if (gw->clients[i].online) n++;
    return n;
}

int miku_msggw_broadcast(miku_msggw_t *gw, const char *msg, size_t len) {
    if (!gw || !msg) return -1;
    int sent = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online) {
            miku_ws_send_text(gw->clients[i].fd, msg, len);
            sent++;
        }
    }
    return sent;
}

int miku_msggw_send_to_user(miku_msggw_t *gw, const char *user_id,
                             const char *msg, size_t len) {
    if (!gw || !user_id || !msg) return -1;
    int sent = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && strcmp(gw->clients[i].user_id, user_id) == 0) {
            miku_ws_send_text(gw->clients[i].fd, msg, len);
            sent++;
        }
    }
    return sent;
}

int miku_msggw_kick_user(miku_msggw_t *gw, const char *user_id) {
    if (!gw || !user_id) return -1;
    int kicked = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && strcmp(gw->clients[i].user_id, user_id) == 0) {
            miku_ws_send_close(gw->clients[i].fd, 1000, "kicked");
            close(gw->clients[i].fd);
            gw->clients[i].online = false;
            gw->clients[i].fd = -1;
            kicked++;
        }
    }
    return kicked;
}
