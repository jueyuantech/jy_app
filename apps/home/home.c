/**
 * @file home.c
 * @brief Home 应用生命周期、消息注册和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_home
 */
#include "home.h"

#include "common/app_framework/app_nav.h"
#include "message.h"
#include "app_def.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "sys_adapter.h"

#include <string.h>

static bool s_home_msg_registered = false;    ///< Home 消息是否已注册

static bool home_msg_cb(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    if (!msg) {
        return false;
    }
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static app_message_t home_msg = {
    .id   = APP_MSG_ID_HOME,
    .name = APP_NAME_HOME,
    .cb   = home_msg_cb,
};

/**
 * @brief 注册 Home 消息处理器。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
static bool home_msg_register_once(void) {
    int ret = 0;

    if (s_home_msg_registered) {
        return true;
    }

    ret = app_msg_register(&home_msg);
    if (ret != 0) {
        return false;
    }
    s_home_msg_registered = true;
    return true;
}

/**
 * @brief 注销 Home 消息处理器。
 * @return 无返回值。
 */
static void home_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_home_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_HOME);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_home_msg_registered = false;
}

/**
 * @brief Home App 启动。
 * @return 无返回值。
 */
static void home_app_on_start(void) {
    if (!home_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }

    if (!app_nav_replace((app_page_t*)home_page_get(), NULL, 0)) {
        floatair_assert(false, "home page replace failed");
    }
}

static app_t s_home_app = {
    .name = APP_NAME_HOME,
    .on_start = home_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = home_msg_unregister_if_needed,
    .on_back = NULL,
};

bool home_app_register(void) {
    return app_manager_register(&s_home_app);
}
