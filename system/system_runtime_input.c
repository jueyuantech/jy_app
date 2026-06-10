/**
 * @file system_runtime_input.c
 * @brief 系统运行时输入事件分发实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system/system_runtime_input.h"
#include "system/system.h"

#include "common/app_framework/app_manager.h"
#include "system/popups/assistant/assistant.h"
#include "app_def.h"
#include "app_lcd.h"
#include "system/popups/notify_list/notify_list.h"
#include "system/system_runtime_ui.h"
#include "system/popups/notify/notify.h"
#include "common/widgets/msgbox.h"

#include <string.h>

static bool s_wearing_state_known = false; ///< 是否已收到过佩戴状态事件
static bool s_wearing_state_worn = true;   ///< 最近一次佩戴状态，默认不屏蔽触控

/**
 * @brief 判断当前触控板输入是否应被配置或佩戴状态屏蔽。
 * @param[in] event 当前触控事件值。
 * @param[in] source 事件来源描述。
 * @return `true` 表示应忽略该事件，`false` 表示可继续处理。
 */
static bool system_touch_input_blocked(uint8_t event, const char* source) {
    if (!system_config_get_touchpad_enabled()) {
        floatair_info("touchpad disabled, ignore %s event %u", source, (unsigned)event);
        return true;
    }

    if (system_config_get_wear_detection_enabled() && s_wearing_state_known && !s_wearing_state_worn) {
        floatair_info("not worn, ignore %s event %u", source, (unsigned)event);
        return true;
    }

    return false;
}

/**
 * @brief 更新当前佩戴状态，用于在未佩戴时屏蔽触控板输入。
 * @param[in] worn `true` 表示已佩戴，`false` 表示未佩戴。
 * @return 无返回值。
 */
void system_runtime_input_set_wearing_state(bool worn) {
    s_wearing_state_known = true;
    s_wearing_state_worn = worn;
    floatair_info("wearing state for touch input: known=%d worn=%d",
                  (int)s_wearing_state_known,
                  (int)s_wearing_state_worn);
}

/**
 * @brief 处理 popup 层激活期间的输入拦截。
 * @param[in] code 待分发的 LVGL 事件码。
 * @return `true` 表示事件已被 popup 层消费，`false` 表示应继续分发给 app 层。
 */
static bool system_try_intercept_popup_event(lv_event_code_t code) {
    if (notify_handle_active_event(code)) {
        floatair_info("notify popup intercepted event %d", code);
        return true;
    }
    if (notify_list_handle_event(code)) {
        floatair_info("notify_list popup intercepted event %d", code);
        return true;
    }
    if (assistant_handle_event(code)) {
        floatair_info("assistant popup intercepted event %d", code);
        return true;
    }
    if (msgbox_handle_active_event(code)) {
        floatair_info("msgbox popup intercepted event %d", code);
        return true;
    }

    return false;
}

/**
 * @brief 获取当前页面根对象，失败时输出统一日志。
 * @return 成功返回当前页面根对象，失败返回 `NULL`。
 */
static lv_obj_t* system_runtime_input_get_current_page_root(void) {
    lv_obj_t* framework_root = app_manager_current_content_root();

    if (framework_root != NULL) {
        return framework_root;
    }
    floatair_err("framework current page root is NULL");
    return NULL;
}

/**
 * @brief 将系统触摸事件转换为 LVGL 事件码。
 * @param[in] event 系统触摸事件值。
 * @return 返回对应 LVGL 事件码；不支持时返回 `LV_EVENT_ALL`。
 */
static lv_event_code_t system_runtime_touch_event_to_lvgl(uint8_t event) {
    switch (event) {
        case SYSTEM_TOUCH_EVENT_CLICKED:
            return LV_EVENT_CLICKED;
        case SYSTEM_TOUCH_EVENT_DCLICKED:
            return LV_EVENT_DCLICKED;
        case SYSTEM_TOUCH_EVENT_LONG_PRESSED:
            return LV_EVENT_LONG_PRESSED;
        case SYSTEM_TOUCH_EVENT_GESTURE_UP:
        case SYSTEM_TOUCH_EVENT_GESTURE_LEFT:
            return LV_EVENT_GESTURE_LEFT;
        case SYSTEM_TOUCH_EVENT_GESTURE_DOWN:
        case SYSTEM_TOUCH_EVENT_GESTURE_RIGHT:
            return LV_EVENT_GESTURE_RIGHT;
        default:
            return LV_EVENT_ALL;
    }
}

/**
 * @brief 将 force 事件转换为 LVGL 事件码。
 * @param[in] event force 事件值。
 * @return 返回对应 LVGL 事件码；不支持时返回 `LV_EVENT_ALL`。
 */
static lv_event_code_t system_runtime_force_event_to_lvgl(uint8_t event) {
    switch (event) {
        case SET_FORCE_SINGLE_CLICK:
            return LV_EVENT_CLICKED;
        case SET_FORCE_DOUBLE_CLICK:
        case SET_FORCE_TRI_CLICK:
            return LV_EVENT_DCLICKED;
        case SET_FORCE_LONG_PRESSED:
            return LV_EVENT_LONG_PRESSED;
        case SET_SLIDE_BACKWORD:
            return LV_EVENT_GESTURE_LEFT;
        case SET_SLIDE_FORWARD:
            return LV_EVENT_GESTURE_RIGHT;
        default:
            return LV_EVENT_ALL;
    }
}

/**
 * @brief 尝试向 top 层当前页面发送 LVGL 事件。
 * @param[in] code 待发送的 LVGL 事件码。
 * @return `true` 表示 top 层已消费事件，`false` 表示应继续分发。
 */
static bool system_runtime_input_try_top_event(lv_event_code_t code) {
    bool consumed = false;
    app_t* current_app = app_manager_current();
    lv_obj_t* obj = NULL;

    if (code == LV_EVENT_ALL) {
        return false;
    }
    if (current_app == NULL || !current_app->use_top_layer) {
        return false;
    }

    obj = system_runtime_input_get_current_page_root();
    if (obj == NULL) {
        return false;
    }

    floatair_info("send event %d to top %p", (int)code, obj);
    (void)lv_obj_send_event(obj, code, &consumed);
    return consumed;
}

/**
 * @brief 向普通 app 层当前页面根对象发送 LVGL 事件。
 * @param[in] raw_event 原始系统事件值，仅用于日志。
 * @param[in] code 待发送的 LVGL 事件码。
 * @return `true` 表示发送成功，`false` 表示当前页不可用。
 */
static bool system_runtime_input_send_event_to_app(uint32_t raw_event, lv_event_code_t code) {
    app_t* current_app = app_manager_current();
    lv_obj_t* obj = NULL;

    if (current_app != NULL && current_app->use_top_layer) {
        return true;
    }

    obj = system_runtime_input_get_current_page_root();
    if (obj == NULL) {
        return false;
    }

    floatair_info("send event %u to app %p", (unsigned)raw_event, obj);
    (void)lv_obj_send_event(obj, code, NULL);
    return true;
}

/**
 * @brief 处理系统触摸事件并向当前页面分发。
 * @param[in] event 系统触摸事件值。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_touch_event(uint8_t event) {
    lv_event_code_t code = system_runtime_touch_event_to_lvgl(event);

    if (system_runtime_input_try_top_event(code)) {
        return true;
    }

    if (system_ui_try_intercept_bt_disconnect_overlay_input()) {
        return true;
    }

    if (code == LV_EVENT_ALL) {
        return true;
    }

    if (system_try_intercept_popup_event(code)) {
        return true;
    }

    return system_runtime_input_send_event_to_app(event, code);
}

/**
 * @brief 处理 force 触控事件并向当前页面分发。
 * @param[in] event force 触控事件值。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_touch_event_convert(uint8_t event) {
    lv_event_code_t code = system_runtime_force_event_to_lvgl(event);

    if (system_touch_input_blocked(event, "force")) {
        return true;
    }

    app_sleep_timer_reset();

    if (system_runtime_input_try_top_event(code)) {
        return true;
    }

    if (system_ui_try_intercept_bt_disconnect_overlay_input()) {
        return true;
    }

    if (code == LV_EVENT_ALL) {
        return true;
    }

    if (system_try_intercept_popup_event(code)) {
        return true;
    }

    if (event == SET_FORCE_TRI_CLICK) {
        floatair_info("tri-click event as double-click");
    }
    return system_runtime_input_send_event_to_app(event, code);
}

/**
 * @brief 处理 IMU 点击事件。
 * @param[in] event IMU 事件值。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_imu_event_convert_to_touch(uint8_t event) {
    if (event != SET_IMU_SINGLE_TAP && event != SET_IMU_DOUBLE_TAP) {
        floatair_err("imu event %d not support", event);
        return false;
    }

    switch (event) {
        case SET_IMU_SINGLE_TAP:
            return true;
        case SET_IMU_DOUBLE_TAP:
            if (floatair_lcd_get_state() == LCD_OFF) {
                floatair_lcd_set_state(LCD_ON);
                system_report_sys_state(1);
                app_sleep_timer_reset();
                return true;
            }

            if (system_runtime_input_try_top_event(LV_EVENT_DCLICKED)) {
                return true;
            }

            if (system_try_intercept_popup_event(LV_EVENT_DCLICKED)) {
                return true;
            }

            const char* current_app = app_router_get_app();
            if (current_app != NULL && strcmp(current_app, APP_NAME_HOME) == 0) {
                floatair_lcd_set_state(LCD_OFF);
                system_report_sys_state(0);
                return true;
            }

            return system_runtime_input_send_event_to_app(SET_FORCE_DOUBLE_CLICK, LV_EVENT_DCLICKED);
        default:
            return true;
    }
}

/**
 * @brief 将 IMU 抬头/低头事件直接映射为系统亮灭屏。
 * @param[in] msg IMU 方向消息。
 * @return `true` 表示事件已处理，`false` 表示处理失败。
 */
bool system_update_imu_tilt(JYT_ELF_MQ_MSG* msg) {
    uint8_t next_state = 0;
    system_head_gesture_config_t config = {0};

    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }

    if (!system_config_get_head_gesture_config(&config) ||
        (!config.up_enabled && !config.down_enabled)) {
        floatair_info("head gesture disabled, ignore imu_tilt %d", msg->Header.simple_data);
        return true;
    }

    floatair_info("imu_tilt %d", msg->Header.simple_data);

    switch (msg->Header.simple_data) {
        case TILT_DIRECTION_UP:
            if (!config.up_enabled) {
                floatair_info(
                    "head up gesture disabled, ignore imu_tilt %d", msg->Header.simple_data);
                return true;
            }
            next_state = 1;
            break;
        case TILT_DIRECTION_DOWN:
            if (!config.down_enabled) {
                floatair_info(
                    "head down gesture disabled, ignore imu_tilt %d", msg->Header.simple_data);
                return true;
            }
            next_state = 0;
            break;
        default:
            floatair_err("imu tilt %d not support", msg->Header.simple_data);
            return true;
    }

    if (system_get_sys_state() == next_state) {
        floatair_info("imu tilt keep lcd state: %u", (unsigned)next_state);
        if (next_state != 0) {
            app_sleep_timer_reset();
        }
        return true;
    }

    system_set_sys_state(next_state);
    system_report_sys_state(next_state);
    return true;
}
