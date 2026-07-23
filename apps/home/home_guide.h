/**
 * @file home_guide.h
 * @brief Home 新手教学页面逻辑接口。
 * @author jytek
 * @version 1.0.0
 * @date 2026-07-01
 * @copyright JYTek
 * @ingroup app_home
 */
#pragma once

#include <stdbool.h>
#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Home 教学需要调用的 Home 页面动作。
 */
typedef struct {
    void (*refresh)(void);          ///< 刷新 Home 页面视图。
    void (*reset_selection)(void);  ///< 重置 Home 默认选中项。
    void (*screen_swipe_left)(void);   ///< 按屏幕左滑语义切换到右侧应用。
    void (*screen_swipe_right)(void);  ///< 按屏幕右滑语义切换到左侧应用。
} home_guide_ops_t;

/**
 * @brief 创建 Home 教学覆盖控件。
 * @param[in] parent 父对象。
 * @param[in] font 教学文案字体。
 * @param[in] font_height 教学文案默认高度。
 * @return 无返回值。
 */
void home_guide_create_controls(lv_obj_t* parent, const lv_font_t* font, int font_height);
/**
 * @brief 销毁 Home 教学中可能挂载到页面外部浮层的控件。
 * @return 无返回值。
 */
void home_guide_destroy_controls(void);
/**
 * @brief 刷新 Home 教学覆盖控件布局。
 * @return 无返回值。
 */
void home_guide_layout_update(void);
/**
 * @brief 教学 step1 时强制选择翻译 App。
 * @param[in] select_app_by_name Home 选择指定 App 的回调。
 * @param[in] view_ready Home 视图是否已创建。
 * @param[in] refresh Home 视图刷新回调。
 * @return `true` 表示当前是 step1 且已尝试选择翻译 App，`false` 表示非 step1。
 */
bool home_guide_apply_step1_selection(bool (*select_app_by_name)(const char*),
                                      bool view_ready,
                                      void (*refresh)(void));
/**
 * @brief 判断当前是否处于 Home 教学 step1。
 * @return `true` 表示当前是 step1，`false` 表示不是。
 */
bool home_guide_is_step1(void);
/**
 * @brief 处理 Home 教学态触摸事件。
 * @param[in] code LVGL 事件码。
 * @param[in] ops Home 页面动作回调。
 * @return `true` 表示事件已被教学态消费，`false` 表示继续交给普通 Home 逻辑。
 */
bool home_guide_handle_touch(lv_event_code_t code, const home_guide_ops_t* ops);
/**
 * @brief 设置 Home 教学异步事件需要使用的 Home 页面动作。
 * @param[in] ops Home 页面动作回调。
 * @return 无返回值。
 */
void home_guide_set_ops(const home_guide_ops_t* ops);
/**
 * @brief 注册 Home 教学需要监听的系统事件。
 * @param[in] root Home 页面根对象。
 * @return 无返回值。
 */
void home_guide_register_events(lv_obj_t* root);
/**
 * @brief Home 页面出现时恢复教学提示画面。
 * @return 无返回值。
 */
void home_guide_on_appear(void);
#ifdef __cplusplus
}
#endif
