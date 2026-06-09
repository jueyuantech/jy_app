/**
 * @file system_runtime_state.c
 * @brief 系统运行时状态同步实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system/system_runtime_state.h"

#include "system/popups/assistant/assistant.h"
#include "app_lcd.h"
#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_router.h"
#include "home/home.h"
#include "system/popups/notify/notify.h"
#include "common/widgets/toast.h"
#include "system/system_notification.h"
#include "system/popups/notify_list/notify_list.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <inttypes.h>
#include <string.h>

static uint8_t s_battery_percent = 0;   ///< 当前缓存电量百分比
static uint8_t s_charge_state_sys = 0;  ///< 当前缓存充电状态
static uint16_t s_voltage_mv_sys = 0;   ///< 当前缓存电池电压
static bool g_bt_connected = false;     ///< 当前缓存蓝牙连接状态
static bool s_device_state_btconn_synced = false; ///< 启动后是否已用设备快照初始化蓝牙连接态
static bool s_call_seen_ringing = false;       ///< 当前通话流程是否出现过振铃态
static bool s_call_seen_connected = false;     ///< 当前通话流程是否出现过接通态
static char s_call_last_number[64] = {0};      ///< 当前通话流程缓存的来电号码

/**
 * @brief 蓝牙通话建立阶段状态定义。
 */
typedef enum {
    SYSTEM_BT_CALL_EVENT_RINGING = JYT_CALL_EVENT_CALLING,        ///< 来电振铃
    SYSTEM_BT_CALL_EVENT_CONNECTED = JYT_CALL_EVENT_CONNECTED,    ///< 电话接通
    SYSTEM_BT_CALL_EVENT_DISCONNECTED = JYT_CALL_EVENT_DISCONNECTED, ///< 电话断开
} system_bt_call_setup_state_t;

/**
 * @brief 重置当前通话流程缓存。
 * @return 无返回值。
 */
static void system_runtime_state_reset_call_flow(void) {
    floatair_info("reset call flow: ringing=%d connected=%d last_number=%s",
                  (int)s_call_seen_ringing,
                  (int)s_call_seen_connected,
                  s_call_last_number[0] ? s_call_last_number : "N/A");
    s_call_seen_ringing = false;
    s_call_seen_connected = false;
    s_call_last_number[0] = '\0';
}

/**
 * @brief 缓存当前通话号码。
 * @param[in] caller_number 当前号码字符串。
 * @return 无返回值。
 */
static void system_runtime_state_cache_call_number(const char* caller_number) {
    if (caller_number == NULL || caller_number[0] == '\0') {
        return;
    }

    strncpy(s_call_last_number, caller_number, sizeof(s_call_last_number) - 1);
    s_call_last_number[sizeof(s_call_last_number) - 1] = '\0';
    floatair_info("cache call number: %s", s_call_last_number);
}

/**
 * @brief 获取当前缓存电量。
 * @return 返回当前缓存电量百分比。
 */
static uint8_t system_runtime_state_get_battery(void) {
    return s_battery_percent;
}

/**
 * @brief 设置当前缓存电量并同步到底部状态栏。
 * @param[in] battery 当前电量百分比。
 * @return 无返回值。
 */
static void system_runtime_state_set_battery(uint8_t battery) {
    s_battery_percent = battery;
    system_ui_update_battery(battery);
}

/**
 * @brief 获取当前缓存充电状态。
 * @return 返回当前缓存充电状态值。
 */
static uint8_t system_runtime_state_get_charge_state(void) {
    return s_charge_state_sys;
}

/**
 * @brief 设置当前缓存充电状态并同步到底部状态栏。
 * @param[in] charge_state 当前充电状态值。
 * @return 无返回值。
 */
static void system_runtime_state_set_charge_state(uint8_t charge_state) {
    s_charge_state_sys = charge_state;
    system_ui_update_charge_state(charge_state);
}

/**
 * @brief 获取当前缓存电池电压。
 * @return 返回当前缓存电池电压值。
 */
static uint16_t system_runtime_state_get_voltage(void) {
    return s_voltage_mv_sys;
}

/**
 * @brief 设置当前缓存电池电压。
 * @param[in] voltage_mv 当前电池电压值。
 * @return 无返回值。
 */
static void system_runtime_state_set_voltage(uint16_t voltage_mv) {
    s_voltage_mv_sys = voltage_mv;
}

/**
 * @brief 应用一次电池状态快照到运行时缓存，并按需同步 UI/上报。
 * @param[in] bat_status 电池状态快照。
 * @param[in] report_changed `true` 表示状态变化时同步上报，`false` 表示仅刷新本地缓存。
 * @return 无返回值。
 */
static void system_runtime_state_apply_bat_status(union bat_state_t bat_status, bool report_changed) {
    uint8_t soc = bat_status.bat_chg_combo.soc;
    uint16_t voltage_mv = bat_status.bat_chg_combo.voltage_mv;
    uint8_t charge_state = bat_status.bat_chg_combo.charger_mode;

    floatair_info("soc %d, voltage_mv %d, charge_state %d", soc, voltage_mv, charge_state);

    if (soc != system_runtime_state_get_battery()) {
        floatair_dbg("battery changed %d to %d", (int)system_runtime_state_get_battery(), (int)soc);
        system_runtime_state_set_battery(soc);
        if (report_changed) {
            system_report_battery(soc);
        }
    }
    if (voltage_mv != system_runtime_state_get_voltage()) {
        floatair_dbg("voltage changed %d to %d", (int)system_runtime_state_get_voltage(), (int)voltage_mv);
        system_runtime_state_set_voltage(voltage_mv);
    }
    if (charge_state != system_runtime_state_get_charge_state()) {
        floatair_dbg("charge state changed %d to %d",
                     (int)system_runtime_state_get_charge_state(),
                     (int)charge_state);
        system_runtime_state_set_charge_state(charge_state);
        if (report_changed) {
            system_report_charge_state(charge_state);
        }
    }
}

/**
 * @brief 刷新蓝牙连接状态并同步相关 UI。
 * @param[in] connected `true` 表示已连接，`false` 表示未连接。
 * @return 无返回值。
 */
static void system_runtime_state_refresh_btconn_state(bool connected) {
    bool prev_connected = g_bt_connected;
    bool changed = prev_connected != connected;
    const char* current_app = app_router_get_app();
    bool langselection_finished = system_config_get_langselection_finish();

    floatair_info("refresh btconn state: prev=%d, next=%d, changed=%d, app=%s, overlay_target=%d",
                  (int)prev_connected,
                  (int)connected,
                  (int)changed,
                  current_app,
                  (int)!connected);
    if (floatair_lcd_get_state() == LCD_OFF) {
        floatair_lcd_set_state(LCD_ON);
        system_report_sys_state(1);
        app_sleep_timer_reset();
    }

    if (changed && !connected) {
        system_runtime_state_reset_call_flow();
        system_notification_clear();
        toast_dismiss_active();
        (void)notify_list_close();
        (void)assistant_close(false);
        home_view_reset_selection();

        if (!langselection_finished) {
            app_t* active_app = app_manager_current();
            if (current_app[0] == '\0' || active_app == NULL || !active_app->use_top_layer) {
                floatair_info("bt disconnect: language selection unfinished, route to home resolver");
                if (!app_router_call_home()) {
                    floatair_warn("bt disconnect: route to langselection failed, current=%s", current_app);
                }
            }
        } else if (current_app[0] != '\0' && strcmp(current_app, APP_NAME_HOME) != 0) {
            floatair_info("bt disconnect: try switch app to home before showing overlay, current=%s", current_app);
            if (!app_router_set_app(APP_NAME_HOME, APP_ROUTER_ENTRY_LOCAL)) {
                floatair_warn("bt disconnect: switch to home failed, current=%s", current_app);
            } else {
                floatair_info("bt disconnect: switched to home before showing overlay");
            }
        }
        current_app = app_router_get_app();
    }

    g_bt_connected = connected;
    system_ui_sync_shell_state();
    floatair_info("refresh btconn state: overlay request finished, connected=%d, app=%s",
                  (int)connected,
                  current_app);
    if (!changed) {
        return;
    }

    floatair_info("bt connection state changed: %d -> %d", (int)prev_connected, (int)connected);

    if (!langselection_finished) {
        app_t* active_app = app_manager_current();
        if (current_app[0] == '\0' || active_app == NULL || !active_app->use_top_layer) {
            floatair_info("bt connection state changed: language selection unfinished, route to home resolver");
            (void)app_router_call_home();
            return;
        }
    }

    if (strcmp(current_app, APP_NAME_HOME) == 0) {
        floatair_info("bt connection state changed: reload home view");
        home_view_reload();
    }
}

/**
 * @brief 处理设备状态消息并同步时间，启动首次额外同步蓝牙连接态。
 * @param[in] msg 设备状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_device_state(JYT_ELF_MQ_MSG* msg) {
    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }
    if (msg->payload_len != sizeof(jyt_device_state_t)) {
        floatair_err("msg payload len is not match[%d][%d]", msg->payload_len, sizeof(jyt_device_state_t));
        return false;
    }

    floatair_dbg("len %d bytes", msg->payload_len);
    jyt_device_state_t device_state;
    memcpy(&device_state, msg->payload, sizeof(device_state));
    floatair_info("device state update: time_now=%" PRIu64 " host_connected=%u speaker_connected=%u btconn_synced=%d",
                  (uint64_t)device_state.time_now,
                  (unsigned)device_state.host_connected,
                  (unsigned)device_state.speaker_connected,
                  (int)s_device_state_btconn_synced);
    if (!s_device_state_btconn_synced) {
        system_runtime_state_refresh_btconn_state(device_state.host_connected != 0);
        s_device_state_btconn_synced = true;
    }
    if (!system_ui_update_time_from_epoch(device_state.time_now)) {
        return false;
    }

    return true;
}

/**
 * @brief 处理 KWS 命中事件，并在蓝牙已连接时唤醒屏幕和上报关键词命中。
 * @param[in] msg KWS 事件消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_kws_state(JYT_ELF_MQ_MSG* msg) {
    const char* current_app = app_router_get_app();

    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }

    uint32_t kws_hit = msg->Header.simple_data;
    floatair_dbg("kws_hit: %" PRIu32, kws_hit);
    if (!system_config_get_keyword_spotting_enabled()) {
        floatair_info("keyword spotting disabled, ignore kws hit=%" PRIu32, kws_hit);
        return true;
    }
    floatair_info("kws state update: hit=%" PRIu32 " lcd_state=%d current_app=%s",
                  kws_hit,
                  (int)floatair_lcd_get_state(),
                  current_app);
    if (!system_get_btconn_state()) {
        floatair_info("ignore kws assistant action while bt disconnect overlay active");
        return true;
    }

    if (floatair_lcd_get_state() == LCD_OFF) {
        floatair_lcd_set_state(LCD_ON);
        system_report_sys_state(1);
        app_sleep_timer_reset();
    }

    (void)system_report_kws_hit();
    return true;
}

/**
 * @brief 处理来电状态消息并控制来电通知显隐。
 * @param[in] msg 来电状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_handle_call_setup_event(JYT_ELF_MQ_MSG* msg) {
    char caller_number[64] = {0};
    uint8_t raw_state = 0;
    size_t caller_len = 0;

    if (msg == NULL || msg->payload_len == 0) {
        floatair_err("call setup msg invalid");
        return false;
    }

    raw_state = msg->payload[0];
    if (msg->payload_len > 1) {
        caller_len = msg->payload_len - 1U;
        if (caller_len >= sizeof(caller_number)) {
            caller_len = sizeof(caller_number) - 1U;
        }
        memcpy(caller_number, msg->payload + 1, caller_len);
    }

    floatair_info("call setup state=%u, payload_len=%u, caller=%s",
                  (unsigned)raw_state,
                  (unsigned)msg->payload_len,
                  caller_number[0] ? caller_number : "N/A");

    system_runtime_state_cache_call_number(caller_number);

    switch ((system_bt_call_setup_state_t)raw_state) {
        case SYSTEM_BT_CALL_EVENT_CONNECTED:
            s_call_seen_connected = true;
            floatair_info("call flow connected: dismiss notification, caller=%s",
                          s_call_last_number[0] ? s_call_last_number : "N/A");
            system_notification_dismiss_call();
            return true;
        case SYSTEM_BT_CALL_EVENT_DISCONNECTED:
            floatair_info("call flow disconnected: ringing=%d connected=%d bt_connected=%d last_number=%s",
                          (int)s_call_seen_ringing,
                          (int)s_call_seen_connected,
                          (int)system_get_btconn_state(),
                          s_call_last_number[0] ? s_call_last_number : "N/A");
            system_notification_dismiss_call();
            if (s_call_seen_ringing && !s_call_seen_connected) {
                if (system_get_btconn_state()) {
                    system_notification_show_missed_call(s_call_last_number[0] ? s_call_last_number : NULL);
                } else {
                    floatair_info("suppress missed call notification while bt disconnect overlay active");
                }
            }
            system_runtime_state_reset_call_flow();
            return true;
        case SYSTEM_BT_CALL_EVENT_RINGING:
            s_call_seen_ringing = true;
            s_call_seen_connected = false;
            floatair_info("call flow ringing: bt_connected=%d caller=%s",
                          (int)system_get_btconn_state(),
                          caller_number[0] ? caller_number : "N/A");
            if (!system_get_btconn_state()) {
                floatair_info("suppress incoming call notification while bt disconnect overlay active");
                return true;
            }
            floatair_info("show incoming call notification without generated title");
            return system_notification_show_call(NULL,
                                                 caller_number[0] ? caller_number : NULL);
        default:
            floatair_warn("unknown call setup state=%u", (unsigned)raw_state);
            system_notification_dismiss_call();
            system_runtime_state_reset_call_flow();
            return false;
    }
}

/**
 * @brief 处理电池状态消息并同步缓存、状态栏与上报。
 * @param[in] msg 电池状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_bat_status(JYT_ELF_MQ_MSG* msg) {
    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }
    if (msg->payload_len == 0 || msg->payload_len < sizeof(union bat_state_t)) {
        floatair_err("msg payload is NULL or empty[%d][%d]", msg->payload_len, sizeof(union bat_state_t));
        return false;
    }

    union bat_state_t bat_status;
    memcpy(&bat_status, msg->payload, sizeof(bat_status));
    system_runtime_state_apply_bat_status(bat_status, true);
    return true;
}

/**
 * @brief 获取当前缓存充电状态。
 * @return 返回当前缓存充电状态值。
 */
uint8_t system_get_charge_state(void) {
    return system_runtime_state_get_charge_state();
}

/**
 * @brief 获取当前缓存电量。
 * @return 返回当前缓存电量百分比。
 */
uint8_t system_get_battery(void) {
    return system_runtime_state_get_battery();
}

/**
 * @brief 获取当前缓存蓝牙连接状态。
 * @return `true` 表示蓝牙已连接，`false` 表示蓝牙未连接。
 */
bool system_get_btconn_state(void) {
    return g_bt_connected;
}

/**
 * @brief 设置显式主机连接事件状态并刷新综合蓝牙连接态。
 * @param[in] connected `true` 表示显式事件为已连接，`false` 表示显式事件为未连接。
 * @return 无返回值。
 */
void system_set_btconn_state(bool connected) {
    floatair_info("set bt connection state request: connected=%d", (int)connected);
    system_runtime_state_refresh_btconn_state(connected);
}
