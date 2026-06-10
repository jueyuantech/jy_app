#include "button.h"

#include <string.h>

/**
 * @brief 按钮组件内部数据结构。
 */
struct button_t {
    ui_widget_t base;
    label_t* label;  ///< 按钮内部文本组件。
};

/**
 * @brief 判断按钮组件及底层对象是否有效。
 *
 * @param button 目标按钮句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static bool button_is_valid(button_t* button) {
    return button && button->base.obj && lv_obj_is_valid(button->base.obj);
}

/**
 * @brief 按钮删除事件回调。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void button_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    button_t* button = (button_t*)lv_obj_get_user_data(obj);

    if (!button) {
        return;
    }

    lv_free(button);
    lv_obj_set_user_data(obj, NULL);
}

/**
 * @brief 按钮尺寸变化后重新居中文本。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void button_on_size_changed(lv_event_t* e) {
    button_t* button = (button_t*)lv_event_get_user_data(e);

    if (!button_is_valid(button) || !button->label) {
        return;
    }

    lv_obj_center(label_get_obj(button->label));
}
/**
 * @brief 获取默认按钮配置。
 *
 * @return 返回填充默认值后的配置结构体。
 */
button_cfg_t button_default_cfg(void) {
    button_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.x = 0;
    cfg.y = 0;
    cfg.w = 120;
    cfg.h = 48;
    cfg.radius = 12;
    cfg.border_width = 1;
    cfg.pad_hor = 0;
    cfg.pad_ver = 0;
    cfg.opa = LV_OPA_COVER;
    cfg.label = label_default_cfg();
    cfg.label.text = "";
    cfg.label.align = LABEL_ALIGN_CENTER;

    return cfg;
}

/**
 * @brief 创建按钮组件。
 *
 * @param parent 父对象；为空时使用当前活动屏幕。
 * @param cfg 按钮配置；为空时使用默认配置。
 * @return 成功返回按钮句柄，失败返回 `NULL`。
 */
button_t* button_create(lv_obj_t* parent, const button_cfg_t* cfg) {
    button_cfg_t default_cfg;
    button_t* button = NULL;
    lv_obj_t* obj = NULL;

    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return NULL;
    }

    default_cfg = button_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    obj = lv_button_create(parent);
    if (!obj) {
        return NULL;
    }

    button = (button_t*)lv_malloc(sizeof(button_t));
    if (!button) {
        lv_obj_delete(obj);
        return NULL;
    }

    memset(button, 0, sizeof(*button));
    ui_widget_init(&button->base, obj, UI_WIDGET_TYPE_BUTTON);

    lv_obj_set_user_data(obj, button);
    lv_obj_add_event_cb(obj, button_on_delete, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(obj, button_on_size_changed, LV_EVENT_SIZE_CHANGED, button);

    lv_obj_remove_style_all(obj);

    button->label = label_create(obj, NULL);
    if (!button->label) {
        lv_obj_delete(obj);
        return NULL;
    }

    button_apply_cfg(button, cfg);

    return button;
}

/**
 * @brief 使用默认配置和文本快速创建按钮组件。
 *
 * @param parent 父对象；为空时使用当前活动屏幕。
 * @param text 按钮文本；为空时按空字符串处理。
 * @return 成功返回按钮句柄，失败返回 `NULL`。
 */
button_t* button_create_from_text(lv_obj_t* parent, const char* text) {
    button_cfg_t cfg = button_default_cfg();

    cfg.label.text = text;
    return button_create(parent, &cfg);
}

/**
 * @brief 设置按钮文本。
 *
 * @param button 目标按钮句柄。
 * @param text 新文本内容。
 * @return 无返回值。
 */
void button_set_text(button_t* button, const char* text) {
    if (!button_is_valid(button) || !button->label) {
        return;
    }

    label_set_text(button->label, text);
    lv_obj_center(label_get_obj(button->label));
}

/**
 * @brief 设置按钮透明度。
 *
 * @param button 目标按钮句柄。
 * @param opa 新透明度。
 * @return 无返回值。
 */
void button_set_opacity(button_t* button, uint8_t opa) {
    if (!button_is_valid(button) || !button->label) {
        return;
    }

    lv_obj_set_style_border_opa(button->base.obj, (lv_opa_t)opa, 0);
    label_set_opacity(button->label, opa);
}

/**
 * @brief 按配置结构批量刷新按钮状态。
 *
 * @param button 目标按钮句柄。
 * @param cfg 配置结构；为空时使用默认配置。
 * @return 无返回值。
 */
void button_apply_cfg(button_t* button, const button_cfg_t* cfg) {
    button_cfg_t default_cfg;

    if (!button_is_valid(button)) {
        return;
    }

    default_cfg = button_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    ui_widget_set_bounds(UI_WIDGET(button), cfg->x, cfg->y, cfg->w, cfg->h);
    lv_obj_set_style_radius(button->base.obj, (lv_coord_t)cfg->radius, 0);
    lv_obj_set_style_border_width(button->base.obj, (lv_coord_t)cfg->border_width, 0);
    lv_obj_set_style_border_color(button->base.obj, lv_color_white(), 0);
    button_set_opacity(button, cfg->opa);
    lv_obj_set_style_bg_color(button->base.obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(button->base.obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(button->base.obj, (lv_coord_t)cfg->pad_hor, 0);
    lv_obj_set_style_pad_ver(button->base.obj, (lv_coord_t)cfg->pad_ver, 0);

    if (button->label) {
        label_apply_cfg(button->label, &cfg->label);
        lv_obj_center(label_get_obj(button->label));
    }
}

/**
 * @brief 获取内部文本组件句柄。
 *
 * @param button 目标按钮句柄。
 * @return 返回内部文本组件；无效时返回 `NULL`。
 */
label_t* button_get_label(button_t* button) {
    if (!button_is_valid(button)) {
        return NULL;
    }

    return button->label;
}

/**
 * @brief 获取底层 LVGL 对象。
 *
 * @param button 目标按钮句柄。
 * @return 返回底层对象指针；无效时返回 `NULL`。
 */
lv_obj_t* button_get_obj(button_t* button) {
    if (!button_is_valid(button)) {
        return NULL;
    }

    return button->base.obj;
}
