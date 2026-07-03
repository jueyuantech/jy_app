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
#include "common/widgets/progress_indicator.h"
#include "message.h"
#include "app_def.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "system/system_file_transfer.h"
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

/**
 * @brief Gallery 页面显示模式。
 */
typedef enum {
    GALLERY_VIEW_MODE_IMAGE = 0,   ///< 图片浏览态，展示图片或错误提示。
    GALLERY_VIEW_MODE_TRANSFER,    ///< 文件传输态，展示全屏传输进度。
} gallery_view_mode_t;

static gallery_view_mode_t s_gallery_view_mode = GALLERY_VIEW_MODE_IMAGE; ///< 当前 Gallery 页面显示模式。
static lv_obj_t* s_transfer_box = NULL;                                   ///< 文件传输态全屏容器。
static progress_indicator_t* s_transfer_indicator = NULL;                 ///< 文件传输进度提示组件。

/**
 * @brief 按当前模式同步图片、错误提示和传输进度层显隐。
 * @return 无返回值。
 */
static void gallery_apply_view_mode(void) {
    bool transfer_mode = (s_gallery_view_mode == GALLERY_VIEW_MODE_TRANSFER);

    if (s_transfer_box) {
        if (transfer_mode) {
            lv_obj_remove_flag(s_transfer_box, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_transfer_box);
        } else {
            lv_obj_add_flag(s_transfer_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 切换 Gallery 页面显示模式。
 * @param[in] mode 目标显示模式。
 * @return 无返回值。
 */
static void gallery_set_view_mode(gallery_view_mode_t mode) {
    if (s_gallery_view_mode == mode) {
        return;
    }

    s_gallery_view_mode = mode;
    gallery_apply_view_mode();
}

/**
 * @brief 刷新文件传输态进度提示。
 * @param[in] percent 文件传输百分比。
 * @return 无返回值。
 */
static void gallery_transfer_set_progress(uint32_t percent) {
    if (s_transfer_indicator == NULL) {
        return;
    }
    if (percent > 100U) {
        percent = 100U;
    }
    progress_indicator_set_percent(s_transfer_indicator, (int32_t)percent);
}

/**
 * @brief 判断路径是否为 Gallery 支持展示的图片文件。
 * @param[in] path 待检查的文件路径。
 * @return `true` 表示是受支持图片，`false` 表示不是。
 */
static bool gallery_is_supported_image_file(const char* path) {
    return path != NULL && system_is_image_file(path);
}

/**
 * @brief 处理系统文件写入进度通知。
 *
 * 只有当前正在 Gallery 页面且写入的是支持的图片文件时，才展示全屏传输态。
 *
 * @param path 正在写入的文件完整路径。
 * @param written 已写入字节数。
 * @param total 文件总字节数。
 * @param user_data 用户数据。
 * @return 无返回值。
 */
static void gallery_on_file_transfer_progress(const char* path,
                                              uint32_t written,
                                              uint32_t total,
                                              void* user_data) {
    uint32_t percent = 0;
    LV_UNUSED(user_data);

    if (path == NULL || total == 0 ||
        strcmp(app_router_get_app(), APP_NAME_GALLERY) != 0 ||
        !gallery_is_supported_image_file(path)) {
        return;
    }

    if (written > total) {
        written = total;
    }
    percent = (uint32_t)(((uint64_t)written * 100U) / (uint64_t)total);
    if (percent > 100U) {
        percent = 100U;
    }

    gallery_transfer_set_progress(percent);

    if (written == total) {
        gallery_update_pic_folder();
        (void)gallery_update_pic(path);
        return;
    }

    if (system_file_transfer_is_upload_progress_visible()) {
        if (s_gallery_view_mode != GALLERY_VIEW_MODE_TRANSFER) {
            gallery_set_view_mode(GALLERY_VIEW_MODE_TRANSFER);
        }
    } else if (s_gallery_view_mode == GALLERY_VIEW_MODE_TRANSFER) {
        gallery_set_view_mode(GALLERY_VIEW_MODE_IMAGE);
    }
}

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
            gallery_set_view_mode(GALLERY_VIEW_MODE_IMAGE);
            return false;
        }

        if (g_err_label) lv_obj_add_flag(g_err_label, LV_OBJ_FLAG_HIDDEN);
        floatair_info("set src %s", img);
        lv_image_set_src(g_pic, img);
        lv_image_set_scale(g_pic, LV_SCALE_NONE);
        lv_obj_remove_flag(g_pic, LV_OBJ_FLAG_HIDDEN);
        gallery_set_view_mode(GALLERY_VIEW_MODE_IMAGE);
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
    gallery_set_view_mode(GALLERY_VIEW_MODE_IMAGE);
}

/**
 * @brief 创建 Gallery 文件传输进度层。
 * @param[in] root 页面根对象。
 * @return 无返回值。
 */
static void gallery_create_transfer_view(lv_obj_t* root) {
    progress_indicator_cfg_t cfg;

    s_transfer_box = lv_obj_create(root);
    floatair_assert(s_transfer_box != NULL, "gallery transfer box NULL");
    lv_obj_remove_style_all(s_transfer_box);
    lv_obj_set_size(s_transfer_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_transfer_box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_transfer_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_layout(s_transfer_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_transfer_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_transfer_box,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_transfer_box, LV_OBJ_FLAG_SCROLLABLE);

    cfg = progress_indicator_default_cfg();
    cfg.w = LV_PCT(100);
    cfg.h = LV_SIZE_CONTENT;
    cfg.text.w = LV_PCT(100);
    cfg.text.h = LV_SIZE_CONTENT;
    cfg.text.align = LABEL_ALIGN_CENTER;
    cfg.text.overflow = LABEL_OVERFLOW_WRAP;
    s_transfer_indicator = progress_indicator_create(s_transfer_box, &cfg);
    floatair_assert(s_transfer_indicator != NULL, "gallery transfer indicator NULL");
    gallery_transfer_set_progress(0);
    lv_obj_add_flag(s_transfer_box, LV_OBJ_FLAG_HIDDEN);
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
    gallery_create_transfer_view(root);
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
    system_file_transfer_clear_progress_callback(gallery_on_file_transfer_progress, NULL);
    s_gallery_view_mode = GALLERY_VIEW_MODE_IMAGE;
    g_pic = NULL;
    g_err_label = NULL;
    s_transfer_box = NULL;
    s_transfer_indicator = NULL;
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
    system_file_transfer_set_progress_callback(gallery_on_file_transfer_progress, NULL);
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
