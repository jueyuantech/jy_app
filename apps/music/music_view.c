/**
 * @file music_view.c
 * @brief Music 页面视图、歌词刷新和底部状态栏交互实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_music
 */
#include <time.h>
#include <string.h>
#include "home/home.h"
#include "music.h"
#include "common/app_framework/app_manager.h"
#include "system/system.h"
#include "floatair_fs.h"
#include "system/system.h"
#include "system/system_def.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "floatair_dbg.h"
#include "lvgl.h"
#include "common/widgets/status_bar.h"

static lv_obj_t* s_center_img = NULL;
static lv_obj_t* s_init_label = NULL;
static lv_obj_t* s_init_cont = NULL;
static lv_obj_t* s_label_artist = NULL;
static lv_obj_t* s_label_album = NULL;
static lv_obj_t* s_label_lyric1 = NULL;
static lv_obj_t* s_label_lyric2 = NULL;
static lv_obj_t* s_label_lyric3 = NULL;

static void music_view_apply_mode(bool show_init) {
    if (show_init) {
        if (s_init_label) lv_obj_remove_flag(s_init_label, LV_OBJ_FLAG_HIDDEN);
        if (s_center_img) lv_obj_remove_flag(s_center_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_init_label) lv_obj_add_flag(s_init_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_label_artist) lv_obj_add_flag(s_label_artist, LV_OBJ_FLAG_HIDDEN);
    if (s_label_album) lv_obj_add_flag(s_label_album, LV_OBJ_FLAG_HIDDEN);
    if (s_label_lyric1) lv_obj_add_flag(s_label_lyric1, LV_OBJ_FLAG_HIDDEN);
    if (s_label_lyric2) lv_obj_add_flag(s_label_lyric2, LV_OBJ_FLAG_HIDDEN);
    if (s_label_lyric3) lv_obj_add_flag(s_label_lyric3, LV_OBJ_FLAG_HIDDEN);
}

static void touch_event_handle(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DCLICKED) {
        (void)app_router_exit_current_app();
    } else if (code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED) {
    } else if (code == LV_EVENT_GESTURE_LEFT) {
    } else if (code == LV_EVENT_GESTURE_RIGHT) {
    }
}

static void music_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    s_init_cont = lv_obj_create(root);
    lv_obj_remove_style_all(s_init_cont);
    lv_obj_set_size(s_init_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_init_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_init_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_init_cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_init_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_init_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_init_cont, LVGL_UI_MARGIN_20, LV_PART_MAIN);

    s_center_img = lv_image_create(s_init_cont);
    lv_obj_set_size(s_center_img, 80, 80);
    lv_image_set_src(s_center_img, FLOATAIR_SYS_IMG("musicB.jpg"));

    s_init_label = lv_label_create(s_init_cont);
    obj_set_text_font(s_init_label, get_system_font());
    lv_obj_set_style_text_align(s_init_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_init_label, lv_color_white(), 0);
    lv_label_set_long_mode(s_init_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_init_label, LV_PCT(100));
    lv_obj_set_height(s_init_label, get_system_font_height() * 2 + get_system_font_row_space() * 3);
    lv_label_set_text(s_init_label, app_get_str("OPEN_IDLE_MUSIC"));

    const lv_font_t* font_sys = get_system_font();
    int32_t sys_h = (int32_t) get_system_font_height();
    int32_t row_space = (int32_t) get_system_font_row_space();
    int32_t lyric_h = sys_h + row_space * 2;

    s_label_artist = lv_label_create(root);
    lv_obj_set_style_text_align(s_label_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_label_artist, lv_color_white(), 0);
    obj_set_text_font(s_label_artist, font_sys);
    lv_label_set_long_mode(s_label_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(s_label_artist, LV_PCT(100), (int32_t) (sys_h + row_space * 2));
    lv_obj_align(s_label_artist,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 -(lyric_h * 3 + (int32_t) (sys_h + row_space * 2)));
    lv_obj_add_flag(s_label_artist, LV_OBJ_FLAG_HIDDEN);

    s_label_album = lv_label_create(root);
    lv_obj_set_style_text_align(s_label_album, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_label_album, lv_color_white(), 0);
    obj_set_text_font(s_label_album, font_sys);
    lv_label_set_long_mode(s_label_album, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(s_label_album, LV_PCT(100), (int32_t) (sys_h + row_space * 2));
    lv_obj_align(s_label_album,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 -(lyric_h * 3));
    lv_obj_add_flag(s_label_album, LV_OBJ_FLAG_HIDDEN);

    s_label_lyric1 = lv_label_create(root);
    lv_obj_set_style_text_align(s_label_lyric1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_label_lyric1, lv_color_white(), 0);
    obj_set_text_font(s_label_lyric1, font_sys);
    lv_label_set_long_mode(s_label_lyric1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(s_label_lyric1, LV_PCT(100), (int32_t) (sys_h + row_space * 2));
    lv_obj_align(s_label_lyric1,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 -(lyric_h * 2));
    lv_obj_add_flag(s_label_lyric1, LV_OBJ_FLAG_HIDDEN);

    s_label_lyric2 = lv_label_create(root);
    lv_obj_set_style_text_align(s_label_lyric2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_label_lyric2, lv_color_white(), 0);
    obj_set_text_font(s_label_lyric2, font_sys);
    lv_label_set_long_mode(s_label_lyric2, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_label_lyric2, LV_PCT(100), (int32_t) (sys_h + row_space * 2));
    lv_obj_align(s_label_lyric2, LV_ALIGN_BOTTOM_MID, 0, -lyric_h);
    lv_obj_add_flag(s_label_lyric2, LV_OBJ_FLAG_HIDDEN);

    s_label_lyric3 = lv_label_create(root);
    lv_obj_set_style_text_align(s_label_lyric3, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_label_lyric3, lv_color_white(), 0);
    obj_set_text_font(s_label_lyric3, font_sys);
    lv_label_set_long_mode(s_label_lyric3, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_label_lyric3, LV_PCT(100), (int32_t) (sys_h + row_space * 2));
    lv_obj_align(s_label_lyric3, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_label_lyric3, LV_OBJ_FLAG_HIDDEN);

    music_view_apply_mode(true);
}

static void music_page_appear(lv_obj_t* root) {
    system_status_bar_set_mode(true);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
}

static app_page_t s_music_page = {
    .name = APP_NAME_MUSIC,
    .on_create = music_page_create,
    .on_appear = music_page_appear,
    .on_disappear = NULL,
    .on_destroy = NULL,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* music_page_get(void) {
    return &s_music_page;
}

void music_avrcp_clear(void) {
    if (s_center_img && lv_obj_has_flag(s_center_img, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_center_img, LV_OBJ_FLAG_HIDDEN);
    }
    music_view_apply_mode(true);
}

void music_avrcp_update_meta(const char* artist, const char* album) {
    if (artist && s_label_artist) {
        lv_obj_remove_flag(s_label_artist, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_label_artist, artist);
    }
    if (album && s_label_album) {
        lv_obj_remove_flag(s_label_album, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_label_album, album);
    }
}

void music_avrcp_update_lyric(const char* text) {
    if (!text || strlen(text) == 0) return;
    if (!s_label_lyric1 || !s_label_lyric2 || !s_label_lyric3) return;
    if (s_init_label) lv_obj_add_flag(s_init_label, LV_OBJ_FLAG_HIDDEN);
    if (s_center_img) lv_obj_add_flag(s_center_img, LV_OBJ_FLAG_HIDDEN);
    const char* l1 = lv_label_get_text(s_label_lyric1);
    const char* l2 = lv_label_get_text(s_label_lyric2);
    if (s_label_lyric3) {
        lv_obj_remove_flag(s_label_lyric3, LV_OBJ_FLAG_HIDDEN);
        if (l2) lv_label_set_text(s_label_lyric3, l2); else lv_label_set_text(s_label_lyric3, "");
    }
    if (s_label_lyric2) {
        lv_obj_remove_flag(s_label_lyric2, LV_OBJ_FLAG_HIDDEN);
        if (l1) lv_label_set_text(s_label_lyric2, l1); else lv_label_set_text(s_label_lyric2, "");
    }
    if (s_label_lyric1) {
        lv_obj_remove_flag(s_label_lyric1, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_label_lyric1, text);
    }
}
