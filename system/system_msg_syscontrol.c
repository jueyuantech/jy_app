/**
 * @file system_msg_syscontrol.c
 * @brief 系统控制消息解析、App 切换和视图查询实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include <time.h>
#include "system/popups/assistant/assistant.h"
#include "app_def.h"
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "common/app_framework/app_router.h"
#include "common/guide_runtime.h"
#include "system/system.h"
#include "system/system_file_transfer.h"
#include "system/system_runtime_ui.h"
#include "app_lcd.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "sys_adapter.h"

static bool system_systemcontrol_unbind(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemcontrol_factoryreset(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    if (!system_cfgfile_reset_to_default()) {
        floatair_err("system_cfgfile_reset_to_default failed");
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    system_factoryreset_invoke();
    floatair_lcd_set_brightness(system_config_get_brightness());
    system_sync_config_to_device();
    system_request_device_state();
    app_sleep_timer_reset();
    (void)system_request_bt_reset_pair();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemcontrol_reboot(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemcontrol_recovery(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemcontrol_getview(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "view");
    mpack_write_cstr(&writer->writer, app_router_get_app());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemcontrol_setview(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char view[MSG_STR_MAX_LEN] = {0};
    if (!app_msg_get_str(node, "viewName", view, sizeof(view))) {
        floatair_err("view is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("view %s", view);
    if (app_router_is_busy()) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (floatair_lcd_get_state() == LCD_OFF) {
        system_set_sys_state(1);
        (void)system_report_sys_state(1);
    }
    if (!app_router_set_app(view, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("set app failed");
        return app_mpack_send_ack(msg, app_router_is_busy() ? ErrNotReady : ErrBadParam);
    }
    floatair_info("view %s done", view);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemcontrol_sendtouchevent(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t event = 0;
    if (!app_msg_get_u8(node, false, "event", &event)) {
        floatair_err("event is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!system_touch_event(event)) {
        floatair_err("touch event failed");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理打开新手引导命令。
 * @param[in] node 消息数据节点，当前未使用。
 * @param[in] msg 原始消息包，用于回复 ACK/NCK。
 * @return `true` 表示回复发送成功，`false` 表示回复发送失败。
 */
static bool system_systemcontrol_openguide(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    char previous_progress[MSG_STR_MAX_LEN] = {0};
    const char* progress = NULL;

    floatair_assert(msg != NULL, "msg is NULL");

    if (app_router_is_busy()) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    progress = system_config_get_userguide();
    if (progress != NULL) {
        strncpy(previous_progress, progress, sizeof(previous_progress));
        previous_progress[sizeof(previous_progress) - 1] = '\0';
    }
    guide_runtime_reset();
    if (!system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_FALSE)) {
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    if (!app_router_set_app(APP_NAME_GUIDE, APP_ROUTER_ENTRY_REMOTE)) {
        if (previous_progress[0] != '\0' && !system_config_set_userguide(previous_progress)) {
            floatair_warn("restore userguide progress failed: %s", previous_progress);
        }
        return app_mpack_send_ack(msg, app_router_is_busy() ? ErrNotReady : ErrBizErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理关闭新手引导命令。
 * @param[in] node 消息数据节点，当前未使用。
 * @param[in] msg 原始消息包，用于回复 ACK/NCK。
 * @return `true` 表示回复发送成功，`false` 表示回复发送失败。
 */
static bool system_systemcontrol_closeguide(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    char previous_progress[MSG_STR_MAX_LEN] = {0};
    const char* progress = NULL;

    floatair_assert(msg != NULL, "msg is NULL");

    if (app_router_is_busy()) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    progress = system_config_get_userguide();
    if (progress != NULL) {
        strncpy(previous_progress, progress, sizeof(previous_progress));
        previous_progress[sizeof(previous_progress) - 1] = '\0';
    }
    guide_runtime_reset();
    if (!system_config_set_userguide(SYSTEM_USERGUIDE_PROGRESS_TRUE)) {
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    if (!app_router_set_app(APP_NAME_HOME, APP_ROUTER_ENTRY_REMOTE)) {
        if (previous_progress[0] != '\0' && !system_config_set_userguide(previous_progress)) {
            floatair_warn("restore userguide progress failed: %s", previous_progress);
        }
        return app_mpack_send_ack(msg, app_router_is_busy() ? ErrNotReady : ErrBizErr);
    }
    (void)system_report_view_change(APP_NAME_HOME);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 判断当前页面是否支持显示上传进度。
 * @return `true` 表示当前页面支持上传进度显隐控制，`false` 表示不支持。
 */
static bool system_systemcontrol_upload_progress_page_supported(void) {
    const char* current_app = app_router_get_app();

    return strcmp(current_app, APP_NAME_PROMPTER) == 0 ||
           strcmp(current_app, APP_NAME_GALLERY) == 0;
}

/**
 * @brief 处理自定义进度提示显隐控制指令。
 * @param[in] node 消息数据节点。
 * @param[in] msg 原始消息包，用于回复 ACK/NCK。
 * @return `true` 表示回复发送成功，`false` 表示回复发送失败。
 */
static bool system_systemcontrol_setprogressvisible(mpack_node_t node, msg_pack_t* msg) {
    bool visible = false;
    uint8_t visible_u8 = 0;
    uint8_t opa_percent = 100;
    uint8_t bg_opa = LV_OPA_COVER;
    char text[MSG_STR_MAX_LEN] = {0};
    mpack_node_t text_node;
    mpack_node_t opa_node;
    size_t text_len = 0;
    system_progress_hint_param_t hint_param;

    floatair_assert(msg != NULL, "msg is NULL");
    if (app_msg_get_u8(node, false, "visible", &visible_u8)) {
        visible = (visible_u8 != 0);
    } else if (!app_msg_get_bool(node, false, "visible", &visible)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    if (!visible) {
        hint_param = (system_progress_hint_param_t){
            .visible = false,
        };
        if (!system_ui_send_progress_hint(&hint_param)) {
            return app_mpack_send_ack(msg, ErrNotReady);
        }
        floatair_info("set progress visible 0");
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    text_node = mpack_node_map_cstr_optional(node, "text");
    if (mpack_node_is_missing(text_node) ||
        mpack_node_is_nil(text_node) ||
        mpack_node_type(text_node) != mpack_type_str) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    text_len = mpack_node_strlen(text_node);
    if (text_len >= sizeof(text)) {
        text_len = sizeof(text) - 1;
        floatair_warn("progress text truncated");
    }
    memcpy(text, mpack_node_str(text_node), text_len);
    text[text_len] = '\0';

    opa_node = mpack_node_map_cstr_optional(node, "opa");
    if (mpack_node_is_missing(opa_node) ||
        mpack_node_is_nil(opa_node) ||
        mpack_node_type(opa_node) != mpack_type_uint) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (mpack_node_u64(opa_node) > 100U) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    opa_percent = mpack_node_u8(opa_node);
    bg_opa = (uint8_t)(((uint16_t)opa_percent * LV_OPA_COVER) / 100U);
    hint_param.visible = visible;
    hint_param.text = text;
    hint_param.bg_opa = bg_opa;
    if (!system_ui_send_progress_hint(&hint_param)) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    floatair_info("set progress visible %d opa %u%%", (int)visible, (unsigned)opa_percent);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理文件上传进度显隐控制指令。
 * @param[in] node 消息数据节点。
 * @param[in] msg 原始消息包，用于回复 ACK/NCK。
 * @return `true` 表示回复发送成功，`false` 表示回复发送失败。
 */
static bool system_systemcontrol_setuploadprogressvisible(mpack_node_t node, msg_pack_t* msg) {
    bool visible = false;
    uint8_t visible_u8 = 0;

    floatair_assert(msg != NULL, "msg is NULL");
    if (app_msg_get_u8(node, false, "visible", &visible_u8)) {
        visible = (visible_u8 != 0);
    } else if (!app_msg_get_bool(node, false, "visible", &visible)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    if (!system_systemcontrol_upload_progress_page_supported()) {
        floatair_warn("set upload progress visible ignored, unsupported app=%s", app_router_get_app());
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!system_file_transfer_is_in_progress()) {
        floatair_warn("set upload progress visible ignored, no file transfer");
        return app_mpack_send_ack(msg, ErrNotReady);
    }

    if (!system_file_transfer_set_upload_progress_visible(visible)) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    floatair_info("set upload progress visible %d", (int)visible);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemcontrol_sendheartbeat(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    if (!system_heart_beat()) {
        floatair_err("heart beat failed");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemcontrol_sendkeepalive(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    if (!system_keep_alive()) {
        floatair_err("keep alive failed");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemcontrol_sendhandshake(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

app_cmd_func_t system_systemcontrol_cmd_funcs[] = {
    {"unbind", system_systemcontrol_unbind},
    {"factoryReset", system_systemcontrol_factoryreset},
    {"reboot", system_systemcontrol_reboot},
    {"recovery", system_systemcontrol_recovery},
    {"getView", system_systemcontrol_getview},
    {"setView", system_systemcontrol_setview},
    {"sendTouchEvent", system_systemcontrol_sendtouchevent},
    {"openGuide", system_systemcontrol_openguide},
    {"closeGuide", system_systemcontrol_closeguide},
    {"openAssistant", assistant_open_cmd},
    {"updateAssistantSttInfo", assistant_update_stt_info_cmd},
    {"closeAssistant", assistant_close_cmd},
    {"sendHeartbeat", system_systemcontrol_sendheartbeat},
    {"sendKeepAlive", system_systemcontrol_sendkeepalive},
    {"sendHandshake", system_systemcontrol_sendhandshake},
    {"setProgressVisible", system_systemcontrol_setprogressvisible},
    {"setUploadProgressVisible", system_systemcontrol_setuploadprogressvisible},
};
const size_t system_systemcontrol_cmd_funcs_count =
    sizeof(system_systemcontrol_cmd_funcs) / sizeof(system_systemcontrol_cmd_funcs[0]);
