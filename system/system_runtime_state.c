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
#include "guide_runtime.h"
#include "product_app_generated.h"
#if defined(APP_NAME_HOME)
#include "home/home.h"
#endif
#include "product_app.h"
#include "system/popups/notify/notify.h"
#include "common/widgets/toast.h"
#include "system/system_notification.h"
#include "system/popups/notify_list/notify_list.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#define SYSTEM_ALS_RAW_MAX_VALUE 2142208U ///< ALS 原始光感最大值，对应最亮环境。

/**
 * @brief ALS 原始值到屏幕亮度的分档映射项。
 */
typedef struct {
    uint32_t raw_max;     ///< 当前档位包含的最大 ALS 原始值。
    uint8_t brightness;   ///< 当前档位对应的 LCD 亮度值，范围 0-255。
} system_als_brightness_level_t;

static const system_als_brightness_level_t s_als_brightness_levels[] = {
    {178517U, 32U},
    {357034U, 64U},
    {714069U, 112U},
    {1071104U, 160U},
    {1606656U, 208U},
    {SYSTEM_ALS_RAW_MAX_VALUE, 255U},
};

static uint8_t s_battery_percent = 0;   ///< 当前缓存电量百分比
static uint8_t s_charge_state_sys = 0;  ///< 当前缓存充电状态
static uint16_t s_voltage_mv_sys = 0;   ///< 当前缓存电池电压
static bool g_bt_connected = false;     ///< 当前缓存蓝牙连接状态
static bool s_device_state_btconn_synced = false; ///< 启动后是否已用设备快照初始化蓝牙连接态
static bool s_call_seen_ringing = false;       ///< 当前通话流程是否出现过振铃态
static bool s_call_seen_connected = false;     ///< 当前通话流程是否出现过接通态
static char s_call_last_number[64] = {0};      ///< 当前通话流程缓存的来电号码
static char s_ancs_call_name[64] = {0};        ///< ANCS 提供的第三方来电联系人名称。
static char s_ancs_call_source[96] = {0};      ///< ANCS 提供的第三方来电 AppIdentifier。
static uint32_t s_ancs_call_time_us = 0;       ///< 最近一次 ANCS 第三方来电信息到达时间。
static bool s_call_uses_ancs_info = false;     ///< 当前 HFP 通话是否使用了 ANCS 来电信息。
static bool s_auto_brightness_valid = false;   ///< 是否已有 ALS 自动亮度目标值。
static uint8_t s_auto_brightness = 0;          ///< 最近一次 ALS 分档得到的自动亮度目标值。
static bool s_btconn_ui_refresh_pending = false; ///< 灭屏期间是否有蓝牙状态需要在亮屏后同步到 UI。
static bool s_btconn_changed_pending = false; ///< 灭屏期间蓝牙连接状态是否发生过变化。
static bool s_btconn_disconnect_cleanup_pending = false; ///< 灭屏期间是否发生过需要补做 UI 清理的蓝牙断连。
static uint32_t s_kws_intercept_reasons = 0; ///< 当前 KWS 软件拦截原因集合。

/** ANCS 来电信息仅用于紧随其后的 HFP 振铃，避免陈旧联系人串到下一通电话。 */
#define SYSTEM_ANCS_CALL_INFO_TIMEOUT_US (5000000U)

/**
 * @brief 判断缓存的 ANCS 来电信息是否仍可用于当前 HFP 事件。
 * @return `true` 表示信息存在且未超时，否则返回 `false`。
 */
static bool system_runtime_state_ancs_call_info_is_fresh(void) {
    return s_ancs_call_name[0] != '\0' &&
           ((uint32_t)GetTimeUs() - s_ancs_call_time_us) <= SYSTEM_ANCS_CALL_INFO_TIMEOUT_US;
}

void system_runtime_state_set_kws_intercept(system_kws_intercept_reason_t reason,
                                            bool blocked) {
    uint32_t previous_reasons = s_kws_intercept_reasons;
    uint32_t reason_mask = (uint32_t)reason;

    if (blocked) {
        s_kws_intercept_reasons |= reason_mask;
    } else {
        s_kws_intercept_reasons &= ~reason_mask;
    }
    if (s_kws_intercept_reasons != previous_reasons) {
        floatair_info("kws software intercept changed: reason=0x%08" PRIx32
                      " blocked=%d reasons=0x%08" PRIx32,
                      reason_mask,
                      (int)blocked,
                      s_kws_intercept_reasons);
    }
}

void system_runtime_state_set_ancs_call_info(const char* caller_name,
                                             const char* source_id) {
    s_ancs_call_name[0] = '\0';
    s_ancs_call_source[0] = '\0';
    if (caller_name != NULL) {
        strncpy(s_ancs_call_name, caller_name, sizeof(s_ancs_call_name) - 1u);
        s_ancs_call_name[sizeof(s_ancs_call_name) - 1u] = '\0';
    }
    if (source_id != NULL) {
        strncpy(s_ancs_call_source, source_id, sizeof(s_ancs_call_source) - 1u);
        s_ancs_call_source[sizeof(s_ancs_call_source) - 1u] = '\0';
    }
    s_ancs_call_time_us = (uint32_t)GetTimeUs();
    floatair_info("cache ANCS call info: source=%s caller=%s",
                  s_ancs_call_source[0] ? s_ancs_call_source : "N/A",
                  s_ancs_call_name[0] ? s_ancs_call_name : "N/A");
    if (s_call_seen_ringing && s_call_last_number[0] == '\0' &&
        s_ancs_call_name[0] != '\0' && system_get_btconn_state()) {
        s_call_uses_ancs_info = true;
        if (!system_notification_update_call_caller(s_ancs_call_name) &&
            !s_call_seen_connected) {
            (void)system_notification_show_call(NULL,
                                               s_ancs_call_name,
                                               NOTIFY_CALL_STATE_RINGING);
        }
    }
}

/**
 * @brief 获取蓝牙连接状态变化事件 ID。
 * @return 返回 LVGL 自定义事件 ID。
 */
uint32_t system_runtime_state_get_btconn_event(void) {
    static uint32_t s_btconn_event_id = 0; ///< 蓝牙连接状态变化事件 ID。

    if (s_btconn_event_id == 0) {
        s_btconn_event_id = lv_event_register_id();
    }
    return s_btconn_event_id;
}

/**
 * @brief 向当前页面通知蓝牙连接状态变化。
 * @param[in] connected `true` 表示已连接，`false` 表示已断开。
 * @return 无返回值。
 */
static void system_runtime_state_notify_btconn_state(bool connected) {
    lv_obj_t* root = app_manager_current_content_root();
    uint8_t state = connected ? 1u : 0u;

    if (root == NULL || !lv_obj_is_valid(root)) {
        floatair_warn("bt connection state event skipped: current page unavailable");
        return;
    }

    floatair_info("send bt connection state %u to app %p", (unsigned)state, root);
    (void)lv_obj_send_event(root, system_runtime_state_get_btconn_event(), &state);
}

/**
 * @brief 判断当前是否存在中断后的新手教学进度。
 * @return `true` 表示存在 step1-step5 进度，`false` 表示未开始或已完成。
 */
static bool system_runtime_state_has_userguide_progress(void) {
    const char* progress = system_config_get_userguide();

    return progress != NULL &&
           strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_FALSE) != 0 &&
           strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_TRUE) != 0;
}

/**
 * @brief 判断当前是否正在新手引导流程中。
 * @param[in] current_app 当前 App 名称。
 * @return `true` 表示正在 Guide 欢迎页或 Home 教学步骤中。
 */
static bool system_runtime_state_is_userguide_active(const char* current_app) {
    const char* guide_app = product_app_role_name(PRODUCT_APP_ROLE_GUIDE);

    return guide_runtime_get_state() != GUIDE_RUNTIME_STATE_IDLE ||
           system_runtime_state_has_userguide_progress() ||
           (guide_app != NULL && current_app != NULL && strcmp(current_app, guide_app) == 0);
}

/**
 * @brief 蓝牙通话建立阶段状态定义。
 */
typedef enum {
    SYSTEM_BT_CALL_EVENT_RINGING = JYT_CALL_EVENT_CALLING,        ///< 来电振铃
    SYSTEM_BT_CALL_EVENT_CONNECTED = JYT_CALL_EVENT_CONNECTED,    ///< 电话接通
    SYSTEM_BT_CALL_EVENT_DISCONNECTED = JYT_CALL_EVENT_DISCONNECTED, ///< 电话断开
    SYSTEM_BT_CALL_EVENT_OUTGOING = JYT_CALL_EVENT_OUTGOING,      ///< 去电拨号或远端响铃
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
    s_ancs_call_name[0] = '\0';
    s_ancs_call_source[0] = '\0';
    s_ancs_call_time_us = 0;
    s_call_uses_ancs_info = false;
}

/**
 * @brief 复制并去除一行文本首尾空白。
 * @param[in] begin 行起始位置。
 * @param[in] end 行结束位置，不包含该位置。
 * @param[out] output 输出字符串。
 * @param[in] output_size 输出容量。
 * @return 无返回值。
 */
static void system_runtime_state_copy_trimmed_line(const char* begin,
                                                   const char* end,
                                                   char* output,
                                                   size_t output_size) {
    size_t length = 0u;

    if (begin == NULL || end == NULL || output == NULL || output_size == 0u) {
        return;
    }
    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        begin++;
    }
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    length = (size_t)(end - begin);
    if (length >= output_size) {
        length = output_size - 1u;
    }
    memcpy(output, begin, length);
    output[length] = '\0';
}

/**
 * @brief 判断两段号码文本去除排版符号后是否表示同一号码。
 * @param[in] lhs 第一段号码文本。
 * @param[in] rhs 第二段号码文本。
 * @return 数字序列相同且非空时返回 `true`。
 */
static bool system_runtime_state_phone_text_equal(const char* lhs, const char* rhs) {
    bool has_digit = false;

    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    while (*lhs != '\0' || *rhs != '\0') {
        while (*lhs == ' ' || *lhs == '\t' || *lhs == '+' || *lhs == '-' ||
               *lhs == '(' || *lhs == ')') {
            lhs++;
        }
        while (*rhs == ' ' || *rhs == '\t' || *rhs == '+' || *rhs == '-' ||
               *rhs == '(' || *rhs == ')') {
            rhs++;
        }
        if ((*lhs != '\0' && (*lhs < '0' || *lhs > '9')) ||
            (*rhs != '\0' && (*rhs < '0' || *rhs > '9'))) {
            return false;
        }
        if (*lhs != *rhs) {
            return false;
        }
        if (*lhs == '\0') {
            break;
        }
        has_digit = true;
        lhs++;
        rhs++;
    }
    return has_digit;
}

/**
 * @brief 解析 HFP 的“联系人换行号码”载荷并生成单行显示文本。
 * @param[in] caller_payload HFP caller 原始文本。
 * @param[out] display_text 联系人或去重后的号码。
 * @param[in] display_size 显示文本容量。
 * @param[out] phone_number 实际号码缓存。
 * @param[in] phone_size 号码缓存容量。
 * @return 无返回值。
 */
static void system_runtime_state_parse_call_caller(const char* caller_payload,
                                                   char* display_text,
                                                   size_t display_size,
                                                   char* phone_number,
                                                   size_t phone_size) {
    const char* first_end = NULL;
    const char* second_begin = NULL;
    const char* second_end = NULL;
    char first_line[64] = {0};
    char second_line[64] = {0};
    const char* display_source = NULL;
    const char* number_source = NULL;

    if (display_text == NULL || display_size == 0u ||
        phone_number == NULL || phone_size == 0u) {
        return;
    }
    display_text[0] = '\0';
    phone_number[0] = '\0';
    if (caller_payload == NULL || caller_payload[0] == '\0') {
        return;
    }

    first_end = caller_payload;
    while (*first_end != '\0' && *first_end != '\r' && *first_end != '\n') {
        first_end++;
    }
    system_runtime_state_copy_trimmed_line(caller_payload,
                                           first_end,
                                           first_line,
                                           sizeof(first_line));
    second_begin = first_end;
    while (*second_begin == '\r' || *second_begin == '\n') {
        second_begin++;
    }
    second_end = second_begin + strlen(second_begin);
    system_runtime_state_copy_trimmed_line(second_begin,
                                           second_end,
                                           second_line,
                                           sizeof(second_line));

    number_source = second_line[0] != '\0' ? second_line : first_line;
    display_source = first_line[0] != '\0' ? first_line : second_line;
    if (second_line[0] != '\0' &&
        system_runtime_state_phone_text_equal(first_line, second_line)) {
        display_source = second_line;
    }
    strncpy(display_text, display_source, display_size - 1u);
    display_text[display_size - 1u] = '\0';
    strncpy(phone_number, number_source, phone_size - 1u);
    phone_number[phone_size - 1u] = '\0';
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
 * @brief 将 ALS 原始光感值映射为 LCD 自动亮度档位。
 * @param[in] raw_value ALS 原始光感值。
 * @return 返回 LCD 亮度值，范围 0-255。
 */
static uint8_t system_runtime_state_als_raw_to_brightness(uint32_t raw_value) {
    uint32_t clamped = raw_value;

    if (clamped > SYSTEM_ALS_RAW_MAX_VALUE) {
        clamped = SYSTEM_ALS_RAW_MAX_VALUE;
    }

    for (size_t i = 0; i < sizeof(s_als_brightness_levels) / sizeof(s_als_brightness_levels[0]); ++i) {
        if (clamped <= s_als_brightness_levels[i].raw_max) {
            return s_als_brightness_levels[i].brightness;
        }
    }

    return s_als_brightness_levels[
        sizeof(s_als_brightness_levels) / sizeof(s_als_brightness_levels[0]) - 1U].brightness;
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
 * @brief 在主机断连时立即清空所有依赖主机连接的业务缓存。
 * @return 无返回值。
 */
static void system_runtime_state_clear_disconnected_host_data(void) {
    app_router_clear_app_config();
    app_message_reset_ancs_state();
    system_runtime_state_reset_call_flow();
    system_notification_clear();
}

/**
 * @brief 将缓存的蓝牙连接状态同步到页面与系统壳层。
 * @param[in] connected `true` 表示当前已连接，`false` 表示当前未连接。
 * @param[in] changed `true` 表示连接状态发生过变化。
 * @param[in] cleanup_disconnected `true` 表示需要执行断连产生的 UI 清理。
 * @return 无返回值。
 */
static void system_runtime_state_apply_btconn_ui(bool connected,
                                                  bool changed,
                                                  bool cleanup_disconnected) {
    const char* current_app = app_router_get_app();
    const char* home_app = product_app_role_name(PRODUCT_APP_ROLE_HOME);
    bool langselection_finished = system_config_get_langselection_finish();

    floatair_info("apply btconn ui: connected=%d, changed=%d, cleanup=%d, app=%s, overlay_target=%d",
                  (int)connected,
                  (int)changed,
                  (int)cleanup_disconnected,
                  current_app,
                  (int)!connected);
    if (cleanup_disconnected) {
        toast_dismiss_active();
        (void)notify_list_close();
        (void)assistant_close(false);
#if defined(APP_NAME_HOME)
        home_view_reset_selection();
#endif

        if (!langselection_finished) {
            app_t* active_app = app_manager_current();
            if (current_app[0] == '\0' || active_app == NULL || !active_app->use_top_layer) {
                floatair_info("bt disconnect cleanup: language selection unfinished, route to home resolver");
                if (!app_router_call_home()) {
                    floatair_warn("bt disconnect cleanup: route to langselection failed, current=%s", current_app);
                }
            }
        } else if (system_runtime_state_is_userguide_active(current_app)) {
            floatair_info("bt disconnect cleanup: keep current app during userguide, current=%s", current_app);
        } else if (home_app != NULL && current_app[0] != '\0' &&
                   strcmp(current_app, home_app) != 0) {
            floatair_info("bt disconnect cleanup: try switch app to home, current=%s", current_app);
            if (!app_router_set_app(home_app, APP_ROUTER_ENTRY_LOCAL)) {
                floatair_warn("bt disconnect cleanup: switch to home failed, current=%s", current_app);
            } else {
                floatair_info("bt disconnect cleanup: switched to home");
            }
        }
        current_app = app_router_get_app();
    }

    system_ui_sync_shell_state();
    floatair_info("apply btconn ui: overlay request finished, connected=%d, app=%s",
                  (int)connected,
                  current_app);
    if (!changed) {
        return;
    }

    floatair_info("bt connection state changed, connected=%d", (int)connected);
    system_runtime_state_notify_btconn_state(connected);

    if (!langselection_finished) {
        app_t* active_app = app_manager_current();
        if (current_app[0] == '\0' || active_app == NULL || !active_app->use_top_layer) {
            floatair_info("bt connection state changed: language selection unfinished, route to home resolver");
            (void)app_router_call_home();
            return;
        }
    }

    if (connected && system_runtime_state_has_userguide_progress()) {
        floatair_info("bt reconnected during userguide, route to guide resume prompt");
        (void)app_router_call_home();
        return;
    }

    if (home_app != NULL && strcmp(current_app, home_app) == 0) {
#if defined(APP_NAME_HOME)
        floatair_info("bt connection state changed: reload home view");
        home_view_reload();
#endif
    }
}

/**
 * @brief 刷新蓝牙连接状态，灭屏时仅缓存并延迟所有 UI 操作。
 * @param[in] connected `true` 表示已连接，`false` 表示未连接。
 * @return 无返回值。
 */
static void system_runtime_state_refresh_btconn_state(bool connected) {
    bool prev_connected = g_bt_connected;
    bool changed = prev_connected != connected;

    g_bt_connected = connected;
    if (changed && !connected) {
        app_sleep_timer_restore_screen_on_scope(
            APP_SLEEP_SCREEN_ON_RESTORE_DISCONNECTED);
        system_runtime_state_clear_disconnected_host_data();
    }
    if (floatair_lcd_is_off()) {
        s_btconn_ui_refresh_pending = true;
        s_btconn_changed_pending = s_btconn_changed_pending || changed;
        s_btconn_disconnect_cleanup_pending =
            s_btconn_disconnect_cleanup_pending || (changed && !connected);
        floatair_info("btconn ui update deferred: lcd off, prev=%d, next=%d, changed=%d, cleanup=%d",
                      (int)prev_connected,
                      (int)connected,
                      (int)s_btconn_changed_pending,
                      (int)s_btconn_disconnect_cleanup_pending);
        return;
    }

    system_runtime_state_apply_btconn_ui(connected, changed, changed && !connected);
}

/**
 * @brief 亮屏后补做灭屏期间延迟的蓝牙状态 UI 同步。
 * @return 无返回值。
 */
void system_runtime_state_flush_pending_after_screen_on(void) {
    bool changed = s_btconn_changed_pending;
    bool cleanup_disconnected = s_btconn_disconnect_cleanup_pending;

    if (floatair_lcd_is_off() || !s_btconn_ui_refresh_pending) {
        return;
    }

    s_btconn_ui_refresh_pending = false;
    s_btconn_changed_pending = false;
    s_btconn_disconnect_cleanup_pending = false;
    floatair_info("flush pending btconn ui: connected=%d, changed=%d, cleanup=%d",
                  (int)g_bt_connected,
                  (int)changed,
                  (int)cleanup_disconnected);
    system_runtime_state_apply_btconn_ui(g_bt_connected, changed, cleanup_disconnected);
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
 * @brief 获取 LCD 亮屏恢复时应使用的亮度。
 * @return 自动亮度开启且已有 ALS 档位时返回自动亮度，否则返回保存的手动亮度。
 */
uint8_t system_runtime_state_get_lcd_resume_brightness(void) {
    if (system_config_get_bl_auto() && s_auto_brightness_valid) {
        return s_auto_brightness;
    }

    return system_config_get_brightness();
}

/**
 * @brief 将最近一次缓存的 ALS 自动亮度立即应用到当前亮屏。
 * @return `true` 表示无需更新或更新成功，`false` 表示亮度上报失败。
 */
bool system_runtime_state_apply_auto_brightness(void) {
    if (!system_config_get_bl_auto() || !s_auto_brightness_valid) {
        return true;
    }
    if (floatair_lcd_is_off()) {
        floatair_info(
            "als brightness deferred while lcd off: %u", (unsigned)s_auto_brightness);
        return true;
    }
    if ((uint8_t)floatair_lcd_get_brightness() == s_auto_brightness) {
        return true;
    }

    floatair_lcd_set_brightness(s_auto_brightness);
    return system_report_brightness(s_auto_brightness);
}

/**
 * @brief 处理 ALS 原始光感数据并在自动亮度开启时刷新屏幕亮度。
 * @param[in] msg ALS 原始数据消息，payload 为 uint32_t 原始光感值。
 * @return `true` 表示处理成功，`false` 表示消息格式错误。
 */
bool system_update_als_raw_data(JYT_ELF_MQ_MSG* msg) {
    uint32_t raw_value = 0;
    uint8_t brightness = 0;

    if (msg == NULL) {
        floatair_err("als raw msg is NULL");
        return false;
    }
    if (msg->payload_len < sizeof(raw_value)) {
        floatair_err("invalid als raw payload_len: %d", msg->payload_len);
        return false;
    }

    memcpy(&raw_value, msg->payload, sizeof(raw_value));
    brightness = system_runtime_state_als_raw_to_brightness(raw_value);
    floatair_info("als raw update: raw=%" PRIu32 " brightness=%u auto=%d",
                  raw_value,
                  (unsigned)brightness,
                  (int)system_config_get_bl_auto());

    s_auto_brightness = brightness;
    s_auto_brightness_valid = true;
    if (!system_config_get_bl_auto()) {
        return true;
    }
    return system_runtime_state_apply_auto_brightness();
}

/**
 * @brief 处理 KWS 命中事件，通过软件策略过滤后唤醒屏幕和上报命中。
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
    if (!system_config_matches_kws_hit_value(kws_hit)) {
        floatair_info("ignore unmatched kws hit=%" PRIu32, kws_hit);
        return true;
    }
    if (s_kws_intercept_reasons != 0u) {
        floatair_info("kws hit intercepted by software: hit=%" PRIu32
                      " reasons=0x%08" PRIx32 " current_app=%s",
                      kws_hit,
                      s_kws_intercept_reasons,
                      current_app != NULL ? current_app : "N/A");
        return true;
    }
    if (!system_config_is_userguide_finished() &&
        !guide_runtime_is_home_step5_wait_assistant()) {
        floatair_info("userguide unfinished before step5, ignore kws hit=%" PRIu32, kws_hit);
        return true;
    }
    lcd_state_t lcd_state = floatair_lcd_get_state();
    floatair_info("kws state update: hit=%" PRIu32 " lcd_state=%u(%s) current_app=%s",
                  kws_hit,
                  (unsigned)lcd_state,
                  floatair_lcd_state_name(lcd_state),
                  current_app);
    if (!system_get_btconn_state()) {
        floatair_info("ignore kws assistant action while bt disconnect overlay active");
        return true;
    }

    if (lcd_state == LCD_OFF) {
        system_set_sys_state(LCD_ON);
        system_report_sys_state(LCD_ON, SYSTEM_SYS_STATE_TRIGGER_KEYWORD_SPOTTING);
    }

    (void)system_report_kws_hit();
    return true;
}

/**
 * @brief 处理通话建立状态消息并控制电话通知显隐。
 * @param[in] msg 通话建立状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_handle_call_setup_event(JYT_ELF_MQ_MSG* msg) {
    char caller_payload[128] = {0};
    char caller_display[64] = {0};
    char caller_number[64] = {0};
    const char* display_caller = NULL;
    uint8_t raw_state = 0;
    size_t caller_len = 0;

    if (msg == NULL || msg->payload_len == 0) {
        floatair_err("call setup msg invalid");
        return false;
    }

    raw_state = msg->payload[0];
    if (msg->payload_len > 1) {
        caller_len = msg->payload_len - 1U;
        if (caller_len >= sizeof(caller_payload)) {
            caller_len = sizeof(caller_payload) - 1U;
        }
        memcpy(caller_payload, msg->payload + 1, caller_len);
    }

    system_runtime_state_parse_call_caller(caller_payload,
                                           caller_display,
                                           sizeof(caller_display),
                                           caller_number,
                                           sizeof(caller_number));

    floatair_info("call setup state=%u, payload_len=%u, caller=%s",
                  (unsigned)raw_state,
                  (unsigned)msg->payload_len,
                  caller_display[0] ? caller_display : "N/A");

    if (caller_number[0] != '\0') {
        strncpy(s_call_last_number, caller_number, sizeof(s_call_last_number) - 1u);
        s_call_last_number[sizeof(s_call_last_number) - 1u] = '\0';
        floatair_info("cache call number: %s", s_call_last_number);
    }
    display_caller = caller_display[0] != '\0'
                         ? caller_display
                         : (system_runtime_state_ancs_call_info_is_fresh()
                                ? s_ancs_call_name
                                : NULL);

    switch ((system_bt_call_setup_state_t)raw_state) {
        case SYSTEM_BT_CALL_EVENT_CONNECTED:
            s_call_seen_connected = true;
            floatair_info("call flow connected: update notification, caller=%s",
                          s_call_last_number[0] ? s_call_last_number : "N/A");
            (void)system_notification_update_call_state(NOTIFY_CALL_STATE_CONNECTED);
            return true;
        case SYSTEM_BT_CALL_EVENT_DISCONNECTED:
            floatair_info("call flow disconnected: ringing=%d connected=%d bt_connected=%d last_number=%s",
                          (int)s_call_seen_ringing,
                          (int)s_call_seen_connected,
                          (int)system_get_btconn_state(),
                          s_call_last_number[0] ? s_call_last_number : "N/A");
            system_notification_dismiss_call();
            if (s_call_seen_ringing && !s_call_seen_connected && !s_call_uses_ancs_info) {
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
            s_call_uses_ancs_info = caller_display[0] == '\0' &&
                                    system_runtime_state_ancs_call_info_is_fresh();
            floatair_info("call flow ringing: bt_connected=%d caller=%s",
                          (int)system_get_btconn_state(),
                          display_caller != NULL ? display_caller : "N/A");
            if (!system_get_btconn_state()) {
                floatair_info("suppress incoming call notification while bt disconnect overlay active");
                return true;
            }
            if (display_caller == NULL) {
                floatair_info("wait ANCS call info before showing incoming call notification");
                return true;
            }
            if (system_notification_update_call_caller(display_caller)) {
                floatair_info("update active incoming call caller: %s", display_caller);
                return true;
            }
            floatair_info("show incoming call notification without generated title");
            return system_notification_show_call(NULL,
                                                 display_caller,
                                                 NOTIFY_CALL_STATE_RINGING);
        case SYSTEM_BT_CALL_EVENT_OUTGOING:
            s_call_seen_ringing = false;
            s_call_seen_connected = false;
            floatair_info("call flow outgoing: bt_connected=%d caller=%s",
                          (int)system_get_btconn_state(),
                          display_caller != NULL ? display_caller : "N/A");
            if (!system_get_btconn_state()) {
                floatair_info("suppress outgoing call notification while bt disconnect overlay active");
                return true;
            }
            return system_notification_show_call(NULL,
                                                 display_caller,
                                                 NOTIFY_CALL_STATE_OUTGOING);
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
