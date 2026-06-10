/**
 * @file notify.h
 * @brief 系统通知浮层接口声明。
 */
#ifndef SYSTEM_POPUPS_NOTIFY_NOTIFY_H
#define SYSTEM_POPUPS_NOTIFY_NOTIFY_H

#include <stddef.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Notify 组件句柄。
 */
typedef struct notify_t notify_t;

/**
 * @brief Notify 输入事件回调。
 *
 * @param notify 触发事件的 Notify 句柄。
 * @param code 触发的 LVGL 事件码，仅会传入 `LV_EVENT_CLICKED`、`LV_EVENT_DCLICKED`、
 * `LV_EVENT_LONG_PRESSED`、`LV_EVENT_GESTURE_LEFT` 或 `LV_EVENT_GESTURE_RIGHT`。
 * @param user_data 回调透传数据。
 * @return 无返回值。
 */
typedef void (*notify_event_cb_t)(notify_t* notify, lv_event_code_t code, void* user_data);

/**
 * @brief Notify 显示模式。
 */
typedef enum {
    NOTIFY_MODE_MESSAGE = 1,
    NOTIFY_MODE_CALL = 2,
} notify_mode_t;

/**
 * @brief Notify 运行时配置项。
 */
typedef struct {
    const char* title;              ///< 标题文本；传 `NULL` 或空串时视为无标题。
    const void* image_src;          ///< 图片源；可传 32x32 L8 原始像素数据、路径或 LVGL 图片描述符。
    size_t image_src_size;          ///< `image_src` 为 L8 原始像素数据时对应长度；其他图片源传 `0`。
    notify_mode_t mode;             ///< Notify 显示模式。
    uint32_t duration_ms;           ///< 自动消失时间，单位毫秒；传 0 表示不自动关闭。
} notify_cfg_t;

/**
 * @brief 获取默认 Notify 配置。
 *
 * 推荐先调用本函数拿一份默认值，再按需修改少数字段后传给
 * `notify_show_with_cfg()`。
 *
 * @return 返回填充好默认值的 Notify 配置结构体。
 */
notify_cfg_t notify_default_cfg(void);

/**
 * @brief 使用完整配置创建并显示一个 Notify。
 *
 * 当前同一时刻只保留一个活动 Notify；新 Notify 显示时会自动关闭旧 Notify。
 *
 * @param cfg Notify 配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回 Notify 句柄，失败返回 `NULL`。
 */
notify_t* notify_show_with_cfg(const notify_cfg_t* cfg);

/**
 * @brief 设置 Notify 事件回调。
 *
 * 仅支持设置当前活动 Notify；Notify 自动关闭或被新 Notify 替换后，旧句柄即失效，调用方不应长期保存后再传回。
 *
 * @param notify 目标 Notify 句柄。
 * @param on_event Notify 事件统一回调；传 `NULL` 表示清空回调。
 * @param user_data 用户透传数据。
 * @return 无返回值。
 */
void notify_set_callbacks(notify_t* notify, notify_event_cb_t on_event, void* user_data);

/**
 * @brief 处理当前活动 Notify 的输入事件。
 *
 * 当前有活动 Notify 时，点击/双击/长按/左右手势会被 Notify 优先消费；
 * 若已设置回调则同步触发回调。
 *
 * @param code LVGL 事件码。
 * @return `true` 表示事件已被活动 Notify 拦截，`false` 表示未拦截。
 */
bool notify_handle_active_event(lv_event_code_t code);

/**
 * @brief 获取当前活动 Notify 的显示模式。
 *
 * @param mode_out 输出当前模式；传 `NULL` 时仅判断是否存在活动 Notify。
 * @return `true` 表示当前存在活动 Notify，`false` 表示当前没有活动 Notify。
 */
bool notify_get_active_mode(notify_mode_t* mode_out);

/**
 * @brief 主动关闭当前活动 Notify。
 *
 * @return 无返回值。
 */
void notify_dismiss(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_POPUPS_NOTIFY_NOTIFY_H */
