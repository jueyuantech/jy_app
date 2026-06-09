/**
 * @file app_page_host.h
 * @brief 应用页面承载层接口。
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
 * @brief 页面承载层配置。
 */
typedef struct {
    int32_t width;             ///< 页面场景宽度
    int32_t height;            ///< 页面场景高度
} app_page_host_config_t;

/**
 * @brief 页面承载层视图对象集合。
 */
typedef struct {
    lv_obj_t* scene_root;    ///< 页面场景根对象，转场动画作用于该对象
    lv_obj_t* content_root;  ///< 页面内容根对象，业务页面只挂载到该对象
} app_page_view_t;

/**
 * @brief 获取默认页面承载层配置。
 * @param[in] width 页面宽度。
 * @param[in] height 页面高度。
 * @return 返回默认配置。
 */
app_page_host_config_t app_page_host_default_config(int32_t width, int32_t height);

/**
 * @brief 创建页面承载层视图。
 * @param[in] parent 页面层父对象。
 * @param[in] cfg 页面承载层配置。
 * @param[out] view 输出页面视图对象集合。
 * @return `true` 表示创建成功，`false` 表示创建失败。
 */
bool app_page_host_create(lv_obj_t* parent, const app_page_host_config_t* cfg, app_page_view_t* view);

/**
 * @brief 更新页面承载层视图尺寸。
 * @param[in,out] view 页面视图对象集合。
 * @param[in] width 页面宽度。
 * @param[in] height 页面高度。
 * @return 无返回值。
 */
void app_page_host_resize(app_page_view_t* view, int32_t width, int32_t height);

/**
 * @brief 销毁页面承载层视图。
 * @param[in,out] view 页面视图对象集合。
 * @return 无返回值。
 */
void app_page_host_destroy(app_page_view_t* view);

/**
 * @brief 获取页面内容根对象。
 * @param[in] view 页面视图对象集合。
 * @return 返回内容根对象；无效时返回 `NULL`。
 */
lv_obj_t* app_page_host_get_content(const app_page_view_t* view);

#ifdef __cplusplus
}
#endif
