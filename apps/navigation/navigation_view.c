/**
 * @file navigation_view.c
 * @brief Navigation 页面视图、路线提示和导航状态展示实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_navigation
 */
#include <time.h>
#include <string.h>
#include "home/home.h"
#include "navigation.h"
#include "common/app_framework/app_manager.h"
#include "system/system.h"
#include "system/system_def.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "floatair_fs.h"
#include "floatair_dbg.h"
#include "lvgl.h"
#include "common/widgets/status_bar.h"

static lv_obj_t* navigation_init_label = NULL;
static lv_obj_t* navigation_init_img = NULL;
static lv_obj_t* navigation_init_cont = NULL;

static lv_obj_t* s_info_frame = NULL;
static lv_obj_t* s_speed_frame = NULL;
static lv_obj_t* s_speed_arc = NULL;
static lv_obj_t* s_heart_frame = NULL;
static lv_obj_t* s_spo_frame = NULL;

static lv_obj_t* s_img_dir = NULL;
static lv_obj_t* s_img_drive = NULL;
static lv_obj_t* s_img_heart = NULL;
static lv_obj_t* s_img_spo = NULL;
static lv_obj_t* s_lbl_distance_left = NULL;
static lv_obj_t* s_lbl_mile_left = NULL;
static lv_obj_t* s_lbl_min_left = NULL;
static lv_obj_t* s_lbl_road_name = NULL;
static lv_obj_t* s_lbl_speed = NULL;
static lv_obj_t* s_lbl_heart = NULL;
static lv_obj_t* s_lbl_spo = NULL;

static lv_image_dsc_t* s_dir_icon_dsc = NULL;
static uint8_t* s_dir_icon_buf = NULL;
static size_t s_dir_icon_buf_sz = 0;

static bool navigation_drive_icon_valid(void) {
    return s_img_drive != NULL && lv_obj_is_valid(s_img_drive);
}

static bool navigation_drive_icon_ensure(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        s_img_drive = NULL;
        return false;
    }
    if (!navigation_drive_icon_valid()) {
        s_img_drive = status_bar_add_image(status_bar, FLOATAIR_SYS_IMG("icon_walk.jpg"), STATUS_BAR_WIDGET_ALIGN_CENTER);
        if (s_img_drive != NULL) {
            lv_obj_add_flag(s_img_drive, LV_OBJ_FLAG_HIDDEN);
        }
    }
    return navigation_drive_icon_valid();
}

static void navigation_view_set_init_visible(bool visible) {
    if (visible) {
        if (navigation_init_cont) lv_obj_remove_flag(navigation_init_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (navigation_init_cont) lv_obj_add_flag(navigation_init_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

static void navigation_view_enter_map_mode(void) {
    navigation_view_set_init_visible(false);
}

static void touch_event_handle(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DCLICKED) {
        (void)app_router_exit_current_app();
    }
}

static void navigation_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    navigation_init_cont = lv_obj_create(root);
    lv_obj_remove_style_all(navigation_init_cont);
    lv_obj_set_size(navigation_init_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_align(navigation_init_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(navigation_init_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(navigation_init_cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(navigation_init_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(navigation_init_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(navigation_init_cont, LVGL_UI_MARGIN_20, LV_PART_MAIN);

    navigation_init_img = lv_image_create(navigation_init_cont);
    navigation_init_label = lv_label_create(navigation_init_cont);
    obj_set_text_font(navigation_init_label, get_system_font());
    lv_obj_set_style_text_align(navigation_init_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(navigation_init_label, lv_color_white(), 0);
    lv_obj_remove_flag(navigation_init_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(navigation_init_label, LV_LABEL_LONG_WRAP);

    lv_obj_set_size(navigation_init_img, LVGL_UI_ICONW_80, LVGL_UI_ICONH_80);
    lv_obj_set_size(navigation_init_label, LV_PCT(100), get_system_font_height() * 2 + get_system_font_row_space() * 3);

    lv_image_set_src(navigation_init_img, FLOATAIR_SYS_IMG("navigationB.jpg"));
    lv_label_set_text(navigation_init_label, app_get_str("OPEN_IDLE_NAVI"));

    const lv_font_t* font_sys = get_system_font();

    s_info_frame = lv_obj_create(root);
    lv_obj_remove_style_all(s_info_frame);
    lv_obj_set_size(s_info_frame, LV_PCT(100), INFO_FRAME_HEIGHT);
    lv_obj_align(s_info_frame, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_info_frame, LV_OBJ_FLAG_HIDDEN);

    s_img_dir = lv_image_create(s_info_frame);
    lv_obj_set_size(s_img_dir, IMG_DIR_SIZE, IMG_DIR_SIZE);
    lv_obj_align(s_img_dir, LV_ALIGN_LEFT_MID, 0, 0);

    s_lbl_distance_left = lv_label_create(s_info_frame);
    obj_set_text_font(s_lbl_distance_left, font_sys);
    lv_obj_set_style_text_color(s_lbl_distance_left, lv_color_white(), 0);
    lv_obj_set_width(s_lbl_distance_left, 120);
    lv_label_set_long_mode(s_lbl_distance_left, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_lbl_distance_left, LV_ALIGN_TOP_LEFT, IMG_DIR_SIZE + 6, 0);

    s_lbl_mile_left = lv_label_create(s_info_frame);
    obj_set_text_font(s_lbl_mile_left, font_sys);
    lv_obj_set_style_text_color(s_lbl_mile_left, lv_color_white(), 0);
    lv_obj_set_width(s_lbl_mile_left, 120);
    lv_label_set_long_mode(s_lbl_mile_left, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_lbl_mile_left, LV_ALIGN_TOP_LEFT, IMG_DIR_SIZE + 140, 0);

    s_lbl_min_left = lv_label_create(s_info_frame);
    obj_set_text_font(s_lbl_min_left, font_sys);
    lv_obj_set_style_text_color(s_lbl_min_left, lv_color_white(), 0);
    lv_obj_set_width(s_lbl_min_left, 120);
    lv_label_set_long_mode(s_lbl_min_left, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_lbl_min_left, LV_ALIGN_TOP_LEFT, IMG_DIR_SIZE + 280, 0);

    s_lbl_road_name = lv_label_create(s_info_frame);
    obj_set_text_font(s_lbl_road_name, font_sys);
    lv_obj_set_style_text_color(s_lbl_road_name, lv_color_white(), 0);
    lv_obj_set_width(s_lbl_road_name, 200);
    lv_label_set_long_mode(s_lbl_road_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_lbl_road_name,
                 LV_ALIGN_TOP_LEFT,
                 IMG_DIR_SIZE + 6,
                 (lv_coord_t)get_font_height(font_sys) + 6);

    s_speed_frame = lv_obj_create(root);
    lv_obj_remove_style_all(s_speed_frame);
    lv_obj_set_size(s_speed_frame, 80, 80);
    lv_obj_align(s_speed_frame, LV_ALIGN_BOTTOM_MID, 160, -INFO_FRAME_HEIGHT - LVGL_UI_MARGIN_20);
    lv_obj_add_flag(s_speed_frame, LV_OBJ_FLAG_HIDDEN);

    s_speed_arc = lv_arc_create(s_speed_frame);
    lv_obj_remove_style_all(s_speed_arc);
    lv_obj_set_size(s_speed_arc, 70, 70);
    lv_arc_set_bg_angles(s_speed_arc, 0, 360);
    lv_obj_set_style_arc_color(s_speed_arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_speed_arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_speed_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_speed_arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_speed_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_speed_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(s_speed_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(s_speed_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_speed_arc);

    s_lbl_speed = lv_label_create(s_speed_arc);
    {
        const lv_font_t* speed_font = get_font_by_size_near(18);
        if (speed_font == NULL) {
            speed_font = font_sys;
        }
        obj_set_text_font(s_lbl_speed, speed_font);
    }
    lv_obj_set_style_text_color(s_lbl_speed, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_lbl_speed, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_speed, 70);
    lv_obj_align(s_lbl_speed, LV_ALIGN_CENTER, 0, 0);

    s_heart_frame = lv_obj_create(root);
    lv_obj_remove_style_all(s_heart_frame);
    lv_obj_set_size(s_heart_frame, 120, 80);
    lv_obj_align(s_heart_frame, LV_ALIGN_BOTTOM_MID, 50, -INFO_FRAME_HEIGHT - LVGL_UI_MARGIN_20);
    lv_obj_set_flex_flow(s_heart_frame, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_heart_frame, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_heart_frame, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_heart_frame, 5, LV_PART_MAIN);
    lv_obj_add_flag(s_heart_frame, LV_OBJ_FLAG_HIDDEN);

    s_img_heart = lv_image_create(s_heart_frame);
    lv_image_set_src(s_img_heart, FLOATAIR_SYS_IMG("icon_hr.jpg"));

    s_lbl_heart = lv_label_create(s_heart_frame);
    obj_set_text_font(s_lbl_heart, font_sys);
    lv_obj_set_style_text_color(s_lbl_heart, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_lbl_heart, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_heart, LV_PCT(100));

    s_spo_frame = lv_obj_create(root);
    lv_obj_remove_style_all(s_spo_frame);
    lv_obj_set_size(s_spo_frame, 100, 80);
    lv_obj_align(s_spo_frame, LV_ALIGN_BOTTOM_MID, 0, -INFO_FRAME_HEIGHT - LVGL_UI_MARGIN_20);
    lv_obj_set_flex_flow(s_spo_frame, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_spo_frame, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_spo_frame, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_spo_frame, 5, LV_PART_MAIN);
    lv_obj_add_flag(s_spo_frame, LV_OBJ_FLAG_HIDDEN);

    s_img_spo = lv_image_create(s_spo_frame);
    lv_image_set_src(s_img_spo, FLOATAIR_SYS_IMG("icon_spo.jpg"));

    s_lbl_spo = lv_label_create(s_spo_frame);
    obj_set_text_font(s_lbl_spo, font_sys);
    lv_obj_set_style_text_color(s_lbl_spo, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_lbl_spo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_spo, LV_PCT(100));

    navigation_view_set_init_visible(true);
}

static void navigation_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(true);
    (void)navigation_drive_icon_ensure();
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
}

static void navigation_page_destroy(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (s_dir_icon_dsc) {
        if (s_dir_icon_buf) {
            free(s_dir_icon_buf);
            s_dir_icon_buf = NULL;
            s_dir_icon_buf_sz = 0;
        }
        free(s_dir_icon_dsc);
        s_dir_icon_dsc = NULL;
    }
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }
    navigation_init_label = NULL;
    navigation_init_img = NULL;
    navigation_init_cont = NULL;
    s_info_frame = NULL;
    s_speed_frame = NULL;
    s_speed_arc = NULL;
    s_heart_frame = NULL;
    s_spo_frame = NULL;
    s_img_dir = NULL;
    s_img_drive = NULL;
    s_img_heart = NULL;
    s_img_spo = NULL;
    s_lbl_distance_left = NULL;
    s_lbl_mile_left = NULL;
    s_lbl_min_left = NULL;
    s_lbl_road_name = NULL;
    s_lbl_speed = NULL;
    s_lbl_heart = NULL;
    s_lbl_spo = NULL;
}

static app_page_t s_navigation_page = {
    .name = APP_NAME_NAVIGATION,
    .on_create = navigation_page_create,
    .on_appear = navigation_page_appear,
    .on_disappear = NULL,
    .on_destroy = navigation_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* navigation_page_get(void) {
    return &s_navigation_page;
}

void navigation_map_clear(void) {
    if (s_info_frame) lv_obj_add_flag(s_info_frame, LV_OBJ_FLAG_HIDDEN);
    if (s_speed_frame) lv_obj_add_flag(s_speed_frame, LV_OBJ_FLAG_HIDDEN);
    if (s_heart_frame) lv_obj_add_flag(s_heart_frame, LV_OBJ_FLAG_HIDDEN);
    if (s_spo_frame) lv_obj_add_flag(s_spo_frame, LV_OBJ_FLAG_HIDDEN);
    if (navigation_drive_icon_valid()) lv_obj_add_flag(s_img_drive, LV_OBJ_FLAG_HIDDEN);

    navigation_view_set_init_visible(true);
}

void navigation_map_update_info(int navMode,
                                const char* nextRoadName,
                                const char* curStepRetainDistance,
                                const char* remainDistance,
                                const char* remainTime,
                                const char* speed) {
    navigation_view_enter_map_mode();
    if (s_info_frame) lv_obj_remove_flag(s_info_frame, LV_OBJ_FLAG_HIDDEN);
    if (nextRoadName && s_lbl_road_name) {
        lv_label_set_text(s_lbl_road_name, nextRoadName);
    }
    if (curStepRetainDistance && s_lbl_distance_left) {
        lv_label_set_text(s_lbl_distance_left, curStepRetainDistance);
    }
    if (remainDistance && s_lbl_mile_left) {
        lv_label_set_text(s_lbl_mile_left, remainDistance);
    }
    if (remainTime && s_lbl_min_left) {
        lv_label_set_text(s_lbl_min_left, remainTime);
    }
    if (speed && strlen(speed) > 0) {
        if (s_speed_frame) lv_obj_remove_flag(s_speed_frame, LV_OBJ_FLAG_HIDDEN);
        if (s_lbl_speed) lv_label_set_text(s_lbl_speed, speed);
    }
    if (navigation_drive_icon_ensure()) {
        lv_obj_remove_flag(s_img_drive, LV_OBJ_FLAG_HIDDEN);
        const char* icon_src = NULL;
        switch (navMode) {
            case DRIVE_TYPE_WALK: icon_src = FLOATAIR_SYS_IMG("icon_walk.jpg"); break;
            case DRIVE_TYPE_BICY: icon_src = FLOATAIR_SYS_IMG("icon_bicycle.jpg"); break;
            case DRIVE_TYPE_CAR:  icon_src = FLOATAIR_SYS_IMG("icon_car.jpg"); break;
            default: icon_src = FLOATAIR_SYS_IMG("icon_car.jpg"); break;
        }
        if (icon_src) {
            lv_image_set_src(s_img_drive, icon_src);
            lv_obj_move_foreground(s_img_drive);
        }
    }
}

void navigation_map_update_bpm(const char* bmp) {
    navigation_view_enter_map_mode();
    if (s_heart_frame) lv_obj_remove_flag(s_heart_frame, LV_OBJ_FLAG_HIDDEN);
    if (s_lbl_heart) lv_label_set_text(s_lbl_heart, bmp ? bmp : "");
}

void navigation_map_update_spo(const char* spo) {
    navigation_view_enter_map_mode();
    if (s_spo_frame) lv_obj_remove_flag(s_spo_frame, LV_OBJ_FLAG_HIDDEN);
    if (s_lbl_spo) lv_label_set_text(s_lbl_spo, spo ? spo : "");
}

void navigation_map_update_dir_icon_bin(const uint8_t* data, size_t size) {
    if (!data || size == 0 || !s_img_dir) return;
    navigation_view_enter_map_mode();
    size_t expect = (size_t) IMG_DIR_SIZE * (size_t) IMG_DIR_SIZE;
    if (size != expect) {
        floatair_err("icon bin size unexpected: %u (expect %u)", (unsigned) size, (unsigned) expect);
        return;
    }
    if (!s_dir_icon_dsc) {
        s_dir_icon_dsc = (lv_image_dsc_t*) malloc(sizeof(lv_image_dsc_t));
        floatair_assert(s_dir_icon_dsc, "malloc dir icon dsc failed");
        memset(s_dir_icon_dsc, 0, sizeof(*s_dir_icon_dsc));
    }
    if (s_dir_icon_buf_sz != expect || !s_dir_icon_buf) {
        if (s_dir_icon_buf) {
            free(s_dir_icon_buf);
            s_dir_icon_buf = NULL;
            s_dir_icon_buf_sz = 0;
        }
        s_dir_icon_buf = (uint8_t*) malloc(expect);
        floatair_assert(s_dir_icon_buf, "malloc dir icon buf failed");
        s_dir_icon_buf_sz = expect;
    }
    memcpy(s_dir_icon_buf, data, expect);
    s_dir_icon_dsc->header.cf = LV_COLOR_FORMAT_L8;
    s_dir_icon_dsc->header.w = IMG_DIR_SIZE;
    s_dir_icon_dsc->header.h = IMG_DIR_SIZE;
    s_dir_icon_dsc->data_size = (uint32_t)expect;
    s_dir_icon_dsc->data = s_dir_icon_buf;
    if (lv_image_get_src(s_img_dir) != s_dir_icon_dsc) {
        lv_image_set_src(s_img_dir, s_dir_icon_dsc);
    } else {
        lv_obj_invalidate(s_img_dir);
    }
}
