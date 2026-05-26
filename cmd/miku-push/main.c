#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_push.h"
#include "miku_offline_push.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static miku_graceful_t g_graceful;

int main(int argc, char **argv) {
    const char *config_dir = "config/";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_dir = argv[++i];
    }

    miku_service_config_t sc;
    miku_service_config_load(&sc, config_dir);

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 300);
    MK_LOG_INFO("miku-push starting");

    miku_push_t *push = miku_push_create();
    if (!push) { MK_LOG_ERROR("Failed to create push service"); return 1; }

    miku_offline_push_t *offline = miku_offline_push_create(MK_PUSH_PROVIDER_DUMMY);
    if (!offline) { MK_LOG_ERROR("Failed to create offline push"); miku_push_destroy(push); return 1; }

    miku_push_start(push);

    miku_offline_push_set_token(offline, "demo_user_001", 1, "fcm_demo_token_abc123");
    miku_offline_push_set_token(offline, "demo_user_002", 2, "jpush_demo_token_xyz789");

    MK_LOG_INFO("miku-push ready — online + offline (provider=%s)",
                 miku_offline_push_provider_name(MK_PUSH_PROVIDER_DUMMY));

    miku_offline_push_send(offline, "demo_user_001", 1,
                            "Welcome", "You have a new message", "miku://chat");

    while (miku_graceful_running(&g_graceful)) {
        miku_graceful_wait(&g_graceful, NULL, NULL);
    }

    MK_LOG_INFO("miku-push shutting down");
    miku_push_stop(push);
    miku_push_destroy(push);
    miku_offline_push_destroy(offline);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
