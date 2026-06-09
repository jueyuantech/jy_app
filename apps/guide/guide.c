/**
 * @file guide.c
 * @brief Guide 应用生命周期、页面入口和引导流程实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-03-14
 * @copyright JYTek
 * @ingroup app_guide
 */
#include "guide.h"

#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_nav.h"
#include "message.h"
#include "app_def.h"
#include "floatair_fs.h"
#include "system/system.h"
#include "system/system_def.h"
#include "system/system_config_json.h"
#include "system/system_runtime_ui.h"
#include "system/system_timer.h"
#include "common/widgets/status_bar.h"
#include "system/system_res.h"

#define USERGUILD_FONT_SIZE_M 36
#define USERGUILD_FONT_SIZE_S 20
#define USERGUIDE_STEP_DELAY_MS 1000

static const lv_font_t* userguide_font_m = NULL;
static const lv_font_t* userguide_font_s = NULL;
static lv_style_t userguide_stylebolder = {0};

static lv_obj_t* guild_cont = NULL;
static lv_obj_t* guide_center_img = NULL;
static lv_obj_t* guide_center_label_op = NULL;
static lv_obj_t* guide_center_label_func = NULL;

typedef enum {
    GUIDE_MODE_CLICK = 0,
    GUIDE_MODE_CLICK_DONE = 1,
    GUIDE_MODE_LONGPRESS = 2,
    GUIDE_MODE_LONGPRESS_DONE = 3,
    GUIDE_MODE_SCROLL = 4,
    GUIDE_MODE_SCROLL_DONE = 5,
    GUIDE_MODE_DCLICK = 6,
    GUIDE_MODE_DCLICK_DONE = 7,
    GUIDE_MODE_FINISH = 8,
    
} guide_mode_t;

static guide_mode_t g_guide_mode = GUIDE_MODE_CLICK;
static uint32_t guide_timer_id = 0;
static uint32_t guide_switch_home_timer_id = 0;
static bool guide_scroll_left_ok = false;
static bool guide_scroll_right_ok = false;
static bool s_guide_msg_registered = false;
static void guide_timer_cb(void* user_data);
static void schedule_mode_after(uint32_t ms, guide_mode_t next);
static void update_guide_ui(guide_mode_t mode);

/* Page lifecycle callbacks */

static void touch_event_handle(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    switch (g_guide_mode) {
        case GUIDE_MODE_CLICK:
            if (code == LV_EVENT_CLICKED) {
                g_guide_mode = GUIDE_MODE_CLICK_DONE;
                update_guide_ui(g_guide_mode);
                schedule_mode_after(USERGUIDE_STEP_DELAY_MS, GUIDE_MODE_LONGPRESS);
            }
            break;
        case GUIDE_MODE_LONGPRESS:
            if (code == LV_EVENT_LONG_PRESSED) {
                g_guide_mode = GUIDE_MODE_LONGPRESS_DONE;
                update_guide_ui(g_guide_mode);
                schedule_mode_after(USERGUIDE_STEP_DELAY_MS, GUIDE_MODE_SCROLL);
            }
            break;
        case GUIDE_MODE_SCROLL:
            if (code == LV_EVENT_GESTURE_LEFT) {
                guide_scroll_left_ok = true;
            } else if (code == LV_EVENT_GESTURE_RIGHT) {
                guide_scroll_right_ok = true;
            }
            if (guide_scroll_left_ok && guide_scroll_right_ok) {
                g_guide_mode = GUIDE_MODE_SCROLL_DONE;
                update_guide_ui(g_guide_mode);
                schedule_mode_after(USERGUIDE_STEP_DELAY_MS, GUIDE_MODE_DCLICK);
            }
            break;
        case GUIDE_MODE_DCLICK:
            if (code == LV_EVENT_DCLICKED) {
                g_guide_mode = GUIDE_MODE_DCLICK_DONE;
                update_guide_ui(g_guide_mode);
                schedule_mode_after(USERGUIDE_STEP_DELAY_MS, GUIDE_MODE_FINISH);
            }
            break;
        default:
            break;
    }
}

/**
 * @brief 创建 Guide 页面内容。
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参。
 */
static void guide_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    g_guide_mode = GUIDE_MODE_CLICK;
    floatair_assert(root != NULL, "root NULL");
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_style_init(&userguide_stylebolder);
    lv_style_set_radius(&userguide_stylebolder, 10);
    lv_style_set_opa(&userguide_stylebolder, LV_OPA_100);
    lv_style_set_border_color(&userguide_stylebolder, lv_color_white());
    lv_style_set_border_width(&userguide_stylebolder, 2);
    lv_style_set_border_opa(&userguide_stylebolder, LV_OPA_100);
    lv_style_set_border_side(&userguide_stylebolder, LV_BORDER_SIDE_FULL);

    userguide_font_m = get_font_by_size_near(USERGUILD_FONT_SIZE_M);
    floatair_assert(userguide_font_m != NULL, "userguide_font_m is NULL");
    userguide_font_s = get_font_by_size_near(USERGUILD_FONT_SIZE_S);
    floatair_assert(userguide_font_s != NULL, "userguide_font_s is NULL");

    guild_cont = lv_obj_create(root);
    lv_obj_remove_style_all(guild_cont);
    lv_obj_set_size(guild_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(guild_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(guild_cont, 0, LV_PART_MAIN);
    lv_obj_remove_flag(guild_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(guild_cont, LV_ALIGN_CENTER, 0, 0);
    guide_center_img = lv_image_create(guild_cont);
    lv_obj_set_size(guide_center_img, 120, 48);
    lv_obj_align(guide_center_img, LV_ALIGN_CENTER, 0, 0);
    //lv_obj_add_flag(guide_center_img, LV_OBJ_FLAG_HIDDEN);
    guide_center_label_op = lv_label_create(guild_cont);
    lv_obj_set_size(guide_center_label_op, LV_PCT(80), (lv_coord_t)get_font_height(userguide_font_s));
    lv_obj_align_to(guide_center_label_op, guide_center_img, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    lv_obj_set_style_text_color(guide_center_label_op, lv_color_white(), 0);
    lv_obj_set_style_text_align(guide_center_label_op, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(guide_center_label_op, lv_color_black(), 0);
    obj_set_text_font(guide_center_label_op, userguide_font_s);
    //lv_obj_add_flag(guide_center_label_op, LV_OBJ_FLAG_HIDDEN);
    guide_center_label_func = lv_label_create(guild_cont);
    lv_obj_set_size(guide_center_label_func, LV_PCT(80), (lv_coord_t)get_font_height(userguide_font_s));
    lv_obj_align_to(guide_center_label_func, guide_center_label_op, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    lv_obj_set_style_text_color(guide_center_label_func, lv_color_white(), 0);
    lv_obj_set_style_text_align(guide_center_label_func, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(guide_center_label_func, lv_color_black(), 0);
    obj_set_text_font(guide_center_label_func, userguide_font_s);
    //lv_obj_add_flag(guide_center_label_func, LV_OBJ_FLAG_HIDDEN);
    update_guide_ui(g_guide_mode);
}

static void guide_timer_cb(void* user_data) {
    guide_mode_t next = (guide_mode_t)(uintptr_t)user_data;
    guide_timer_id = 0;
    g_guide_mode = next;
    update_guide_ui(g_guide_mode);
}


static void schedule_mode_after(uint32_t ms, guide_mode_t next) {
    if (guide_timer_id != 0) {
        system_timer_autodestroy_cancel(guide_timer_id);
        guide_timer_id = 0;
    }
    if (!system_timer_autodestroy_start(ms,
                                       guide_timer_cb,
                                       (void*)(uintptr_t)next,
                                       &guide_timer_id)) {
        guide_timer_cb((void*)(uintptr_t)next);
    }
}

static void switch_home_cb(void* user_data) {
    (void)user_data;
    guide_switch_home_timer_id = 0;
    (void)app_router_exit_current_app();
}

static void update_guide_ui(guide_mode_t mode) {
    const char* op = NULL;
    const char* func = NULL;
    const char* img_src = NULL;
    bool hide_func = false;
    bool skip_ui = false;
    switch (mode) {
        case GUIDE_MODE_CLICK:
            img_src = FLOATAIR_SYS_IMG("guide_press.jpg");
            op = "GUILD_CLICK";
            func = "GUILD_CLICK_FUNC";
            break;
        case GUIDE_MODE_CLICK_DONE:
            img_src = FLOATAIR_SYS_IMG("guide_suc.jpg");
            op = "GUILD_SUC";
            hide_func = true;
            break;
        case GUIDE_MODE_LONGPRESS:
            img_src = FLOATAIR_SYS_IMG("guide_press.jpg");
            op = "GUILD_LONGPRESS_RIGHT";
            func = "GUILD_LONGPRESS_FUNC";
            break;
        case GUIDE_MODE_LONGPRESS_DONE:
            img_src = FLOATAIR_SYS_IMG("guide_suc.jpg");
            op = "GUILD_SUC";
            hide_func = true;
            break;
        case GUIDE_MODE_SCROLL:
            guide_scroll_left_ok = false;
            guide_scroll_right_ok = false;
            img_src = FLOATAIR_SYS_IMG("guide_scroll.jpg");
            op = "GUILD_STROLL";
            func = "GUILD_STROLL_FUNC";
            break;
        case GUIDE_MODE_SCROLL_DONE:
            img_src = FLOATAIR_SYS_IMG("guide_suc.jpg");
            op = "GUILD_SUC";
            hide_func = true;
            break;
        case GUIDE_MODE_DCLICK:
            img_src = FLOATAIR_SYS_IMG("guide_press.jpg");
            op = "GUILD_DCLICK";
            func = "GUILD_DCLICK_FUNC";
            break;
        case GUIDE_MODE_DCLICK_DONE:
            img_src = FLOATAIR_SYS_IMG("guide_suc.jpg");
            op = "GUILD_SUC";
            hide_func = true;
            break;
        case GUIDE_MODE_FINISH:
            img_src = FLOATAIR_SYS_IMG("guide_suc.jpg");
            op = "GUILD_SUC";
            hide_func = true;
            system_config_set_userguide_finish(true);
            skip_ui = true;
            break;
        default:
            break;
    }
    if (skip_ui) {
        if (guide_switch_home_timer_id != 0) {
            system_timer_autodestroy_cancel(guide_switch_home_timer_id);
            guide_switch_home_timer_id = 0;
        }
        if (!system_timer_autodestroy_start(10, switch_home_cb, NULL, &guide_switch_home_timer_id)) {
            (void)app_router_exit_current_app();
        }
        return;
    }
    if (img_src) {
        lv_image_set_src(guide_center_img, img_src);
    }
    if (op) {
        lv_label_set_text(guide_center_label_op, app_get_str(op));
    }
    if (func) {
        lv_label_set_text(guide_center_label_func, app_get_str(func));
        lv_obj_remove_flag(guide_center_label_func, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(guide_center_label_func, LV_OBJ_FLAG_HIDDEN);
    }
    if (hide_func) {
        lv_obj_add_flag(guide_center_label_func, LV_OBJ_FLAG_HIDDEN);
    }
}

static void guide_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    system_status_bar_set_mode(true);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
}

static void guide_page_destroy(void) {
    userguide_font_m = NULL;
    userguide_font_s = NULL;
    if (guide_timer_id != 0) {
        system_timer_autodestroy_cancel(guide_timer_id);
        guide_timer_id = 0;
    }
    if (guide_switch_home_timer_id != 0) {
        system_timer_autodestroy_cancel(guide_switch_home_timer_id);
        guide_switch_home_timer_id = 0;
    }
    lv_style_reset(&userguide_stylebolder);
}

static bool guide_msg_cb(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    if (!msg) {
        return false;
    }
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static app_message_t guide_msg = {
    .id   = APP_MSG_ID_GUIDE,
    .name = APP_NAME_GUIDE,
    .cb   = guide_msg_cb,
};

static bool guide_msg_register_once(void) {
    int ret = 0;

    if (s_guide_msg_registered) {
        return true;
    }

    ret = app_msg_register(&guide_msg);
    if (ret != 0) {
        return false;
    }
    s_guide_msg_registered = true;
    return true;
}

static void guide_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_guide_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_GUIDE);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_guide_msg_registered = false;
}

static app_page_t s_guide_page = {
    .name = APP_NAME_GUIDE,
    .on_create = guide_page_create,
    .on_appear = guide_page_appear,
    .on_disappear = NULL,
    .on_destroy = guide_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* guide_page_get(void) {
    return &s_guide_page;
}

static void guide_app_on_start(void) {
    if (!guide_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)guide_page_get(), NULL, 0)) {
        floatair_assert(false, "guide page replace failed");
    }
}

static void guide_app_on_stop(void) {
    guide_msg_unregister_if_needed();
    guide_page_destroy();
}

static app_t s_guide_app = {
    .name = APP_NAME_GUIDE,
    .on_start = guide_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = guide_app_on_stop,
    .on_back = NULL,
};

bool guide_app_register(void) {
    return app_manager_register(&s_guide_app);
}
