/**
 * @file music.c
 * @brief Music 应用生命周期、消息注册和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_music
 */
#include <time.h>
#include "music.h"

#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_nav.h"
#include "message.h"
#include "system/system.h"
#include "app_def.h"

static app_message_t music_msg = {
    .id   = APP_MSG_ID_MUSIC,
    .name = APP_NAME_MUSIC,
    .cb   = music_route_cmd,
};

static bool s_music_msg_registered = false;

static bool music_msg_register_once(void) {
    int ret = 0;

    if (s_music_msg_registered) {
        return true;
    }

    ret = app_msg_register(&music_msg);
    if (ret != 0) {
        return false;
    }
    s_music_msg_registered = true;
    return true;
}

static void music_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_music_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_MUSIC);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_music_msg_registered = false;
}

static void music_app_on_start(void) {
    if (!music_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)music_page_get(), NULL, 0)) {
        floatair_assert(false, "music page replace failed");
    }
}

static void music_app_on_stop(void) {
    music_msg_unregister_if_needed();
    music_avrcp_clear();
}

static app_t s_music_app = {
    .name = APP_NAME_MUSIC,
    .on_start = music_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = music_app_on_stop,
    .on_back = NULL,
};

bool music_app_register(void) {
    return app_manager_register(&s_music_app);
}
