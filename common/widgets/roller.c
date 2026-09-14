#include "roller.h"

#include <string.h>

#include "app_def.h"
#include "system/system_res.h"

/**
 * @brief 滚轮组件内部数据结构。
 */
struct roller_t {
    ui_widget_t base;                             ///< 统一组件基类。
    char** items;                                 ///< 内部持有的文本数组副本。
    uint32_t count;                               ///< 当前选项数量。
    uint32_t selected;                            ///< 当前选中项索引。
    roller_cfg_t cfg;                             ///< 当前生效配置缓存。
    roller_overflow_mode_t overflow_mode;         ///< 当前项文本溢出处理方式。
    roller_selected_cb_t on_selected_changed;     ///< 选中变化回调。
    roller_activate_cb_t on_activate;             ///< 激活回调。
    void* callback_user_data;                     ///< 回调透传数据。
    label_t* label_prev;                          ///< 上一项文本组件。
    label_t* label_cur;                           ///< 当前项文本组件。
    label_t* label_next;                          ///< 下一项文本组件。
    label_t* label_hint;                          ///< 可选的底部操作提示文本组件。
    int32_t pad;                                  ///< 当前项上下内边距缓存。
    int32_t gap;                                  ///< 行间距缓存。

    /* -------- 旧接口兼容运行态：仅在 roller_widget_* 边界做签名桥接 -------- */
    roller_widget_selected_cb_t legacy_on_selected_changed; ///< 旧接口选中变化回调。
    roller_widget_activate_cb_t legacy_on_activate;         ///< 旧接口激活回调。
    const lv_font_t* font_normal;                           ///< 旧接口非选中项字体覆盖。
    const lv_font_t* font_selected;                         ///< 旧接口当前项字体覆盖。
    bool legacy_font_override;                              ///< 是否仍由旧接口字体参数接管文本字体。
};

/**
 * @brief 判断滚轮句柄及底层对象是否有效。
 *
 * @param roller 目标滚轮组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool roller_is_valid(roller_t* roller) {
    return roller && ui_widget_is_valid(UI_WIDGET(roller))
           && roller->label_prev && roller->label_cur && roller->label_next;
}

/**
 * @brief 通过底层对象解析滚轮组件句柄。
 *
 * @param obj 滚轮底层对象。
 * @return 成功返回滚轮组件句柄，失败返回 `NULL`。
 */
static roller_t* roller_from_obj(lv_obj_t* obj) {
    roller_t* roller = NULL;

    if (!obj || !lv_obj_is_valid(obj)) {
        return NULL;
    }

    roller = (roller_t*)lv_obj_get_user_data(obj);
    return roller_is_valid(roller) ? roller : NULL;
}

/**
 * @brief 释放内部持有的文本数组。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 无返回值。
 */
static void roller_free_items(roller_t* roller) {
    uint32_t i = 0;

    if (!roller || !roller->items) {
        return;
    }

    for (i = 0; i < roller->count; ++i) {
        if (roller->items[i]) {
            lv_free(roller->items[i]);
        }
    }

    lv_free(roller->items);
    roller->items = NULL;
    roller->count = 0;
    roller->selected = 0;
}

/**
 * @brief 复制一份新的选项文本数组。
 *
 * @param items 文本数组。
 * @param count 文本数量。
 * @return 成功返回新数组，失败返回 `NULL`。
 */
static char** roller_dup_items(const char** items, uint32_t count) {
    char** copy = NULL;
    uint32_t i = 0;

    if (!items || count == 0) {
        return NULL;
    }

    copy = (char**)lv_malloc(sizeof(char*) * count);
    if (!copy) {
        return NULL;
    }
    memset(copy, 0, sizeof(char*) * count);

    for (i = 0; i < count; ++i) {
        const char* src = items[i] ? items[i] : "";
        size_t len = strlen(src);

        copy[i] = (char*)lv_malloc(len + 1);
        if (!copy[i]) {
            uint32_t j = 0;
            for (j = 0; j < i; ++j) {
                if (copy[j]) {
                    lv_free(copy[j]);
                }
            }
            lv_free(copy);
            return NULL;
        }

        memcpy(copy[i], src, len + 1);
    }

    return copy;
}

/**
 * @brief 获取默认滚轮配置。
 *
 * @return 返回填充默认值后的滚轮配置结构体。
 */
roller_cfg_t roller_default_cfg(void) {
    roller_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.items = NULL;
    cfg.count = 0;
    cfg.label = label_default_cfg();
    cfg.label.align = LABEL_ALIGN_CENTER;
    cfg.overflow_mode = ROLLER_OVERFLOW_SCROLL;
    cfg.row_height = 0;
    cfg.row_gap = -1;
    cfg.selected_pad_ver = 2;
    cfg.radius = 12;
    cfg.border_width = 2;
    cfg.opa_normal = LV_OPA_70;
    cfg.opa_selected = LV_OPA_100;
    cfg.show_hint = false;
    cfg.hint_gap = 48;

    return cfg;
}

/**
 * @brief 获取当前承载选中项的文本槽位。
 *
 * 两个选项时，两个文本固定显示在上下槽位，选中样式在槽位间移动；
 * 其他数量仍由中间槽位承载当前选中项。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回当前选中项对应的文本组件。
 */
static label_t* roller_get_selected_slot(roller_t* roller) {
    if (!roller_is_valid(roller)) {
        return NULL;
    }

    if (roller->count == 2 && (roller->selected % roller->count) == 0) {
        return roller->label_prev;
    }

    return roller->label_cur;
}

/**
 * @brief 应用三个文本区域的样式与字体。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 无返回值。
 */
static void roller_apply_label_styles(roller_t* roller) {
    label_t* labels[3];
    label_t* selected_label = NULL;
    uint32_t i = 0;

    if (!roller_is_valid(roller)) {
        return;
    }

    labels[0] = roller->label_prev;
    labels[1] = roller->label_cur;
    labels[2] = roller->label_next;
    selected_label = roller_get_selected_slot(roller);

    for (i = 0; i < 3; ++i) {
        bool selected = labels[i] == selected_label;
        lv_obj_t* label_obj = label_get_obj(labels[i]);

        lv_obj_set_style_bg_color(label_obj, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(label_obj, LV_OPA_COVER, 0);
        label_set_radius(labels[i], roller->cfg.radius);
        lv_obj_set_style_border_color(label_obj, lv_color_white(), 0);
        lv_obj_set_style_text_color(label_obj, lv_color_white(), 0);
        label_set_align(labels[i], roller->cfg.label.align);
        label_set_border_width(labels[i], selected ? roller->cfg.border_width : 0);
        label_set_opacity(labels[i],
                          selected ? roller->cfg.opa_selected : roller->cfg.opa_normal);
        obj_set_text_font(label_obj,
                          selected ? roller->font_selected : roller->font_normal);
        lv_label_set_long_mode(
            label_obj,
            selected && roller->overflow_mode == ROLLER_OVERFLOW_SCROLL
                ? LV_LABEL_LONG_SCROLL_CIRCULAR
                : LV_LABEL_LONG_CLIP);
    }
}

/**
 * @brief 刷新三个文本区域的显示内容。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 无返回值。
 */
static void roller_update_labels(roller_t* roller) {
    uint32_t sel = 0;
    uint32_t prev = 0;
    uint32_t next = 0;

    if (!roller_is_valid(roller) || roller->count == 0) {
        if (roller_is_valid(roller)) {
            label_set_text(roller->label_prev, "");
            label_set_text(roller->label_cur, "");
            label_set_text(roller->label_next, "");
            ui_widget_set_visible(UI_WIDGET(roller->label_prev), false);
            ui_widget_set_visible(UI_WIDGET(roller->label_cur), false);
            ui_widget_set_visible(UI_WIDGET(roller->label_next), false);
        }
        return;
    }

    if (roller->count == 2) {
        label_set_text(roller->label_prev, roller->items[0]);
        label_set_text(roller->label_cur, roller->items[1]);
        label_set_text(roller->label_next, "");
        ui_widget_set_visible(UI_WIDGET(roller->label_prev), true);
        ui_widget_set_visible(UI_WIDGET(roller->label_cur), true);
        ui_widget_set_visible(UI_WIDGET(roller->label_next), false);
    } else {
        sel = roller->selected % roller->count;
        prev = (sel + roller->count - 1) % roller->count;
        next = (sel + 1) % roller->count;

        label_set_text(roller->label_cur, roller->items[sel]);
        ui_widget_set_visible(UI_WIDGET(roller->label_cur), true);

        if (roller->count >= 3) {
            label_set_text(roller->label_prev, roller->items[prev]);
            label_set_text(roller->label_next, roller->items[next]);
            ui_widget_set_visible(UI_WIDGET(roller->label_prev), true);
            ui_widget_set_visible(UI_WIDGET(roller->label_next), true);
        } else {
            label_set_text(roller->label_prev, "");
            label_set_text(roller->label_next, "");
            ui_widget_set_visible(UI_WIDGET(roller->label_prev), false);
            ui_widget_set_visible(UI_WIDGET(roller->label_next), false);
        }
    }

    roller_apply_label_styles(roller);
}

/**
 * @brief 计算固定行高下实际使用的行高。
 *
 * @param configured_height 调用方配置的行高；小于等于 0 表示按内容自适应。
 * @param line_height 当前字体行高。
 * @param pad_ver 上下留白。
 * @param border_width 边框宽度。
 * @return 返回可容纳字体与装饰的实际行高。
 */
static int32_t roller_resolve_row_height(int32_t configured_height,
                                         int32_t line_height,
                                         int32_t pad_ver,
                                         int32_t border_width) {
    int32_t min_height = line_height;

    if (pad_ver > 0) {
        min_height += pad_ver * 2;
    }
    if (border_width > 0) {
        min_height += border_width * 2;
    }

    if (configured_height > min_height) {
        return configured_height;
    }

    return min_height;
}

/**
 * @brief 为固定高度槽位应用垂直居中所需的内边距。
 *
 * @param label 目标文本组件。
 * @param font 当前槽位字体。
 * @param height 槽位高度。
 * @param base_pad 基础上下留白。
 * @param border_width 当前边框宽度。
 * @return 无返回值。
 */
static void roller_apply_slot_padding(label_t* label,
                                      const lv_font_t* font,
                                      int32_t height,
                                      int32_t base_pad,
                                      int32_t border_width) {
    int32_t line_height = 0;
    int32_t available = 0;
    int32_t pad = base_pad;
    lv_obj_t* label_obj = label_get_obj(label);

    if (!label_obj || !font) {
        return;
    }

    line_height = (int32_t)lv_font_get_line_height(font);
    available = height - (border_width * 2) - (base_pad * 2);
    if (available > line_height) {
        pad += (available - line_height) / 2;
    }

    lv_obj_set_style_pad_top(label_obj, (lv_coord_t)pad, 0);
    lv_obj_set_style_pad_bottom(label_obj, (lv_coord_t)pad, 0);
}

/**
 * @brief 刷新滚轮内部布局。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 无返回值。
 */
static void roller_update_layout(roller_t* roller) {
    int32_t width = 0;
    int32_t lh_normal = 0;
    int32_t lh_selected = 0;
    int32_t h_cur = 0;
    int32_t h_normal = 0;
    int32_t h_slot = 0;
    int32_t offset = 0;
    int32_t rows = 0;
    int32_t height = 0;
    int32_t main_height = 0;
    int32_t main_offset = 0;
    int32_t hint_height = 0;
    int32_t hint_gap = 0;
    bool show_prev = false;
    bool show_next = false;
    bool show_hint = false;
    bool two_item_mode = false;
    label_t* selected_label = NULL;
    lv_obj_t* obj = NULL;

    if (!roller_is_valid(roller)) {
        return;
    }

    obj = roller->base.obj;
    width = (int32_t)lv_obj_get_width(obj);

    ui_widget_set_size(UI_WIDGET(roller->label_prev), width, LV_SIZE_CONTENT);
    ui_widget_set_size(UI_WIDGET(roller->label_cur), width, LV_SIZE_CONTENT);
    ui_widget_set_size(UI_WIDGET(roller->label_next), width, LV_SIZE_CONTENT);

    lh_normal = roller->font_normal ? (int32_t)lv_font_get_line_height(roller->font_normal) : 0;
    lh_selected = roller->font_selected ? (int32_t)lv_font_get_line_height(roller->font_selected) : lh_normal;
    roller->pad = roller->cfg.selected_pad_ver >= 0 ? roller->cfg.selected_pad_ver : 0;
    roller->gap = roller->cfg.row_gap >= 0 ? roller->cfg.row_gap : (lh_normal / 3);

    h_cur = roller_resolve_row_height(roller->cfg.row_height,
                                      lh_selected,
                                      roller->pad,
                                      roller->cfg.border_width);
    h_normal = roller_resolve_row_height(roller->cfg.row_height, lh_normal, 0, 0);
    h_slot = h_cur > h_normal ? h_cur : h_normal;
    offset = h_cur / 2 + h_normal / 2 + roller->gap;
    two_item_mode = roller->count == 2;
    selected_label = roller_get_selected_slot(roller);

    if (roller->cfg.row_height > 0 || two_item_mode) {
        ui_widget_set_size(UI_WIDGET(roller->label_cur),
                           width,
                           two_item_mode ? h_slot : h_cur);
        ui_widget_set_size(UI_WIDGET(roller->label_prev),
                           width,
                           two_item_mode ? h_slot : h_normal);
        ui_widget_set_size(UI_WIDGET(roller->label_next), width, h_normal);

        roller_apply_slot_padding(
            roller->label_cur,
            selected_label == roller->label_cur ? roller->font_selected : roller->font_normal,
            two_item_mode ? h_slot : h_cur,
            selected_label == roller->label_cur ? roller->pad : 0,
            selected_label == roller->label_cur ? roller->cfg.border_width : 0);
        roller_apply_slot_padding(
            roller->label_prev,
            selected_label == roller->label_prev ? roller->font_selected : roller->font_normal,
            two_item_mode ? h_slot : h_normal,
            selected_label == roller->label_prev ? roller->pad : 0,
            selected_label == roller->label_prev ? roller->cfg.border_width : 0);
        roller_apply_slot_padding(roller->label_next,
                                  roller->font_normal,
                                  h_normal,
                                  0,
                                  0);
    } else {
        ui_widget_set_size(UI_WIDGET(roller->label_cur), width, LV_SIZE_CONTENT);
        ui_widget_set_size(UI_WIDGET(roller->label_prev), width, LV_SIZE_CONTENT);
        ui_widget_set_size(UI_WIDGET(roller->label_next), width, LV_SIZE_CONTENT);
        label_set_padding(roller->label_cur,
                          0,
                          selected_label == roller->label_cur ? roller->pad : 0);
        label_set_padding(roller->label_prev,
                          0,
                          selected_label == roller->label_prev ? roller->pad : 0);
        label_set_padding(roller->label_next,
                          0,
                          selected_label == roller->label_next ? roller->pad : 0);
    }

    /* 水平留白统一使用文本配置，非居中选项补齐选中框占用，避免切换时文字横跳。 */
    {
        label_t* labels[] = {roller->label_prev, roller->label_cur, roller->label_next};
        for (uint32_t i = 0; i < 3; ++i) {
            int32_t pad_hor = roller->cfg.label.pad_hor;
            if (roller->cfg.label.align != LABEL_ALIGN_CENTER &&
                labels[i] != selected_label && roller->cfg.border_width > 0) {
                pad_hor += roller->cfg.border_width;
            }
            lv_obj_set_style_pad_left(label_get_obj(labels[i]), pad_hor, 0);
            lv_obj_set_style_pad_right(label_get_obj(labels[i]), pad_hor, 0);
        }
    }

    show_prev = !ui_widget_is_hidden(UI_WIDGET(roller->label_prev));
    show_next = !ui_widget_is_hidden(UI_WIDGET(roller->label_next));
    show_hint = roller->label_hint != NULL;

    rows = 1 + (show_prev ? 1 : 0) + (show_next ? 1 : 0);
    main_height = two_item_mode
                      ? (2 * h_slot + roller->gap)
                      : ((rows == 1) ? h_cur
                                     : (rows == 2 ? (h_cur + h_normal + roller->gap)
                                                  : (h_cur + 2 * (h_normal + roller->gap))));
    /* 带提示的滚轮始终保留三槽位高度，避免少于三项时提示上浮。 */
    if (show_hint && rows < 3) {
        main_height = 3 * h_slot + 2 * roller->gap;
    }
    height = main_height;

    if (show_hint) {
        lv_obj_t* hint_obj = label_get_obj(roller->label_hint);

        ui_widget_set_size(UI_WIDGET(roller->label_hint), width, LV_SIZE_CONTENT);
        lv_obj_update_layout(hint_obj);
        hint_height = (int32_t)lv_obj_get_height(hint_obj);
        hint_gap = roller->cfg.hint_gap > 0 ? roller->cfg.hint_gap : 0;
        main_offset = -(hint_gap + hint_height) / 2;
        height += hint_gap + hint_height;
    }

    lv_obj_set_height(obj, (lv_coord_t)height);

    if (two_item_mode) {
        int32_t slot_offset = (h_slot + roller->gap) / 2;
        lv_obj_align(label_get_obj(roller->label_prev),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset - slot_offset);
        lv_obj_align(label_get_obj(roller->label_cur),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset + slot_offset);
    } else if (show_prev && show_next) {
        lv_obj_align(label_get_obj(roller->label_cur), LV_ALIGN_CENTER, 0, main_offset);
        lv_obj_align(label_get_obj(roller->label_prev),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset - offset);
        lv_obj_align(label_get_obj(roller->label_next),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset + offset);
    } else if (show_prev && !show_next) {
        int32_t y_prev = -(h_cur + roller->gap) / 2;
        int32_t y_cur = (h_normal + roller->gap) / 2;
        lv_obj_align(label_get_obj(roller->label_prev),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset + y_prev);
        lv_obj_align(label_get_obj(roller->label_cur),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset + y_cur);
    } else if (!show_prev && show_next) {
        int32_t y_cur = -(h_normal + roller->gap) / 2;
        int32_t y_next = (h_cur + roller->gap) / 2;
        lv_obj_align(label_get_obj(roller->label_cur),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset + y_cur);
        lv_obj_align(label_get_obj(roller->label_next),
                     LV_ALIGN_CENTER,
                     0,
                     main_offset + y_next);
    } else {
        lv_obj_align(label_get_obj(roller->label_cur), LV_ALIGN_CENTER, 0, main_offset);
    }

    if (show_hint) {
        lv_obj_align(label_get_obj(roller->label_hint),
                     LV_ALIGN_CENTER,
                     0,
                     (main_height + hint_gap) / 2);
    }
    lv_obj_move_foreground(label_get_obj(selected_label));
}

/**
 * @brief 通知外部当前选中项变化。
 *
 * 新旧回调是两套互斥接口；新接口优先，回调执行后不再访问滚轮实例。
 *
 * @param roller 目标滚轮组件句柄。
 * @param notify 是否触发通知。
 * @return 无返回值。
 */
static void roller_notify_selected_changed(roller_t* roller, bool notify) {
    uint32_t selected = 0;

    if (!roller_is_valid(roller) || !notify || roller->count == 0) {
        return;
    }

    selected = roller->selected % roller->count;

    if (roller->on_selected_changed) {
        roller->on_selected_changed(roller, selected, roller->callback_user_data);
        return;
    }
    if (roller->legacy_on_selected_changed) {
        roller->legacy_on_selected_changed(roller->base.obj, selected, roller->callback_user_data);
    }
}

/**
 * @brief 内部设置当前选中项并决定是否通知外部。
 *
 * @param roller 目标滚轮组件句柄。
 * @param index 目标索引。
 * @param notify 是否通知外部。
 * @return 无返回值。
 */
static void roller_set_selected_internal(roller_t* roller, uint32_t index, bool notify) {
    if (!roller_is_valid(roller) || roller->count == 0) {
        return;
    }

    roller->selected = index % roller->count;
    roller_update_labels(roller);
    roller_update_layout(roller);
    roller_notify_selected_changed(roller, notify);
}

/**
 * @brief 滚轮删除事件回调。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void roller_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    roller_t* roller = (roller_t*)lv_obj_get_user_data(obj);

    if (!roller) {
        return;
    }

    roller_free_items(roller);
    lv_free(roller);
    lv_obj_set_user_data(obj, NULL);
}

/**
 * @brief 滚轮尺寸变化事件回调。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void roller_on_size_changed(lv_event_t* e) {
    roller_t* roller = roller_from_obj(lv_event_get_target(e));

    if (!roller) {
        return;
    }

    roller_update_layout(roller);
}

/**
 * @brief 应用可选底部操作提示的样式和文案。
 *
 * @param roller 目标滚轮组件句柄。
 * @param cfg 当前滚轮配置。
 * @return 无返回值。
 */
static void roller_apply_hint_cfg(roller_t* roller, const roller_cfg_t* cfg) {
    label_cfg_t hint_cfg;

    if (!roller || !cfg) {
        return;
    }

    if (!cfg->show_hint) {
        if (roller->label_hint) {
            ui_widget_destroy(UI_WIDGET(roller->label_hint));
            roller->label_hint = NULL;
        }
        return;
    }

    if (!roller->label_hint) {
        roller->label_hint = label_create(roller->base.obj, NULL);
        if (!roller->label_hint) {
            LV_LOG_ERROR("roller create hint failed");
            return;
        }
    }

    hint_cfg = label_default_cfg();
    hint_cfg.w = LV_PCT(100);
    hint_cfg.h = LV_SIZE_CONTENT;
    hint_cfg.opa = cfg->opa_selected;
    hint_cfg.align = LABEL_ALIGN_CENTER;
    hint_cfg.overflow = LABEL_OVERFLOW_CLIP;
    hint_cfg.font = cfg->label.font;
    hint_cfg.text = app_get_str("ROLLER_OPERATION_HINT");
    label_apply_cfg(roller->label_hint, &hint_cfg);
}

/**
 * @brief 创建滚轮组件。
 *
 * @param parent 父对象；传 `NULL` 时使用当前活动屏幕。
 * @param cfg 滚轮配置；传 `NULL` 时使用默认配置。
 * @return 成功返回滚轮组件句柄，失败返回 `NULL`。
 */
roller_t* roller_create(lv_obj_t* parent, const roller_cfg_t* cfg) {
    roller_cfg_t default_cfg;
    roller_t* roller = NULL;
    lv_obj_t* obj = NULL;

    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return NULL;
    }

    default_cfg = roller_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }
    if (!cfg->items || cfg->count == 0) {
        LV_LOG_ERROR("roller create invalid params");
        return NULL;
    }

    obj = lv_obj_create(parent);
    if (!obj) {
        return NULL;
    }

    roller = (roller_t*)lv_malloc(sizeof(roller_t));
    if (!roller) {
        lv_obj_delete(obj);
        return NULL;
    }
    memset(roller, 0, sizeof(*roller));

    roller->items = roller_dup_items(cfg->items, cfg->count);
    if (!roller->items) {
        lv_free(roller);
        lv_obj_delete(obj);
        return NULL;
    }

    ui_widget_init(&roller->base, obj, UI_WIDGET_TYPE_ROLLER);
    roller->count = cfg->count;
    roller->selected = 0;
    roller->overflow_mode = cfg->overflow_mode;
    roller->cfg = *cfg;
    roller->font_normal = get_system_font();
    roller->font_selected = get_system_font();

    lv_obj_set_user_data(obj, roller);
    lv_obj_add_event_cb(obj, roller_on_delete, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(obj, roller_on_size_changed, LV_EVENT_SIZE_CHANGED, NULL);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    ui_widget_set_size(UI_WIDGET(roller), LV_PCT(66), 0);
    ui_widget_set_visible(UI_WIDGET(roller), true);

    roller->label_prev = label_create(obj, NULL);
    roller->label_cur = label_create(obj, NULL);
    roller->label_next = label_create(obj, NULL);
    if (cfg->show_hint) {
        roller->label_hint = label_create(obj, NULL);
    }
    if (!roller->label_prev || !roller->label_cur || !roller->label_next
        || (cfg->show_hint && !roller->label_hint)) {
        lv_obj_delete(obj);
        return NULL;
    }

    roller_apply_cfg(roller, cfg);
    return roller;
}

/**
 * @brief 销毁滚轮组件。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 无返回值。
 */
void roller_destroy(roller_t* roller) {
    if (!roller_is_valid(roller)) {
        return;
    }

    ui_widget_destroy(UI_WIDGET(roller));
}

/**
 * @brief 批量应用滚轮配置。
 *
 * 这是新接口的主配置入口；一旦重新应用 `roller_cfg_t`，旧接口暂存的字体覆盖会失效，
 * 后续文本样式完全由 `cfg.label` 和其派生样式接管。
 *
 * @param roller 目标滚轮组件句柄。
 * @param cfg 配置结构；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
void roller_apply_cfg(roller_t* roller, const roller_cfg_t* cfg) {
    roller_cfg_t default_cfg;
    label_cfg_t label_cfg;

    if (!roller_is_valid(roller)) {
        return;
    }

    default_cfg = roller_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    roller->overflow_mode = cfg->overflow_mode;
    label_cfg = cfg->label;
    label_cfg.text = label_get_text(roller->label_prev);
    label_apply_cfg(roller->label_prev, &label_cfg);
    label_cfg.text = label_get_text(roller->label_cur);
    if (cfg->selected_font.weight > 0) {
        label_cfg.font = cfg->selected_font;
    }
    label_apply_cfg(roller->label_cur, &label_cfg);
    label_cfg.text = label_get_text(roller->label_next);
    label_cfg.font = cfg->label.font;
    label_apply_cfg(roller->label_next, &label_cfg);
    roller->font_normal = lv_obj_get_style_text_font(label_get_obj(roller->label_prev), LV_PART_MAIN);
    if (!roller->font_normal) {
        roller->font_normal = get_system_font();
    }
    roller->font_selected = lv_obj_get_style_text_font(label_get_obj(roller->label_cur), LV_PART_MAIN);
    if (!roller->font_selected) {
        roller->font_selected = roller->font_normal;
    }
    roller->legacy_font_override = false;
    roller->cfg = *cfg;
    roller_apply_hint_cfg(roller, cfg);
    roller_update_labels(roller);
    roller_update_layout(roller);
}

/**
 * @brief 设置滚轮回调。
 *
 * @param roller 目标滚轮组件句柄。
 * @param on_selected_changed 选中项变化回调。
 * @param on_activate 当前项激活回调。
 * @param user_data 用户透传数据。
 * @return 无返回值。
 */
void roller_set_callbacks(roller_t* roller,
                          roller_selected_cb_t on_selected_changed,
                          roller_activate_cb_t on_activate,
                          void* user_data) {
    if (!roller_is_valid(roller)) {
        return;
    }

    roller->on_selected_changed = on_selected_changed;
    roller->on_activate = on_activate;
    roller->callback_user_data = user_data;
}

/**
 * @brief 处理滚轮输入事件。
 *
 * @param roller 目标滚轮组件句柄。
 * @param code 事件码。
 * @return `true` 表示事件已处理，`false` 表示未处理。
 */
bool roller_key_handler(roller_t* roller, lv_event_code_t code) {
    uint32_t selected = 0;

    if (!roller_is_valid(roller) || roller->count == 0) {
        return false;
    }

    if (code == LV_EVENT_GESTURE_LEFT) {
        if (roller->count > 1) {
            roller_set_selected_internal(roller, roller->selected + roller->count + 1, true);
        }
        return true;
    }
    if (code == LV_EVENT_GESTURE_RIGHT) {
        if (roller->count > 1) {
            roller_set_selected_internal(
                roller,
                roller->selected + roller->count - 1,
                true);
        }
        return true;
    }
    if (code == LV_EVENT_CLICKED) {
        selected = roller->selected % roller->count;
        if (roller->on_activate) {
            roller->on_activate(roller, selected, code, roller->callback_user_data);
            return true;
        }
        if (roller->legacy_on_activate) {
            roller->legacy_on_activate(roller->base.obj, selected, code, roller->callback_user_data);
        }
        return true;
    }

    return false;
}

/**
 * @brief 更新滚轮选项列表。
 *
 * @param roller 目标滚轮组件句柄。
 * @param items 文本数组。
 * @param count 文本数量。
 * @return 无返回值。
 */
void roller_set_items(roller_t* roller, const char** items, uint32_t count) {
    char** copy = NULL;

    if (!roller_is_valid(roller)) {
        return;
    }

    if (!items || count == 0) {
        roller_free_items(roller);
        roller_update_labels(roller);
        roller_update_layout(roller);
        return;
    }

    copy = roller_dup_items(items, count);
    if (!copy) {
        return;
    }

    roller_free_items(roller);
    roller->items = copy;
    roller->count = count;
    roller->selected = 0;
    roller_update_labels(roller);
    roller_update_layout(roller);
    roller_notify_selected_changed(roller, true);
}

/**
 * @brief 设置当前选中项。
 *
 * @param roller 目标滚轮组件句柄。
 * @param index 目标索引。
 * @param anim 是否带动画；当前实现保留但不执行动画。
 * @return 无返回值。
 */
void roller_set_selected(roller_t* roller, uint32_t index, bool anim) {
    (void)anim;
    roller_set_selected_internal(roller, index, true);
}

/**
 * @brief 获取当前选中项索引。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回当前选中项索引；无效时返回 `0`。
 */
uint32_t roller_get_selected(roller_t* roller) {
    if (!roller_is_valid(roller)) {
        return 0;
    }

    return roller->selected;
}

/**
 * @brief 获取当前选中项文本。
 *
 * @param roller 目标滚轮组件句柄。
 * @param buf 输出缓冲区。
 * @param size 缓冲区大小。
 * @return 无返回值。
 */
void roller_get_selected_text(roller_t* roller, char* buf, uint32_t size) {
    const char* text = NULL;

    if (!buf || size == 0) {
        return;
    }

    buf[0] = '\0';

    if (!roller_is_valid(roller) || roller->count == 0) {
        return;
    }

    text = roller->items[roller->selected % roller->count];
    strncpy(buf, text ? text : "", size - 1);
    buf[size - 1] = '\0';
}

/**
 * @brief 获取滚轮当前选项总数。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回当前选项总数；无效时返回 `0`。
 */
uint32_t roller_get_count(roller_t* roller) {
    if (!roller_is_valid(roller)) {
        return 0;
    }

    return roller->count;
}

/**
 * @brief 获取滚轮底层 LVGL 对象。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回底层对象指针；无效时返回 `NULL`。
 */
lv_obj_t* roller_get_obj(roller_t* roller) {
    if (!roller_is_valid(roller)) {
        return NULL;
    }

    return ui_widget_get_obj(UI_WIDGET(roller));
}

/**
 * @brief 获取滚轮内部当前项文本组件。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回当前项文本组件句柄；无效时返回 `NULL`。
 */
label_t* roller_get_selected_label(roller_t* roller) {
    if (!roller_is_valid(roller)) {
        return NULL;
    }

    return roller_get_selected_slot(roller);
}


/* -------- 兼容旧接口：roller_widget_*（薄包装，内部仍复用 roller_* 主实现） -------- */

/**
 * @brief 兼容旧版接口创建滚轮组件。
 *
 * 先走 `roller_create()` 建立新接口对象，再把旧签名中的字体参数写回兼容运行态。
 *
 * @param parent 父对象。
 * @param items 文本数组。
 * @param count 文本数量。
 * @param font_normal 非选中项字体。
 * @param font_selected 当前项字体。
 * @param overflow_mode 当前项溢出模式。
 * @return 返回滚轮底层对象指针。
 */
lv_obj_t* roller_widget_create(lv_obj_t* parent,
                               const char** items,
                               uint32_t count,
                               const lv_font_t* font_normal,
                               const lv_font_t* font_selected,
                               roller_overflow_mode_t overflow_mode) {
    roller_cfg_t cfg = roller_default_cfg();
    roller_t* roller = NULL;

    cfg.items = items;
    cfg.count = count;
    cfg.overflow_mode = overflow_mode;

    roller = roller_create(parent, &cfg);
    if (roller) {
        roller->font_normal = font_normal ? font_normal : get_system_font();
        roller->font_selected = font_selected ? font_selected : roller->font_normal;
        roller->legacy_font_override = true;
        roller_update_labels(roller);
        roller_update_layout(roller);
    }
    return roller ? roller_get_obj(roller) : NULL;
}

/**
 * @brief 兼容旧版接口设置样式。
 *
 * 这里只做旧样式字段到 `roller->cfg` 的映射，不保留独立的旧样式副本。
 *
 * @param roller_obj 滚轮底层对象。
 * @param style 旧版样式结构体。
 * @return 无返回值。
 */
void roller_widget_set_style(lv_obj_t* roller_obj, const roller_widget_style_t* style) {
    roller_t* roller = roller_from_obj(roller_obj);

    if (!roller) {
        return;
    }

    if (!style) {
        roller->cfg.row_height = 0;
        roller->cfg.row_gap = -1;
        roller->cfg.selected_pad_ver = 2;
        roller->cfg.radius = roller_default_cfg().radius;
        roller->cfg.border_width = 2;
        roller->cfg.opa_normal = LV_OPA_70;
        roller->cfg.opa_selected = LV_OPA_100;
    } else {
        roller->cfg.row_height = style->row_height;
        roller->cfg.row_gap = style->row_gap;
        roller->cfg.selected_pad_ver = style->selected_pad_ver;
        roller->cfg.radius = style->radius;
        roller->cfg.border_width = style->border_width;
        roller->cfg.opa_normal = style->text_opa_normal > 0 ? style->text_opa_normal : style->border_opa_normal;
        roller->cfg.opa_selected = style->text_opa_selected > 0 ? style->text_opa_selected : style->border_opa_selected;
    }
    roller_update_labels(roller);
    roller_update_layout(roller);
}

/**
 * @brief 兼容旧版接口设置回调。
 *
 * 兼容层只保存旧签名回调指针；真正的触发时机仍由新接口主实现统一驱动。
 *
 * @param roller_obj 滚轮底层对象。
 * @param on_selected_changed 旧版选中变化回调。
 * @param on_activate 旧版激活回调。
 * @param user_data 用户透传数据。
 * @return 无返回值。
 */
void roller_widget_set_callbacks(lv_obj_t* roller_obj,
                                 roller_widget_selected_cb_t on_selected_changed,
                                 roller_widget_activate_cb_t on_activate,
                                 void* user_data) {
    roller_t* roller = roller_from_obj(roller_obj);

    if (!roller) {
        return;
    }

    roller->legacy_on_selected_changed = on_selected_changed;
    roller->legacy_on_activate = on_activate;
    roller->callback_user_data = user_data;
}

/**
 * @brief 兼容旧版接口处理输入事件。
 *
 * @param roller_obj 滚轮底层对象。
 * @param code 事件码。
 * @return `true` 表示事件已处理，`false` 表示未处理。
 */
bool roller_widget_key_handler(lv_obj_t* roller_obj, lv_event_code_t code) {
    roller_t* roller = roller_from_obj(roller_obj);

    return roller ? roller_key_handler(roller, code) : false;
}

/**
 * @brief 兼容旧版接口更新文本列表。
 *
 * @param roller_obj 滚轮底层对象。
 * @param items 文本数组。
 * @param count 文本数量。
 * @return 无返回值。
 */
void roller_widget_set_items(lv_obj_t* roller_obj, const char** items, uint32_t count) {
    roller_t* roller = roller_from_obj(roller_obj);

    if (!roller) {
        return;
    }

    roller_set_items(roller, items, count);
}

/**
 * @brief 兼容旧版接口设置当前选中项。
 *
 * @param roller_obj 滚轮底层对象。
 * @param index 目标索引。
 * @param anim 是否带动画。
 * @return 无返回值。
 */
void roller_widget_set_selected(lv_obj_t* roller_obj, uint32_t index, bool anim) {
    roller_t* roller = roller_from_obj(roller_obj);

    if (!roller) {
        return;
    }

    roller_set_selected(roller, index, anim);
}

/**
 * @brief 兼容旧版接口获取当前选中项。
 *
 * @param roller_obj 滚轮底层对象。
 * @return 返回当前选中项索引。
 */
uint32_t roller_widget_get_selected(lv_obj_t* roller_obj) {
    roller_t* roller = roller_from_obj(roller_obj);

    return roller ? roller_get_selected(roller) : 0;
}

/**
 * @brief 兼容旧版接口获取当前选中项文本。
 *
 * @param roller_obj 滚轮底层对象。
 * @param buf 输出缓冲区。
 * @param size 缓冲区大小。
 * @return 无返回值。
 */
void roller_widget_get_selected_text(lv_obj_t* roller_obj, char* buf, uint32_t size) {
    roller_t* roller = roller_from_obj(roller_obj);

    if (!roller) {
        return;
    }

    roller_get_selected_text(roller, buf, size);
}

/**
 * @brief 兼容旧版接口获取当前选项数量。
 *
 * @param roller_obj 滚轮底层对象。
 * @return 返回当前选项数量。
 */
uint32_t roller_widget_get_count(lv_obj_t* roller_obj) {
    roller_t* roller = roller_from_obj(roller_obj);

    return roller ? roller_get_count(roller) : 0;
}
