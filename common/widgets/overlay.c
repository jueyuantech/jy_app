#include "overlay.h"

#include <string.h>

#define OVERLAY_POINT_DEFAULT_SIZE 6
#define OVERLAY_POINT_DEFAULT_OPA LV_OPA_COVER

/**
 * @brief 通用叠加层组件内部数据结构。
 */
struct overlay_t {
    ui_widget_t base;     ///< 统一组件基类。
    lv_obj_t** points;    ///< 点位对象数组。
    label_t** texts;      ///< 文本对象数组。
    uint16_t max_items;   ///< 可用点位/文本槽位总数。
    uint16_t point_count; ///< 当前已追加的点位数量。
    uint16_t text_count;  ///< 当前已追加的文本数量。
    overlay_point_t point_cfg;///< 默认点位配置。
    label_cfg_t text_cfg; ///< 默认文本配置。
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
    if (overlay->texts) {
        lv_free(overlay->texts);
    }

    lv_free(overlay);
    lv_obj_set_user_data(obj, NULL);
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
    lv_obj_remove_flag(overlay->points[index], LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_remove_style_all(obj);
    overlay_apply_layer_cfg(overlay);

    overlay->max_items = cfg->max_items;
    overlay->point_count = 0;
    overlay->text_count = 0;
    overlay->point_cfg = cfg->point;
    overlay->text_cfg = cfg->text;
    overlay->points = (lv_obj_t**)lv_malloc(sizeof(lv_obj_t*) * overlay->max_items);
    overlay->texts = (label_t**)lv_malloc(sizeof(label_t*) * overlay->max_items);
    if (!overlay->points || !overlay->texts) {
        lv_obj_delete(obj);
        return NULL;
    }
    lv_memzero(overlay->points, sizeof(lv_obj_t*) * overlay->max_items);
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

    if (!overlay_is_valid(overlay) || !overlay->points) {
        return;
    }

    for (i = 0; i < overlay->max_items; ++i) {
        if (overlay->points[i]) {
            lv_obj_add_flag(overlay->points[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!points) {
        overlay->point_count = 0;
        return;
    }

    for (i = 0; i < count && i < overlay->max_items; ++i) {
        overlay_apply_point(overlay, i, &points[i]);
    }
    overlay->point_count = (count < overlay->max_items) ? count : overlay->max_items;
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

    if (!overlay_is_valid(overlay) || !overlay->texts) {
        return;
    }

    for (i = 0; i < overlay->max_items; ++i) {
        if (overlay->texts[i]) {
            ui_widget_set_visible(UI_WIDGET(overlay->texts[i]), false);
        }
    }

    if (!texts) {
        overlay->text_count = 0;
        return;
    }

    for (i = 0; i < count && i < overlay->max_items; ++i) {
        overlay_apply_text(overlay, i, &texts[i]);
    }
    overlay->text_count = (count < overlay->max_items) ? count : overlay->max_items;
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
 * @brief 清空所有点位与文本。
 *
 * @param overlay 目标组件句柄。
 * @return 无返回值。
 */
void overlay_clear(overlay_t* overlay) {
    uint16_t i;

    if (!overlay_is_valid(overlay)) {
        return;
    }

    for (i = 0; i < overlay->max_items; ++i) {
        if (overlay->points[i]) {
            lv_obj_add_flag(overlay->points[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (overlay->texts[i]) {
            ui_widget_set_visible(UI_WIDGET(overlay->texts[i]), false);
        }
    }
    overlay->point_count = 0;
    overlay->text_count = 0;
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
