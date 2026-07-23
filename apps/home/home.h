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

extern bool simple_guide;
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
 * @brief 注册 Home 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool home_app_register(void);

/**
 * @brief Get simple guide switch
 * @return true enabled; false disabled
 */
bool home_get_simple_guide(void);
/**
 * @brief Set simple guide switch
 * @param[in] guide switch value
 */
void home_set_simple_guide(bool guide);
/**
 * @brief Get play audio switch
 * @return true enabled; false disabled
 */
bool home_get_play_audio(void);
/**
 * @brief Set play audio switch
 * @param[in] play switch value
 */
void home_set_play_audio(bool play);

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
