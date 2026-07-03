/**
 * @file imagefusion.c
 * @brief Imagefusion 应用生命周期和大十字页面实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-06-16
 * @copyright JYTek
 * @ingroup app_imagefusion
 */
#include "imagefusion.h"

#include "app_def.h"
#include "common/app_framework/app_nav.h"
#include "floatair_dbg.h"
#include "message.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <lvgl/lvgl.h>

#define IMAGEFUSION_CROSS_SIZE_PCT 72   ///< 十字长边占页面宽高的百分比。
#define IMAGEFUSION_CROSS_THICKNESS 1  ///< 十字线条厚度。

/**
 * @brief Imagefusion 消息注册项。
 */
static app_message_t s_imagefusion_msg = {
    .id = APP_MSG_ID_IMAGEFUSION,
    .name = APP_NAME_IMAGEFUSION,
    .cb = imagefusion_route_cmd,
};

static bool s_imagefusion_msg_registered = false;  ///< Imagefusion 消息是否已注册。

/**
 * @brief 创建十字条对象。
 *
 * @param parent 父对象。
 * @param width 条形宽度。
 * @param height 条形高度。
 * @return 创建成功返回 LVGL 对象，失败返回 `NULL`。
 */
static lv_obj_t* imagefusion_create_cross_bar(lv_obj_t* parent, lv_coord_t width, lv_coord_t height) {
    lv_obj_t* bar = lv_obj_create(parent);

    if (bar == NULL) {
        return NULL;
    }

    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, width, height);
    lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    return bar;
}

/**
 * @brief 创建 Imagefusion 页面视图。
 *
 * @param root 页面根对象。
 * @param data 页面入参；当前未使用。
 * @return 无返回值。
 */
static void imagefusion_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;

    floatair_assert(root != NULL, "root NULL");

    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    floatair_assert(imagefusion_create_cross_bar(root,
                                                 LV_PCT(IMAGEFUSION_CROSS_SIZE_PCT),
                                                 IMAGEFUSION_CROSS_THICKNESS) != NULL,
                    "imagefusion horizontal bar create failed");
    floatair_assert(imagefusion_create_cross_bar(root,
                                                 IMAGEFUSION_CROSS_THICKNESS,
                                                 LV_PCT(IMAGEFUSION_CROSS_SIZE_PCT)) != NULL,
                    "imagefusion vertical bar create failed");
}

/**
 * @brief 页面显示时隐藏状态栏，让十字独占视野。
 *
 * @param root 页面根对象。
 * @return 无返回值。
 */
static void imagefusion_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(false);
}

static app_page_t s_imagefusion_page = {
    .name = APP_NAME_IMAGEFUSION,
    .on_create = imagefusion_page_create,
    .on_appear = imagefusion_page_appear,
    .on_disappear = NULL,
    .on_destroy = NULL,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* imagefusion_page_get(void) {
    return &s_imagefusion_page;
}

/**
 * @brief 注册 Imagefusion 消息处理器。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
static bool imagefusion_msg_register_once(void) {
    int ret = 0;

    if (s_imagefusion_msg_registered) {
        return true;
    }

    ret = app_msg_register(&s_imagefusion_msg);
    if (ret != 0) {
        return false;
    }
    s_imagefusion_msg_registered = true;
    return true;
}

static void imagefusion_app_on_start(void) {
    if (!imagefusion_msg_register_once()) {
        floatair_assert(false, "imagefusion app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)imagefusion_page_get(), NULL, 0)) {
        floatair_assert(false, "imagefusion page replace failed");
    }
}

static app_t s_imagefusion_app = {
    .name = APP_NAME_IMAGEFUSION,
    .on_start = imagefusion_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = NULL,
    .on_back = NULL,
};

bool imagefusion_app_register(void) {
    if (!imagefusion_msg_register_once()) {
        return false;
    }
    return app_manager_register(&s_imagefusion_app);
}
