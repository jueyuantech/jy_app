/**
 * @file paged_text.c
 * @brief 分页文本组件实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-22
 * @copyright JYTek
 */
#include "paged_text.h"

#include <string.h>

#include "lvgl/src/misc/lv_text_private.h"

/**
 * @brief 分页文本组件内部数据结构。
 */
struct paged_text_t {
    ui_widget_t base;
    label_t* label;                    ///< 内部文本组件。
    lv_obj_t* top_mask;                ///< 高亮窗口顶部半透明遮罩。
    lv_obj_t* bottom_mask;             ///< 高亮窗口底部半透明遮罩。
    lv_obj_t* highlight_box;           ///< 高亮窗口边框。
    int32_t text_inset;                ///< 内部文本相对高亮窗口的内缩距离。
    paged_text_step_mode_t step_mode;  ///< 当前翻页步进策略。
    uint8_t step_percent;              ///< 当前百分比步进值。
    const char* source_text;           ///< 原始文本指针，不归组件所有。
    uint32_t source_text_len;          ///< 原始文本字节长度。
    uint32_t* page_start_offsets;      ///< 每页起始 offset。
    uint32_t* page_end_offsets;        ///< 每页结束 offset。
    uint32_t page_count;               ///< 当前总页数。
    uint32_t current_page_idx;         ///< 当前页索引，从 0 开始。
    uint32_t lines_per_page;           ///< 当前每页可显示整行数。
};

/**
 * @brief 判断组件句柄及底层对象是否有效。
 *
 * @param paged_text 需要检查的组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static bool paged_text_handle_is_valid(paged_text_t* paged_text) {
    lv_obj_t* label_obj = NULL;

    if (paged_text == NULL) {
        return false;
    }

    label_obj = label_get_obj(paged_text->label);
    return paged_text->base.obj != NULL &&
           label_obj != NULL &&
           lv_obj_is_valid(paged_text->base.obj) &&
           lv_obj_is_valid(label_obj);
}

/**
 * @brief 判断高亮窗口对象是否已创建且有效。
 * @param paged_text 目标组件句柄。
 * @return `true` 表示高亮层可用。
 */
static bool paged_text_highlight_is_valid(paged_text_t* paged_text) {
    return paged_text_handle_is_valid(paged_text) &&
           paged_text->top_mask != NULL &&
           paged_text->bottom_mask != NULL &&
           paged_text->highlight_box != NULL &&
           lv_obj_is_valid(paged_text->top_mask) &&
           lv_obj_is_valid(paged_text->bottom_mask) &&
           lv_obj_is_valid(paged_text->highlight_box);
}

/**
 * @brief 隐藏指定高亮窗口层对象。
 * @param obj 高亮窗口层对象。
 * @return 无返回值。
 */
static void paged_text_hide_layer(lv_obj_t* obj) {
    if (obj != NULL && lv_obj_is_valid(obj)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 初始化高亮窗口单个层对象的基础属性。
 * @param obj 目标层对象。
 * @return 无返回值。
 */
static void paged_text_init_highlight_layer(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 按配置创建高亮窗口层。
 * @param paged_text 目标组件句柄。
 * @param cfg 高亮窗口配置。
 * @return `true` 表示创建成功或未启用，`false` 表示创建失败。
 */
static bool paged_text_create_highlight_layers(paged_text_t* paged_text,
                                               const paged_text_highlight_cfg_t* cfg) {
    lv_obj_t* root = NULL;

    if (paged_text == NULL || cfg == NULL || !cfg->enabled) {
        return true;
    }

    root = paged_text->base.obj;
    paged_text->top_mask = lv_obj_create(root);
    paged_text->bottom_mask = lv_obj_create(root);
    paged_text->highlight_box = lv_obj_create(root);
    if (paged_text->top_mask == NULL ||
        paged_text->bottom_mask == NULL ||
        paged_text->highlight_box == NULL) {
        return false;
    }

    paged_text_init_highlight_layer(paged_text->top_mask);
    paged_text_init_highlight_layer(paged_text->bottom_mask);
    paged_text_init_highlight_layer(paged_text->highlight_box);

    lv_obj_set_style_bg_color(paged_text->top_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_color(paged_text->bottom_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(paged_text->top_mask, cfg->mask_opa, 0);
    lv_obj_set_style_bg_opa(paged_text->bottom_mask, cfg->mask_opa, 0);
    lv_obj_set_style_bg_opa(paged_text->highlight_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(paged_text->highlight_box, lv_color_white(), 0);
    lv_obj_set_style_border_opa(paged_text->highlight_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(paged_text->highlight_box, (lv_coord_t)cfg->border_width, 0);
    lv_obj_set_style_radius(paged_text->highlight_box, (lv_coord_t)cfg->radius, 0);

    return true;
}

/**
 * @brief 获取页数，保证最小为 1。
 *
 * @param paged_text 目标组件句柄。
 * @return 返回总页数。
 */
static uint32_t paged_text_total_pages_cached(paged_text_t* paged_text) {
    if (paged_text == NULL || paged_text->page_count == 0) {
        return 1;
    }

    return paged_text->page_count;
}

/**
 * @brief 获取当前单行高度。
 *
 * @param paged_text 目标组件句柄。
 * @return 返回单行高度，最小为 1。
 */
static int32_t paged_text_get_line_height(paged_text_t* paged_text) {
    lv_obj_t* label_obj = NULL;
    const lv_font_t* font = NULL;
    int32_t line_height = 1;
    int32_t line_space = 0;

    if (!paged_text_handle_is_valid(paged_text)) {
        return line_height;
    }

    label_obj = label_get_obj(paged_text->label);
    font = lv_obj_get_style_text_font(label_obj, LV_PART_MAIN);
    if (font == NULL) {
        return line_height;
    }

    line_space = lv_obj_get_style_text_line_space(label_obj, LV_PART_MAIN);
    line_height = (int32_t)lv_font_get_line_height(font) + line_space;
    if (line_height <= 0) {
        line_height = 1;
    }

    return line_height;
}

/**
 * @brief 获取当前可视高度。
 *
 * @param paged_text 目标组件句柄。
 * @return 返回可视高度，最小为 1。
 */
static int32_t paged_text_get_view_height(paged_text_t* paged_text) {
    int32_t view_height = 1;

    if (!paged_text_handle_is_valid(paged_text)) {
        return view_height;
    }

    lv_obj_update_layout(paged_text->base.obj);
    view_height = (int32_t)lv_obj_get_height(paged_text->base.obj);
    if (view_height <= 0) {
        view_height = 1;
    }

    return view_height;
}

/**
 * @brief 获取当前可用文本宽度。
 *
 * @param paged_text 目标组件句柄。
 * @return 返回文本宽度。
 */
static int32_t paged_text_get_text_width(paged_text_t* paged_text) {
    lv_obj_t* label_obj = NULL;
    int32_t width = 0;

    if (!paged_text_handle_is_valid(paged_text)) {
        return 0;
    }

    label_obj = label_get_obj(paged_text->label);
    lv_obj_update_layout(label_obj);
    width = (int32_t)lv_obj_get_content_width(label_obj);
    if (width <= 0) {
        width = (int32_t)lv_obj_get_width(label_obj);
    }
    if (width < 0) {
        width = 0;
    }

    return width;
}

/**
 * @brief 释放分页 offset 数组。
 *
 * @param paged_text 目标组件句柄。
 */
static void paged_text_clear_offsets(paged_text_t* paged_text) {
    if (paged_text == NULL) {
        return;
    }

    if (paged_text->page_start_offsets != NULL) {
        lv_free(paged_text->page_start_offsets);
        paged_text->page_start_offsets = NULL;
    }
    if (paged_text->page_end_offsets != NULL) {
        lv_free(paged_text->page_end_offsets);
        paged_text->page_end_offsets = NULL;
    }
    paged_text->page_count = 0;
}

/**
 * @brief 追加一个 uint32_t 值到动态数组。
 *
 * @param items 动态数组地址。
 * @param count 当前元素数量地址。
 * @param value 待追加值。
 * @return `true` 表示追加成功。
 */
static bool paged_text_append_u32(uint32_t** items, uint32_t* count, uint32_t value) {
    uint32_t* next_items = NULL;

    if (items == NULL || count == NULL) {
        return false;
    }

    next_items = (uint32_t*)lv_realloc(*items, sizeof(uint32_t) * (*count + 1));
    if (next_items == NULL) {
        return false;
    }

    *items = next_items;
    (*items)[*count] = value;
    (*count)++;
    return true;
}

/**
 * @brief 追加一个页边界。
 *
 * @param paged_text 目标组件句柄。
 * @param start_offset 当前页起始 offset。
 * @param end_offset 当前页结束 offset。
 * @return `true` 表示追加成功。
 */
static bool paged_text_append_page(paged_text_t* paged_text,
                                   uint32_t start_offset,
                                   uint32_t end_offset) {
    uint32_t* start_offsets = NULL;
    uint32_t* end_offsets = NULL;

    if (paged_text == NULL) {
        return false;
    }

    start_offsets = (uint32_t*)lv_realloc(paged_text->page_start_offsets,
                                          sizeof(uint32_t) * (paged_text->page_count + 1));
    if (start_offsets == NULL) {
        return false;
    }
    paged_text->page_start_offsets = start_offsets;

    end_offsets = (uint32_t*)lv_realloc(paged_text->page_end_offsets,
                                        sizeof(uint32_t) * (paged_text->page_count + 1));
    if (end_offsets == NULL) {
        return false;
    }
    paged_text->page_end_offsets = end_offsets;

    paged_text->page_start_offsets[paged_text->page_count] = start_offset;
    paged_text->page_end_offsets[paged_text->page_count] = end_offset;
    paged_text->page_count++;
    return true;
}

/**
 * @brief 计算当前分页的行步进。
 *
 * @param paged_text 目标组件句柄。
 * @return 返回每次翻页前进的行数。
 */
static uint32_t paged_text_calc_step_lines(paged_text_t* paged_text) {
    uint32_t step_lines = paged_text->lines_per_page;

    if (step_lines == 0) {
        step_lines = 1;
    }

    if (paged_text->step_mode == PAGED_TEXT_STEP_VIEW_PERCENT) {
        step_lines = (step_lines * (uint32_t)paged_text->step_percent) / 100;
        if (step_lines == 0) {
            step_lines = 1;
        }
    }

    return step_lines;
}

/**
 * @brief 预计算分页边界。
 *
 * @param paged_text 目标组件句柄。
 * @return `true` 表示分页成功。
 */
static bool paged_text_paginate(paged_text_t* paged_text) {
    uint32_t offset = 0;
    uint32_t line_count = 0;
    uint32_t* line_offsets = NULL;
    uint32_t line_offset_count = 0;
    uint32_t start_line = 0;
    uint32_t step_lines = 1;
    int32_t width = 0;
    int32_t line_height = 1;
    int32_t view_height = 1;
    lv_obj_t* label_obj = NULL;
    const lv_font_t* font = NULL;
    int32_t letter_space = 0;

    if (!paged_text_handle_is_valid(paged_text)) {
        return false;
    }

    paged_text_clear_offsets(paged_text);
    paged_text->current_page_idx = 0;
    paged_text->lines_per_page = 1;

    label_obj = label_get_obj(paged_text->label);
    font = lv_obj_get_style_text_font(label_obj, LV_PART_MAIN);
    letter_space = lv_obj_get_style_text_letter_space(label_obj, LV_PART_MAIN);

    if (paged_text->source_text == NULL || paged_text->source_text_len == 0 || font == NULL) {
        return paged_text_append_page(paged_text, 0, 0);
    }

    width = paged_text_get_text_width(paged_text);
    if (width <= 0) {
        return paged_text_append_page(paged_text, 0, paged_text->source_text_len);
    }

    line_height = paged_text_get_line_height(paged_text);
    view_height = paged_text_get_view_height(paged_text);
    paged_text->lines_per_page = (uint32_t)(view_height / line_height);
    if (paged_text->lines_per_page == 0) {
        paged_text->lines_per_page = 1;
    }
    step_lines = paged_text_calc_step_lines(paged_text);

    if (!paged_text_append_u32(&line_offsets, &line_offset_count, 0)) {
        return false;
    }

    while (offset < paged_text->source_text_len) {
        uint32_t advance = lv_text_get_next_line(&paged_text->source_text[offset],
                                                 font,
                                                 letter_space,
                                                 width,
                                                 NULL,
                                                 LV_TEXT_FLAG_NONE);
        if (advance == 0) {
            offset++;
            continue;
        }

        offset += advance;
        line_count++;
        if (!paged_text_append_u32(&line_offsets, &line_offset_count, offset)) {
            lv_free(line_offsets);
            return false;
        }
    }

    if (line_count == 0) {
        lv_free(line_offsets);
        return paged_text_append_page(paged_text, 0, 0);
    }

    while (start_line < line_count) {
        uint32_t end_line = start_line + paged_text->lines_per_page;
        uint32_t start_offset = 0;
        uint32_t end_offset = 0;

        if (end_line > line_count) {
            end_line = line_count;
        }
        start_offset = line_offsets[start_line];
        end_offset = line_offsets[end_line];
        if (!paged_text_append_page(paged_text, start_offset, end_offset)) {
            lv_free(line_offsets);
            return false;
        }
        start_line += step_lines;
    }

    lv_free(line_offsets);
    return paged_text->page_count > 0;
}

/**
 * @brief 渲染当前页文本。
 *
 * @param paged_text 目标组件句柄。
 */
static void paged_text_render_current_page(paged_text_t* paged_text) {
    uint32_t total_pages = 1;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t page_len = 0;
    char* page_text = NULL;

    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    total_pages = paged_text_total_pages_cached(paged_text);
    if (paged_text->current_page_idx >= total_pages) {
        paged_text->current_page_idx = total_pages - 1;
    }

    if (paged_text->page_count > 0) {
        start = paged_text->page_start_offsets[paged_text->current_page_idx];
        end = paged_text->page_end_offsets[paged_text->current_page_idx];
    }
    if (end < start || start > paged_text->source_text_len) {
        start = 0;
        end = 0;
    }
    if (end > paged_text->source_text_len) {
        end = paged_text->source_text_len;
    }
    page_len = end - start;

    page_text = (char*)lv_malloc(page_len + 1);
    if (page_text == NULL) {
        return;
    }
    if (page_len > 0 && paged_text->source_text != NULL) {
        memcpy(page_text, &paged_text->source_text[start], page_len);
    }
    page_text[page_len] = '\0';
    label_set_text(paged_text->label, page_text);
    lv_free(page_text);
    lv_obj_set_y(label_get_obj(paged_text->label), (lv_coord_t)paged_text->text_inset);
}

/**
 * @brief LVGL 删除事件回调。
 *
 * @param e LVGL 事件对象。
 */
static void paged_text_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    paged_text_t* paged_text = (paged_text_t*)lv_obj_get_user_data(obj);

    if (paged_text == NULL) {
        return;
    }

    paged_text_clear_offsets(paged_text);
    lv_free(paged_text);
    lv_obj_set_user_data(obj, NULL);
}

/**
 * @brief LVGL 尺寸变化事件回调。
 *
 * @param e LVGL 事件对象。
 */
static void paged_text_on_size_changed(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    paged_text_t* paged_text = (paged_text_t*)lv_obj_get_user_data(obj);

    if (paged_text == NULL) {
        return;
    }

    paged_text_refresh(paged_text);
}

/**
 * @brief 获取分页文本组件默认配置。
 * @return 返回填充默认值的配置结构。
 */
paged_text_cfg_t paged_text_default_cfg(void) {
    paged_text_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.label = label_default_cfg();
    cfg.label.align = LABEL_ALIGN_LEFT;
    cfg.label.overflow = LABEL_OVERFLOW_WRAP;
    cfg.highlight.enabled = false;
    cfg.highlight.mask_opa = LV_OPA_60;
    cfg.highlight.border_width = 2;
    cfg.highlight.radius = 6;
    cfg.highlight.outset = 10;
    cfg.step_mode = PAGED_TEXT_STEP_LINE_PAGE;
    cfg.step_percent = 100;

    return cfg;
}

/**
 * @brief 创建分页文本组件。
 * @param[in] parent 父对象；传 `NULL` 时使用当前活动屏幕。
 * @param[in] cfg 分页文本配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回组件句柄，失败返回 `NULL`。
 */
paged_text_t* paged_text_create(lv_obj_t* parent, const paged_text_cfg_t* cfg) {
    paged_text_cfg_t default_cfg;
    label_cfg_t label_cfg;
    paged_text_t* paged_text = NULL;
    lv_obj_t* root = NULL;
    lv_obj_t* label_obj = NULL;
    int32_t label_inset = 0;

    if (parent == NULL) {
        parent = lv_screen_active();
    }
    if (parent == NULL) {
        return NULL;
    }

    default_cfg = paged_text_default_cfg();
    if (cfg == NULL) {
        cfg = &default_cfg;
    }

    root = lv_obj_create(parent);
    if (root == NULL) {
        return NULL;
    }

    paged_text = (paged_text_t*)lv_malloc(sizeof(paged_text_t));
    if (paged_text == NULL) {
        lv_obj_delete(root);
        return NULL;
    }

    label_cfg = label_default_cfg();
    label_cfg.w = LV_PCT(100);
    label_cfg.h = LV_PCT(100);
    label_cfg.overflow = LABEL_OVERFLOW_WRAP;
    label_cfg.align = cfg->label.align;
    label_cfg.max_lines = cfg->label.max_lines;
    label_cfg.font = cfg->label.font;
    label_cfg.opa = cfg->label.opa;
    label_cfg.pad_hor = cfg->label.pad_hor;
    label_cfg.pad_ver = cfg->label.pad_ver;
    if (cfg->highlight.enabled && cfg->highlight.outset > 0) {
        label_inset = cfg->highlight.outset;
        if (cfg->label.w > label_inset * 2) {
            label_cfg.x = label_inset;
            label_cfg.w = cfg->label.w - label_inset * 2;
        }
        if (cfg->label.h > label_inset * 2) {
            label_cfg.y = label_inset;
            label_cfg.h = cfg->label.h - label_inset * 2;
        }
    }

    memset(paged_text, 0, sizeof(*paged_text));
    ui_widget_init(&paged_text->base, root, UI_WIDGET_TYPE_PAGED_TEXT);
    paged_text->label = label_create(root, &label_cfg);
    if (paged_text->label == NULL) {
        lv_free(paged_text);
        lv_obj_delete(root);
        return NULL;
    }
    label_obj = label_get_obj(paged_text->label);

    paged_text->step_mode = cfg->step_mode;
    paged_text->step_percent = (cfg->step_percent == 0) ? 100 : cfg->step_percent;
    paged_text->text_inset = label_inset;
    paged_text->source_text = cfg->label.text ? cfg->label.text : "";
    paged_text->source_text_len = (uint32_t)strlen(paged_text->source_text);

    lv_obj_set_user_data(root, paged_text);
    lv_obj_add_event_cb(root, paged_text_on_delete, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(root, paged_text_on_size_changed, LV_EVENT_SIZE_CHANGED, NULL);

    lv_obj_remove_style_all(root);
    ui_widget_set_bounds(UI_WIDGET(paged_text), cfg->label.x, cfg->label.y, cfg->label.w, cfg->label.h);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_bg_opa(label_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label_obj, 0, 0);
    lv_obj_remove_flag(label_obj, LV_OBJ_FLAG_SCROLLABLE);

    if (!paged_text_create_highlight_layers(paged_text, &cfg->highlight)) {
        lv_obj_delete(root);
        return NULL;
    }
    paged_text_refresh(paged_text);

    return paged_text;
}

/**
 * @brief 设置分页源文本并重新分页。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] text 新源文本；组件不持有该指针。
 * @return 无返回值。
 */
void paged_text_set_text(paged_text_t* paged_text, const char* text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    paged_text->source_text = text ? text : "";
    paged_text->source_text_len = (uint32_t)strlen(paged_text->source_text);
    paged_text->current_page_idx = 0;
    paged_text_refresh(paged_text);
}

/**
 * @brief 直接显示当前窗口文本并跳过分页计算。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] text 当前窗口文本；传 `NULL` 时按空字符串处理。
 * @return 无返回值。
 */
void paged_text_set_visible_text(paged_text_t* paged_text, const char* text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    paged_text->source_text = text ? text : "";
    paged_text->source_text_len = (uint32_t)strlen(paged_text->source_text);
    paged_text->current_page_idx = 0;
    paged_text->lines_per_page = 1;
    paged_text_clear_offsets(paged_text);
    (void)paged_text_append_page(paged_text, 0, paged_text->source_text_len);
    label_set_text(paged_text->label, paged_text->source_text);
    lv_obj_set_y(label_get_obj(paged_text->label), (lv_coord_t)paged_text->text_inset);
}

/**
 * @brief 设置内部文本组件字体配置并重新分页。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] font_info 字体配置；传 `NULL` 时回退默认字体配置。
 * @return 无返回值。
 */
void paged_text_set_font_info(paged_text_t* paged_text, const app_font_info_t* font_info) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    label_set_font_info(paged_text->label, font_info);
    paged_text_refresh(paged_text);
}

/**
 * @brief 设置内部文本组件对齐方式并重新分页。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] align 文本对齐方式。
 * @return 无返回值。
 */
void paged_text_set_align(paged_text_t* paged_text, label_align_t align) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    label_set_align(paged_text->label, align);
    paged_text_refresh(paged_text);
}

/**
 * @brief 设置翻页步进策略并重新分页。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] mode 翻页步进策略。
 * @param[in] percent 可视高度百分比；传 0 时回退为 100。
 * @return 无返回值。
 */
void paged_text_set_step_mode(paged_text_t* paged_text,
                              paged_text_step_mode_t mode,
                              uint8_t percent) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    paged_text->step_mode = mode;
    paged_text->step_percent = (percent == 0) ? 100 : percent;
    paged_text_refresh(paged_text);
}

/**
 * @brief 按当前尺寸、字体和源文本重建分页。
 * @param[in] paged_text 目标分页文本组件。
 * @return 无返回值。
 */
void paged_text_refresh(paged_text_t* paged_text) {
    uint32_t old_idx = 0;
    uint32_t total_pages = 1;

    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    old_idx = paged_text->current_page_idx;
    if (!paged_text_paginate(paged_text)) {
        paged_text_clear_offsets(paged_text);
        (void)paged_text_append_page(paged_text, 0, 0);
    }

    total_pages = paged_text_total_pages_cached(paged_text);
    paged_text->current_page_idx = (old_idx < total_pages) ? old_idx : (total_pages - 1);
    paged_text_render_current_page(paged_text);
}

/**
 * @brief 跳转到第一页并渲染当前页。
 * @param[in] paged_text 目标分页文本组件。
 * @return 无返回值。
 */
void paged_text_page_init(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    paged_text->current_page_idx = 0;
    paged_text_render_current_page(paged_text);
}

/**
 * @brief 向上一页渲染。
 * @param[in] paged_text 目标分页文本组件。
 * @return `true` 表示页码发生变化，`false` 表示已在首页或组件无效。
 */
bool paged_text_page_up(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return false;
    }

    if (paged_text->current_page_idx == 0) {
        return false;
    }

    paged_text->current_page_idx--;
    paged_text_render_current_page(paged_text);
    return true;
}

/**
 * @brief 向下一页渲染。
 * @param[in] paged_text 目标分页文本组件。
 * @return `true` 表示页码发生变化，`false` 表示已在末页或组件无效。
 */
bool paged_text_page_down(paged_text_t* paged_text) {
    uint32_t total_pages = 1;

    if (!paged_text_handle_is_valid(paged_text)) {
        return false;
    }

    total_pages = paged_text_total_pages_cached(paged_text);
    if (paged_text->current_page_idx + 1 >= total_pages) {
        return false;
    }

    paged_text->current_page_idx++;
    paged_text_render_current_page(paged_text);
    return true;
}

/**
 * @brief 跳转到指定页索引并渲染。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] page_idx 目标页索引，从 0 开始。
 * @return `true` 表示跳转成功，`false` 表示索引越界或组件无效。
 */
bool paged_text_set_page(paged_text_t* paged_text, uint32_t page_idx) {
    uint32_t total_pages = 1;

    if (!paged_text_handle_is_valid(paged_text)) {
        return false;
    }

    total_pages = paged_text_total_pages_cached(paged_text);
    if (page_idx >= total_pages) {
        return false;
    }

    paged_text->current_page_idx = page_idx;
    paged_text_render_current_page(paged_text);
    return true;
}

/**
 * @brief 按源文本偏移渲染文本。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] offset 源文本中首个渲染字符的 UTF-8 字节偏移。
 * @return 无返回值。
 */
void paged_text_apply_text_offset(paged_text_t* paged_text, uint32_t offset) {
    lv_obj_t* label_obj = NULL;

    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    if (offset > paged_text->source_text_len) {
        offset = paged_text->source_text_len;
    }

    label_set_text(paged_text->label, paged_text->source_text + offset);
    label_obj = label_get_obj(paged_text->label);
    if (label_obj != NULL && lv_obj_is_valid(label_obj)) {
        lv_obj_set_y(label_obj, (lv_coord_t)paged_text->text_inset);
    }
}

/**
 * @brief 按文本视口坐标刷新上下遮罩和高亮框。
 * @param[in] paged_text 目标分页文本组件。
 * @param[in] top_mask_height 顶部遮罩底边 y 坐标，单位像素。
 * @param[in] bottom_mask_height 底部遮罩顶边 y 坐标，单位像素。
 * @return 无返回值。
 */
void paged_text_set_highlight_window(paged_text_t* paged_text,
                                     int32_t top_mask_height,
                                     int32_t bottom_mask_height) {
    int32_t view_w = 0;
    int32_t view_h = 0;
    int32_t top_mask_bottom = 0;
    int32_t bottom_mask_top = 0;
    int32_t bottom_mask_h = 0;
    int32_t highlight_h = 0;
    int32_t highlight_top = 0;
    int32_t highlight_bottom = 0;

    if (!paged_text_highlight_is_valid(paged_text)) {
        return;
    }

    lv_obj_update_layout(paged_text->base.obj);
    view_w = (int32_t)lv_obj_get_width(paged_text->base.obj);
    view_h = (int32_t)lv_obj_get_height(paged_text->base.obj);
    top_mask_bottom = LV_CLAMP(0, top_mask_height + paged_text->text_inset, view_h);
    bottom_mask_top = LV_CLAMP(0, bottom_mask_height + paged_text->text_inset, view_h);
    if (bottom_mask_top < top_mask_bottom) {
        bottom_mask_top = top_mask_bottom;
    }

    bottom_mask_h = view_h - bottom_mask_top;
    highlight_h = bottom_mask_top - top_mask_bottom;
    highlight_top = top_mask_bottom;
    highlight_bottom = bottom_mask_top;

    if (top_mask_bottom > 0) {
        lv_obj_set_pos(paged_text->top_mask, 0, 0);
        lv_obj_set_size(paged_text->top_mask, (lv_coord_t)view_w, (lv_coord_t)top_mask_bottom);
        lv_obj_remove_flag(paged_text->top_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(paged_text->top_mask);
    } else {
        paged_text_hide_layer(paged_text->top_mask);
    }

    if (bottom_mask_h > 0) {
        lv_obj_set_pos(paged_text->bottom_mask, 0, (lv_coord_t)bottom_mask_top);
        lv_obj_set_size(paged_text->bottom_mask, (lv_coord_t)view_w, (lv_coord_t)bottom_mask_h);
        lv_obj_remove_flag(paged_text->bottom_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(paged_text->bottom_mask);
    } else {
        paged_text_hide_layer(paged_text->bottom_mask);
    }

    if (highlight_h > 0) {
        lv_obj_set_pos(paged_text->highlight_box,
                       0,
                       (lv_coord_t)highlight_top);
        lv_obj_set_size(paged_text->highlight_box,
                        (lv_coord_t)view_w,
                        (lv_coord_t)(highlight_bottom - highlight_top));
        lv_obj_remove_flag(paged_text->highlight_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(paged_text->highlight_box);
    } else {
        paged_text_hide_layer(paged_text->highlight_box);
    }
}

/**
 * @brief 隐藏上下遮罩和高亮框。
 * @param[in] paged_text 目标分页文本组件。
 * @return 无返回值。
 */
void paged_text_hide_highlight_window(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return;
    }

    paged_text_hide_layer(paged_text->top_mask);
    paged_text_hide_layer(paged_text->bottom_mask);
    paged_text_hide_layer(paged_text->highlight_box);
}

/**
 * @brief 获取当前页码。
 * @param[in] paged_text 目标分页文本组件。
 * @return 返回当前页码，从 1 开始；组件无效时返回 1。
 */
uint32_t paged_text_get_current_page(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return 1;
    }

    return paged_text->current_page_idx + 1;
}

/**
 * @brief 获取当前页索引。
 * @param[in] paged_text 目标分页文本组件。
 * @return 返回当前页索引，从 0 开始；组件无效时返回 0。
 */
uint32_t paged_text_get_current_page_index(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return 0;
    }

    return paged_text->current_page_idx;
}

/**
 * @brief 获取总页数。
 * @param[in] paged_text 目标分页文本组件。
 * @return 返回总页数，最小为 1。
 */
uint32_t paged_text_get_total_pages(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return 1;
    }

    return paged_text_total_pages_cached(paged_text);
}

/**
 * @brief 获取内部文本相对分页文本视口边缘的内缩距离。
 * @param[in] paged_text 目标分页文本组件。
 * @return 返回内部文本内缩距离；组件无效时返回 0。
 */
int32_t paged_text_get_text_inset(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return 0;
    }

    return paged_text->text_inset;
}

/**
 * @brief 获取分页文本组件根对象。
 * @param[in] paged_text 目标分页文本组件。
 * @return 返回根对象指针；组件无效时返回 `NULL`。
 */
lv_obj_t* paged_text_get_obj(paged_text_t* paged_text) {
    if (!paged_text_handle_is_valid(paged_text)) {
        return NULL;
    }

    return paged_text->base.obj;
}
