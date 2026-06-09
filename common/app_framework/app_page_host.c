/**
 * @file app_page_host.c
 * @brief 应用页面承载层实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-21
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#include "common/app_framework/app_page_host.h"

#include "common/app_framework/app_layers.h"

/**
 * @brief 配置页面承载层基础样式。
 * @param[in] obj 目标对象。
 * @param[in] width 宽度。
 * @param[in] height 高度。
 * @return 无返回值。
 */
static void app_page_host_prepare_obj(lv_obj_t* obj, int32_t width, int32_t height) {
    if (obj == NULL) {
        return;
    }

    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

app_page_host_config_t app_page_host_default_config(int32_t width, int32_t height) {
    app_page_host_config_t cfg = {
        .width = width,
        .height = height,
    };
    return cfg;
}

bool app_page_host_create(lv_obj_t* parent, const app_page_host_config_t* cfg, app_page_view_t* view) {
    app_page_host_config_t local_cfg;

    if (view == NULL) {
        return false;
    }
    *view = (app_page_view_t){0};

    if (cfg == NULL) {
        return false;
    }
    if (cfg->width <= 0 || cfg->height <= 0) {
        return false;
    }

    local_cfg = *cfg;
    parent = (parent != NULL) ? parent : app_layers_get_app();
    if (parent == NULL) {
        return false;
    }

    view->scene_root = lv_obj_create(parent);
    if (view->scene_root == NULL) {
        return false;
    }
    app_page_host_prepare_obj(view->scene_root, local_cfg.width, local_cfg.height);
    lv_obj_align(view->scene_root, LV_ALIGN_TOP_LEFT, 0, 0);

    view->content_root = lv_obj_create(view->scene_root);
    if (view->content_root == NULL) {
        app_page_host_destroy(view);
        return false;
    }
    app_page_host_prepare_obj(view->content_root, local_cfg.width, local_cfg.height);
    lv_obj_set_style_bg_color(view->content_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(view->content_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(view->content_root, LV_ALIGN_TOP_LEFT, 0, 0);

    return true;
}

void app_page_host_resize(app_page_view_t* view, int32_t width, int32_t height) {
    if (view == NULL || width <= 0 || height <= 0) {
        return;
    }

    if (view->scene_root != NULL && lv_obj_is_valid(view->scene_root)) {
        lv_obj_set_size(view->scene_root, width, height);
        lv_obj_align(view->scene_root, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    if (view->content_root != NULL && lv_obj_is_valid(view->content_root)) {
        lv_obj_set_size(view->content_root, width, height);
        lv_obj_align(view->content_root, LV_ALIGN_TOP_LEFT, 0, 0);
    }
}

void app_page_host_destroy(app_page_view_t* view) {
    if (view == NULL) {
        return;
    }

    if (view->scene_root != NULL && lv_obj_is_valid(view->scene_root)) {
        lv_obj_delete(view->scene_root);
    }
    *view = (app_page_view_t){0};
}

lv_obj_t* app_page_host_get_content(const app_page_view_t* view) {
    if (view == NULL || view->content_root == NULL || !lv_obj_is_valid(view->content_root)) {
        return NULL;
    }
    return view->content_root;
}
