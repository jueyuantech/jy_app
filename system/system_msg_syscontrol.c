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
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "common/app_framework/app_router.h"
#include "system/system.h"
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
    if (!app_router_set_app(view, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("set app failed");
        return app_mpack_send_ack(msg, app_router_is_busy() ? ErrNotReady : ErrBadParam);
    }
    if (floatair_lcd_get_state() == LCD_OFF) {
        floatair_lcd_set_state(LCD_ON);
        system_report_sys_state(1);
        app_sleep_timer_reset();
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
    {"openAssistant", assistant_open_cmd},
    {"updateAssistantSttInfo", assistant_update_stt_info_cmd},
    {"closeAssistant", assistant_close_cmd},
    {"sendHeartbeat", system_systemcontrol_sendheartbeat},
    {"sendKeepAlive", system_systemcontrol_sendkeepalive},
    {"sendHandshake", system_systemcontrol_sendhandshake},
};
const size_t system_systemcontrol_cmd_funcs_count =
    sizeof(system_systemcontrol_cmd_funcs) / sizeof(system_systemcontrol_cmd_funcs[0]);
