/**
 * @file status_bar.c
 * @brief Bottom status bar implementation
 */
#include <stdio.h>
#include <string.h>
#include "status_bar.h"
#include "app_def.h"
#include "system/system_res.h"
#include "floatair_dbg.h"
#include "lvgl/src/misc/lv_text.h"
#include "floatair_fs.h"

#define MAX_WIDGETS 10

/**
 * @brief Status bar data structure
 */
typedef struct {
    int32_t width;
    const lv_font_t* font;
    lv_obj_t* container;
    lv_obj_t* time_label;
    lv_obj_t* battery_label;
    lv_obj_t* wear_detection_slot; ///< 佩戴检测图标占位槽位
    lv_obj_t* battery_icon;
    uint8_t battery_level;
    uint8_t charge_state;
    status_bar_widget_t widgets[MAX_WIDGETS];
    uint8_t widget_count;
} status_bar_data_t;

static void status_bar_on_delete(lv_event_t* e) {
    lv_obj_t* status_bar = lv_event_get_target(e);
    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);

    if (!data) {
        return;
    }

    lv_free(data);
    lv_obj_set_user_data(status_bar, NULL);
}

/**
 * @brief 获取电量文本固定预留宽度。
 *
 * 电量显示可能从 "0%" 变化到 "100%"，槽位计算必须按最大宽度预留，避免后续更新挤占自定义组件。
 *
 * @param font 电量文本使用的字体。
 * @return 返回包含左右内边距的固定宽度。
 */
static lv_coord_t status_bar_get_battery_text_width(const lv_font_t* font) {
    lv_coord_t width =
        lv_text_get_width(STATUS_BAR_MAX_BATTERY_TEXT,
                          (uint32_t)strlen(STATUS_BAR_MAX_BATTERY_TEXT),
                          font,
                          0);

    return width + STATUS_BAR_LABEL_SIDE_PADDING * 2;
}

/**
 * @brief 获取右侧电池区域总宽度。
 *
 * 右侧区域包含电量文本、电池图标，以及预留给佩戴检测图标的占位槽位。
 *
 * @param[in] data 状态栏私有数据。
 * @return 返回右侧保留区域宽度。
 */
static int32_t status_bar_get_right_reserved_width(const status_bar_data_t* data) {
    lv_coord_t battery_label_width = 0;
    int32_t battery_width = 0;

    if (data == NULL || data->battery_icon == NULL) {
        return 0;
    }

    if (data->battery_label != NULL) {
        battery_label_width = status_bar_get_battery_text_width(data->font);
    }

    battery_width = STATUS_BAR_IMG_W + battery_label_width + STATUS_BAR_ITEM_SPACING * 2;
    if (data->wear_detection_slot != NULL) {
        battery_width += STATUS_BAR_IMG_W + STATUS_BAR_ITEM_SPACING;
    }

    return battery_width;
}

/**
 * @brief 对齐底部状态栏右侧组件。
 *
 * 右侧组件包含电池图标、电量文本和佩戴检测占位槽位，统一在这里维护相对位置，
 * 以便后续替换为正式图标时无需再次调整布局逻辑。
 *
 * @param[in] data 状态栏私有数据。
 */
static void status_bar_align_right_widgets(status_bar_data_t* data) {
    if (data == NULL || data->battery_icon == NULL) {
        return;
    }

    lv_obj_align(data->battery_icon, LV_ALIGN_RIGHT_MID, 0, 0);

    if (data->battery_label != NULL) {
        lv_obj_align_to(data->battery_label,
                        data->battery_icon,
                        LV_ALIGN_OUT_LEFT_MID,
                        -STATUS_BAR_ITEM_SPACING,
                        0);
    }

    if (data->wear_detection_slot != NULL && data->battery_label != NULL) {
        lv_obj_align_to(data->wear_detection_slot,
                        data->battery_label,
                        LV_ALIGN_OUT_LEFT_MID,
                        -STATUS_BAR_ITEM_SPACING,
                        0);
    }
}

/**
 * @brief Check if widget position overlaps with existing widgets
 * @param data Status bar data
 * @param x_offset New widget x offset
 * @param width New widget width
 * @return true if overlap, false otherwise
 */
static bool check_widget_overlap(status_bar_data_t* data, int32_t x_offset, int32_t width) {
    lv_coord_t time_width = 0;
    if (data->time_label != NULL) {
        time_width = lv_obj_get_width(data->time_label);
    }
    if (x_offset < time_width + STATUS_BAR_ITEM_SPACING) {
        return true;
    }

    int32_t battery_width = status_bar_get_right_reserved_width(data);
    int32_t battery_start = data->width - battery_width;
    if (x_offset + width > battery_start - STATUS_BAR_ITEM_SPACING) {
        return true;
    }

    for (uint8_t i = 0; i < data->widget_count; i++) {
        status_bar_widget_t* widget = &data->widgets[i];
        if ((x_offset < widget->x_offset + widget->width + STATUS_BAR_ITEM_SPACING) && 
            (x_offset + width + STATUS_BAR_ITEM_SPACING > widget->x_offset)) {
            return true;
        }
    }
    
    return false;
}

static bool find_slot(status_bar_data_t* data, status_bar_widget_align_t align, int32_t want_width, int32_t* out_x) {
    lv_coord_t time_width = (data->time_label != NULL) ? lv_obj_get_width(data->time_label) : 0;
    int32_t battery_width = status_bar_get_right_reserved_width(data);
    int32_t battery_start = data->width - battery_width;
    if (align == STATUS_BAR_WIDGET_ALIGN_LEFT) {
        int32_t x = time_width + STATUS_BAR_ITEM_SPACING;
        while (1) {
            if (x + want_width > battery_start - STATUS_BAR_ITEM_SPACING) {
                return false;
            }
            bool overlapped = false;
            for (uint8_t i = 0; i < data->widget_count; i++) {
                status_bar_widget_t* w = &data->widgets[i];
                if ((x < w->x_offset + w->width + STATUS_BAR_ITEM_SPACING) &&
                    (x + want_width + STATUS_BAR_ITEM_SPACING > w->x_offset)) {
                    x = w->x_offset + w->width + STATUS_BAR_ITEM_SPACING;
                    overlapped = true;
                    break;
                }
            }
            if (!overlapped) {
                *out_x = x;
                return true;
            }
        }
    } else if (align == STATUS_BAR_WIDGET_ALIGN_RIGHT) {
        int32_t x = battery_start - STATUS_BAR_ITEM_SPACING - want_width;
        while (1) {
            if (x < time_width + STATUS_BAR_ITEM_SPACING) {
                return false;
            }
            bool overlapped = false;
            for (uint8_t i = 0; i < data->widget_count; i++) {
                status_bar_widget_t* w = &data->widgets[i];
                if ((x < w->x_offset + w->width + STATUS_BAR_ITEM_SPACING) &&
                    (x + want_width + STATUS_BAR_ITEM_SPACING > w->x_offset)) {
                    x = w->x_offset - STATUS_BAR_ITEM_SPACING - want_width;
                    overlapped = true;
                    break;
                }
            }
            if (!overlapped) {
                *out_x = x;
                return true;
            }
        }
    } else {
        int32_t center_x = (data->width - want_width) / 2;
        int32_t step = STATUS_BAR_ITEM_SPACING;
        for (int32_t offset = 0; offset <= data->width; offset += step) {
            int32_t x1 = center_x - offset;
            if (x1 >= time_width + STATUS_BAR_ITEM_SPACING &&
                x1 + want_width <= battery_start - STATUS_BAR_ITEM_SPACING &&
                !check_widget_overlap(data, x1, want_width)) {
                *out_x = x1;
                return true;
            }
            int32_t x2 = center_x + offset;
            if (x2 >= time_width + STATUS_BAR_ITEM_SPACING &&
                x2 + want_width <= battery_start - STATUS_BAR_ITEM_SPACING &&
                !check_widget_overlap(data, x2, want_width)) {
                *out_x = x2;
                return true;
            }
        }
        return false;
    }
}

lv_obj_t* status_bar_create_with_pos(lv_obj_t* parent, int32_t width, const lv_font_t* font, status_bar_widget_pos_t pos) {
    if (parent == NULL || width <= 0) {
        return NULL;
    }
    if (font == NULL) {
        font = get_system_font();
    }
    lv_obj_t* status_bar = lv_obj_create(parent);
    if (status_bar == NULL) {
        return NULL;
    }
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, width, STATUS_BAR_HEIGHT);
    lv_obj_align(status_bar, (pos == STATUS_BAR_POS_BOTTOM) ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_t* container = lv_obj_create(status_bar);
    if (container == NULL) {
        lv_obj_delete(status_bar);
        return NULL;
    }
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, width, STATUS_BAR_HEIGHT);
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_t* time_label = NULL;
    lv_obj_t* battery_icon = NULL;
    lv_obj_t* battery_label = NULL;
    lv_obj_t* wear_detection_slot = NULL;
    if (pos == STATUS_BAR_POS_BOTTOM) {
        time_label = lv_label_create(status_bar);
        lv_coord_t font_h = 0;
        if (font != NULL) {
            font_h = lv_font_get_line_height(font);
        }
        if (font_h <= 0 || font_h > STATUS_BAR_HEIGHT) {
            font_h = STATUS_BAR_HEIGHT;
        }
        lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
        if (font != NULL) {
            obj_set_text_font(time_label, font);
        }
        lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(time_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(time_label, DEFAULT_TIME);
        lv_coord_t time_text_width =
            lv_text_get_width(DEFAULT_TIME, (uint32_t)strlen(DEFAULT_TIME), font, 0);
        lv_coord_t time_label_width =
            time_text_width + STATUS_BAR_LABEL_SIDE_PADDING * 2;
        lv_obj_set_size(time_label, time_label_width, font_h);
        lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 0, 0);
        battery_icon = lv_image_create(status_bar);
        lv_obj_remove_style_all(battery_icon);
        lv_obj_set_size(battery_icon, STATUS_BAR_IMG_W, STATUS_BAR_IMG_H);
        lv_obj_align(battery_icon, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(battery_icon, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(battery_icon, LV_OPA_COVER, LV_PART_MAIN);
        battery_label = lv_label_create(status_bar);
        lv_obj_remove_style_all(battery_label);
        lv_obj_set_style_text_color(battery_label, lv_color_white(), 0);
        if (font != NULL) {
            obj_set_text_font(battery_label, font);
        }
        lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_bg_color(battery_label, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(battery_label, LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_long_mode(battery_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(battery_label, "0%");
        lv_coord_t battery_text_width = status_bar_get_battery_text_width(font);
        lv_obj_set_size(battery_label, battery_text_width, font_h);

        wear_detection_slot = lv_image_create(status_bar);
        lv_obj_remove_style_all(wear_detection_slot);
        lv_obj_set_size(wear_detection_slot, STATUS_BAR_IMG_W, STATUS_BAR_IMG_H);
        lv_image_set_src(wear_detection_slot, FLOATAIR_SYS_IMG("touch.jpg"));
        lv_obj_add_flag(wear_detection_slot, LV_OBJ_FLAG_HIDDEN);
    }
    status_bar_data_t* data = (status_bar_data_t*)lv_malloc(sizeof(status_bar_data_t));
    if (data == NULL) {
        lv_obj_delete(status_bar);
        return NULL;
    }
    data->width = width;
    data->font = font;
    data->container = container;
    data->time_label = time_label;
    data->battery_label = battery_label;
    data->wear_detection_slot = wear_detection_slot;
    data->battery_icon = battery_icon;
    data->battery_level = DEFAULT_BATTERY_LEVEL;
    data->charge_state = 0;
    data->widget_count = 0;
    for (uint8_t i = 0; i < MAX_WIDGETS; i++) {
        data->widgets[i].type = STATUS_BAR_WIDGET_TEXT;
        data->widgets[i].obj = NULL;
        data->widgets[i].x_offset = 0;
        data->widgets[i].width = 0;
    }
    lv_obj_set_user_data(status_bar, data);
    lv_obj_add_event_cb(status_bar, status_bar_on_delete, LV_EVENT_DELETE, NULL);
    if (pos == STATUS_BAR_POS_BOTTOM) {
        status_bar_align_right_widgets(data);
        status_bar_update_battery(status_bar, 0);
    }
    return status_bar;
}

/**
 * @brief Destroy status bar
 * @param status_bar Status bar object
 */
void status_bar_destroy(lv_obj_t* status_bar) {
    if (status_bar != NULL) {
        lv_obj_delete(status_bar);
    }
}

/**
 * @brief Update status bar width
 * @param status_bar Status bar object
 * @param width New width
 */
void status_bar_update_width(lv_obj_t* status_bar, int32_t width) {
    if (status_bar == NULL || width <= 0) {
        floatair_err("status_bar is NULL or width <= 0");
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return;
    }

    if (data->width == width) {
        return;
    }

    data->width = width;
    lv_obj_set_size(status_bar, width, STATUS_BAR_HEIGHT);
    lv_obj_set_size(data->container, width, STATUS_BAR_HEIGHT);

    // Realign the reserved right-side group after the status bar width changes.
    status_bar_align_right_widgets(data);

    // Realign custom widgets
    for (uint8_t i = 0; i < data->widget_count; i++) {
        status_bar_widget_t* widget = &data->widgets[i];
        if (widget->obj != NULL) {
            lv_obj_set_x(widget->obj, widget->x_offset);
        }
    }
}

/**
 * @brief Update time display
 * @param status_bar Status bar object
 * @param time_str Time string (format: "HH:MM")
 */
void status_bar_update_time(lv_obj_t* status_bar, const char* time_str) {
    if (status_bar == NULL) {
        floatair_err("status_bar is NULL");
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL || data->time_label == NULL) {
        return;
    }

    const char* text = time_str ? time_str : DEFAULT_TIME;
    const char* current = lv_label_get_text(data->time_label);
    if (current != NULL && strcmp(current, text) == 0) {
        return;
    }

    lv_label_set_text(data->time_label, text);

    const lv_font_t* font = data->font != NULL ? data->font : get_system_font();
    lv_coord_t text_width =
        lv_text_get_width(text, (uint32_t)strlen(text), font, 0);
    lv_coord_t label_width =
        text_width + STATUS_BAR_LABEL_SIDE_PADDING * 2;
    lv_obj_set_width(data->time_label, label_width);
    lv_obj_align(data->time_label, LV_ALIGN_LEFT_MID, 0, 0);
}

/**
 * @brief 设置状态栏时间文本显隐。
 * @param[in] status_bar 状态栏对象。
 * @param[in] visible `true` 表示显示时间，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void status_bar_set_time_visible(lv_obj_t* status_bar, bool visible) {
    if (status_bar == NULL) {
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL || data->time_label == NULL) {
        return;
    }

    if (visible) {
        lv_obj_remove_flag(data->time_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(data->time_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Update battery level
 * @param status_bar Status bar object
 * @param level Battery level (0-100)
 */
void status_bar_update_battery(lv_obj_t* status_bar, uint8_t level) {
    if (status_bar == NULL) {
        floatair_err("status_bar is NULL");
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL || data->battery_label == NULL || data->battery_icon == NULL) {
        floatair_err("battery_label or battery_icon is NULL");
        return;
    }

    // Update battery percentage label
    char battery_text[10] = {0};
    snprintf(battery_text, sizeof(battery_text), "%u%%", (unsigned)level);
    const char* current = lv_label_get_text(data->battery_label);
    bool level_changed = data->battery_level != level;
    bool text_changed = current == NULL || strcmp(current, battery_text) != 0;

    data->battery_level = level;
    if (text_changed) {
        lv_label_set_text(data->battery_label, battery_text);
    }

    if (text_changed) {
        lv_coord_t battery_text_width = status_bar_get_battery_text_width(data->font);
        lv_obj_set_width(data->battery_label, battery_text_width);
        status_bar_align_right_widgets(data);
    }
    if (level_changed || lv_image_get_src(data->battery_icon) == NULL) {
        status_bar_update_charge_state(status_bar, data->charge_state);
    }
}

void status_bar_update_charge_state(lv_obj_t* status_bar, uint8_t state) {
    if (status_bar == NULL) {
        floatair_err("status_bar is NULL");
        return;
    }
    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL || data->battery_icon == NULL) {
        floatair_err("battery_icon is NULL");
        return;
    }

    const char* src = NULL;
    if (state == 1) {
        src = FLOATAIR_SYS_IMG("powing.jpg");
    } else {
        uint8_t level = data->battery_level;
        if (level < 10) {
            src = FLOATAIR_SYS_IMG("pow_0.jpg");
        } else if (level < 30) {
            src = FLOATAIR_SYS_IMG("pow_1.jpg");
        } else if (level < 45) {
            src = FLOATAIR_SYS_IMG("pow_2.jpg");
        } else if (level < 75) {
            src = FLOATAIR_SYS_IMG("pow_3.jpg");
        } else if (level < 90) {
            src = FLOATAIR_SYS_IMG("pow_4.jpg");
        } else {
            src = FLOATAIR_SYS_IMG("pow_5.jpg");
        }
    }

    if (src != NULL) {
        const void* current_src = lv_image_get_src(data->battery_icon);
        bool same_src = false;
        if (current_src != NULL && lv_image_src_get_type(current_src) == LV_IMAGE_SRC_FILE) {
            same_src = (strcmp((const char*)current_src, src) == 0);
        }
        if (!same_src) {
            lv_image_set_src(data->battery_icon, src);
        }
        lv_obj_remove_flag(data->battery_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(data->battery_icon, LV_OBJ_FLAG_HIDDEN);
    }
    data->charge_state = state;
}

/**
 * @brief Add text to status bar
 * @param status_bar Status bar object
 * @param text Text content
 * @param x_offset X offset from left (after time display)
 * @param x_width Text width
 * @return Created label object, NULL if failed
 */
lv_obj_t* status_bar_add_text_at(lv_obj_t* status_bar, const char* text, int32_t x_offset, int32_t x_width) {
    if (status_bar == NULL || x_width <= 0) {
        return NULL;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL || data->container == NULL) {
        return NULL;
    }

    // Check if widget array is full
    if (data->widget_count >= MAX_WIDGETS) {
        return NULL;
    }

    // Check for overlap
    if (check_widget_overlap(data, x_offset, x_width)) {
        return NULL;
    }

    lv_obj_t* label = lv_label_create(data->container);
    if (label == NULL) {
        return NULL;
    }

    lv_coord_t font_h = 0;
    if (data->font != NULL) {
        font_h = lv_font_get_line_height(data->font);
    }
    if (font_h <= 0 || font_h > STATUS_BAR_HEIGHT) {
        font_h = STATUS_BAR_HEIGHT;
    }

    lv_obj_set_size(label, x_width, font_h);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, x_offset, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    if (data->font != NULL) {
        obj_set_text_font(label, data->font);
    }
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text ? text : "");

    // Check if text needs scrolling (simplified)
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Add widget to array
    status_bar_widget_t* widget = &data->widgets[data->widget_count++];
    widget->type = STATUS_BAR_WIDGET_TEXT;
    widget->obj = label;
    widget->x_offset = x_offset;
    widget->width = x_width;

    return label;
}

lv_obj_t* status_bar_add_text(lv_obj_t* status_bar, const char* text, status_bar_widget_align_t align, int32_t x_width) {
    if (status_bar == NULL || x_width <= 0) {
        return NULL;
    }
    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return NULL;
    }
    int32_t x = 0;
    if (!find_slot(data, align, x_width, &x)) {
        return NULL;
    }
    return status_bar_add_text_at(status_bar, text, x, x_width);
}
/**
 * @brief Add image to status bar
 * @param status_bar Status bar object
 * @param src Image source
 * @param x_offset X offset from left (after time display)
 * @return Created image object, NULL if failed
 */
lv_obj_t* status_bar_add_image_at(lv_obj_t* status_bar, const void* src, int32_t x_offset) {
    if (status_bar == NULL) {
        return NULL;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL || data->container == NULL) {
        return NULL;
    }

    // Check if widget array is full
    if (data->widget_count >= MAX_WIDGETS) {
        return NULL;
    }

    int32_t img_width = STATUS_BAR_IMG_W;
    int32_t img_height = STATUS_BAR_IMG_H;

    if (img_height > STATUS_BAR_HEIGHT) {
        return NULL;
    }

    if (check_widget_overlap(data, x_offset, img_width)) {
        return NULL;
    }

    lv_obj_t* image = lv_image_create(data->container);
    if (image == NULL) {
        return NULL;
    }

    if (src == NULL) {
        lv_obj_delete(image);
        return NULL;
    }
    lv_image_set_src(image, src);

    lv_obj_set_size(image, STATUS_BAR_IMG_W, STATUS_BAR_IMG_H);
    lv_obj_set_x(image, x_offset);
    lv_obj_set_y(image, (STATUS_BAR_HEIGHT - STATUS_BAR_IMG_H) / 2);

    status_bar_widget_t* widget = &data->widgets[data->widget_count++];
    widget->type = STATUS_BAR_WIDGET_IMAGE;
    widget->obj = image;
    widget->x_offset = x_offset;
    widget->width = img_width;

    return image;
}

lv_obj_t* status_bar_add_image(lv_obj_t* status_bar, const void* src, status_bar_widget_align_t align) {
    if (status_bar == NULL) {
        return NULL;
    }
    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return NULL;
    }
    int32_t want_w = STATUS_BAR_IMG_W;
    int32_t x = 0;
    if (!find_slot(data, align, want_w, &x)) {
        return NULL;
    }
    return status_bar_add_image_at(status_bar, src, x);
}

/**
 * @brief Update text widget content
 * @param status_bar Status bar object
 * @param widget Text widget object
 * @param text New text content
 */
void status_bar_update_text(lv_obj_t* status_bar, lv_obj_t* widget, const char* text) {
    if (status_bar == NULL || widget == NULL) {
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return;
    }

    // Find the widget
    for (uint8_t i = 0; i < data->widget_count; i++) {
        if (data->widgets[i].obj == widget && data->widgets[i].type == STATUS_BAR_WIDGET_TEXT) {
            lv_label_set_text(widget, text ? text : "");
            
            // Check if text needs scrolling (simplified)
            lv_label_set_long_mode(widget, LV_LABEL_LONG_SCROLL_CIRCULAR);
            break;
        }
    }
}

/**
 * @brief Update image widget content
 * @param status_bar Status bar object
 * @param widget Image widget object
 * @param src New image source
 */
void status_bar_update_image(lv_obj_t* status_bar, lv_obj_t* widget, const void* src) {
    if (status_bar == NULL || widget == NULL) {
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return;
    }

    // Find the widget
    for (uint8_t i = 0; i < data->widget_count; i++) {
        if (data->widgets[i].obj == widget && data->widgets[i].type == STATUS_BAR_WIDGET_IMAGE) {
            if (src == NULL) {
                lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            lv_image_set_src(widget, src);
            lv_obj_set_size(widget, STATUS_BAR_IMG_W, STATUS_BAR_IMG_H);
            lv_obj_set_y(widget, (STATUS_BAR_HEIGHT - STATUS_BAR_IMG_H) / 2);
            lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);

            data->widgets[i].width = STATUS_BAR_IMG_W;
            break;
        }
    }
}

bool status_bar_text_slot_available(lv_obj_t* status_bar, int32_t x_offset, int32_t x_width) {
    if (status_bar == NULL || x_width <= 0) {
        return false;
    }
    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return false;
    }
    return !check_widget_overlap(data, x_offset, x_width);
}

bool status_bar_img_slot_available(lv_obj_t* status_bar, int32_t x_offset) {
    if (status_bar == NULL) {
        return false;
    }
    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return false;
    }
    return !check_widget_overlap(data, x_offset, STATUS_BAR_IMG_W);
}

bool status_bar_get_slot_available(lv_obj_t* status_bar, status_bar_widget_align_t align, int32_t *x_offset, int32_t *x_width) {
    if (status_bar == NULL || x_offset == NULL || x_width == NULL || *x_width <= 0) {
        return false;
    }
    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return false;
    }
    int32_t x = 0;
    if (!find_slot(data, align, *x_width, &x)) {
        return false;
    }
    *x_offset = x;
    return true;
}

/**
 * @brief Set status bar visibility
 * @param status_bar Status bar object
 * @param visible Visibility flag
 */
void status_bar_set_visible(lv_obj_t* status_bar, bool visible) {
    if (status_bar != NULL) {
        if (lv_obj_has_flag(status_bar, LV_OBJ_FLAG_HIDDEN) == !visible) {
            return;
        }
        if (visible) {
            lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief Set widget visibility
 * @param status_bar Status bar object
 * @param widget Widget object
 * @param visible Visibility flag
 */
void status_bar_set_widget_visible(lv_obj_t* status_bar, lv_obj_t* widget, bool visible) {
    if (status_bar == NULL || widget == NULL) {
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return;
    }

    // Check if widget is time label
    if (widget == data->time_label) {
        if (visible) {
            lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Check if widget is battery label
    if (widget == data->battery_label) {
        if (visible) {
            lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Check if widget is battery icon
    if (widget == data->battery_icon) {
        if (visible) {
            lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Check if widget is in custom widgets array
    for (uint8_t i = 0; i < data->widget_count; i++) {
        if (data->widgets[i].obj == widget) {
            if (visible) {
                lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
    }
}

/**
 * @brief 设置佩戴检测图标占位槽位显隐。
 * @param[in] status_bar 状态栏对象。
 * @param[in] visible `true` 表示显示占位槽位，`false` 表示隐藏占位槽位。
 * @return 无返回值。
 */
void status_bar_set_wear_detection_visible(lv_obj_t* status_bar, bool visible) {
    if (status_bar == NULL) {
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL || data->wear_detection_slot == NULL) {
        return;
    }

    if (visible) {
        lv_obj_remove_flag(data->wear_detection_slot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(data->wear_detection_slot, LV_OBJ_FLAG_HIDDEN);
    }
}

void status_bar_clear_custom_widgets(lv_obj_t* status_bar) {
    if (status_bar == NULL) {
        return;
    }

    status_bar_data_t* data = (status_bar_data_t*)lv_obj_get_user_data(status_bar);
    if (data == NULL) {
        return;
    }

    for (uint8_t i = 0; i < data->widget_count; i++) {
        if (data->widgets[i].obj != NULL) {
            lv_obj_delete(data->widgets[i].obj);
            data->widgets[i].obj = NULL;
        }
        data->widgets[i].x_offset = 0;
        data->widgets[i].width = 0;
    }
    data->widget_count = 0;
}
