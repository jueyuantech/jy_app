#include "container.h"

#include <string.h>

/**
 * @brief 容器组件内部数据结构。
 */
struct container_t {
    ui_widget_t base;                         ///< 统一组件基类。
    container_t* parent;                      ///< 父容器句柄；父对象不是容器时为空。
    container_height_policy_t height_policy;  ///< 高度自适应策略。
};

/**
 * @brief 从 LVGL 对象取回容器句柄。
 *
 * @param obj 待检查的 LVGL 对象。
 * @return 若对象由 container 封装创建则返回容器句柄，否则返回 `NULL`。
 */
static container_t* container_from_obj(lv_obj_t* obj) {
    ui_widget_t* widget = NULL;

    if (!obj || !lv_obj_is_valid(obj)) {
        return NULL;
    }

    widget = (ui_widget_t*)lv_obj_get_user_data(obj);
    if (!widget || widget->obj != obj || widget->type != UI_WIDGET_TYPE_CONTAINER) {
        return NULL;
    }

    return (container_t*)widget;
}

/**
 * @brief 获取对象样式中扣除上下内边距后的最大内容高度。
 *
 * 当父容器本身也是按内容自适应时，当前 content height 可能尚未被子项撑开，
 * 此时需要用已经解析出的 max-height 作为可用高度来源。
 *
 * @param obj 待计算的 LVGL 对象。
 * @return 成功返回最大内容高度，失败返回 0。
 */
static lv_coord_t container_get_max_content_height(lv_obj_t* obj) {
    lv_coord_t max_height = 0;
    lv_coord_t pad_top = 0;
    lv_coord_t pad_bottom = 0;

    if (!obj || !lv_obj_is_valid(obj)) {
        return 0;
    }

    max_height = lv_obj_get_style_max_height(obj, LV_PART_MAIN);
    if (max_height <= 0 || max_height == LV_COORD_MAX || LV_COORD_IS_PCT(max_height)) {
        return 0;
    }

    pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
    pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
    max_height -= pad_top + pad_bottom;

    return max_height > 0 ? max_height : 0;
}

/**
 * @brief 应用单个容器的高度自适应策略。
 *
 * @param container 目标容器句柄。
 * @return 无返回值。
 */
static void container_apply_height_policy(container_t* container) {
    lv_obj_t* obj = NULL;
    lv_obj_t* parent = NULL;
    container_t* parent_container = NULL;
    lv_coord_t max_height = 0;
    lv_coord_t parent_max_content_height = 0;

    if (!container_is_valid(container)) {
        return;
    }
    if (container->height_policy != CONTAINER_HEIGHT_POLICY_CONTENT_MAX_PARENT) {
        return;
    }

    obj = container->base.obj;
    parent = lv_obj_get_parent(obj);
    if (!parent || !lv_obj_is_valid(parent)) {
        return;
    }

    lv_obj_update_layout(parent);
    parent_container = container_from_obj(parent);
    parent_max_content_height = container_get_max_content_height(parent);
    if (parent_container &&
        parent_container->height_policy == CONTAINER_HEIGHT_POLICY_CONTENT_MAX_PARENT &&
        parent_max_content_height > 0) {
        max_height = parent_max_content_height;
    } else {
        max_height = lv_obj_get_content_height(parent);
    }
    if (max_height <= 0) {
        max_height = parent_max_content_height;
    }
    if (max_height <= 0) {
        return;
    }

    lv_obj_set_style_max_height(obj, max_height, LV_PART_MAIN);
    lv_obj_update_layout(obj);
}

/**
 * @brief 判断容器是否可滚动。
 *
 * @param container 目标容器句柄。
 * @return `true` 表示可滚动，`false` 表示不可滚动。
 */
static bool container_is_scrollable(container_t* container) {
    if (!container_is_valid(container)) {
        return false;
    }

    return lv_obj_has_flag(container->base.obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 从自身到父容器逐级刷新布局。
 *
 * 可滚动容器是布局传播边界：其内部内容变化不应直接撑开外层父容器。
 * 只有滚动容器自身视口高度发生变化时，才继续通知父容器。
 *
 * @param container 链路起点容器句柄。
 * @return 无返回值。
 */
static void container_refresh_layout_up(container_t* container) {
    lv_obj_t* obj = NULL;
    lv_coord_t old_height = 0;
    lv_coord_t new_height = 0;
    bool should_notify_parent = true;

    if (!container_is_valid(container)) {
        return;
    }

    obj = container->base.obj;
    old_height = lv_obj_get_height(obj);

    if (container->height_policy == CONTAINER_HEIGHT_POLICY_CONTENT_MAX_PARENT) {
        lv_obj_set_height(obj, LV_SIZE_CONTENT);
    }
    container_apply_height_policy(container);
    lv_obj_update_layout(obj);
    new_height = lv_obj_get_height(obj);

    if (container_is_scrollable(container) && old_height == new_height) {
        should_notify_parent = false;
    }

    if (container->parent && should_notify_parent) {
        container_refresh_layout_up(container->parent);
    }
}

/**
 * @brief 判断子项当前是否参与父容器布局。
 *
 * 被隐藏、浮动或显式忽略布局的子项，不应参与剩余空间计算。
 *
 * @param child 待检查的子项对象。
 * @return `true` 表示参与布局，`false` 表示不参与。
 */
static bool container_child_in_layout(lv_obj_t* child) {
    if (!child || !lv_obj_is_valid(child)) {
        return false;
    }

    return !lv_obj_has_flag_any(child,
                                LV_OBJ_FLAG_IGNORE_LAYOUT |
                                LV_OBJ_FLAG_HIDDEN |
                                LV_OBJ_FLAG_FLOATING);
}

/**
 * @brief 将封装层交叉轴对齐枚举转换为 LVGL 对齐枚举。
 *
 * 交叉轴仅支持 `START/CENTER/END`，其余值统一回退到 `START`。
 *
 * @param align 封装层交叉轴对齐方式。
 * @return 返回 LVGL Flex 对齐枚举值。
 */
static lv_flex_align_t container_to_lv_cross_align(container_align_t align) {
    switch (align) {
        case CONTAINER_ALIGN_CENTER:
            return LV_FLEX_ALIGN_CENTER;
        case CONTAINER_ALIGN_END:
            return LV_FLEX_ALIGN_END;
        case CONTAINER_ALIGN_START:
        case CONTAINER_ALIGN_SPACE_BETWEEN:
        case CONTAINER_ALIGN_SPACE_AROUND:
        case CONTAINER_ALIGN_SPACE_EVENLY:
        default:
            return LV_FLEX_ALIGN_START;
    }
}

/**
 * @brief 将封装层对齐枚举转换为 LVGL 对齐枚举。
 *
 * @param align 封装层对齐方式。
 * @return 返回 LVGL Flex 对齐枚举值。
 */
static lv_flex_align_t container_to_lv_align(container_align_t align) {
    switch (align) {
        case CONTAINER_ALIGN_CENTER:
            return LV_FLEX_ALIGN_CENTER;
        case CONTAINER_ALIGN_END:
            return LV_FLEX_ALIGN_END;
        case CONTAINER_ALIGN_SPACE_BETWEEN:
            return LV_FLEX_ALIGN_SPACE_BETWEEN;
        case CONTAINER_ALIGN_SPACE_AROUND:
            return LV_FLEX_ALIGN_SPACE_AROUND;
        case CONTAINER_ALIGN_SPACE_EVENLY:
            return LV_FLEX_ALIGN_SPACE_EVENLY;
        case CONTAINER_ALIGN_START:
        default:
            return LV_FLEX_ALIGN_START;
    }
}

/**
 * @brief 判断容器句柄及底层对象是否有效。
 *
 * @param container 目标容器句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool container_is_valid(container_t* container) {
    return container && container->base.obj && lv_obj_is_valid(container->base.obj);
}

/**
 * @brief LVGL 删除事件回调。
 *
 * 容器被删除时负责释放句柄内存。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void container_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    container_t* container = (container_t*)lv_obj_get_user_data(obj);

    if (!container) {
        return;
    }

    lv_free(container);
    lv_obj_set_user_data(obj, NULL);
}

/**
 * @brief 获取默认容器配置。
 *
 * @return 返回填充默认值后的配置结构体。
 */
container_cfg_t container_default_cfg(void) {
    container_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.x = 0;
    cfg.y = 0;
    cfg.w = LV_PCT(100);
    cfg.h = LV_PCT(100);
    cfg.max_w = LV_COORD_MAX;
    cfg.max_h = LV_COORD_MAX;
    cfg.radius = 0;
    cfg.border_width = 0;
    cfg.pad_hor = 0;
    cfg.pad_ver = 0;
    cfg.opa = LV_OPA_TRANSP;

    return cfg;
}

/**
 * @brief 创建一个容器组件。
 *
 * @param parent 父对象；为空时使用当前活动屏幕。
 * @param cfg 容器配置；为空时使用默认配置。
 * @return 成功返回容器句柄，失败返回 `NULL`。
 */
container_t* container_create(lv_obj_t* parent, const container_cfg_t* cfg) {
    container_cfg_t default_cfg;
    container_t* container = NULL;
    lv_obj_t* obj = NULL;

    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return NULL;
    }

    default_cfg = container_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    obj = lv_obj_create(parent);
    if (!obj) {
        return NULL;
    }

    container = (container_t*)lv_malloc(sizeof(container_t));
    if (!container) {
        lv_obj_delete(obj);
        return NULL;
    }

    memset(container, 0, sizeof(*container));
    ui_widget_init(&container->base, obj, UI_WIDGET_TYPE_CONTAINER);
    container->parent = container_from_obj(parent);

    lv_obj_set_user_data(obj, container);
    lv_obj_add_event_cb(obj, container_on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_remove_style_all(obj);
    container_apply_cfg(container, cfg);

    return container;
}

/**
 * @brief 为容器追加样式。
 *
 * @param container 目标容器句柄。
 * @param style 目标样式。
 * @param selector LVGL 样式选择器。
 * @return 无返回值。
 */
void container_add_style(container_t* container, const lv_style_t* style, uint32_t selector) {
    if (!container_is_valid(container) || !style) {
        return;
    }

    lv_obj_add_style(container->base.obj, style, selector);
}

/**
 * @brief 设置容器圆角半径。
 *
 * @param container 目标容器句柄。
 * @param radius 圆角半径。
 * @return 无返回值。
 */
void container_set_radius(container_t* container, int32_t radius) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_radius(container->base.obj, (lv_coord_t)radius, 0);
}

/**
 * @brief 设置容器边框宽度。
 *
 * @param container 目标容器句柄。
 * @param border_width 边框宽度。
 * @return 无返回值。
 */
void container_set_border_width(container_t* container, int32_t border_width) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_border_width(container->base.obj, (lv_coord_t)border_width, 0);
}

/**
 * @brief 设置容器透明度。
 *
 * @param container 目标容器句柄。
 * @param opa 新透明度。
 * @return 无返回值。
 */
void container_set_opacity(container_t* container, uint8_t opa) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_border_opa(container->base.obj, (lv_opa_t)opa, 0);
    lv_obj_set_style_bg_opa(container->base.obj, (lv_opa_t)opa, 0);
}

/**
 * @brief 设置容器最大宽度。
 *
 * @param container 目标容器句柄。
 * @param max_width 最大宽度；传 `LV_COORD_MAX` 表示不限制。
 * @return 无返回值。
 */
void container_set_max_width(container_t* container, int32_t max_width) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_max_width(container->base.obj, max_width, 0);
}

/**
 * @brief 设置容器最大高度。
 *
 * @param container 目标容器句柄。
 * @param max_height 最大高度；传 `LV_COORD_MAX` 表示不限制。
 * @return 无返回值。
 */
void container_set_max_height(container_t* container, int32_t max_height) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_max_height(container->base.obj, max_height, 0);
}

/**
 * @brief 设置容器内边距。
 *
 * @param container 目标容器句柄。
 * @param pad_hor 左右内边距。
 * @param pad_ver 上下内边距。
 * @return 无返回值。
 */
void container_set_padding(container_t* container, int32_t pad_hor, int32_t pad_ver) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_pad_hor(container->base.obj, (lv_coord_t)pad_hor, 0);
    lv_obj_set_style_pad_ver(container->base.obj, (lv_coord_t)pad_ver, 0);
}

/**
 * @brief 分别设置容器四边内边距。
 *
 * @param container 目标容器句柄。
 * @param pad_left 左侧内边距。
 * @param pad_right 右侧内边距。
 * @param pad_top 上侧内边距。
 * @param pad_bottom 下侧内边距。
 * @return 无返回值。
 */
void container_set_padding_box(container_t* container,
                               int32_t pad_left,
                               int32_t pad_right,
                               int32_t pad_top,
                               int32_t pad_bottom) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_pad_left(container->base.obj, (lv_coord_t)pad_left, 0);
    lv_obj_set_style_pad_right(container->base.obj, (lv_coord_t)pad_right, 0);
    lv_obj_set_style_pad_top(container->base.obj, (lv_coord_t)pad_top, 0);
    lv_obj_set_style_pad_bottom(container->base.obj, (lv_coord_t)pad_bottom, 0);
}

/**
 * @brief 将容器应用为 VBox 布局。
 *
 * @param container 目标容器句柄。
 * @return 无返回值。
 */
void container_set_layout_vbox(container_t* container) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_layout(container->base.obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container->base.obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(container->base.obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 将容器应用为带间距的 VBox 布局。
 *
 * @param container 目标容器句柄。
 * @param spacing 子项间距。
 * @return 无返回值。
 */
void container_set_layout_vbox_spaced(container_t* container, int32_t spacing) {
    container_set_layout_vbox(container);
    container_set_spacing(container, spacing);
}

/**
 * @brief 将容器应用为 HBox 布局。
 *
 * @param container 目标容器句柄。
 * @return 无返回值。
 */
void container_set_layout_hbox(container_t* container) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_layout(container->base.obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container->base.obj, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(container->base.obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 将容器应用为带间距的 HBox 布局。
 *
 * @param container 目标容器句柄。
 * @param spacing 子项间距。
 * @return 无返回值。
 */
void container_set_layout_hbox_spaced(container_t* container, int32_t spacing) {
    container_set_layout_hbox(container);
    container_set_spacing(container, spacing);
}

/**
 * @brief 设置容器主轴方向。
 *
 * @param container 目标容器句柄。
 * @param flow 主轴方向。
 * @return 无返回值。
 */
void container_set_flow(container_t* container, container_flow_t flow) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_layout(container->base.obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container->base.obj,
                         flow == CONTAINER_FLOW_ROW ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(container->base.obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 设置容器布局对齐方式。
 *
 * @param container 目标容器句柄。
 * @param main_align 主轴对齐方式。
 * @param cross_align 交叉轴对齐方式。
 * @param track_align 多行/多列轨道对齐方式。
 * @return 无返回值。
 */
void container_set_align(container_t* container,
                         container_align_t main_align,
                         container_align_t cross_align,
                         container_align_t track_align) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_flex_align(container->base.obj,
                          container_to_lv_align(main_align),
                          container_to_lv_cross_align(cross_align),
                          container_to_lv_align(track_align));
}

/**
 * @brief 设置容器内子项间距。
 *
 * @param container 目标容器句柄。
 * @param spacing 子项之间的间距。
 * @return 无返回值。
 */
void container_set_spacing(container_t* container, int32_t spacing) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_style_pad_row(container->base.obj, (lv_coord_t)spacing, 0);
    lv_obj_set_style_pad_column(container->base.obj, (lv_coord_t)spacing, 0);
}

/**
 * @brief 设置容器是否可滚动。
 *
 * @param container 目标容器句柄。
 * @param scrollable `true` 表示允许滚动，`false` 表示关闭滚动。
 * @return 无返回值。
 */
void container_set_scrollable(container_t* container, bool scrollable) {
    if (!container_is_valid(container)) {
        return;
    }

    if (scrollable) {
        lv_obj_add_flag(container->base.obj, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_obj_remove_flag(container->base.obj, LV_OBJ_FLAG_SCROLLABLE);
    }
}

/**
 * @brief 设置容器滚动方向。
 *
 * @param container 目标容器句柄。
 * @param dir 滚动方向。
 * @return 无返回值。
 */
void container_set_scroll_dir(container_t* container, lv_dir_t dir) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_scroll_dir(container->base.obj, dir);
}

/**
 * @brief 设置容器滚动条模式。
 *
 * @param container 目标容器句柄。
 * @param mode 滚动条模式。
 * @return 无返回值。
 */
void container_set_scrollbar_mode(container_t* container, lv_scrollbar_mode_t mode) {
    if (!container_is_valid(container)) {
        return;
    }

    lv_obj_set_scrollbar_mode(container->base.obj, mode);
}

/**
 * @brief 设置容器高度自适应策略。
 *
 * @param container 目标容器句柄。
 * @param policy 高度自适应策略。
 * @return 无返回值。
 */
void container_set_height_policy(container_t* container, container_height_policy_t policy) {
    if (!container_is_valid(container)) {
        return;
    }

    container->height_policy = policy;
    container_refresh_layout_up(container);
}

/**
 * @brief 通知父容器某个子对象的布局尺寸已变化。
 *
 * @param child_layout_obj 布局发生变化的子对象。
 * @return 无返回值。
 */
void container_notify_child_layout_changed(lv_obj_t* child_layout_obj) {
    lv_obj_t* parent = NULL;

    if (!child_layout_obj || !lv_obj_is_valid(child_layout_obj)) {
        return;
    }

    lv_obj_update_layout(child_layout_obj);
    parent = lv_obj_get_parent(child_layout_obj);
    while (parent && lv_obj_is_valid(parent)) {
        container_t* container = container_from_obj(parent);

        if (container) {
            container_refresh_layout_up(container);
            return;
        }
        lv_obj_update_layout(parent);
        parent = lv_obj_get_parent(parent);
    }
}

/**
 * @brief 将容器内容直接滚动到顶部。
 *
 * 调用前会先刷新容器布局。
 *
 * @param container 目标容器句柄。
 * @param anim_en 是否启用滚动动画。
 * @return 无返回值。
 */
void container_scroll_to_top(container_t* container, lv_anim_enable_t anim_en) {
    if (!container_is_valid(container)) {
        return;
    }

    container_refresh_layout_up(container);
    lv_obj_update_layout(container->base.obj);
    lv_obj_scroll_to_y(container->base.obj, 0, anim_en);
}

/**
 * @brief 将容器内容直接滚动到底部。
 *
 * 调用前会先刷新容器布局。
 *
 * @param container 目标容器句柄。
 * @param anim_en 是否启用滚动动画。
 * @return 无返回值。
 */
void container_scroll_to_bottom(container_t* container, lv_anim_enable_t anim_en) {
    lv_obj_t* obj = NULL;

    if (!container_is_valid(container)) {
        return;
    }

    obj = container->base.obj;
    container_refresh_layout_up(container);
    lv_obj_update_layout(obj);
    lv_obj_scroll_to_y(obj, lv_obj_get_scroll_top(obj) + lv_obj_get_scroll_bottom(obj), anim_en);
}

static void container_scroll_step(container_t* container,
                                  int32_t direction,
                                  float step_ratio) {
    lv_obj_t* obj = NULL;
    lv_coord_t step = 0;
    int32_t target = 0;

    if (!container_is_valid(container)) {
        return;
    }
    if (step_ratio <= 0.0f) {
        return;
    }

    obj = container->base.obj;
    container_refresh_layout_up(container);
    lv_obj_update_layout(obj);
    step = (lv_coord_t)(lv_obj_get_height(obj) * step_ratio);
    if (step <= 0) {
        step = 1;
    }

    if (direction < 0) {
        target = lv_obj_get_scroll_y(obj) - step;
    } else if (direction > 0) {
        target = lv_obj_get_scroll_y(obj) + step;
    } else {
        return;
    }

    lv_obj_scroll_to_y(obj, target, LV_ANIM_OFF);
}

/**
 * @brief 将容器内容按给定比例向上滚动。
 *
 * 调用前会先刷新容器布局；滚动步长为容器高度乘以 `step_ratio`，
 * 滚动带动画。
 *
 * @param container 目标容器句柄。
 * @param step_ratio 步长比例，传小于等于 0 时忽略本次滚动。
 * @return 无返回值。
 */
void container_scroll_up(container_t* container, float step_ratio) {
    container_scroll_step(container, -1, step_ratio);
}

/**
 * @brief 将容器内容按给定比例向下滚动。
 *
 * 调用前会先刷新容器布局；滚动步长为容器高度乘以 `step_ratio`，
 * 滚动带动画。
 *
 * @param container 目标容器句柄。
 * @param step_ratio 步长比例，传小于等于 0 时忽略本次滚动。
 * @return 无返回值。
 */
void container_scroll_down(container_t* container, float step_ratio) {
    container_scroll_step(container, 1, step_ratio);
}

/**
 * @brief 设置子项的 grow 权重。
 *
 * @param child 目标子项对象。
 * @param grow grow 值，0 表示不拉伸。
 * @return 无返回值。
 */
void container_set_child_grow(lv_obj_t* child, uint8_t grow) {
    if (!child || !lv_obj_is_valid(child)) {
        return;
    }

    lv_obj_set_flex_grow(child, grow);
}

/**
 * @brief 将子项宽高设置为父容器的 100%。
 *
 * @param child 目标子项对象。
 * @param fill_x `true` 表示宽度设为父容器的 100%。
 * @param fill_y `true` 表示高度设为父容器的 100%。
 * @return 无返回值。
 */
void container_set_child_fill(lv_obj_t* child, bool fill_x, bool fill_y) {
    if (!child || !lv_obj_is_valid(child)) {
        return;
    }

    if (fill_x) {
        lv_obj_set_width(child, LV_PCT(100));
    }
    if (fill_y) {
        lv_obj_set_height(child, LV_PCT(100));
    }
}

/**
 * @brief 在 Row Flex 容器中按剩余空间限制目标子项宽度。
 *
 * 该接口会先让目标子项恢复为按内容自适应宽度，再扣除同级兄弟项、
 * 列间距以及目标子项自身 margin，最终只在自然宽度超过剩余空间时
 * 收窄子项宽度，从而让文本类子项在容器内触发自动换行。
 *
 * @param container 目标容器句柄。
 * @param child 需要限制宽度的子项对象。
 * @return 无返回值。
 */
void container_limit_child_width_to_remaining_row_space(container_t* container, lv_obj_t* child) {
    lv_obj_t* obj = NULL;
    lv_obj_t* iter = NULL;
    lv_flex_flow_t flow;
    uint32_t visible_child_count = 0;
    int32_t sibling_width = 0;
    int32_t remaining_width = 0;
    int32_t child_margin = 0;
    int32_t gap = 0;
    int32_t natural_width = 0;

    if (!container_is_valid(container) || !child || !lv_obj_is_valid(child)) {
        return;
    }

    obj = container->base.obj;
    if (lv_obj_get_parent(child) != obj || !container_child_in_layout(child)) {
        return;
    }

    flow = lv_obj_get_style_flex_flow(obj, LV_PART_MAIN);
    if (flow & LV_FLEX_COLUMN) {
        return;
    }

    lv_obj_set_width(child, LV_SIZE_CONTENT);
    lv_obj_update_layout(obj);

    gap = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);
    iter = lv_obj_get_child(obj, 0);
    while (iter) {
        if (container_child_in_layout(iter)) {
            visible_child_count++;
            if (iter != child) {
                sibling_width += lv_obj_get_width(iter) +
                                 lv_obj_get_style_margin_left(iter, LV_PART_MAIN) +
                                 lv_obj_get_style_margin_right(iter, LV_PART_MAIN);
            }
        }
        iter = lv_obj_get_child(obj, lv_obj_get_index(iter) + 1);
    }

    remaining_width = lv_obj_get_content_width(obj) - sibling_width;
    if (visible_child_count > 1) {
        remaining_width -= gap * ((int32_t)visible_child_count - 1);
    }

    child_margin = lv_obj_get_style_margin_left(child, LV_PART_MAIN) +
                   lv_obj_get_style_margin_right(child, LV_PART_MAIN);
    remaining_width -= child_margin;
    if (remaining_width < 0) {
        remaining_width = 0;
    }

    natural_width = lv_obj_get_width(child);
    if (natural_width > remaining_width) {
        lv_obj_set_width(child, remaining_width);
    }
}

/**
 * @brief 按配置结构批量刷新容器状态。
 *
 * @param container 目标容器句柄。
 * @param cfg 配置结构；为空时使用默认配置。
 * @return 无返回值。
 */
void container_apply_cfg(container_t* container, const container_cfg_t* cfg) {
    container_cfg_t default_cfg;

    if (!container_is_valid(container)) {
        return;
    }

    default_cfg = container_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    ui_widget_set_bounds(UI_WIDGET(container), cfg->x, cfg->y, cfg->w, cfg->h);
    container_set_max_width(container, cfg->max_w);
    container_set_max_height(container, cfg->max_h);
    container_set_radius(container, cfg->radius);
    container_set_border_width(container, cfg->border_width);
    lv_obj_set_style_bg_color(container->base.obj, lv_color_black(), 0);
    lv_obj_set_style_border_color(container->base.obj, lv_color_white(), 0);
    container_set_opacity(container, cfg->opa);
    container_set_padding(container, cfg->pad_hor, cfg->pad_ver);
}

/**
 * @brief 获取底层 LVGL 对象。
 *
 * @param container 目标容器句柄。
 * @return 返回底层对象指针；无效时返回 `NULL`。
 */
lv_obj_t* container_get_obj(container_t* container) {
    if (!container_is_valid(container)) {
        return NULL;
    }

    return container->base.obj;
}
