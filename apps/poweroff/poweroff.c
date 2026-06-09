/**
 * @file poweroff.c
 * @brief Poweroff 应用生命周期和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-03-14
 * @copyright JYTek
 * @ingroup app_poweroff
 */
#include <time.h>
#include "poweroff.h"

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

static void poweroff_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* image = lv_image_create(root);
    lv_image_set_src(image, FLOATAIR_SYS_IMG("poweroff.jpg"));
    lv_obj_center(image);
}

/**
 * @brief Poweroff 页面显示时声明底部状态栏显示策略。
 * @param[in] root 页面根对象。
 * @return 无返回值。
 */
static void poweroff_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(true);
}

static app_page_t s_poweroff_page = {
    .name = APP_NAME_POWEROFF,
    .on_create = poweroff_page_create,
    .on_appear = poweroff_page_appear,
    .on_disappear = NULL,
    .on_destroy = NULL,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* poweroff_page_get(void) {
    return &s_poweroff_page;
}

static void poweroff_app_on_start(void) {
    if (!app_nav_replace((app_page_t*)poweroff_page_get(), NULL, 0)) {
        floatair_assert(false, "poweroff page replace failed");
    }
}

static app_t s_poweroff_app = {
    .name = APP_NAME_POWEROFF,
    .on_start = poweroff_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = NULL,
    .on_back = NULL,
};

bool poweroff_app_register(void) {
    return app_manager_register(&s_poweroff_app);
}
