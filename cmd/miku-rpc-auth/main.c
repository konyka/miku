#include "miku_common.h"
#include "miku_log.h"
#include "miku_auth.h"
#include "miku_json.h"
#include "miku_string.h"
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

static volatile int g_running = 1;
static void signal_handler(int sig) { (void)sig; g_running = 0; }

int main(int argc, char **argv) {
    int port = 10100;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
    }
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);
    miku_log_init(NULL, MK_LOG_DEBUG);
    MK_LOG_INFO("miku-rpc-auth starting on :%d", port);

    miku_auth_service_t *svc = miku_auth_service_create();
    if (!svc) { MK_LOG_ERROR("Failed to create auth service"); return 1; }

    int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(lfd, 64) < 0) {
        MK_LOG_ERROR("auth bind/listen failed: %s", strerror(errno));
        close(lfd); miku_auth_service_destroy(svc); return 1;
    }
    MK_LOG_INFO("miku-rpc-auth ready");

    while (g_running) {
        fd_set fds; FD_ZERO(&fds); FD_SET(lfd, &fds);
        struct timeval tv = {0, 100000};
        if (select(lfd + 1, &fds, NULL, NULL, &tv) <= 0) continue;
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) continue;
        char buf[4096] = {0};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            miku_json_val_t *j = miku_json_parse_str(buf);
            const char *uid = j ? miku_json_str(miku_json_get(j, "userID")) : NULL;
            const char *secret = j ? miku_json_str(miku_json_get(j, "secret")) : NULL;
            int64_t plat = j ? miku_json_int(miku_json_get(j, "platformID")) : 0;
            char token[512] = {0};
            int rc = miku_auth_user_token(svc, uid, secret, (int)plat, token, sizeof(token));
            miku_json_val_t *out = miku_json_create_object();
            miku_json_object_set(out, "errCode", miku_json_create_int(rc == 0 ? 0 : 401));
            if (rc == 0) miku_json_object_set(out, "token", miku_json_create_str(token));
            miku_string_t *s = miku_json_stringify(out);
            write(fd, s->data, s->len);
            miku_str_destroy(s);
            miku_json_destroy(out);
            if (j) miku_json_destroy(j);
        }
        close(fd);
    }

    MK_LOG_INFO("miku-rpc-auth shutting down");
    close(lfd);
    miku_auth_service_destroy(svc);
    return 0;
}
