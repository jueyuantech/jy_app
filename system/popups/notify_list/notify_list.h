/**
 * @file notify_list.h
 * @brief Notify list 弹窗公共接口声明。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <lvgl/lvgl.h>

/**
 * @brief 打开通知列表弹窗。
 * @return `true` 表示打开成功，`false` 表示打开失败。
 */
bool notify_list_open(void);
/**
 * @brief 关闭通知列表弹窗。
 * @return `true` 表示关闭成功，`false` 表示关闭失败。
 */
bool notify_list_close(void);
/**
 * @brief 查询通知列表弹窗是否正在显示。
 * @return `true` 表示正在显示，`false` 表示未显示。
 */
bool notify_list_is_open(void);
/**
 * @brief 处理通知列表弹窗输入事件。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示事件已被通知列表弹窗消费，`false` 表示未消费。
 */
bool notify_list_handle_event(lv_event_code_t code);
/**
 * @brief 刷新通知列表弹窗内容。
 * @return 无返回值。
 */
void notify_list_view_reload(void);

#ifdef __cplusplus
}
#endif
