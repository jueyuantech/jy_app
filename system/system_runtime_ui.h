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

/** S 系列应用统一使用的底部半屏内容高度。 */
#define SYSTEM_UI_HALF_PAGE_HEIGHT 170

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 自定义进度提示事件参数。
 */
typedef struct {
    bool visible;      ///< 是否显示自定义提示遮罩。
    const char* text;  ///< 提示文字；空字符串表示只显示加载图标。
    uint8_t bg_opa;    ///< 遮罩背景透明度，范围 0~255。
} system_progress_hint_param_t;

/**
 * @brief 获取自定义进度提示事件 ID。
 *
 * 页面可监听该事件，按需显示自己的提示遮罩。
 *
 * @return 返回 LVGL 自定义事件 ID。
 */
uint32_t system_ui_get_progress_hint_event(void);

/**
 * @brief 向当前页面发送自定义进度提示事件。
 *
 * @param[in] param 自定义提示参数。
 * @return `true` 表示事件已发往当前页面，`false` 表示当前页面不可用。
 */
bool system_ui_send_progress_hint(const system_progress_hint_param_t* param);

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
 * @brief 将左右耳机附件连接状态同步到全局状态栏。
 * @param[in] left_connected `true` 表示左侧耳机附件已连接。
 * @param[in] right_connected `true` 表示右侧耳机附件已连接。
 * @return 无返回值。
 */
void system_ui_update_headset_state(bool left_connected, bool right_connected);

/**
 * @brief 获取当前设备时间及手机校时或 RTC 恢复的可靠性。
 * @param[out] reliable 时间是否已由手机校时或 RTC 恢复确认可靠；允许传入 `NULL`。
 * @return 当前设备时间戳。
 */
time_t system_ui_time_now(bool* reliable);

/**
 * @brief 按指定时间戳刷新底部状态栏时间。
 * @param[in] time_now 需要显示的时间戳。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
bool system_ui_update_time_from_epoch(time_t time_now);

/**
 * @brief 设置状态栏时间是否已由手机校时或 RTC 恢复确认可靠。
 * @param[in] reliable `true` 表示时间可靠并允许显示，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void system_ui_set_time_reliable(bool reliable);

/**
 * @brief 在蓝牙断连遮罩显示时拦截页面输入事件，按 App 模式支持双击返回。
 * @param[in] code 归一化后的 LVGL 输入事件码。
 * @return `true` 表示事件已被拦截，`false` 表示应继续分发。
 */
bool system_ui_try_intercept_bt_disconnect_overlay_input(lv_event_code_t code);

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
 * @brief 获取当前状态栏模式下页面内容区的纵向偏移。
 * @return 顶部状态栏可见时返回状态栏高度，否则返回 0。
 */
lv_coord_t system_ui_get_page_content_offset_y(void);
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
 * @brief 请求在下一次应用刷新周期强制刷新当前活动屏幕。
 *
 * 连续请求会合并；灭屏期间的请求会保留到亮屏后的刷新周期。
 *
 * @return 始终返回 `true`，表示请求已记录。
 */
bool system_ui_request_screen_refresh(void);
/**
 * @brief 将待处理的强制刷屏请求转换为当前屏幕的 LVGL 无效区域。
 *
 * 本函数不立即刷屏，应在周期调用 `lv_timer_handler()` 前执行。
 *
 * @return `true` 表示本次应用了一个待处理请求，`false` 表示无请求或暂不可刷新。
 */
bool system_ui_apply_pending_screen_refresh(void);
/**
 * @brief 获取当前持久化的应用垂直显示位置。
 * @return 返回顶部、中部或底部显示位置。
 */
system_display_position_t system_ui_get_display_position(void);
/**
 * @brief 设置支持搬屏应用的垂直显示位置。
 *
 * 新位置在下一次刷新周期应用；未声明显示位置能力的 App 始终保持默认位置。
 *
 * @param[in] position 垂直显示位置。
 * @return `true` 表示位置有效，`false` 表示参数无效。
 */
bool system_ui_set_display_position(system_display_position_t position);
/**
 * @brief 灭屏前同步写入黑底居中的 processing 提示帧。
 *
 * 函数内部临时创建提示遮罩，同步刷新后立即销毁对象；已提交的 framebuffer
 * 内容由随后执行的平台灭屏操作保留。
 *
 * @return `true` 表示提示帧已完成刷新，`false` 表示 UI 尚未就绪。
 */
bool system_ui_render_screen_off_frame(void);
/**
 * @brief 亮屏后统一补刷灭屏期间延迟的系统 UI 更新。
 * @return 无返回值。
 */
void system_ui_flush_pending_after_screen_on(void);
/**
 * @brief 获取指定位置的状态栏对象。
 * @param[in] pos 状态栏位置。
 * @return 返回对应位置的状态栏对象；不支持时返回 `NULL`。
 */
lv_obj_t* system_get_status_bar(status_bar_widget_pos_t pos);
/**
 * @brief 设置状态栏显示模式与位置。
 * @param[in] visible `true` 表示显示状态栏，`false` 表示隐藏。
 * @param[in] pos 状态栏目标位置。
 * @return 无返回值。
 */
void system_status_bar_set_mode_at(bool visible, status_bar_widget_pos_t pos);
/**
 * @brief 保持当前显隐状态，仅更新状态栏位置。
 * @param[in] pos 状态栏目标位置。
 * @return 无返回值。
 */
void system_status_bar_set_position(status_bar_widget_pos_t pos);
/**
 * @brief 设置默认底部状态栏显示模式。
 * @param[in] show_bottom `true` 表示显示底部状态栏，`false` 表示隐藏。
 * @return 无返回值。
 */
void system_status_bar_set_mode(bool show_bottom);
/**
 * @brief 设置状态栏是否叠加在全高页面内容之上。
 * @param[in] overlay `true` 表示状态栏不占页面高度，`false` 表示状态栏保留页面占位。
 * @return 无返回值。
 */
void system_status_bar_set_overlay(bool overlay);
/**
 * @brief 设置当前 App 状态栏的时间显示策略。
 * @param[in] visible `true` 表示时间可靠时允许显示，`false` 表示始终隐藏。
 * @return 无返回值。
 */
void system_status_bar_set_time_visible(bool visible);

/**
 * @brief 设置底部状态栏最左侧显示的当前 App 名称。
 * @param[in] app_name 展示名称；传入 `NULL` 或空字符串时隐藏。
 * @return 无返回值。
 */
void system_status_bar_set_app_name(const char* app_name);

#ifdef __cplusplus
}
#endif
