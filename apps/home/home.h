/**
 * @file home.h
 * @brief Home 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup app_home Home App @{ */

typedef struct app_page_t app_page_t;

#include <stddef.h>
#include <stdint.h>
#include <lvgl/lvgl.h>

#include "app_def.h"
#include "common/app_framework/app_manager.h"
#include "system/system.h"
#include "i18n.h"


extern int32_t idle_img_center_h;
extern int32_t idle_img_center_w;
extern int32_t idle_img_left_h;
extern int32_t idle_img_left_w;
extern int32_t idle_img_right_h;
extern int32_t idle_img_right_w;
extern int32_t layout_gap;
extern bool home_enable_app_float;
extern const app_home_unit_t g_home_units_arr[];
extern const size_t g_home_units_count;

/**
 * @brief 按系统配置生成当前应显示的 Home 单元列表。
 * @param[out] out_units 返回动态分配的单元数组，调用方负责使用 `free` 释放。
 * @param[out] out_count 返回有效单元数量。
 * @return 配置中至少存在一个受支持单元且成功生成时返回 `true`；调用方应在失败时回退静态数组。
 */
bool home_units_build_from_config(app_home_unit_t** out_units, size_t* out_count);

/**
 * @brief 按产品清单声明激活指定 Home 单元。
 * @param[in] unit 待激活的 Home 单元。
 * @return 动作执行成功返回 `true`，否则返回 `false`。
 */
bool home_unit_activate(const app_home_unit_t* unit);

/**
 * @brief 注册 Home 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool home_app_register(void);

/**
 * @brief 获取 Home 页面描述符。
 * @return 返回 Home 页面描述符。
 */
const app_page_t* home_page_get(void);
/**
 * @brief 判断指定 App 是否属于当前产品 Home 支持范围。
 * @param[in] app_name App 名称。
 * @return `true` 表示支持，`false` 表示不支持。
 */
bool home_is_supported_app(const char* app_name);

/**
 * @brief 获取指定 App 在 Home 中使用的国际化展示名称。
 * @param[in] app_name App 协议名称。
 * @return 返回当前语言展示名称；App 不在 Home 列表中时返回 `NULL`。
 */
const char* home_get_app_display_name(const char* app_name);

/**
 * @brief Show home icons view
 * @param[in] root root object
 */
void home_show_icons(lv_obj_t* root);

/**
 * @brief Create home unit views
 * @param[in] root root object
 */
void create_home_uint(lv_obj_t* root);
/**
 * @brief Delete home unit views
 * @param[in] root root object
 */
void delete_home_uint(lv_obj_t* root);
/**
 * @brief Update home unit views
 * @param[in] root root object
 */
void update_home_uint(lv_obj_t* root);

/**
 * @brief 刷新 Home 页面当前蓝牙连接态展示。
 */
void home_view_reload(void);
/**
 * @brief 获取 Home 当前选中的应用名称。
 * @return 返回应用名称；Home 无可用应用时返回 `NULL`。
 */
const char* home_view_get_selected_app_name(void);
/**
 * @brief 设置 Home 当前运行时选中的应用。
 * @param[in] app_name 目标应用名称。
 * @return `true` 表示设置成功，`false` 表示应用不在当前 Home 显示列表中。
 */
bool home_view_select_app_by_name(const char* app_name);
void home_view_reset_selection(void);

/** @} */
#ifdef __cplusplus
}
#endif
