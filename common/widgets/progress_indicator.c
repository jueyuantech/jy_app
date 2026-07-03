/**
 * @file progress_indicator.c
 * @brief 图文状态提示组件实现。
 */
#include "progress_indicator.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "floatair_fs.h"
#include "system/system_def.h"
#include "ui_res.h"

/**
 * @brief 图文状态提示组件内部数据结构。
 */
struct progress_indicator_t {
    ui_widget_t base;  ///< 统一组件基类。
    img_t* icon;       ///< 图标组件。
    label_t* text;     ///< 提示文案组件。
    int32_t gap;       ///< 图标与文字之间的垂直间距。
};

/**
 * @brief 判断图文状态提示组件句柄及底层对象是否有效。
 *
 * @param indicator 目标组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static bool progress_indicator_is_valid(progress_indicator_t* indicator) {
    return indicator && ui_widget_is_valid(UI_WIDGET(indicator));
}

/**
 * @brief 同步内部图标和文字布局。
 *
 * @param indicator 目标组件句柄。
 * @return 无返回值。
 */
static void progress_indicator_sync_layout(progress_indicator_t* indicator) {
    if (!progress_indicator_is_valid(indicator)) {
        return;
    }

    lv_obj_set_layout(indicator->base.obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(indicator->base.obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(indicator->base.obj,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(indicator->base.obj, (lv_coord_t)indicator->gap, 0);
}

/**
 * @brief LVGL 删除事件回调。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void progress_indicator_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    progress_indicator_t* indicator = (progress_indicator_t*)lv_obj_get_user_data(obj);

    if (!indicator) {
        return;
    }

    lv_free(indicator);
    lv_obj_set_user_data(obj, NULL);
}

progress_indicator_cfg_t progress_indicator_default_cfg(void) {
    progress_indicator_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.x = 0;
    cfg.y = 0;
    cfg.w = LV_SIZE_CONTENT;
    cfg.h = LV_SIZE_CONTENT;
    cfg.gap = 10;
    cfg.icon = img_default_cfg();
    cfg.icon.w = LVGL_UI_ICONW_80;
    cfg.icon.h = LVGL_UI_ICONH_80;
    cfg.icon.src = UI_RES_IMAGE_CONNECTING;
    cfg.text = label_default_cfg();
    cfg.text.text = "0%";
    cfg.text.align = LABEL_ALIGN_CENTER;
    cfg.text.overflow = LABEL_OVERFLOW_CLIP;
    cfg.text.opa = LV_OPA_COVER;

    return cfg;
}

progress_indicator_t* progress_indicator_create(lv_obj_t* parent,
                                                const progress_indicator_cfg_t* cfg) {
    progress_indicator_cfg_t default_cfg;
    progress_indicator_t* indicator = NULL;
    lv_obj_t* obj = NULL;

    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return NULL;
    }

    default_cfg = progress_indicator_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    obj = lv_obj_create(parent);
    if (!obj) {
        return NULL;
    }

    indicator = (progress_indicator_t*)lv_malloc(sizeof(progress_indicator_t));
    if (!indicator) {
        lv_obj_delete(obj);
        return NULL;
    }

    memset(indicator, 0, sizeof(*indicator));
    ui_widget_init(&indicator->base, obj, UI_WIDGET_TYPE_PROGRESS_INDICATOR);
    lv_obj_set_user_data(obj, indicator);
    lv_obj_add_event_cb(obj, progress_indicator_on_delete, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    indicator->icon = img_create(obj, &cfg->icon);
    if (indicator->icon == NULL) {
        lv_obj_delete(obj);
        return NULL;
    }

    indicator->text = label_create(obj, &cfg->text);
    if (indicator->text == NULL) {
        lv_obj_delete(obj);
        return NULL;
    }

    progress_indicator_apply_cfg(indicator, cfg);
    return indicator;
}

void progress_indicator_apply_cfg(progress_indicator_t* indicator,
                                  const progress_indicator_cfg_t* cfg) {
    progress_indicator_cfg_t default_cfg;

    if (!progress_indicator_is_valid(indicator)) {
        return;
    }

    default_cfg = progress_indicator_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    indicator->gap = cfg->gap;
    ui_widget_set_bounds(UI_WIDGET(indicator), cfg->x, cfg->y, cfg->w, cfg->h);
    if (indicator->icon != NULL) {
        img_apply_cfg(indicator->icon, &cfg->icon);
    }
    if (indicator->text != NULL) {
        label_apply_cfg(indicator->text, &cfg->text);
    }
    progress_indicator_sync_layout(indicator);
}

void progress_indicator_set_text(progress_indicator_t* indicator, const char* text) {
    if (!progress_indicator_is_valid(indicator) || indicator->text == NULL) {
        return;
    }

    label_set_text(indicator->text, text ? text : "");
    progress_indicator_sync_layout(indicator);
}

void progress_indicator_set_text_fmt(progress_indicator_t* indicator, const char* fmt, ...) {
    char text[128];
    va_list args;

    if (!progress_indicator_is_valid(indicator) || indicator->text == NULL) {
        return;
    }

    if (fmt == NULL) {
        progress_indicator_set_text(indicator, "");
        return;
    }

    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    progress_indicator_set_text(indicator, text);
}

void progress_indicator_set_text_font_info(progress_indicator_t* indicator,
                                           const app_font_info_t* font_info) {
    if (!progress_indicator_is_valid(indicator) || indicator->text == NULL) {
        return;
    }

    label_set_font_info(indicator->text, font_info);
    progress_indicator_sync_layout(indicator);
}

void progress_indicator_set_icon_visible(progress_indicator_t* indicator, bool visible) {
    if (!progress_indicator_is_valid(indicator) || indicator->icon == NULL) {
        return;
    }

    img_set_opacity(indicator->icon, visible ? LV_OPA_COVER : LV_OPA_TRANSP);
}

void progress_indicator_set_percent(progress_indicator_t* indicator, int32_t percent) {
    char text[8];

    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    snprintf(text, sizeof(text), "%d%%", (int)percent);
    progress_indicator_set_text(indicator, text);
}

lv_obj_t* progress_indicator_get_obj(progress_indicator_t* indicator) {
    if (!progress_indicator_is_valid(indicator)) {
        return NULL;
    }

    return indicator->base.obj;
}
