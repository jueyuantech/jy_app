/**
 * @file system_runtime_ui.h
 * @brief 系统运行时 UI 壳层内部接口声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#pragma once

#include "system/system_runtime_types.h"
#include "common/widgets/status_bar.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将电量值同步到底部状态栏。
 * @param[in] battery 电量百分比。
 * @return 无返回值。
 */
void system_ui_update_battery(uint8_t battery);

/**
 * @brief 将充电状态同步到底部状态栏。
 * @param[in] charge_state 充电状态值。
 * @return 无返回值。
 */
void system_ui_update_charge_state(uint8_t charge_state);

/**
 * @brief 设置底部状态栏佩戴检测图标显隐。
 * @param[in] visible `true` 表示显示图标占位，`false` 表示隐藏图标占位。
 * @return 无返回值。
 */
void system_ui_set_wear_detection_visible(bool visible);

/**
 * @brief 按指定时间戳刷新底部状态栏时间。
 * @param[in] time_now 需要显示的时间戳。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
bool system_ui_update_time_from_epoch(time_t time_now);

/**
 * @brief 设置状态栏时间是否已通过手机对表确认可靠。
 * @param[in] reliable `true` 表示时间可靠并允许显示，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void system_ui_set_time_reliable(bool reliable);

/**
 * @brief 在蓝牙断连遮罩显示时拦截页面输入事件。
 * @return `true` 表示事件已被拦截，`false` 表示应继续分发。
 */
bool system_ui_try_intercept_bt_disconnect_overlay_input(void);

/**
 * @brief 控制蓝牙断连遮罩显隐。
 * @param[in] visible `true` 表示显示遮罩，`false` 表示隐藏遮罩。
 * @return 无返回值。
 */
void system_ui_set_bt_disconnect_overlay_visible(bool visible);

/**
 * @brief 刷新蓝牙断连遮罩的文案与蓝牙名称。
 * @return 无返回值。
 */
void system_ui_refresh_bt_disconnect_overlay_text(void);

/**
 * @brief 按当前系统运行时状态统一同步系统壳层显隐与层级。
 * @return 无返回值。
 */
void system_ui_sync_shell_state(void);

/**
 * @brief 初始化系统 LVGL 根节点、页面容器和底部状态栏。
 * @return 返回当前活动屏幕根对象。
 */
lv_obj_t* system_init_lvgl_fb(void);
/**
 * @brief 获取当前状态栏模式下的页面内容区高度。
 * @return 返回内容区高度。
 */
lv_coord_t system_ui_get_page_content_height(void);
/**
 * @brief 立即刷新指定状态栏的缓存时间、电量和充电状态。
 * @param[in] status_bar 目标状态栏对象。
 * @return 无返回值。
 */
void system_ui_refresh_status_bar(lv_obj_t* status_bar);
/**
 * @brief 按当前 display level 立即刷新正在显示的页面场景。
 * @return 无返回值。
 */
void system_ui_refresh_display_distance_level(void);
/**
 * @brief 立即将当前活动屏幕标记为脏并触发一次 LVGL 刷屏。
 * @return `true` 表示已触发刷屏，`false` 表示当前没有有效活动屏幕。
 */
bool system_ui_refresh_screen_now(void);
/**
 * @brief 获取指定位置的状态栏对象。
 * @param[in] pos 状态栏位置。
 * @return 返回对应位置的状态栏对象；不支持时返回 `NULL`。
 */
lv_obj_t* system_get_status_bar(status_bar_widget_pos_t pos);
/**
 * @brief 设置底部状态栏显示模式。
 * @param[in] show_bottom `true` 表示显示底部状态栏，`false` 表示隐藏。
 * @return 无返回值。
 */
void system_status_bar_set_mode(bool show_bottom);

#ifdef __cplusplus
}
#endif
