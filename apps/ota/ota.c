/**
 * @file ota.c
 * @brief OTA 应用页面实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-03-14
 * @copyright JYTek
 * @ingroup app_ota
 */
#include <time.h>
#include "ota.h"

#include "message.h"
#include "app_def.h"
#include "floatair_fs.h"
#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_nav.h"
#include "system/system.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *g_progress_bar = NULL;
static lv_obj_t *g_progress_label = NULL;
static bool s_ota_msg_registered = false;

void ota_update_progress(uint8_t progress) {
    if (g_progress_bar) {
        lv_bar_set_value(g_progress_bar, progress, LV_ANIM_ON);
    }
    if (g_progress_label) {
        char progress_text[16] = {0};
        snprintf(progress_text, sizeof(progress_text), "%d%%", progress);
        lv_label_set_text(g_progress_label, progress_text);
    }
}

static void ota_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 显示 ota.jpg 图片。 */
    lv_obj_t* image = lv_image_create(root);
    lv_image_set_src(image, FLOATAIR_SYS_IMG("ota.jpg"));
    lv_obj_set_style_pad_bottom(image, 20, 0);

    g_progress_bar = lv_bar_create(root);
    lv_obj_set_size(g_progress_bar, 200, 20);

    /* 进度条背景使用黑底白边。 */
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_progress_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(g_progress_bar, 2, 0);
    lv_obj_set_style_border_opa(g_progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_progress_bar, 10, 0);

    lv_obj_set_style_bg_color(g_progress_bar, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_progress_bar, 8, LV_PART_INDICATOR);

    lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);

    /* 创建进度百分比文本。 */
    g_progress_label = lv_label_create(root);
    lv_obj_set_style_text_color(g_progress_label, lv_color_white(), 0);
    obj_set_text_font(g_progress_label, get_system_font());
    lv_label_set_text(g_progress_label, "0%");
    lv_obj_set_style_pad_top(g_progress_label, 10, 0);
}

static void ota_page_destroy(void) {
    g_progress_bar = NULL;
    g_progress_label = NULL;
}

static app_message_t ota_msg = {
    .id   = APP_MSG_ID_OTA,
    .name = APP_NAME_OTA,
    .cb   = ota_route_cmd,
};

static bool ota_msg_register_once(void) {
    int ret = 0;

    if (s_ota_msg_registered) {
        return true;
    }

    ret = app_msg_register(&ota_msg);
    if (ret != 0) {
        return false;
    }
    s_ota_msg_registered = true;
    return true;
}

static void ota_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_ota_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_OTA);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_ota_msg_registered = false;
}

/**
 * @brief OTA 页面显示时声明底部状态栏显示策略。
 * @param[in] root 页面根对象。
 * @return 无返回值。
 */
static void ota_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(true);
}

static app_page_t s_ota_page = {
    .name = APP_NAME_OTA,
    .on_create = ota_page_create,
    .on_appear = ota_page_appear,
    .on_disappear = NULL,
    .on_destroy = ota_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* ota_page_get(void) {
    return &s_ota_page;
}

static void ota_app_on_start(void) {
    if (!ota_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)ota_page_get(), NULL, 0)) {
        floatair_assert(false, "ota page replace failed");
    }
}

static void ota_app_on_stop(void) {
    ota_msg_unregister_if_needed();
    ota_page_destroy();
}

static app_t s_ota_app = {
    .name = APP_NAME_OTA,
    .on_start = ota_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = ota_app_on_stop,
    .on_back = NULL,
};

bool ota_app_register(void) {
    return app_manager_register(&s_ota_app);
}
