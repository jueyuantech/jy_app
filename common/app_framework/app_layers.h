/**
 * @file app_layers.h
 * @brief 应用框架固定 UI 层级接口。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-21
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#pragma once

#include <lvgl/lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化固定 UI 层级。
 * @param[in] screen_root 层级父对象；传 `NULL` 时使用当前活动屏幕。
 * @param[in] width 层级宽度。
 * @param[in] height 层级高度。
 * @return `true` 表示初始化成功，`false` 表示初始化失败。
 */
bool app_layers_init(lv_obj_t* screen_root, int32_t width, int32_t height);

/**
 * @brief 重新设置所有固定层级尺寸。
 * @param[in] width 层级宽度。
 * @param[in] height 层级高度。
 * @return 无返回值。
 */
void app_layers_resize(int32_t width, int32_t height);

/**
 * @brief 获取背景层。
 * @return 返回背景层对象；未初始化时返回 `NULL`。
 */
lv_obj_t* app_layers_get_background(void);

/**
 * @brief 获取应用页面层。
 * @return 返回应用页面层对象；未初始化时返回 `NULL`。
 */
lv_obj_t* app_layers_get_app(void);

/**
 * @brief 获取应用内容浮层。
 * @return 返回应用内容浮层对象；未初始化时返回 `NULL`。
 */
lv_obj_t* app_layers_get_app_float(void);

/**
 * @brief 获取系统遮罩层。
 * @return 返回系统遮罩层对象；未初始化时返回 `NULL`。
 */
lv_obj_t* app_layers_get_overlay(void);

/**
 * @brief 获取弹窗层。
 * @return 返回弹窗层对象；未初始化时返回 `NULL`。
 */
lv_obj_t* app_layers_get_popup(void);

/**
 * @brief 获取最高优先级 UI 层。
 * @return 返回最高优先级 UI 层对象；未初始化时返回 `NULL`。
 */
lv_obj_t* app_layers_get_top(void);

#ifdef __cplusplus
}
#endif
