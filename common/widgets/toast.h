/**
 * @file toast.h
 * @brief Toast 组件接口，提供单实例提示的显示与关闭能力。
 */
#ifndef COMMON_WIDGETS_TOAST_H
#define COMMON_WIDGETS_TOAST_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "container.h"
#include "label.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Toast 组件句柄。
 */
typedef struct toast_t toast_t;

#define TOAST_ID_DEFAULT 0U ///< 默认 Toast 业务标识。

/**
 * @brief Toast 组件配置项。
 */
typedef struct {
    container_cfg_t box;      ///< Toast 外层容器配置；Toast 固定居中显示，`box.x/y` 不参与布局。
    label_cfg_t label;        ///< Toast 文本默认配置。
    uint32_t id;              ///< Toast 业务标识；用于按类型关闭当前活动 Toast。
    uint32_t duration_ms;     ///< 自动消失时间，单位毫秒；传 0 表示不自动关闭。
    uint8_t level;            ///< Toast 显示等级；数值越小优先级越高，低等级不会覆盖高等级。
} toast_cfg_t;

/**
 * @brief 获取默认 Toast 配置。
 *
 * 推荐先调用本函数拿一份默认值，再按需修改少数字段后传给
 * `toast_show_with_cfg()`。
 *
 * @return 返回填充好默认值的 Toast 配置结构体。
 */
toast_cfg_t toast_default_cfg(void);

/**
 * @brief 创建并显示一个 Toast。
 *
 * 当前同一时刻只保留一个活动 Toast；满足覆盖条件时优先原地更新当前
 * Toast 的文本与配置，并重置自动关闭定时器；仅在当前活动 Toast 不可复用时
 * 才会关闭旧 Toast 并重建。若当前活动 Toast 等级更高，本次 Toast 会被忽略
 * 并返回 `NULL`。
 *
 * @param text Toast 显示文本；传 `NULL` 时返回 `NULL`。
 * @return 创建成功返回 Toast 句柄，失败或被高等级 Toast 拦截时返回 `NULL`。
 */
toast_t* toast_show(const char* text);

/**
 * @brief 按指定等级创建并显示一个 Toast。
 *
 * 当前同一时刻只保留一个活动 Toast；同等级或更高等级的新 Toast
 * 会优先更新当前 Toast 并重置定时器，低等级新 Toast 会被忽略。
 *
 * @param text Toast 显示文本；传 `NULL` 时返回 `NULL`。
 * @param level Toast 显示等级；数值越小优先级越高。
 * @return 创建成功返回 Toast 句柄，失败或被高等级 Toast 拦截时返回 `NULL`。
 */
toast_t* toast_show_with_level(const char* text, uint8_t level);

/**
 * @brief 使用完整配置创建并显示一个 Toast。
 *
 * 当前同一时刻只保留一个活动 Toast；同等级或更高等级的新 Toast
 * 会优先更新当前 Toast 并重置定时器，低等级新 Toast 会被忽略。
 *
 * @param text Toast 显示文本；传 `NULL` 时返回 `NULL`。
 * @param cfg Toast 配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回 Toast 句柄，失败或被高等级 Toast 拦截时返回 `NULL`。
 */
toast_t* toast_show_with_cfg(const char* text, const toast_cfg_t* cfg);

/**
 * @brief 按业务标识关闭当前活动 Toast。
 *
 * @param id 需要关闭的 Toast 业务标识。
 * @return 无返回值。
 */
void toast_dismiss(uint32_t id);

/**
 * @brief 关闭当前活动 Toast。
 * @return 无返回值。
 */
void toast_dismiss_active(void);

#ifdef __cplusplus
}
#endif

#endif
