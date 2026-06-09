/**
 * @file gallery.c
 * @brief 图片库应用页面实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_gallery
 */
#include <time.h>
#include "gallery.h"

#include "common/app_framework/app_nav.h"
#include "common/app_framework/app_manager.h"
#include "message.h"
#include "app_def.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "floatair_fs.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


lv_obj_t *g_pic = NULL;
static lv_obj_t *g_err_label = NULL;
static char **g_files = NULL;
static size_t g_file_count = 0;
static size_t g_cur_idx = 0;
static bool s_gallery_msg_registered = false;

bool gallery_update_pic(const char *img) {
    if (img == NULL || strlen(img) == 0) {
        floatair_err("img is NULL");
        return false;
    }
    if (app_image_path_valid(img)) {
        floatair_info("probe src %s", img);
        lv_image_header_t header;
        lv_result_t res = lv_image_decoder_get_info(img, &header);
        if (res != LV_RESULT_OK) {
            floatair_err("decode header failed: %s", img);
            if (g_err_label) {
                lv_label_set_text(g_err_label, app_get_str("GALLERY_IMAGE_DECODE_FAILED"));
                lv_obj_remove_flag(g_err_label, LV_OBJ_FLAG_HIDDEN);
            }
            return false;
        }

        if (g_err_label) lv_obj_add_flag(g_err_label, LV_OBJ_FLAG_HIDDEN);
        floatair_info("set src %s", img);
        lv_image_set_src(g_pic, img);
        lv_image_set_scale(g_pic, LV_SCALE_NONE);
        lv_obj_remove_flag(g_pic, LV_OBJ_FLAG_HIDDEN);
        return true;
    }
    return false;
}

static void touch_event_handle(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (g_file_count == 0 || !g_pic) {
        floatair_err("g_file_count == 0 || !g_pic");
        return;
    }
    if (code == LV_EVENT_GESTURE_LEFT) {
        if (g_cur_idx == 0) g_cur_idx = g_file_count -1;
        else g_cur_idx--;
        gallery_update_pic(g_files[g_cur_idx]);
    } else if (code == LV_EVENT_GESTURE_RIGHT) {
        g_cur_idx = (g_cur_idx + 1) % g_file_count;
        gallery_update_pic(g_files[g_cur_idx]);
    } else if (code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED) {
        floatair_info("click not supported");
    } else if (code == LV_EVENT_DCLICKED) {
        (void)app_router_exit_current_app();
    }
    return;
}

void gallery_update_pic_folder(void) {
    if (g_files) {
        for (size_t i = 0; i < g_file_count; i++) {
            free(g_files[i]);
            g_files[i] = NULL;
        }
        free(g_files);
        g_files = NULL;
        g_file_count = 0;
        g_cur_idx = 0;
    }
    floatair_dir_t *dir = NULL;
    char img_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_images_path(APP_NAME_GALLERY, img_path, sizeof(img_path))) {
        floatair_err("get app images path failed");
        return;
    }
    if (floatair_fs_dir_open(img_path, &dir) != FLOATAIR_FS_OK) {
        floatair_err("open %s failed", img_path);
        return;
    }
    size_t cap = 16;
    g_files = (char**)malloc(sizeof(char*) * cap);
    floatair_assert(g_files != NULL, "malloc failed");
    char namebuf[SYSTEM_MAX_PATH_LEN] = {0};
    for (;;) {
        int r = floatair_fs_dir_read(dir, namebuf, sizeof(namebuf), NULL);
        if (r != 0) {
            floatair_err("read dir failed: %s", namebuf);
            break;
        }
        if (namebuf[0] == '\0') {
            floatair_info("read dir end");
            break;
        }
        if (strcmp(namebuf, ".") == 0 || strcmp(namebuf, "..") == 0) continue;
        size_t need = strlen(img_path) + 1 + strlen(namebuf);
        char *full = (char*)malloc(need);
        floatair_assert(full != NULL, "malloc failed");
        memset(full, 0, need);
        snprintf(full, need, "%s%s", img_path, namebuf);
        if (system_is_image_file(full)) {
            if (g_file_count == cap) {
                cap *= 2;
                char **tmp = (char**)realloc(g_files, sizeof(char*) * cap);
                floatair_assert(tmp != NULL, "realloc g_files failed");
                g_files = tmp;
            }
            floatair_info("add %s[%zu]", full, g_file_count);
            g_files[g_file_count++] = full;
        } else {
            floatair_info("skip %s", full);
            free(full);
            full = NULL;
        }
    }
    floatair_fs_dir_close(dir);
}

void gallery_clear_view(void) {
    if (g_err_label) {
        lv_label_set_text(g_err_label, "");
        lv_obj_add_flag(g_err_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_pic) {
        lv_obj_add_flag(g_pic, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 页面生命周期回调 */

/**
 * @brief 创建 Gallery 页面内容。
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参。
 */
static void gallery_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    g_pic = lv_image_create(root);
    floatair_assert(g_pic != NULL, "g_pic NULL");
    lv_obj_set_size(g_pic, LV_PCT(100), LV_PCT(100));
    lv_obj_align(g_pic, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_inner_align(g_pic, LV_IMAGE_ALIGN_CENTER);

    g_err_label = lv_label_create(root);
    floatair_assert(g_err_label != NULL, "g_err_label NULL");
    lv_obj_set_size(g_err_label, LV_PCT(100), LV_PCT(33));
    lv_obj_set_style_text_align(g_err_label, LV_TEXT_ALIGN_CENTER, 0);
    obj_set_text_font(g_err_label, get_system_font());
    lv_label_set_long_mode(g_err_label, LV_LABEL_LONG_WRAP);
    lv_obj_center(g_err_label);
    lv_obj_add_flag(g_err_label, LV_OBJ_FLAG_HIDDEN);
    gallery_update_pic_folder();
    if (g_file_count > 0) {
        g_cur_idx = 0;
        gallery_update_pic(g_files[g_cur_idx]);
    } else {
        floatair_warn("no images");
    }
}

static void gallery_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(true);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
}

static void gallery_page_destroy(void) {
    if (g_files) {
        for (size_t i = 0; i < g_file_count; i++) {
            free(g_files[i]);
        }
        free(g_files);
        g_files = NULL;
        g_file_count = 0;
        g_cur_idx = 0;
    }
    g_pic = NULL;
    g_err_label = NULL;
}

static app_message_t gallery_msg = {
    .id   = APP_MSG_ID_GALLERY,
    .name = APP_NAME_GALLERY,
    .cb   = gallery_route_cmd,
};

static bool gallery_msg_register_once(void) {
    int ret = 0;

    if (s_gallery_msg_registered) {
        return true;
    }

    ret = app_msg_register(&gallery_msg);
    if (ret != 0) {
        return false;
    }
    s_gallery_msg_registered = true;
    return true;
}

static void gallery_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_gallery_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_GALLERY);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_gallery_msg_registered = false;
}

static app_page_t s_gallery_page = {
    .name = APP_NAME_GALLERY,
    .on_create = gallery_page_create,
    .on_appear = gallery_page_appear,
    .on_disappear = NULL,
    .on_destroy = gallery_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* gallery_page_get(void) {
    return &s_gallery_page;
}

static void gallery_app_on_start(void) {
    if (!gallery_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)gallery_page_get(), NULL, 0)) {
        floatair_assert(false, "gallery page replace failed");
    }
}

static void gallery_app_on_stop(void) {
    gallery_msg_unregister_if_needed();
    gallery_clear_view();
    gallery_page_destroy();
}

static app_t s_gallery_app = {
    .name = APP_NAME_GALLERY,
    .on_start = gallery_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = gallery_app_on_stop,
    .on_back = NULL,
};

bool gallery_app_register(void) {
    return app_manager_register(&s_gallery_app);
}
