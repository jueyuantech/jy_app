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
#include "home_guide.h"
#include "guide_runtime.h"

#include "app_def.h"
#include "common/app_framework/app_layers.h"
#include "common/app_framework/app_manager.h"
#include "common/widgets/img.h"
#include "common/widgets/container.h"
#include "floatair_fs.h"
#include "message.h"
#include "system/system.h"
#include "system/system_def.h"
#include "sys_adapter.h"
#include "ui_res.h"

#include <stdlib.h>
#include <string.h>

#define HOME_ICON_BOTTOM_OFFSET 40 ///< Home 图标组距离底部的布局偏移。

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
static size_t s_home_units_count = 0;
static const app_home_unit_t* s_home_units_cur = NULL;
static app_home_unit_t* s_home_units_filtered = NULL;
#if PRODUCT_HOME_GUIDE_APPS_COUNT > 0
static app_home_unit_t s_home_guide_units[PRODUCT_HOME_GUIDE_APPS_COUNT]; ///< 独立教学菜单，复用真实首页图标与文案。
static bool s_home_using_guide_units = false; ///< 当前菜单是否为教学专用列表。
#endif

#if PRODUCT_HOME_VIEW == 2
#define HOME_SWIPE_ICON_COUNT 4u ///< 同步移动的左、中、右及补位图标数。
#define HOME_SWIPE_DURATION 300u ///< 一次完整切换的时长，单位毫秒。
#define HOME_SWIPE_PROGRESS 1000 ///< 动画插值的完整进度。
#define HOME_SWIPE_TEXT_OFFSET 12 ///< 名称淡入淡出时的最大横向位移。

/** @brief 一个滑动图标的大小图素材与基础位置。 */
typedef struct {
    container_t* group;       ///< 同一深度下叠放大小图的容器。
    img_t* small;             ///< 未选中素材，过渡时作为底图。
    img_t* large;             ///< 选中素材，以透明度交接亮度。
    int32_t x;               ///< 起始横向中心坐标。
    int32_t y;               ///< 纵向中心坐标。
    int32_t small_w;         ///< 未选中素材宽度。
    int32_t small_h;         ///< 未选中素材高度。
} home_swipe_icon_t;

/** @brief 一次 Home 切换的统一动画状态。 */
typedef struct {
    home_swipe_icon_t icons[HOME_SWIPE_ICON_COUNT]; ///< 四个参与切换的图标。
    int32_t shift;            ///< 本次移动的有符号距离。
    int32_t direction;        ///< 横向切换方向。
    int32_t text_y;           ///< 名称固定的纵向基线。
    int32_t center_x;         ///< 名称与选中项的横向中心。
    const char* next_text;   ///< 切换后名称，来自当前语言资源。
    uint8_t entering;        ///< 从侧边进入中心的图标下标。
    bool text_changed;       ///< 名称是否已在全透明时切换。
    bool active;             ///< 是否正在播放动画。
} home_swipe_state_t;

static home_swipe_state_t s_home_swipe = {0}; ///< 当前动画及其页面内资源。

/**
 * @brief 同步更新图标的位置、大小、亮度、双眼视差及名称过渡。
 * @param[in] var 当前 Home 动画状态。
 * @param[in] value 已缓动的动画进度，范围 0 到 HOME_SWIPE_PROGRESS。
 * @return 无返回值。
 */
static void home_swipe_anim_step(void* var, int32_t value) {
    home_swipe_state_t* state = var;
    if (!state->active) {
        return;
    }
    for (size_t i = 0; i < HOME_SWIPE_ICON_COUNT; ++i) {
        home_swipe_icon_t* icon = &state->icons[i];
        int32_t focus = i == 1 ? HOME_SWIPE_PROGRESS - value :
                        i == state->entering ? value : 0;
        int32_t width = icon->small_w + (idle_img_center_w - icon->small_w) * focus / HOME_SWIPE_PROGRESS;
        lv_obj_t* group = ui_widget_get_obj(UI_WIDGET(icon->group));
        ui_widget_set_position(UI_WIDGET(icon->group),
            icon->x + state->shift * value / HOME_SWIPE_PROGRESS - idle_img_center_w / 2,
            icon->y - idle_img_center_h / 2);
        img_set_zoom(icon->small, (uint16_t)(LV_SCALE_NONE * width / icon->small_w));
        img_set_zoom(icon->large, (uint16_t)(LV_SCALE_NONE * width / idle_img_center_w));
        img_set_opacity(icon->large, (uint8_t)(LV_OPA_COVER * focus / HOME_SWIPE_PROGRESS));
        if (home_enable_app_float && lv_obj_get_parent(group) == app_layers_get_app_float()) {
            app_layers_set_float_depth(group,
                (FLOATAIR_APP_FLOAT_DISTANCE * focus + HOME_SWIPE_PROGRESS / 2) / HOME_SWIPE_PROGRESS);
        }
    }
    if (value >= HOME_SWIPE_PROGRESS / 2 && !state->text_changed) {
        lv_label_set_text(idle_text_center, state->next_text);
        state->text_changed = true;
    }
    int32_t phase = state->text_changed ? value - HOME_SWIPE_PROGRESS : value;
    int32_t opacity = LV_OPA_COVER * abs(value - HOME_SWIPE_PROGRESS / 2) / (HOME_SWIPE_PROGRESS / 2);
    lv_obj_set_style_text_opa(idle_text_center, (lv_opa_t)opacity, 0);
    lv_obj_update_layout(idle_text_center);
    lv_obj_set_pos(idle_text_center,
        state->center_x - lv_obj_get_width(idle_text_center) / 2 +
        state->direction * HOME_SWIPE_TEXT_OFFSET * phase / (HOME_SWIPE_PROGRESS / 2), state->text_y);
}

/**
 * @brief 取消统一动画并还原静态控件，供刷新、离页和销毁复用。
 * @return 无返回值。
 */
static void home_swipe_cancel(void) {
    lv_anim_delete(&s_home_swipe, home_swipe_anim_step);
    s_home_swipe.active = false;
    for (size_t i = 0; i < HOME_SWIPE_ICON_COUNT; ++i) {
        ui_widget_set_visible(UI_WIDGET(s_home_swipe.icons[i].group), false);
    }
    lv_obj_t* originals[] = {idle_img_left, idle_img_center, idle_img_right};
    for (size_t i = 0; i < 3; ++i) {
        if (originals[i] != NULL && lv_obj_is_valid(originals[i])) {
            lv_obj_remove_flag(originals[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (idle_text_center != NULL && lv_obj_is_valid(idle_text_center)) {
        lv_obj_set_style_text_opa(idle_text_center, LV_OPA_COVER, 0);
    }
}
#endif

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
#if PRODUCT_HOME_VIEW == 2
    lv_obj_set_size(idle_img_center, idle_img_center_w, idle_img_center_h);
    lv_obj_set_size(idle_img_left, idle_img_left_w, idle_img_left_h);
    lv_obj_set_size(idle_img_right, idle_img_right_w, idle_img_right_h);
#endif
    lv_obj_align(idle_img_center_anchor, LV_ALIGN_BOTTOM_MID, 0, -HOME_ICON_BOTTOM_OFFSET);
    lv_obj_align(idle_img_center, LV_ALIGN_BOTTOM_MID, 0, -HOME_ICON_BOTTOM_OFFSET);
    lv_obj_align_to(idle_img_left, idle_img_center_anchor, LV_ALIGN_OUT_LEFT_MID, -layout_gap, 0);
    lv_obj_align_to(idle_img_right, idle_img_center_anchor, LV_ALIGN_OUT_RIGHT_MID, layout_gap, 0);
    if (lv_obj_is_valid(idle_text_center)) {
        lv_obj_update_layout(idle_text_center);
        lv_obj_align_to(idle_text_center, idle_img_center, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        lv_coord_t baseline_y = lv_obj_get_y(idle_img_center) + lv_obj_get_height(idle_img_center) + 10;
        lv_obj_set_y(idle_text_center, baseline_y);
    }
}

static void home_units_release_filtered(void) {
    if (s_home_units_filtered != NULL) {
        free(s_home_units_filtered);
        s_home_units_filtered = NULL;
    }
}

static const app_home_unit_t* home_units_at(size_t index) {
    if (!s_home_units_cur) return NULL;
    return (index < s_home_units_count) ? &s_home_units_cur[index] : NULL;
}

static bool home_select_app_by_name(const char* app_name) {
    if (app_name == NULL || s_home_units_cur == NULL || s_home_units_count == 0) {
        return false;
    }
    for (size_t i = 0; i < s_home_units_count; ++i) {
        if (s_home_units_cur[i].name != NULL && strcmp(s_home_units_cur[i].name, app_name) == 0) {
            home_select = (uint8_t)i;
            return true;
        }
    }
    return false;
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
        lv_image_set_src(idlepbl, UI_RES_IMAGE_IDLEMORE_LEFT);
    }
    if (lv_obj_is_valid(idlepbr)) {
        lv_image_set_src(idlepbr, UI_RES_IMAGE_IDLEMORE_RIGHT);
    }
    if (lv_obj_is_valid(idle_text_center)) lv_label_set_text(idle_text_center, app_get_str(unit->icontext));
    home_guide_layout_update();
    floatair_info("home_uints_update: %s", s_home_units_cur[home_select].name);
}

static void home_view_update(void) {
#if PRODUCT_HOME_VIEW == 2
    if (s_home_swipe.active) {
        home_swipe_cancel();
    }
#endif
    home_uints_update();
    home_layout_update();
}

#if PRODUCT_HOME_VIEW == 2
/**
 * @brief 切换到位后恢复原控件，保留中心图标与文字的原有浮层。
 * @param[in] anim 已完成的统一动画。
 * @return 无返回值。
 */
static void home_swipe_anim_ready(lv_anim_t* anim) {
    (void)anim;
    home_swipe_cancel();
    home_view_update();
}

/**
 * @brief 从旧选中项连续滑向新选中项，并同步交接选中态和双眼深度。
 * @param[in] previous 切换前的菜单下标。
 * @param[in] direction 图标移动方向；负数向左，正数向右。
 * @return true 表示动画已启动；false 表示调用方应立即刷新菜单。
 */
static bool home_swipe_start(uint8_t previous, int32_t direction) {
    lv_obj_t* originals[] = {idle_img_left, idle_img_center, idle_img_right};
    const app_home_unit_t* units[HOME_SWIPE_ICON_COUNT] = {
        home_units_prev(previous), home_units_at(previous), home_units_next(previous),
        direction < 0 ? home_units_next(home_select) : home_units_prev(home_select)
    };
    const app_home_unit_t* selected = home_units_at(home_select);
    if (s_home_units_count < 2 || selected == NULL) {
        return false;
    }
    home_layout_update();
    lv_obj_update_layout(home_buttons_container);
    s_home_swipe.entering = direction < 0 ? 2 : 0;
    for (size_t i = 0; i < HOME_SWIPE_ICON_COUNT; ++i) {
        home_swipe_icon_t* icon = &s_home_swipe.icons[i];
        if (units[i] == NULL || units[i]->smallicon == NULL || units[i]->bigicon == NULL ||
            !ui_widget_is_valid(UI_WIDGET(icon->group))) {
            return false;
        }
        if (i < 3) {
            icon->x = lv_obj_get_x(originals[i]) + lv_obj_get_width(originals[i]) / 2;
            icon->y = lv_obj_get_y(originals[i]) + lv_obj_get_height(originals[i]) / 2;
        }
        icon->small_w = i == 2 || (i == 3 && direction < 0) || (i == 1 && direction > 0)
                            ? idle_img_right_w : idle_img_left_w;
        icon->small_h = i == 2 || (i == 3 && direction < 0) || (i == 1 && direction > 0)
                            ? idle_img_right_h : idle_img_left_h;
        ui_widget_set_size(UI_WIDGET(icon->small), icon->small_w, icon->small_h);
        if (!img_set_decoded_l8_src(icon->small, units[i]->smallicon)) {
            return false;
        }
        bool needs_large = i == 1 || i == s_home_swipe.entering;
        if (needs_large && !img_set_decoded_l8_src(icon->large, units[i]->bigicon)) {
            return false;
        }
        lv_obj_center(ui_widget_get_obj(UI_WIDGET(icon->small)));
        lv_obj_center(ui_widget_get_obj(UI_WIDGET(icon->large)));
        ui_widget_set_visible(UI_WIDGET(icon->large), needs_large);
    }
    int32_t step = direction < 0 ? s_home_swipe.icons[2].x - s_home_swipe.icons[1].x :
                                  s_home_swipe.icons[1].x - s_home_swipe.icons[0].x;
    if (step <= 0) {
        return false;
    }
    home_swipe_icon_t* extra = &s_home_swipe.icons[3];
    home_swipe_icon_t* edge = &s_home_swipe.icons[direction < 0 ? 2 : 0];
    extra->x = edge->x - direction * step;
    extra->y = edge->y;
    s_home_swipe.shift = direction * step;
    s_home_swipe.direction = direction;
    s_home_swipe.center_x = s_home_swipe.icons[1].x;
    s_home_swipe.text_y = lv_obj_get_y(idle_text_center);
    s_home_swipe.next_text = app_get_str(selected->icontext);
    s_home_swipe.text_changed = false;
    s_home_swipe.active = true;
    home_swipe_anim_step(&s_home_swipe, 0);
    for (size_t i = 0; i < HOME_SWIPE_ICON_COUNT; ++i) {
        ui_widget_set_visible(UI_WIDGET(s_home_swipe.icons[i].group), true);
    }
    for (size_t i = 0; i < 3; ++i) {
        lv_obj_add_flag(originals[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_home_swipe);
    lv_anim_set_exec_cb(&anim, home_swipe_anim_step);
    lv_anim_set_values(&anim, 0, HOME_SWIPE_PROGRESS);
    lv_anim_set_duration(&anim, HOME_SWIPE_DURATION);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim, home_swipe_anim_ready);
    if (lv_anim_start(&anim) == NULL) {
        home_swipe_cancel();
        return false;
    }
    return true;
}
#endif

/**
 * @brief 处理屏幕右滑，选中当前 Home 菜单左侧的应用。
 * @return 无返回值。
 */
static void home_select_left_app(void) {
#if PRODUCT_HOME_VIEW == 2
    uint8_t previous = home_select;
    if (s_home_swipe.active) {
        return;
    }
#endif
    if (!s_home_units_cur || s_home_units_count == 0) {
        floatair_err("home select left failed: %u", home_select);
        return;
    }
    if (home_select >= s_home_units_count) {
        floatair_err("home select left failed: %u", home_select);
        return;
    }
    if (home_select == 0) {
        home_select = (uint8_t)(s_home_units_count - 1);
    } else {
        home_select = (uint8_t)(home_select - 1);
    }
    floatair_info("home select left app: %s", s_home_units_cur[home_select].name);
#if PRODUCT_HOME_VIEW == 2
    if (home_swipe_start(previous, 1)) {
        return;
    }
#endif
    home_view_update();
}

/**
 * @brief 处理屏幕左滑，选中当前 Home 菜单右侧的应用。
 * @return 无返回值。
 */
static void home_select_right_app(void) {
#if PRODUCT_HOME_VIEW == 2
    uint8_t previous = home_select;
    if (s_home_swipe.active) {
        return;
    }
#endif
    if (!s_home_units_cur || s_home_units_count == 0) {
        floatair_err("home select right failed: %u", home_select);
        return;
    }
    if (home_select >= s_home_units_count) {
        floatair_err("home select right failed: %u", home_select);
        return;
    }
    home_select = (home_select + 1) % s_home_units_count;
    floatair_info("home select right app: %s", s_home_units_cur[home_select].name);
#if PRODUCT_HOME_VIEW == 2
    if (home_swipe_start(previous, -1)) {
        return;
    }
#endif
    home_view_update();
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
    if (!home_unit_activate(unit)) {
        floatair_warn("home unit activate failed: target=%s", unit->name);
    }
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

/**
 * @brief 处理 Home 页面长按，上报触摸事件交由手机端决定目标功能。
 * @return 无返回值。
 */
static void home_unit_long_press(void) {
    if (!system_report_touch_event(LV_EVENT_LONG_PRESSED)) {
        floatair_warn("home long press report touch event failed");
    }
}

static bool home_uints_init(void) {
    home_units_release_filtered();
    s_home_units_cur = g_home_units_arr;
    s_home_units_count = g_home_units_count;
#if PRODUCT_HOME_GUIDE_APPS_COUNT > 0
    s_home_using_guide_units = false;
    if (guide_runtime_get_state() != GUIDE_RUNTIME_STATE_IDLE) {
        const char* const guide_apps[] = PRODUCT_HOME_GUIDE_APPS;

        for (size_t i = 0; i < PRODUCT_HOME_GUIDE_APPS_COUNT; ++i) {
            size_t j;
            for (j = 0; j < g_home_units_count; ++j) {
                if (strcmp(g_home_units_arr[j].name, guide_apps[i]) == 0) {
                    s_home_guide_units[i] = g_home_units_arr[j];
                    break;
                }
            }
            if (j == g_home_units_count) {
                floatair_err("guide app missing from home configuration: %s", guide_apps[i]);
                s_home_units_cur = NULL;
                s_home_units_count = 0;
                s_home_units_initialized = false;
                home_select = 0;
                return false;
            }
        }
        s_home_units_cur = s_home_guide_units;
        s_home_units_count = PRODUCT_HOME_GUIDE_APPS_COUNT;
        s_home_using_guide_units = true;
        home_select_app_by_name(PRODUCT_HOME_GUIDE_STEP2_APP_NAME);
        s_home_units_initialized = true;
        return true;
    }
#endif
    if (home_units_build_from_config(&s_home_units_filtered, &s_home_units_count)) {
        s_home_units_cur = s_home_units_filtered;
    } else {
        s_home_units_cur = g_home_units_arr;
        s_home_units_count = g_home_units_count;
    }
    if (s_home_units_count == 0) {
        s_home_units_cur = NULL;
        home_select = 0;
        s_home_units_initialized = false;
        floatair_err("home units init failed: no available units");
        return false;
    }
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

const char* home_view_get_selected_app_name(void) {
    const app_home_unit_t* unit = NULL;

    if (!s_home_units_initialized && !home_uints_init()) {
        return NULL;
    }
    unit = home_units_at(home_select);
    return unit != NULL ? unit->name : NULL;
}

bool home_view_select_app_by_name(const char* app_name) {
    if (!s_home_units_initialized && !home_uints_init()) {
        return false;
    }
    if (!home_select_app_by_name(app_name)) {
        return false;
    }
    if (home_buttons_container != NULL && lv_obj_is_valid(home_buttons_container)) {
        home_view_update();
    }
    return true;
}

/**
 * @brief 重置首页当前选中位置，并在视图已创建时立即刷新。
 */
void home_view_reset_selection(void) {
    home_uints_init();
    if (home_buttons_container != NULL && lv_obj_is_valid(home_buttons_container)) {
        if (home_guide_is_step1()) {
            home_guide_apply_step1_selection(home_select_app_by_name, true, home_view_update);
        } else {
            home_view_update();
        }
    }
}

static void touch_event_handle(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    static uint32_t s_last_gesture_tick = 0;
    const home_guide_ops_t guide_ops = {
        .refresh = home_view_update,
        .reset_selection = home_view_reset_selection,
        .screen_swipe_left = home_select_right_app,
        .screen_swipe_right = home_select_left_app,
    };

    if (code == LV_EVENT_GESTURE_LEFT || code == LV_EVENT_GESTURE_RIGHT) {
        if (lv_tick_elaps(s_last_gesture_tick) < 120) {
            return;
        }
        s_last_gesture_tick = lv_tick_get();
        if (!system_report_touch_event(code)) {
            floatair_warn("home swipe report touch event failed: %d", code);
        }
    }
    if (home_guide_handle_touch(code, &guide_ops)) {
        return;
    }

    switch (code) {
        case LV_EVENT_GESTURE_LEFT:
            home_select_right_app();
            break;
        case LV_EVENT_GESTURE_RIGHT:
            home_select_left_app();
            break;
        case LV_EVENT_CLICKED:
            home_unit_click();
            break;
        case LV_EVENT_LONG_PRESSED:
            home_unit_long_press();
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

    if (!s_home_units_initialized
#if PRODUCT_HOME_GUIDE_APPS_COUNT > 0
        || s_home_using_guide_units != (guide_runtime_get_state() != GUIDE_RUNTIME_STATE_IDLE)
#endif
    ) {
        home_uints_init();
    }
    home_guide_apply_step1_selection(home_select_app_by_name, false, NULL);

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
    lv_obj_align(idle_img_center_anchor, LV_ALIGN_BOTTOM_MID, 0, -HOME_ICON_BOTTOM_OFFSET);
    lv_obj_clear_flag(idle_img_center_anchor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(idle_img_center_anchor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(idle_img_center_anchor, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_null_on_delete(&idle_img_center_anchor);

    home_float_container = home_float_container_create(home_buttons_container);
    floatair_assert(home_float_container != NULL, "home_float_container NULL");
    lv_obj_null_on_delete(&home_float_container);

    idle_img_center = lv_image_create(home_float_container);
    floatair_assert(idle_img_center != NULL, "idle_img_center NULL");
    lv_obj_align(idle_img_center, LV_ALIGN_BOTTOM_MID, 0, -HOME_ICON_BOTTOM_OFFSET);
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

    home_guide_create_controls(home_buttons_container,
                               home_float_container,
                               system_font,
                               font_height);

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

#if PRODUCT_HOME_VIEW == 2
    lv_obj_t* swipe_parent = home_enable_app_float ? app_layers_get_app_float() : home_buttons_container;
    if (swipe_parent == NULL) swipe_parent = home_buttons_container;
    for (size_t i = 0; i < HOME_SWIPE_ICON_COUNT; ++i) {
        home_swipe_icon_t* icon = &s_home_swipe.icons[i];
        container_cfg_t group_cfg = container_default_cfg();
        group_cfg.w = idle_img_center_w;
        group_cfg.h = idle_img_center_h;
        group_cfg.opa = LV_OPA_TRANSP;
        group_cfg.pad_hor = 0;
        group_cfg.pad_ver = 0;
        group_cfg.border_width = 0;
        icon->group = container_create(swipe_parent, &group_cfg);
        floatair_assert(icon->group != NULL, "home swipe group create failed");
        lv_obj_t* group = ui_widget_get_obj(UI_WIDGET(icon->group));
        lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        img_cfg_t img_cfg = img_default_cfg();
        icon->small = img_create(group, &img_cfg);
        img_cfg.w = idle_img_center_w;
        img_cfg.h = idle_img_center_h;
        icon->large = img_create(group, &img_cfg);
        floatair_assert(icon->small != NULL && icon->large != NULL, "home swipe image create failed");
        if (swipe_parent == app_layers_get_app_float()) {
            floatair_assert(app_layers_set_float_depth(group, 0), "home swipe depth create failed");
        }
        ui_widget_set_visible(UI_WIDGET(icon->group), false);
    }
#endif

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
    const home_guide_ops_t guide_ops = {
        .refresh = home_view_update,
        .reset_selection = home_view_reset_selection,
        .screen_swipe_left = home_select_right_app,
        .screen_swipe_right = home_select_left_app,
    };

    floatair_assert(root != NULL, "root is NULL");
#if PRODUCT_HOME_GUIDE_APPS_COUNT > 0
    /* 已缓存的 Home 再次出现时，也要隔离教学与真实菜单。 */
    if (s_home_using_guide_units != (guide_runtime_get_state() != GUIDE_RUNTIME_STATE_IDLE)) {
        home_uints_init();
    }
#endif
    system_status_bar_set_mode_at(
        true,
        PRODUCT_HOME_STATUS_BAR_AT_TOP ? STATUS_BAR_POS_TOP : STATUS_BAR_POS_BOTTOM);
    home_guide_set_ops(&guide_ops);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
    home_guide_register_events(root);
    home_view_update();
    home_guide_on_appear();
}

#if PRODUCT_HOME_VIEW == 2
/**
 * @brief 离开首页时取消未完成动画并恢复当前菜单，支持立即返回。
 * @return 无返回值。
 */
static void home_page_disappear(void) {
    home_swipe_cancel();
    home_view_update();
}
#endif

/**
 * @brief 销毁 Home 页面在页面根节点之外持有的浮层控件。
 * @return 无返回值。
 */
static void home_page_destroy(void) {
#if PRODUCT_HOME_VIEW == 2
    home_swipe_cancel();
    for (size_t i = 0; i < HOME_SWIPE_ICON_COUNT; ++i) {
        ui_widget_destroy(UI_WIDGET(s_home_swipe.icons[i].group));
    }
    memset(&s_home_swipe, 0, sizeof(s_home_swipe));
#endif
    home_guide_destroy_controls();
    home_float_container_delete();
}

static app_page_t s_home_page = {
    .name = APP_NAME_HOME,
    .on_create = home_page_create,
    .on_appear = home_page_appear,
#if PRODUCT_HOME_VIEW == 2
    .on_disappear = home_page_disappear,
#else
    .on_disappear = NULL,
#endif
    .on_destroy = home_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* home_page_get(void) {
    return &s_home_page;
}
