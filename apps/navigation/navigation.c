/**
 * @file navigation.c
 * @brief Navigation 应用生命周期、消息注册和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_navigation
 */
#include <time.h>
#include "navigation.h"

#include "common/app_framework/app_nav.h"
#include "common/app_framework/app_manager.h"
#include "message.h"
#include "app_def.h"
#include "system/system.h"

static app_message_t navigation_msg = {
    .id   = APP_MSG_ID_NAVIGATION,
    .name = APP_NAME_NAVIGATION,
    .cb   = navigation_route_cmd,
};

static bool s_navigation_msg_registered = false;

static bool navigation_msg_register_once(void) {
    int ret = 0;

    if (s_navigation_msg_registered) {
        return true;
    }

    ret = app_msg_register(&navigation_msg);
    if (ret != 0) {
        return false;
    }
    s_navigation_msg_registered = true;
    return true;
}

static void navigation_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_navigation_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_NAVIGATION);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_navigation_msg_registered = false;
}

static void navigation_app_on_start(void) {
    if (!navigation_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)navigation_page_get(), NULL, 0)) {
        floatair_assert(false, "navigation page replace failed");
    }
}

static void navigation_app_on_stop(void) {
    navigation_msg_unregister_if_needed();
    navigation_map_clear();
}

static app_t s_navigation_app = {
    .name = APP_NAME_NAVIGATION,
    .on_start = navigation_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = navigation_app_on_stop,
    .on_back = NULL,
};

bool navigation_app_register(void) {
    return app_manager_register(&s_navigation_app);
}
