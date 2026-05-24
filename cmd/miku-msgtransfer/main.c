#include "miku_common.h"
#include "miku_log.h"

#include <signal.h>

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(void) {
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    miku_log_init(NULL, MK_LOG_DEBUG);
    MK_LOG_INFO("miku-msgtransfer starting");

    MK_LOG_INFO("miku-msgtransfer shutting down");
    return 0;
}
