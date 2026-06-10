/**
 * @file home_view.c
 * @brief Home 页面视图、输入分发和本地 App 路由实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_home
 */
#include "home.h"

#include "app_def.h"
#include "common/app_framework/app_layers.h"
#include "common/app_framework/app_manager.h"
#include "floatair_fs.h"
#include "message.h"
#include "system/system.h"
#include "common/app_framework/app_router.h"
#include "system/system_config_json.h"
#include "system/system_def.h"
#include "sys_adapter.h"

static lv_obj_t* idlepbl  = NULL;
static lv_obj_t* idlepbr = NULL;
static lv_obj_t* idle_img_center_anchor = NULL;
static lv_obj_t* idle_img_center = NULL;
static lv_obj_t* idle_text_center = NULL;
static lv_obj_t* idle_img_left = NULL;
static lv_obj_t* idle_img_right = NULL;
static lv_obj_t* home_float_container = NULL;
static lv_obj_t* home_buttons_container = NULL;

static uint8_t home_select = 0;
static bool s_home_units_initialized = false;
static bool s_home_route_pending = false;
static char s_home_pending_app[MSG_STR_MAX_LEN] = {0};
static size_t s_home_units_count = 0;
static const app_home_unit_t* s_home_units_cur = NULL;

/**
 * @brief 创建 Home 选中项浮层容器。
 * @param[in] fallback_parent 框架浮层不可用时使用的页面父对象。
 * @return 成功返回浮层容器对象，失败返回 `NULL`。
 */
static lv_obj_t* home_float_container_create(lv_obj_t* fallback_parent) {
    lv_obj_t* parent = home_enable_app_float ? app_layers_get_app_float() : fallback_parent;
    lv_obj_t* container = NULL;

    if (parent == NULL || !lv_obj_is_valid(parent)) {
        parent = fallback_parent;
    }
    if (parent == NULL) {
        return NULL;
    }

    container = lv_obj_create(parent);
    if (container == NULL) {
        return NULL;
    }

    lv_obj_remove_style_all(container);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    return container;
}

/**
 * @brief 清理 Home 选中项浮层对象。
 * @return 无返回值。
 */
static void home_float_container_delete(void) {
    if (home_float_container != NULL && lv_obj_is_valid(home_float_container)) {
        lv_obj_delete(home_float_container);
    }
    home_float_container = NULL;
    idle_img_center = NULL;
    idle_text_center = NULL;
}

/**
 * @brief 将 Home 选中项浮层容器同步到 Home 内容区坐标系。
 * @return 无返回值。
 */
static void home_float_container_sync_layout(void) {
    if (home_float_container == NULL || !lv_obj_is_valid(home_float_container) ||
        home_buttons_container == NULL || !lv_obj_is_valid(home_buttons_container)) {
        return;
    }

    lv_obj_update_layout(home_buttons_container);
    lv_obj_set_size(home_float_container,
                    lv_obj_get_width(home_buttons_container),
                    lv_obj_get_height(home_buttons_container));
    lv_obj_align(home_float_container, LV_ALIGN_TOP_LEFT, 0, 0);
}

static void home_layout_update(void) {
    if (!idle_img_center_anchor || !idle_img_center || !idle_img_left || !idle_img_right) return;
    home_float_container_sync_layout();
    lv_obj_align(idle_img_center_anchor, LV_ALIGN_BOTTOM_MID, 0, -LVGL_UI_MARGIN_80);
    lv_obj_align(idle_img_center, LV_ALIGN_BOTTOM_MID, 0, -LVGL_UI_MARGIN_80);
    lv_obj_align_to(idle_img_left, idle_img_center_anchor, LV_ALIGN_OUT_LEFT_MID, -layout_gap, 0);
    lv_obj_align_to(idle_img_right, idle_img_center_anchor, LV_ALIGN_OUT_RIGHT_MID, layout_gap, 0);
    if (lv_obj_is_valid(idle_text_center)) {
        lv_obj_update_layout(idle_text_center);
        lv_obj_align_to(idle_text_center, idle_img_center, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        lv_coord_t baseline_y = lv_obj_get_y(idle_img_center) + lv_obj_get_height(idle_img_center) + 10;
        lv_obj_set_y(idle_text_center, baseline_y);
    }
}

static const app_home_unit_t* home_units_at(size_t index) {
    if (!s_home_units_cur) return NULL;
    return (index < s_home_units_count) ? &s_home_units_cur[index] : NULL;
}

static const app_home_unit_t* home_units_prev(size_t index) {
    if (!s_home_units_cur || s_home_units_count == 0) return NULL;
    if (index >= s_home_units_count) return NULL;
    return &s_home_units_cur[index == 0 ? (s_home_units_count - 1) : (index - 1)];
}

static const app_home_unit_t* home_units_next(size_t index) {
    if (!s_home_units_cur || s_home_units_count == 0) return NULL;
    if (index >= s_home_units_count) return NULL;
    return &s_home_units_cur[(index + 1) % s_home_units_count];
}

static void home_uints_update(void) {
    const app_home_unit_t* unit = home_units_at(home_select);
    if (!unit) {
        floatair_err("home_units_find_by_index %u failed", home_select);
        return;
    }
    const app_home_unit_t* prev = home_units_prev(home_select);
    if (!prev) {
        floatair_err("home_units_prev %u failed", home_select);
        return;
    }
    const app_home_unit_t* next = home_units_next(home_select);
    if (!next) {
        floatair_err("home_units_next %u failed", home_select);
        return;
    }
    if (unit->bigicon && lv_obj_is_valid(idle_img_center)) {
        lv_image_set_src(idle_img_center, unit->bigicon);
    }
    if (prev->smallicon && lv_obj_is_valid(idle_img_left)) {
        lv_image_set_src(idle_img_left, prev->smallicon);
    }
    if (next->smallicon && lv_obj_is_valid(idle_img_right)) {
        lv_image_set_src(idle_img_right, next->smallicon);
    }
    if (lv_obj_is_valid(idlepbl)) {
        lv_image_set_src(idlepbl, FLOATAIR_SYS_IMG("idlemore_left.jpg"));
    }
    if (lv_obj_is_valid(idlepbr)) {
        lv_image_set_src(idlepbr, FLOATAIR_SYS_IMG("idlemore_right.jpg"));
    }
    if (lv_obj_is_valid(idle_text_center)) lv_label_set_text(idle_text_center, app_get_str(unit->icontext));
    floatair_info("home_uints_update: %s", s_home_units_cur[home_select].name);
}

static void home_view_update(void) {
    home_uints_update();
    home_layout_update();
}

static void home_unit_left(void) {
    if (!s_home_units_cur || s_home_units_count == 0) {
        floatair_err("home_units_left %u failed", home_select);
        return;
    }
    if (home_select >= s_home_units_count) {
        floatair_err("home_units_left %u failed", home_select);
        return;
    }
    if (home_select == 0) {
        home_select = (uint8_t)(s_home_units_count - 1);
    } else {
        home_select = (uint8_t)(home_select - 1);
    }
    floatair_info("home_unit_left: %s", s_home_units_cur[home_select].name);
    home_view_update();
}

static void home_unit_right(void) {
    if (!s_home_units_cur || s_home_units_count == 0) {
        floatair_err("home_units_right %u failed", home_select);
        return;
    }
    if (home_select >= s_home_units_count) {
        floatair_err("home_units_right %u failed", home_select);
        return;
    }
    home_select = (home_select + 1) % s_home_units_count;
    floatair_info("home_unit_right: %s", s_home_units_cur[home_select].name);
    home_view_update();
}

/**
 * @brief 在 LVGL 事件回调退出后执行 Home 到目标 App 的切换。
 * @param[in] user_data 未使用。
 * @return 无返回值。
 */
static void home_route_pending_app_async(void* user_data) {
    char target[MSG_STR_MAX_LEN] = {0};

    (void)user_data;
    if (!s_home_route_pending || s_home_pending_app[0] == '\0') {
        return;
    }

    snprintf(target, sizeof(target), "%s", s_home_pending_app);
    s_home_pending_app[0] = '\0';
    s_home_route_pending = false;
    (void)app_router_set_app(target, APP_ROUTER_ENTRY_LOCAL);
}

/**
 * @brief 请求从 Home 路由到目标 App。
 * @param[in] app 目标 App 名称。
 * @return 无返回值。
 */
static void home_route_to_app(const char* app) {
    if (app == NULL || app[0] == '\0') {
        return;
    }
    if (s_home_route_pending) {
        return;
    }

    snprintf(s_home_pending_app, sizeof(s_home_pending_app), "%s", app);
    s_home_route_pending = true;
    if (lv_async_call(home_route_pending_app_async, NULL) != LV_RESULT_OK) {
        s_home_pending_app[0] = '\0';
        s_home_route_pending = false;
        floatair_warn("home route async failed: target=%s", app);
    }
}

static void home_unit_click(void) {
    if (!s_home_units_cur || s_home_units_count == 0) {
        floatair_err("home_units_click %u failed", home_select);
        return;
    }
    if (home_select >= s_home_units_count) {
        floatair_err("home_units_click %u failed", home_select);
        return;
    }
    const app_home_unit_t* unit = home_units_at(home_select);
    if (!unit) {
        floatair_err("home_units_click %u failed", home_select);
        return;
    }
    home_route_to_app(unit->name);
}

static void home_unit_dclick(void) {
    if (!s_home_units_cur || s_home_units_count == 0) {
        floatair_err("home_units_dclick %u failed", home_select);
        return;
    }
    if (home_select >= s_home_units_count) {
        floatair_err("home_units_dclick %u failed", home_select);
        return;
    }
    const app_home_unit_t* unit = home_units_at(home_select);
    if (!unit) {
        floatair_err("home_units_dclick %u failed", home_select);
        return;
    }
    floatair_info("home_unit_dclick: %s", unit->name);
}

static bool home_uints_init(void) {
    s_home_units_cur = g_home_units_arr;
    s_home_units_count = g_home_units_count;
    home_select = 0;
    s_home_units_initialized = true;
    return true;
}

/**
 * @brief 供系统层主动刷新 Home 当前蓝牙连接态展示。
 */
void home_view_reload(void) {
    home_view_update();
}

/**
 * @brief 重置首页当前选中位置，并在视图已创建时立即刷新。
 */
void home_view_reset_selection(void) {
    home_uints_init();
    if (home_buttons_container != NULL && lv_obj_is_valid(home_buttons_container)) {
        home_view_update();
    }
}

static void touch_event_handle(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    static uint32_t s_last_gesture_tick = 0;
    if (code == LV_EVENT_GESTURE_LEFT || code == LV_EVENT_GESTURE_RIGHT) {
        if (lv_tick_elaps(s_last_gesture_tick) < 120) {
            return;
        }
        s_last_gesture_tick = lv_tick_get();
    }
    switch (code) {
        case LV_EVENT_GESTURE_LEFT:
            home_unit_left();
            break;
        case LV_EVENT_GESTURE_RIGHT:
            home_unit_right();
            break;
        case LV_EVENT_CLICKED:
        case LV_EVENT_LONG_PRESSED:
            home_unit_click();
            break;
        case LV_EVENT_DCLICKED:
            home_unit_dclick();
            break;
        default:
            break;
    }
}

static void home_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    floatair_assert(root != NULL, "root NULL");
    const lv_font_t* system_font = get_system_font();
    int font_height = (int)get_font_height(system_font);

    if (!s_home_units_initialized) {
        home_uints_init();
    }

    home_buttons_container = lv_obj_create(root);
    lv_obj_remove_style_all(home_buttons_container);
    lv_obj_set_size(home_buttons_container, LV_PCT(100), LV_PCT(100));
    lv_obj_align(home_buttons_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(home_buttons_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(home_buttons_container, 0, LV_PART_MAIN);
    lv_obj_null_on_delete(&home_buttons_container);

    idle_img_center_anchor = lv_obj_create(home_buttons_container);
    floatair_assert(idle_img_center_anchor != NULL, "idle_img_center_anchor NULL");
    lv_obj_remove_style_all(idle_img_center_anchor);
    lv_obj_set_size(idle_img_center_anchor, LVGL_UI_ICONW_80, LVGL_UI_ICONH_80);
    lv_obj_align(idle_img_center_anchor, LV_ALIGN_BOTTOM_MID, 0, -LVGL_UI_MARGIN_80);
    lv_obj_clear_flag(idle_img_center_anchor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(idle_img_center_anchor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(idle_img_center_anchor, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_null_on_delete(&idle_img_center_anchor);

    home_float_container = home_float_container_create(home_buttons_container);
    floatair_assert(home_float_container != NULL, "home_float_container NULL");
    lv_obj_null_on_delete(&home_float_container);

    idle_img_center = lv_image_create(home_float_container);
    floatair_assert(idle_img_center != NULL, "idle_img_center NULL");
    lv_obj_align(idle_img_center, LV_ALIGN_BOTTOM_MID, 0, -LVGL_UI_MARGIN_80);
    lv_obj_set_size(idle_img_center, LVGL_UI_ICONW_80, LVGL_UI_ICONH_80);
    lv_obj_null_on_delete(&idle_img_center);

    idle_text_center = lv_label_create(home_float_container);
    floatair_assert(idle_text_center != NULL, "idle_text_center NULL");
    lv_obj_set_size(idle_text_center, LV_SIZE_CONTENT, font_height);
    lv_obj_align_to(idle_text_center, idle_img_center, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    obj_set_text_font(idle_text_center, system_font);
    lv_obj_set_style_text_color(idle_text_center, lv_color_white(), 0);
    lv_obj_set_style_text_align(idle_text_center, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(idle_text_center, LV_LABEL_LONG_CLIP);
    lv_obj_null_on_delete(&idle_text_center);

    idle_img_left = lv_image_create(home_buttons_container);
    floatair_assert(idle_img_left != NULL, "idle_img_left NULL");
    lv_obj_set_size(idle_img_left, LVGL_UI_ICONW_80, LVGL_UI_ICONH_80);
    lv_obj_align_to(idle_img_left, idle_img_center_anchor, LV_ALIGN_OUT_LEFT_TOP, -LVGL_UI_MARGIN_50, 0);
    lv_obj_null_on_delete(&idle_img_left);

    idle_img_right = lv_image_create(home_buttons_container);
    floatair_assert(idle_img_right != NULL, "idle_img_right NULL");
    lv_obj_set_size(idle_img_right, LVGL_UI_ICONW_80, LVGL_UI_ICONH_80);
    lv_obj_align_to(idle_img_right, idle_img_center_anchor, LV_ALIGN_OUT_RIGHT_TOP, (LVGL_UI_MARGIN_50), 0);
    lv_obj_null_on_delete(&idle_img_right);

    idlepbl = lv_image_create(home_buttons_container);
    floatair_assert(idlepbl != NULL, "idlepbl NULL");
    lv_obj_set_size(idlepbl, PAGE_BASE_ICON_H, PAGE_BASE_ICON_V);
    lv_obj_align_to(idlepbl, idle_img_left, LV_ALIGN_OUT_LEFT_TOP, -LVGL_UI_MARGIN_20, LVGL_UI_ICONH_48 / 2);
    lv_obj_null_on_delete(&idlepbl);

    idlepbr = lv_image_create(home_buttons_container);
    floatair_assert(idlepbr != NULL, "idlepbr NULL");
    lv_obj_set_size(idlepbr, PAGE_BASE_ICON_H, PAGE_BASE_ICON_V);
    lv_obj_align_to(idlepbr, idle_img_right, LV_ALIGN_OUT_RIGHT_TOP, LVGL_UI_MARGIN_20, LVGL_UI_ICONH_48 / 2);
    lv_obj_null_on_delete(&idlepbr);

    lv_obj_add_flag(idlepbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idlepbr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(idle_img_center_anchor, idle_img_center_w, idle_img_center_h);
    lv_obj_set_size(idle_img_center, idle_img_center_w, idle_img_center_h);
    lv_obj_set_size(idle_img_left, idle_img_left_w, idle_img_left_h);
    lv_obj_set_size(idle_img_right, idle_img_right_w, idle_img_right_h);
    home_layout_update();
}

static void home_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root is NULL");
    system_status_bar_set_mode(true);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
    home_view_update();
}

static app_page_t s_home_page = {
    .name = APP_NAME_HOME,
    .on_create = home_page_create,
    .on_appear = home_page_appear,
    .on_disappear = NULL,
    .on_destroy = home_float_container_delete,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* home_page_get(void) {
    return &s_home_page;
}
