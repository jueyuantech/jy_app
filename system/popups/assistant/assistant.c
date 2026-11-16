/**
 * @file assistant.c
 * @brief Assistant 弹窗模块生命周期实现。
 */
#include "assistant.h"

#include "app_def.h"
#include "common/app_framework/app_router.h"
#include "floatair_fs.h"
#include "product_app.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <string.h>

static bool s_assistant_service_ready = false;          ///< assistant STT 服务是否已初始化。
static bool s_assistant_service_suspended = false;      ///< 是否已挂起底层 STT 服务状态。
static stt_service_snapshot_t s_assistant_stt_snapshot; ///< assistant 打开前的底层 STT 状态快照。
uint32_t assistant_get_close_event(void) {
    static uint32_t s_assistant_close_event_id = 0; ///< assistant 关闭事件 ID。

    if (s_assistant_close_event_id == 0) {
        s_assistant_close_event_id = lv_event_register_id();
    }
    return s_assistant_close_event_id;
}

/**
 * @brief 判断当前应用是否需要忽略 Assistant 打开请求。
 * @return `true` 表示需要忽略，`false` 表示允许打开。
 */
static bool assistant_should_ignore_open_request(void) {
    return product_app_name_has_capability(
        app_router_get_app(),
        PRODUCT_APP_CAP_ASSISTANT_OPEN_BLOCKED);
}

/**
 * @brief 释放 assistant 占用的 STT 服务资源。
 * @return 无返回值。
 */
static void assistant_release_service(void) {
    if (!s_assistant_service_ready) {
        return;
    }

    if (s_assistant_service_suspended) {
        stt_service_resume(&s_assistant_stt_snapshot);
        s_assistant_service_suspended = false;
    } else {
        stt_service_deinit();
    }
    s_assistant_service_ready = false;
}

/**
 * @brief 确保 assistant 依赖的 STT 服务已初始化。
 * @return `true` 表示服务可用，`false` 表示初始化失败。
 */
static bool assistant_ensure_service(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    const char* assistant_name = product_app_role_name(PRODUCT_APP_ROLE_ASSISTANT);

    if (s_assistant_service_ready) {
        return true;
    }

    if (assistant_name == NULL ||
        !floatair_fs_get_app_config_file(assistant_name, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return false;
    }

    if (!stt_service_suspend(&s_assistant_stt_snapshot)) {
        floatair_err("suspend stt service failed");
        return false;
    }
    s_assistant_service_suspended = true;
    stt_service_init(config_path);
    stt_config.textMode = TEXTMODE_HISTORY;
    stt_config.transMode = TRANSMODE_SHOW_DUAL;
    s_assistant_service_ready = true;
    return true;
}

/**
 * @brief 打开 assistant 弹窗并初始化依赖资源。
 * @return `true` 表示打开成功，`false` 表示打开失败。
 */
bool assistant_open(bool report_open) {
    const char* current_app = app_router_get_app();
    bool was_open = assistant_is_open();

    if (assistant_should_ignore_open_request()) {
        floatair_info("ignore assistant open request in current_app=%s", current_app);
        return true;
    }

    if (!assistant_ensure_service()) {
        return false;
    }

    if (!assistant_popup_open()) {
        (void)assistant_close(false);
        return false;
    }

    assistant_stt_clear();
    if (report_open && !was_open) {
        (void)system_report_assistant_open();
    }
    if (PRODUCT_BT_DISCONNECT_POLICY_APP) {
        system_ui_sync_shell_state();
    }
    return true;
}

/**
 * @brief 关闭 assistant 弹窗并释放依赖资源。
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return `true` 表示关闭成功，`false` 表示关闭失败。
 */
bool assistant_close(bool report_close) {
    (void)assistant_popup_close(report_close);
    assistant_release_service();
    return true;
}

/**
 * @brief 处理 assistant popup 被外部删除后的生命周期清理。
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return 无返回值。
 */
void assistant_on_popup_deleted(bool report_close) {
    if (report_close) {
        (void)system_report_assistant_close();
    }
    assistant_release_service();
    if (PRODUCT_BT_DISCONNECT_POLICY_APP) {
        system_ui_sync_shell_state();
    }
}
