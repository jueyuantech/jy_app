/**
 * @file reader.c
 * @brief Reader 应用生命周期、消息注册和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_reader
 */
#include "reader.h"

#include "common/app_framework/app_nav.h"
#include "message.h"
#include "app_def.h"
#include "system/system.h"
#include "common/app_framework/app_manager.h"

static app_message_t reader_msg = {
    .id   = APP_MSG_ID_READER,
    .name = APP_NAME_READER,
    .cb   = reader_route_cmd,
};

static bool s_reader_msg_registered = false;

static bool reader_msg_register_once(void) {
    int ret = 0;

    if (s_reader_msg_registered) {
        return true;
    }

    ret = app_msg_register(&reader_msg);
    if (ret != 0) {
        return false;
    }
    s_reader_msg_registered = true;
    return true;
}

static void reader_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_reader_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_READER);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_reader_msg_registered = false;
}

static void reader_app_on_start(void) {
    if (!reader_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)reader_page_get(), NULL, 0)) {
        floatair_assert(false, "reader page replace failed");
    }
}

static void reader_app_on_stop(void) {
    reader_msg_unregister_if_needed();
    reader_view_reset();
}

static app_t s_reader_app = {
    .name = APP_NAME_READER,
    .on_start = reader_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = reader_app_on_stop,
    .on_back = NULL,
};

bool reader_app_register(void) {
    (void)system_factoryreset_register(APP_NAME_READER, reader_config_reset_to_default);
    return app_manager_register(&s_reader_app);
}
