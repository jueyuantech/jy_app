#ifndef COMMON_WIDGETS_UI_WIDGET_H
#define COMMON_WIDGETS_UI_WIDGET_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用组件类型标识。
 */
typedef enum {
    UI_WIDGET_TYPE_UNKNOWN = 0,
    UI_WIDGET_TYPE_CONTAINER,
    UI_WIDGET_TYPE_LABEL,
    UI_WIDGET_TYPE_IMG,
    UI_WIDGET_TYPE_BUTTON,
    UI_WIDGET_TYPE_OVERLAY,
    UI_WIDGET_TYPE_ROLLER,
    UI_WIDGET_TYPE_MSGBOX,
    UI_WIDGET_TYPE_PAGED_TEXT, ///< 分页文本组件。
} ui_widget_type_t;

typedef struct ui_widget_t ui_widget_t;

/**
 * @brief 统一组件基类。
 *
 * 具体组件需要把该结构放在自身结构体的首字段，
 * 这样即可安全地向上转型为 `ui_widget_t*` 使用通用 helper。
 */
struct ui_widget_t {
    lv_obj_t* obj;                 ///< 组件对应的底层 LVGL 对象。
    ui_widget_type_t type;         ///< 组件类型标识。
};

/**
 * @brief 将任意组件句柄视为统一基类句柄。
 */
#define UI_WIDGET(handle) ((ui_widget_t*)(handle))

/**
 * @brief 初始化统一组件基类。
 *
 * @param widget 目标基类指针。
 * @param obj 底层 LVGL 对象。
 * @param type 组件类型。
 * @return 无返回值。
 */
static inline void ui_widget_init(ui_widget_t* widget,
                                  lv_obj_t* obj,
                                  ui_widget_type_t type)
{
    if (!widget) {
        return;
    }

    widget->obj = obj;
    widget->type = type;
}

/**
 * @brief 判断统一组件句柄及底层对象是否有效。
 *
 * @param widget 目标组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static inline bool ui_widget_is_valid(const ui_widget_t* widget)
{
    return widget && widget->obj && lv_obj_is_valid(widget->obj);
}

/**
 * @brief 获取统一组件的底层 LVGL 对象。
 *
 * @param widget 目标组件句柄。
 * @return 返回底层对象；无效时返回 `NULL`。
 */
static inline lv_obj_t* ui_widget_get_obj(const ui_widget_t* widget)
{
    if (!ui_widget_is_valid(widget)) {
        return NULL;
    }

    return widget->obj;
}

/**
 * @brief 获取统一组件的类型标识。
 *
 * @param widget 目标组件句柄。
 * @return 返回组件类型；无效时返回 `UI_WIDGET_TYPE_UNKNOWN`。
 */
static inline ui_widget_type_t ui_widget_get_type(const ui_widget_t* widget)
{
    if (!widget) {
        return UI_WIDGET_TYPE_UNKNOWN;
    }

    return widget->type;
}

/**
 * @brief 通过统一组件句柄销毁组件。
 *
 * 对于已接入统一句柄头的组件，直接删除其根 LVGL 对象即可触发
 * 组件自己的 `LV_EVENT_DELETE` 清理逻辑。
 *
 * @param widget 目标组件句柄。
 * @return 无返回值。
 */
static inline void ui_widget_destroy(ui_widget_t* widget)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);

    if (!obj) {
        return;
    }

    lv_obj_delete(obj);
}

/**
 * @brief 设置统一组件的位置。
 *
 * @param widget 目标组件句柄。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无返回值。
 */
static inline void ui_widget_set_position(ui_widget_t* widget, int32_t x, int32_t y)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);

    if (!obj) {
        return;
    }

    lv_obj_set_pos(obj, (lv_coord_t)x, (lv_coord_t)y);
}

/**
 * @brief 设置统一组件的尺寸。
 *
 * @param widget 目标组件句柄。
 * @param w 新宽度；可传 `LV_SIZE_CONTENT` 或 `LV_PCT()`。
 * @param h 新高度；可传 `LV_SIZE_CONTENT` 或 `LV_PCT()`。
 * @return 无返回值。
 */
static inline void ui_widget_set_size(ui_widget_t* widget, int32_t w, int32_t h)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);

    if (!obj) {
        return;
    }

    if (w > 0 || w == LV_SIZE_CONTENT || LV_COORD_IS_PCT(w)) {
        lv_obj_set_width(obj, (lv_coord_t)w);
    }
    if (h > 0 || h == LV_SIZE_CONTENT || LV_COORD_IS_PCT(h)) {
        lv_obj_set_height(obj, (lv_coord_t)h);
    }
}

/**
 * @brief 同时设置统一组件的位置和尺寸。
 *
 * @param widget 目标组件句柄。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @param w 新宽度；可传 `LV_SIZE_CONTENT` 或 `LV_PCT()`。
 * @param h 新高度；可传 `LV_SIZE_CONTENT` 或 `LV_PCT()`。
 * @return 无返回值。
 */
static inline void ui_widget_set_bounds(ui_widget_t* widget, int32_t x, int32_t y, int32_t w, int32_t h)
{
    ui_widget_set_position(widget, x, y);
    ui_widget_set_size(widget, w, h);
}

/**
 * @brief 获取组件相对指定祖先组件的坐标和尺寸。
 *
 * 常用于业务逻辑需要把子组件几何转换到页面或预览根容器坐标系的场景。
 * 输出坐标为目标组件左上角相对祖先组件左上角的位置；目标组件必须位于
 * `ancestor` 的子树内。
 *
 * @param widget 目标组件句柄。
 * @param ancestor 目标祖先组件句柄。
 * @param x 输出相对 X 坐标；可传 `NULL`。
 * @param y 输出相对 Y 坐标；可传 `NULL`。
 * @param w 输出目标宽度；可传 `NULL`。
 * @param h 输出目标高度；可传 `NULL`。
 * @return `true` 表示成功获取，`false` 表示组件无效或不在祖先子树内。
 */
static inline bool ui_widget_get_geometry_relative_to(const ui_widget_t* widget,
                                                      const ui_widget_t* ancestor,
                                                      int32_t* x,
                                                      int32_t* y,
                                                      int32_t* w,
                                                      int32_t* h)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);
    lv_obj_t* ancestor_obj = ui_widget_get_obj(ancestor);
    lv_obj_t* cur = obj;
    int32_t rel_x = 0;
    int32_t rel_y = 0;

    if (!obj || !ancestor_obj) {
        return false;
    }

    lv_obj_update_layout(ancestor_obj);
    while (cur && lv_obj_is_valid(cur) && cur != ancestor_obj) {
        rel_x += (int32_t)lv_obj_get_x(cur);
        rel_y += (int32_t)lv_obj_get_y(cur);
        cur = lv_obj_get_parent(cur);
    }
    if (cur != ancestor_obj) {
        return false;
    }

    if (x) {
        *x = rel_x;
    }
    if (y) {
        *y = rel_y;
    }
    if (w) {
        *w = (int32_t)lv_obj_get_width(obj);
    }
    if (h) {
        *h = (int32_t)lv_obj_get_height(obj);
    }
    return true;
}

/**
 * @brief 设置统一组件显隐状态。
 *
 * @param widget 目标组件句柄。
 * @param visible `true` 表示显示，`false` 表示隐藏。
 * @return 无返回值。
 */
static inline void ui_widget_set_visible(ui_widget_t* widget, bool visible)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);

    if (!obj) {
        return;
    }

    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 判断统一组件当前是否隐藏。
 *
 * @param widget 目标组件句柄。
 * @return `true` 表示隐藏或无效，`false` 表示当前可见。
 */
static inline bool ui_widget_is_hidden(const ui_widget_t* widget)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);

    if (!obj) {
        return true;
    }

    return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 获取统一组件的父对象。
 *
 * @param widget 目标组件句柄。
 * @return 返回父对象；无效时返回 `NULL`。
 */
static inline lv_obj_t* ui_widget_get_parent(const ui_widget_t* widget)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);

    if (!obj) {
        return NULL;
    }

    return lv_obj_get_parent(obj);
}

/**
 * @brief 将统一组件移动到当前层级最前面。
 *
 * @param widget 目标组件句柄。
 * @return 无返回值。
 */
static inline void ui_widget_move_foreground(ui_widget_t* widget)
{
    lv_obj_t* obj = ui_widget_get_obj(widget);

    if (!obj) {
        return;
    }

    lv_obj_move_foreground(obj);
}

#ifdef __cplusplus
}
#endif

#endif
