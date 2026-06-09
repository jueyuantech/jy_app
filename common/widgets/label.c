#include "label.h"

#include <string.h>

#include "container.h"
#include "system/system_res.h"

/**
 * @brief 文本组件内部数据结构。
 */
struct label_t {
    ui_widget_t base;
    const lv_font_t* font;       ///< 当前组件绑定的系统注册字体；未绑定时为 `NULL`。
    uint32_t max_lines;           ///< 最大显示行数；0 表示不限制。
    lv_area_t clip_area_backup;   ///< 绘制裁剪前的原始裁剪区域。
    bool clip_area_backup_valid;  ///< 是否已保存原始裁剪区域。
};

/**
 * @brief 判断组件句柄及底层对象是否有效。
 *
 * @param label 需要检查的组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static bool label_handle_is_valid(label_t* label) {
    return label && label->base.obj && lv_obj_is_valid(label->base.obj);
}

/**
 * @brief 通知父容器当前文本组件布局尺寸已变化。
 *
 * @param label 目标组件句柄。
 * @return 无返回值。
 */
static void label_notify_layout_changed(label_t* label) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    container_notify_child_layout_changed(label->base.obj);
}

/**
 * @brief 当文本组件布局尺寸真实变化时通知父容器刷新布局。
 *
 * @param label 目标组件句柄。
 * @param old_width 文本更新前的组件宽度。
 * @param old_height 文本更新前的组件高度。
 * @return 无返回值。
 */
static void label_notify_layout_changed_if_size_changed(label_t* label,
                                                        lv_coord_t old_width,
                                                        lv_coord_t old_height) {
    lv_coord_t new_width = 0;
    lv_coord_t new_height = 0;

    if (!label_handle_is_valid(label)) {
        return;
    }

    lv_obj_update_layout(label->base.obj);
    new_width = lv_obj_get_width(label->base.obj);
    new_height = lv_obj_get_height(label->base.obj);

    if (new_width != old_width || new_height != old_height) {
        label_notify_layout_changed(label);
    }
}

/**
 * @brief 将封装层对齐枚举转换为 LVGL 对齐枚举。
 *
 * @param align 封装层对齐方式。
 * @return 返回 LVGL 文本对齐枚举值。
 */
static lv_text_align_t label_to_lv_align(label_align_t align) {
    switch (align) {
    case LABEL_ALIGN_LEFT:
        return LV_TEXT_ALIGN_LEFT;
    case LABEL_ALIGN_RIGHT:
        return LV_TEXT_ALIGN_RIGHT;
    case LABEL_ALIGN_CENTER:
    default:
        return LV_TEXT_ALIGN_CENTER;
    }
}

/**
 * @brief 将封装层溢出策略转换为 LVGL 长文本模式。
 *
 * @param overflow 封装层溢出处理方式。
 * @return 返回 LVGL 长文本模式枚举值。
 */
static lv_label_long_mode_t label_to_lv_long_mode(label_overflow_t overflow) {
    switch (overflow) {
    case LABEL_OVERFLOW_WRAP:
        return LV_LABEL_LONG_WRAP;
    case LABEL_OVERFLOW_SCROLL:
        return LV_LABEL_LONG_SCROLL;
    case LABEL_OVERFLOW_SCROLL_CIRCULAR:
        return LV_LABEL_LONG_SCROLL_CIRCULAR;
    case LABEL_OVERFLOW_CLIP:
    default:
        return LV_LABEL_LONG_CLIP;
    }
}

/**
 * @brief 清理组件内部缓存的字体引用。
 *
 * @param label 目标组件句柄。
 * @return 无返回值。
 */
static void label_release_font(label_t* label) {
    if (!label) {
        return;
    }

    label->font = NULL;
}

/**
 * @brief 求两个区域的交集。
 *
 * @param[out] result 交集输出区域。
 * @param[in] a 第一个区域。
 * @param[in] b 第二个区域。
 * @return `true` 表示存在交集，`false` 表示无交集。
 */
static bool label_area_intersect(lv_area_t* result, const lv_area_t* a, const lv_area_t* b) {
    result->x1 = LV_MAX(a->x1, b->x1);
    result->y1 = LV_MAX(a->y1, b->y1);
    result->x2 = LV_MIN(a->x2, b->x2);
    result->y2 = LV_MIN(a->y2, b->y2);

    return result->x1 <= result->x2 && result->y1 <= result->y2;
}

/**
 * @brief 在最大行数限制下裁剪文本绘制区域。
 *
 * LVGL 的 `LV_LABEL_LONG_WRAP` 会按内容继续向下绘制；`max_height` 只限制布局尺寸，
 * 这里在绘制期间把裁剪区域临时收敛到 label 自身，避免超出最大行数的内容露出。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void label_clip_draw_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_current_target(e);
    label_t* label = (label_t*)lv_obj_get_user_data(obj);
    lv_layer_t* layer = NULL;
    lv_area_t obj_area;
    lv_area_t clipped_area;

    if (!label || label->max_lines == 0) {
        return;
    }

    layer = lv_event_get_layer(e);
    if (!layer) {
        return;
    }

    if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
        label->clip_area_backup = layer->_clip_area;
        label->clip_area_backup_valid = false;
        lv_obj_get_coords(obj, &obj_area);
        if (label_area_intersect(&clipped_area, &layer->_clip_area, &obj_area)) {
            layer->_clip_area = clipped_area;
            label->clip_area_backup_valid = true;
        }
    } else if (code == LV_EVENT_DRAW_MAIN_END && label->clip_area_backup_valid) {
        layer->_clip_area = label->clip_area_backup;
        label->clip_area_backup_valid = false;
    }
}

/**
 * @brief 根据当前字体和行距刷新最大显示行数限制。
 *
 * @param label 目标组件句柄。
 * @return 无返回值。
 */
static void label_apply_max_lines(label_t* label) {
    const lv_font_t* font = NULL;
    int32_t line_space = 0;
    int32_t max_height = 0;
    int32_t space_top = 0;
    int32_t space_bottom = 0;

    if (!label_handle_is_valid(label)) {
        return;
    }

    if (label->max_lines == 0) {
        lv_obj_set_style_max_height(label->base.obj, LV_COORD_MAX, 0);
        return;
    }

    font = lv_obj_get_style_text_font(label->base.obj, LV_PART_MAIN);
    if (!font) {
        lv_obj_set_style_max_height(label->base.obj, LV_COORD_MAX, 0);
        return;
    }

    line_space = lv_obj_get_style_text_line_space(label->base.obj, LV_PART_MAIN);
    max_height = (int32_t)lv_font_get_line_height(font) * (int32_t)label->max_lines;
    if (label->max_lines > 1) {
        max_height += line_space * ((int32_t)label->max_lines - 1);
    }
    space_top = lv_obj_get_style_space_top(label->base.obj, LV_PART_MAIN);
    space_bottom = lv_obj_get_style_space_bottom(label->base.obj, LV_PART_MAIN);
    max_height += space_top + space_bottom;

    lv_obj_set_style_max_height(label->base.obj, (lv_coord_t)max_height, 0);
}

/**
 * @brief 根据字体配置更新组件字体、字间距和行间距。
 *
 * 统一复用 `system_res` 的系统注册字体；非法或未配置字号时回退为系统默认字体。
 *
 * @param label 目标组件句柄。
 * @param font_info 字体配置；传 `NULL` 时回退为系统默认字体。
 * @return 无返回值。
 */
static void label_apply_font(label_t* label, const app_font_info_t* font_info) {
    const lv_font_t* font = get_system_font();

    if (!label_handle_is_valid(label)) {
        return;
    }

    label_release_font(label);

    if (font_info && app_fontsize_valid((int32_t)font_info->weight)) {
        label->font = get_font_by_size_near(font_info->weight);
        if (label->font != NULL) {
            font = label->font;
        }
    }

    if (font) {
        label->font = font;
        lv_obj_set_style_text_font(label->base.obj, font, 0);
    }

    lv_obj_set_style_text_letter_space(label->base.obj,
                                       font_info ? (int32_t)font_info->wordSpace : 0,
                                       0);
    lv_obj_set_style_text_line_space(label->base.obj,
                                     font_info ? (int32_t)font_info->rowSpace : 0,
                                     0);
}

/**
 * @brief LVGL 删除事件回调。
 *
 * 组件被删除时负责释放内部持有的字体资源和句柄内存。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void label_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    label_t* label = (label_t*)lv_obj_get_user_data(obj);

    if (!label) {
        return;
    }

    label_release_font(label);
    lv_free(label);
    lv_obj_set_user_data(obj, NULL);
}

/**
 * @brief 获取默认配置。
 *
 * @return 返回填充默认值后的配置结构体。
 */
label_cfg_t label_default_cfg(void) {
    label_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.x = 0;
    cfg.y = 0;
    cfg.w = LV_SIZE_CONTENT;
    cfg.h = LV_SIZE_CONTENT;
    cfg.radius = 0;
    cfg.border_width = 0;
    cfg.pad_hor = 0;
    cfg.pad_ver = 0;
    cfg.opa = LV_OPA_COVER;
    cfg.align = LABEL_ALIGN_CENTER;
    cfg.overflow = LABEL_OVERFLOW_CLIP;
    cfg.max_lines = 0;
    cfg.text = "";

    return cfg;
}

/**
 * @brief 创建文本组件。
 *
 * @param parent 父对象；为空时使用当前活动屏幕。
 * @param cfg 组件配置；为空时使用默认配置。
 * @return 成功返回组件句柄，失败返回 `NULL`。
 */
label_t* label_create(lv_obj_t* parent, const label_cfg_t* cfg) {
    label_cfg_t default_cfg;
    label_t* label = NULL;
    lv_obj_t* obj = NULL;

    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return NULL;
    }

    default_cfg = label_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    obj = lv_label_create(parent);
    if (!obj) {
        return NULL;
    }

    label = (label_t*)lv_malloc(sizeof(label_t));
    if (!label) {
        lv_obj_delete(obj);
        return NULL;
    }

    memset(label, 0, sizeof(*label));
    ui_widget_init(&label->base, obj, UI_WIDGET_TYPE_LABEL);

    lv_obj_set_user_data(obj, label);
    lv_obj_add_event_cb(obj, label_on_delete, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(obj,
                        label_clip_draw_event_cb,
                        LV_EVENT_DRAW_MAIN_BEGIN | LV_EVENT_PREPROCESS,
                        NULL);
    lv_obj_add_event_cb(obj, label_clip_draw_event_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    label_apply_cfg(label, cfg);

    return label;
}

/**
 * @brief 使用默认配置和文本快速创建文本组件。
 *
 * @param parent 父对象；为空时使用当前活动屏幕。
 * @param text 初始文本；为空时按空字符串处理。
 * @return 成功返回组件句柄，失败返回 `NULL`。
 */
label_t* label_create_from_text(lv_obj_t* parent, const char* text) {
    label_cfg_t cfg = label_default_cfg();

    cfg.text = text;
    return label_create(parent, &cfg);
}

/**
 * @brief 使用默认配置、文本和字号快速创建文本组件。
 *
 * @param parent 父对象；为空时使用当前活动屏幕。
 * @param text 初始文本；为空时按空字符串处理。
 * @param font_size 文本字号。
 * @return 成功返回组件句柄，失败返回 `NULL`。
 */
label_t* label_create_from_text_and_font_size(lv_obj_t* parent, const char* text, int32_t font_size) {
    label_cfg_t cfg = label_default_cfg();

    cfg.text = text;
    if (font_size > 0) {
        cfg.font.weight = (uint32_t)font_size;
    }

    return label_create(parent, &cfg);
}

/**
 * @brief 设置显示文本。
 *
 * @param label 目标组件句柄。
 * @param text 新文本；为空时按空字符串处理。
 * @return 无返回值。
 */
void label_set_text(label_t* label, const char* text) {
    const char* new_text = text ? text : "";
    lv_coord_t old_width = 0;
    lv_coord_t old_height = 0;

    if (!label_handle_is_valid(label)) {
        return;
    }

    if (strcmp(lv_label_get_text(label->base.obj), new_text) == 0) {
        return;
    }

    lv_obj_update_layout(label->base.obj);
    old_width = lv_obj_get_width(label->base.obj);
    old_height = lv_obj_get_height(label->base.obj);

    lv_label_set_text(label->base.obj, new_text);
    label_notify_layout_changed_if_size_changed(label, old_width, old_height);
}

/**
 * @brief 获取当前显示文本。
 *
 * @param label 目标组件句柄。
 * @return 返回当前文本；组件无效时返回空字符串。
 */
const char* label_get_text(label_t* label) {
    if (!label_handle_is_valid(label)) {
        return "";
    }

    return lv_label_get_text(label->base.obj);
}

/**
 * @brief 在当前文本末尾追加内容。
 *
 * @param label 目标组件句柄。
 * @param text 待追加文本。
 * @return 无返回值。
 */
void label_append_text(label_t* label, const char* text) {
    if (!label_handle_is_valid(label) || text == NULL || text[0] == '\0') {
        return;
    }

    lv_label_ins_text(label->base.obj, LV_LABEL_POS_LAST, text);
    label_notify_layout_changed(label);
}

/**
 * @brief 设置组件内边距。
 *
 * @param label 目标组件句柄。
 * @param pad_hor 左右内边距。
 * @param pad_ver 上下内边距。
 * @return 无返回值。
 */
void label_set_padding(label_t* label, int32_t pad_hor, int32_t pad_ver) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    lv_obj_set_style_pad_hor(label->base.obj, (lv_coord_t)pad_hor, 0);
    lv_obj_set_style_pad_ver(label->base.obj, (lv_coord_t)pad_ver, 0);
}

/**
 * @brief 设置圆角。
 *
 * @param label 目标组件句柄。
 * @param radius 圆角半径。
 * @return 无返回值。
 */
void label_set_radius(label_t* label, int32_t radius) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    lv_obj_set_style_radius(label->base.obj, (lv_coord_t)radius, 0);
}

/**
 * @brief 设置边框宽度。
 *
 * @param label 目标组件句柄。
 * @param border_width 边框宽度。
 * @return 无返回值。
 */
void label_set_border_width(label_t* label, int32_t border_width) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    lv_obj_set_style_border_width(label->base.obj, (lv_coord_t)border_width, 0);
}

/**
 * @brief 设置文字透明度。
 *
 * @param label 目标组件句柄。
 * @param opa 透明度。
 * @return 无返回值。
 */
void label_set_opacity(label_t* label, uint8_t opa) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    lv_obj_set_style_text_opa(label->base.obj, (lv_opa_t)opa, 0);
    lv_obj_set_style_border_opa(label->base.obj, (lv_opa_t)opa, 0);
}

/**
 * @brief 设置文本对齐方式。
 *
 * @param label 目标组件句柄。
 * @param align 对齐方式。
 * @return 无返回值。
 */
void label_set_align(label_t* label, label_align_t align) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    lv_obj_set_style_text_align(label->base.obj, label_to_lv_align(align), 0);
}

/**
 * @brief 设置文本溢出处理方式。
 *
 * @param label 目标组件句柄。
 * @param overflow 溢出处理方式。
 * @return 无返回值。
 */
void label_set_overflow(label_t* label, label_overflow_t overflow) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    lv_label_set_long_mode(label->base.obj, label_to_lv_long_mode(overflow));
}

/**
 * @brief 设置最大显示行数。
 *
 * @param label 目标组件句柄。
 * @param max_lines 最大显示行数；0 表示不限制。
 * @return 无返回值。
 */
void label_set_max_lines(label_t* label, uint32_t max_lines) {
    if (!label_handle_is_valid(label)) {
        return;
    }

    label->max_lines = max_lines;
    label_apply_max_lines(label);
    label_notify_layout_changed(label);
}

/**
 * @brief 设置字体信息。
 *
 * @param label 目标组件句柄。
 * @param font_info 字体配置。
 * @return 无返回值。
 */
void label_set_font_info(label_t* label, const app_font_info_t* font_info) {
    label_apply_font(label, font_info);
    label_apply_max_lines(label);
    label_notify_layout_changed(label);
}

/**
 * @brief 获取组件父对象。
 *
 * @param label 目标组件句柄。
 * @return 返回父对象；组件无效时返回 `NULL`。
 */
lv_obj_t* label_get_parent(label_t* label) {
    if (!label_handle_is_valid(label)) {
        return NULL;
    }

    return lv_obj_get_parent(label->base.obj);
}

/**
 * @brief 按配置结构批量刷新组件状态。
 *
 * @param label 目标组件句柄。
 * @param cfg 配置结构；为空时使用默认配置。
 * @return 无返回值。
 */
void label_apply_cfg(label_t* label, const label_cfg_t* cfg) {
    label_cfg_t default_cfg;

    if (!label_handle_is_valid(label)) {
        return;
    }

    default_cfg = label_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    label_set_text(label, cfg->text);
    ui_widget_set_bounds(UI_WIDGET(label), cfg->x, cfg->y, cfg->w, cfg->h);
    label_set_padding(label, cfg->pad_hor, cfg->pad_ver);
    label_set_radius(label, cfg->radius);
    label_set_border_width(label, cfg->border_width);
    lv_obj_set_style_text_color(label->base.obj, lv_color_white(), 0);
    lv_obj_set_style_bg_color(label->base.obj, lv_color_black(), 0);
    lv_obj_set_style_border_color(label->base.obj, lv_color_white(), 0);
    label_set_opacity(label, cfg->opa);
    lv_obj_set_style_bg_opa(label->base.obj, LV_OPA_COVER, 0);
    label_set_align(label, cfg->align);
    label_set_overflow(label, cfg->overflow);
    label_set_font_info(label, &cfg->font);
    label_set_max_lines(label, cfg->max_lines);
}

/**
 * @brief 获取底层 LVGL 对象。
 *
 * @param label 目标组件句柄。
 * @return 返回底层对象指针；无效时返回 `NULL`。
 */
lv_obj_t* label_get_obj(label_t* label) {
    if (!label_handle_is_valid(label)) {
        return NULL;
    }

    return label->base.obj;
}
