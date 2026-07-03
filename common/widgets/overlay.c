/**
 * @file overlay.c
 * @brief 通用叠加层组件实现，统一管理点位、线段与文本标注对象。
 */
#include "overlay.h"

#include <string.h>

/* Overlay 点位默认直径。 */
#define OVERLAY_POINT_DEFAULT_SIZE 6
/* Overlay 点位默认透明度。 */
#define OVERLAY_POINT_DEFAULT_OPA LV_OPA_COVER
/* Overlay 线段默认线宽。 */
#define OVERLAY_LINE_DEFAULT_WIDTH 1
/* Overlay 线段默认透明度。 */
#define OVERLAY_LINE_DEFAULT_OPA LV_OPA_COVER
/**
 * @brief 通用叠加层组件内部数据结构。
 */
struct overlay_t {
    ui_widget_t base;          ///< 统一组件基类。
    lv_obj_t** points;         ///< 点位对象数组。
    overlay_line_t* lines;     ///< 线段配置数组。
    label_t** texts;           ///< 文本对象数组。
    uint16_t max_items;        ///< 可用点位、线段、文本槽位总数。
    uint16_t point_count;      ///< 当前已追加的点位数量。
    uint16_t line_count;       ///< 当前已追加的线段数量。
    uint16_t text_count;       ///< 当前已追加的文本数量。
    overlay_point_t point_cfg; ///< 默认点位配置。
    overlay_line_t line_cfg;   ///< 默认线段配置。
    label_cfg_t text_cfg;      ///< 默认文本配置。
};

/**
 * @brief 判断组件句柄及关键对象是否有效。
 *
 * @param overlay 目标组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool overlay_is_valid(overlay_t* overlay) {
    return overlay && ui_widget_is_valid(UI_WIDGET(overlay));
}

/**
 * @brief 按需隐藏 LVGL 对象，避免重复隐藏制造无效刷新区域。
 *
 * @param obj 目标 LVGL 对象。
 * @return 无返回值。
 */
static void overlay_hide_obj(lv_obj_t* obj) {
    if (obj && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 按需显示 LVGL 对象，避免重复显示制造无效刷新区域。
 *
 * @param obj 目标 LVGL 对象。
 * @return 无返回值。
 */
static void overlay_show_obj(lv_obj_t* obj) {
    if (obj && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 计算线段实际绘制参数。
 *
 * @param overlay 目标组件句柄。
 * @param line 线段配置。
 * @param width 输出实际线宽。
 * @param opa 输出实际透明度。
 * @return `true` 表示线段可见，`false` 表示无需绘制。
 */
static bool overlay_resolve_line_draw(const overlay_t* overlay,
                                      const overlay_line_t* line,
                                      int32_t* width,
                                      lv_opa_t* opa) {
    int32_t resolved_width;
    uint8_t resolved_opa;

    if (!overlay || !line || !width || !opa) {
        return false;
    }

    resolved_width = line->width > 0
                         ? line->width
                         : (overlay->line_cfg.width > 0
                                ? overlay->line_cfg.width
                                : OVERLAY_LINE_DEFAULT_WIDTH);
    resolved_opa = line->opa > 0
                       ? line->opa
                       : (overlay->line_cfg.opa > 0
                              ? overlay->line_cfg.opa
                              : OVERLAY_LINE_DEFAULT_OPA);
    if (resolved_width <= 0 || resolved_opa == 0) {
        return false;
    }

    *width = resolved_width;
    *opa = (lv_opa_t)resolved_opa;
    return true;
}

/**
 * @brief 标记单条线段覆盖区域为脏区。
 *
 * @param overlay 目标组件句柄。
 * @param line 线段配置。
 * @return 无返回值。
 */
static void overlay_invalidate_line_area(overlay_t* overlay, const overlay_line_t* line) {
    lv_obj_t* obj = NULL;
    lv_area_t overlay_area;
    lv_area_t line_area;
    int32_t width = 0;
    lv_opa_t opa = LV_OPA_TRANSP;
    int32_t radius;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (!overlay_is_valid(overlay) || !line ||
        !overlay_resolve_line_draw(overlay, line, &width, &opa)) {
        return;
    }

    obj = ui_widget_get_obj(UI_WIDGET(overlay));
    if (!obj) {
        return;
    }

    lv_obj_get_coords(obj, &overlay_area);
    radius = width / 2;
    x0 = overlay_area.x1 + line->start_x;
    y0 = overlay_area.y1 + line->start_y;
    x1 = overlay_area.x1 + line->end_x;
    y1 = overlay_area.y1 + line->end_y;
    line_area.x1 = LV_MIN(x0, x1) - radius;
    line_area.y1 = LV_MIN(y0, y1) - radius;
    line_area.x2 = LV_MAX(x0, x1) + radius;
    line_area.y2 = LV_MAX(y0, y1) + radius;
    lv_obj_invalidate_area(obj, &line_area);
}

/**
 * @brief 批量标记线段覆盖区域为脏区。
 *
 * @param overlay 目标组件句柄。
 * @param lines 线段数组。
 * @param count 线段数量。
 * @return 无返回值。
 */
static void overlay_invalidate_lines(overlay_t* overlay, const overlay_line_t* lines, uint16_t count) {
    if (!lines) {
        return;
    }

    for (uint16_t i = 0; i < count; ++i) {
        overlay_invalidate_line_area(overlay, &lines[i]);
    }
}

/**
 * @brief 应用 overlay 根对象的默认样式。
 *
 * @param overlay 目标组件句柄。
 * @return 无返回值。
 */
static void overlay_apply_layer_cfg(overlay_t* overlay) {
    lv_obj_t* obj = NULL;

    if (!overlay_is_valid(overlay)) {
        return;
    }

    obj = ui_widget_get_obj(UI_WIDGET(overlay));
    if (!obj) {
        return;
    }

    ui_widget_set_bounds(UI_WIDGET(overlay), 0, 0, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_border_color(obj, lv_color_white(), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(obj, 0, 0);
    lv_obj_set_style_pad_ver(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 根容器删除时释放内部数组与句柄内存。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void overlay_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    overlay_t* overlay = (overlay_t*)lv_obj_get_user_data(obj);

    if (!overlay) {
        return;
    }

    if (overlay->points) {
        lv_free(overlay->points);
    }
    if (overlay->lines) {
        lv_free(overlay->lines);
    }
    if (overlay->texts) {
        lv_free(overlay->texts);
    }

    lv_free(overlay);
    lv_obj_set_user_data(obj, NULL);
}

/**
 * @brief 使用 Bresenham 光栅化绘制线段，避开后端斜线绘制差异。
 *
 * @param layer LVGL 绘制层。
 * @param x0 起点全局 X 坐标。
 * @param y0 起点全局 Y 坐标。
 * @param x1 终点全局 X 坐标。
 * @param y1 终点全局 Y 坐标。
 * @param width 线宽。
 * @param opa 透明度。
 * @return 无返回值。
 */
static void overlay_draw_raster_line(lv_layer_t* layer,
                                     int32_t x0,
                                     int32_t y0,
                                     int32_t x1,
                                     int32_t y1,
                                     int32_t width,
                                     lv_opa_t opa) {
    lv_draw_rect_dsc_t rect_dsc;
    int32_t dx = LV_ABS(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -LV_ABS(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    int32_t radius = width / 2;

    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = opa;
    rect_dsc.border_opa = LV_OPA_TRANSP;
    rect_dsc.radius = width > 2 ? LV_RADIUS_CIRCLE : 0;

    while (true) {
        lv_area_t area;
        area.x1 = x0 - radius;
        area.y1 = y0 - radius;
        area.x2 = x0 + radius;
        area.y2 = y0 + radius;
        lv_draw_rect(layer, &rect_dsc, &area);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/**
 * @brief 在 overlay 根对象上直接绘制线段并打印最终坐标。
 *
 * @param e LVGL 绘制事件对象。
 * @return 无返回值。
 */
static void overlay_on_draw_main(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_current_target(e);
    overlay_t* overlay = (overlay_t*)lv_obj_get_user_data(obj);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t overlay_area;

    if (!overlay_is_valid(overlay) || !overlay->lines || overlay->line_count == 0 || !layer) {
        return;
    }

    lv_obj_get_coords(obj, &overlay_area);
    for (uint16_t i = 0; i < overlay->line_count; ++i) {
        const overlay_line_t* line = &overlay->lines[i];
        int32_t resolved_width = 0;
        lv_opa_t resolved_opa = LV_OPA_TRANSP;

        if (!overlay_resolve_line_draw(overlay, line, &resolved_width, &resolved_opa)) {
            continue;
        }

        overlay_draw_raster_line(layer,
                                 overlay_area.x1 + line->start_x,
                                 overlay_area.y1 + line->start_y,
                                 overlay_area.x1 + line->end_x,
                                 overlay_area.y1 + line->end_y,
                                 resolved_width,
                                 (lv_opa_t)resolved_opa);
    }
}

/**
 * @brief 将单个点位数据应用到指定槽位。
 *
 * @param overlay 目标组件句柄。
 * @param index 目标槽位索引。
 * @param point 点位数据。
 * @return `true` 表示更新成功，`false` 表示参数非法或组件无效。
 */
static bool overlay_apply_point(overlay_t* overlay, uint16_t index, const overlay_point_t* point) {
    lv_coord_t resolved_size;
    lv_coord_t point_size;
    uint8_t resolved_opa;

    if (!overlay_is_valid(overlay) || !overlay->points || !point) {
        return false;
    }
    if (index >= overlay->max_items || !overlay->points[index]) {
        return false;
    }

    resolved_size = (lv_coord_t)(point->size > 0
                                     ? point->size
                                     : (overlay->point_cfg.size > 0
                                            ? overlay->point_cfg.size
                                            : OVERLAY_POINT_DEFAULT_SIZE));
    resolved_opa = point->opa > 0
                       ? point->opa
                       : (overlay->point_cfg.opa > 0 ? overlay->point_cfg.opa : OVERLAY_POINT_DEFAULT_OPA);
    lv_obj_set_size(overlay->points[index], resolved_size, resolved_size);
    lv_obj_set_style_bg_opa(overlay->points[index], (lv_opa_t)resolved_opa, LV_PART_MAIN);
    point_size = lv_obj_get_width(overlay->points[index]);
    lv_obj_set_pos(overlay->points[index],
                   (lv_coord_t)(point->x - (point_size / 2)),
                   (lv_coord_t)(point->y - (point_size / 2)));
    overlay_show_obj(overlay->points[index]);
    return true;
}

/**
 * @brief 将单个文本数据应用到指定槽位。
 *
 * @param overlay 目标组件句柄。
 * @param index 目标槽位索引。
 * @param text 文本数据。
 * @return `true` 表示更新成功，`false` 表示参数非法或组件无效。
 */
static bool overlay_apply_text(overlay_t* overlay, uint16_t index, const label_cfg_t* text) {
    label_cfg_t resolved_cfg;
    bool has_font_override;

    if (!overlay_is_valid(overlay) || !overlay->texts || !text) {
        return false;
    }
    if (index >= overlay->max_items || !overlay->texts[index]) {
        return false;
    }

    resolved_cfg = overlay->text_cfg;
    has_font_override = text->font.weight != 0 || text->font.wordSpace != 0 || text->font.rowSpace != 0;

    resolved_cfg.x = text->x;
    resolved_cfg.y = text->y;
    resolved_cfg.w = text->w != 0 ? text->w : resolved_cfg.w;
    resolved_cfg.h = text->h != 0 ? text->h : resolved_cfg.h;
    resolved_cfg.radius = text->radius != 0 ? text->radius : resolved_cfg.radius;
    resolved_cfg.border_width = text->border_width != 0 ? text->border_width : resolved_cfg.border_width;
    resolved_cfg.pad_hor = text->pad_hor != 0 ? text->pad_hor : resolved_cfg.pad_hor;
    resolved_cfg.pad_ver = text->pad_ver != 0 ? text->pad_ver : resolved_cfg.pad_ver;
    resolved_cfg.opa = text->opa != 0 ? text->opa : resolved_cfg.opa;
    resolved_cfg.align = text->align != LABEL_ALIGN_LEFT ? text->align : resolved_cfg.align;
    resolved_cfg.overflow = text->overflow != LABEL_OVERFLOW_CLIP ? text->overflow : resolved_cfg.overflow;
    resolved_cfg.font = has_font_override ? text->font : resolved_cfg.font;
    resolved_cfg.text = text->text != NULL ? text->text : resolved_cfg.text;

    label_apply_cfg(overlay->texts[index], &resolved_cfg);
    ui_widget_set_visible(UI_WIDGET(overlay->texts[index]), true);
    return true;
}

/**
 * @brief 获取默认配置。
 *
 * @return 返回填充默认值后的配置结构体。
 */
overlay_cfg_t overlay_default_cfg(void) {
    overlay_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = label_default_cfg();
    cfg.text.align = LABEL_ALIGN_LEFT;
    cfg.text.overflow = LABEL_OVERFLOW_CLIP;
    cfg.point.size = OVERLAY_POINT_DEFAULT_SIZE;
    cfg.point.opa = OVERLAY_POINT_DEFAULT_OPA;
    cfg.line.width = OVERLAY_LINE_DEFAULT_WIDTH;
    cfg.line.opa = OVERLAY_LINE_DEFAULT_OPA;

    cfg.max_items = 16;

    return cfg;
}

/**
 * @brief 创建叠加层组件。
 *
 * @param parent 父对象；为空时使用当前活动屏幕。
 * @param cfg 配置结构；为空时使用默认配置。
 * @return 成功返回组件句柄，失败返回 `NULL`。
 */
overlay_t* overlay_create(lv_obj_t* parent, const overlay_cfg_t* cfg) {
    overlay_cfg_t default_cfg;
    label_cfg_t text_cfg;
    overlay_t* overlay = NULL;
    lv_obj_t* obj = NULL;
    uint16_t i;

    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return NULL;
    }

    default_cfg = overlay_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    if (cfg->max_items == 0) {
        return NULL;
    }

    obj = lv_obj_create(parent);
    if (!obj) {
        return NULL;
    }

    overlay = (overlay_t*)lv_malloc(sizeof(overlay_t));
    if (!overlay) {
        lv_obj_delete(obj);
        return NULL;
    }
    lv_memzero(overlay, sizeof(*overlay));
    ui_widget_init(&overlay->base, obj, UI_WIDGET_TYPE_OVERLAY);

    lv_obj_set_user_data(obj, overlay);
    lv_obj_add_event_cb(obj, overlay_on_delete, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(obj, overlay_on_draw_main, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_remove_style_all(obj);
    overlay_apply_layer_cfg(overlay);

    overlay->max_items = cfg->max_items;
    overlay->point_count = 0;
    overlay->line_count = 0;
    overlay->text_count = 0;
    overlay->point_cfg = cfg->point;
    overlay->line_cfg = cfg->line;
    overlay->text_cfg = cfg->text;
    overlay->points = (lv_obj_t**)lv_malloc(sizeof(lv_obj_t*) * overlay->max_items);
    overlay->lines = (overlay_line_t*)lv_malloc(sizeof(overlay_line_t) * overlay->max_items);
    overlay->texts = (label_t**)lv_malloc(sizeof(label_t*) * overlay->max_items);
    if (!overlay->points || !overlay->lines || !overlay->texts) {
        lv_obj_delete(obj);
        return NULL;
    }
    lv_memzero(overlay->points, sizeof(lv_obj_t*) * overlay->max_items);
    lv_memzero(overlay->lines, sizeof(overlay_line_t) * overlay->max_items);
    lv_memzero(overlay->texts, sizeof(label_t*) * overlay->max_items);

    text_cfg = cfg->text;

    for (i = 0; i < overlay->max_items; ++i) {
        overlay->points[i] = lv_obj_create(obj);
        if (!overlay->points[i]) {
            ui_widget_destroy(UI_WIDGET(overlay));
            return NULL;
        }
        lv_obj_remove_style_all(overlay->points[i]);
        lv_obj_set_size(overlay->points[i], OVERLAY_POINT_DEFAULT_SIZE, OVERLAY_POINT_DEFAULT_SIZE);
        lv_obj_set_style_radius(overlay->points[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(overlay->points[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay->points[i], OVERLAY_POINT_DEFAULT_OPA, LV_PART_MAIN);
        lv_obj_add_flag(overlay->points[i], LV_OBJ_FLAG_HIDDEN);

        overlay->texts[i] = label_create(obj, &text_cfg);
        if (!overlay->texts[i]) {
            ui_widget_destroy(UI_WIDGET(overlay));
            return NULL;
        }
        ui_widget_set_visible(UI_WIDGET(overlay->texts[i]), false);
    }

    return overlay;
}

/**
 * @brief 销毁叠加层组件。
 *
 * @param overlay 目标组件句柄。
 * @return 无返回值。
 */
void overlay_destroy(overlay_t* overlay) {
    if (!overlay_is_valid(overlay)) {
        return;
    }

    ui_widget_destroy(UI_WIDGET(overlay));
}

/**
 * @brief 批量设置 overlay 点位。
 *
 * @param overlay 目标组件句柄。
 * @param points 点位数组。
 * @param count 点位数量。
 * @return 无返回值。
 */
void overlay_set_points(overlay_t* overlay, const overlay_point_t* points, uint16_t count) {
    uint16_t i;
    uint16_t point_count;

    if (!overlay_is_valid(overlay) || !overlay->points) {
        return;
    }

    point_count = (points && count < overlay->max_items) ? count : overlay->max_items;
    if (!points) {
        point_count = 0;
    }

    for (i = point_count; i < overlay->point_count && i < overlay->max_items; ++i) {
        if (overlay->points[i]) {
            overlay_hide_obj(overlay->points[i]);
        }
    }

    if (!points) {
        overlay->point_count = 0;
        return;
    }

    for (i = 0; i < point_count; ++i) {
        overlay_apply_point(overlay, i, &points[i]);
    }
    overlay->point_count = point_count;
}

/**
 * @brief 批量设置 overlay 线段。
 *
 * @param overlay 目标组件句柄。
 * @param lines 线段数组。
 * @param count 线段数量。
 * @return 无返回值。
 */
void overlay_set_lines(overlay_t* overlay, const overlay_line_t* lines, uint16_t count) {
    uint16_t line_count;
    bool changed = false;

    if (!overlay_is_valid(overlay) || !overlay->lines) {
        return;
    }

    if (!lines) {
        if (overlay->line_count > 0) {
            overlay_invalidate_lines(overlay, overlay->lines, overlay->line_count);
            overlay->line_count = 0;
            lv_memzero(overlay->lines, sizeof(overlay_line_t) * overlay->max_items);
        }
        return;
    }

    line_count = (count < overlay->max_items) ? count : overlay->max_items;
    changed = overlay->line_count != line_count ||
              (line_count > 0 &&
               memcmp(overlay->lines, lines, sizeof(overlay_line_t) * line_count) != 0);
    if (!changed) {
        return;
    }

    overlay_invalidate_lines(overlay, overlay->lines, overlay->line_count);
    overlay_invalidate_lines(overlay, lines, line_count);

    if (line_count > 0) {
        lv_memcpy(overlay->lines, lines, sizeof(overlay_line_t) * line_count);
    }
    if (line_count < overlay->max_items) {
        lv_memzero(&overlay->lines[line_count], sizeof(overlay_line_t) * (overlay->max_items - line_count));
    }
    overlay->line_count = line_count;
}

/**
 * @brief 追加一个 overlay 点位到下一个可用槽位。
 *
 * @param overlay 目标组件句柄。
 * @param point 点位数据。
 * @return `true` 表示追加成功，`false` 表示已满或组件无效。
 */
bool overlay_add_point(overlay_t* overlay, const overlay_point_t* point) {
    bool ok;

    if (!overlay_is_valid(overlay) || overlay->point_count >= overlay->max_items) {
        return false;
    }

    ok = overlay_apply_point(overlay, overlay->point_count, point);

    if (ok) {
        overlay->point_count++;
    }

    return ok;
}

/**
 * @brief 批量设置 overlay 文本。
 *
 * @param overlay 目标组件句柄。
 * @param texts 文本数组。
 * @param count 文本数量。
 * @return 无返回值。
 */
void overlay_set_texts(overlay_t* overlay, const label_cfg_t* texts, uint16_t count) {
    uint16_t i;
    uint16_t text_count;

    if (!overlay_is_valid(overlay) || !overlay->texts) {
        return;
    }

    text_count = (texts && count < overlay->max_items) ? count : overlay->max_items;
    if (!texts) {
        text_count = 0;
    }

    for (i = text_count; i < overlay->text_count && i < overlay->max_items; ++i) {
        if (overlay->texts[i]) {
            ui_widget_set_visible(UI_WIDGET(overlay->texts[i]), false);
        }
    }

    if (!texts) {
        overlay->text_count = 0;
        return;
    }

    for (i = 0; i < text_count; ++i) {
        overlay_apply_text(overlay, i, &texts[i]);
    }
    overlay->text_count = text_count;
}

/**
 * @brief 追加一个 overlay 文本到下一个可用槽位。
 *
 * @param overlay 目标组件句柄。
 * @param text 文本数据。
 * @return `true` 表示追加成功，`false` 表示已满或组件无效。
 */
bool overlay_add_text(overlay_t* overlay, const label_cfg_t* text) {
    bool ok;

    if (!overlay_is_valid(overlay) || overlay->text_count >= overlay->max_items) {
        return false;
    }

    ok = overlay_apply_text(overlay, overlay->text_count, text);

    if (ok) {
        overlay->text_count++;
    }

    return ok;
}

/**
 * @brief 使用文本和字号快速追加一个 overlay 文本到下一个可用槽位。
 *
 * @param overlay 目标组件句柄。
 * @param text 文本内容。
 * @param font_size 文本字号。
 * @return `true` 表示追加成功，`false` 表示已满或组件无效。
 */
bool overlay_add_text_from_font(overlay_t* overlay, const char* text, int32_t font_size) {
    label_cfg_t cfg;

    if (!overlay_is_valid(overlay)) {
        return false;
    }

    cfg = overlay->text_cfg;
    cfg.text = text;
    if (font_size > 0) {
        cfg.font.weight = (uint32_t)font_size;
    }

    return overlay_add_text(overlay, &cfg);
}

/**
 * @brief 清空所有点位、线段与文本。
 *
 * @param overlay 目标组件句柄。
 * @return 无返回值。
 */
void overlay_clear(overlay_t* overlay) {
    uint16_t i;
    bool had_content;

    if (!overlay_is_valid(overlay)) {
        return;
    }

    had_content = overlay->point_count > 0 || overlay->line_count > 0 || overlay->text_count > 0;
    for (i = 0; i < overlay->max_items; ++i) {
        if (overlay->points[i]) {
            overlay_hide_obj(overlay->points[i]);
        }
        if (overlay->texts[i]) {
            ui_widget_set_visible(UI_WIDGET(overlay->texts[i]), false);
        }
    }
    if (overlay->lines) {
        lv_memzero(overlay->lines, sizeof(overlay_line_t) * overlay->max_items);
    }
    overlay->point_count = 0;
    overlay->line_count = 0;
    overlay->text_count = 0;
    if (had_content) {
        lv_obj_invalidate(ui_widget_get_obj(UI_WIDGET(overlay)));
    }
}

/**
 * @brief 获取组件根对象。
 *
 * @param overlay 目标组件句柄。
 * @return 返回底层对象；无效时返回 `NULL`。
 */
lv_obj_t* overlay_get_obj(overlay_t* overlay) {
    if (!overlay_is_valid(overlay)) {
        return NULL;
    }

    return ui_widget_get_obj(UI_WIDGET(overlay));
}
