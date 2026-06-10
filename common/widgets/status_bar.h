/**
 * @file status_bar.h
 * @brief Bottom status bar implementation
 */
#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "lvgl/lvgl.h"

#define STATUS_BAR_HEIGHT 50

#define STATUS_BAR_IMG_W 32
#define STATUS_BAR_IMG_H 32

#define STATUS_BAR_LABEL_SIDE_PADDING 1

#define DEFAULT_TIME_FORMAT "%02d:%02d"
#define DEFAULT_TIME "00:00"
#define DEFAULT_BATTERY_LEVEL 0

#define STATUS_BAR_MAX_BATTERY_TEXT "100%"

#define STATUS_BAR_ITEM_SPACING 4

/**
 * @brief Status bar widget types
 */
typedef enum {
    STATUS_BAR_WIDGET_TEXT,
    STATUS_BAR_WIDGET_IMAGE
} status_bar_widget_type_t;

/**
 * @brief Status bar widget position types
 */
typedef enum {
    STATUS_BAR_POS_TOP,
    STATUS_BAR_POS_BOTTOM
} status_bar_widget_pos_t;

/**
 * @brief Status bar widget alignment types
 */
typedef enum {
    STATUS_BAR_WIDGET_ALIGN_LEFT,
    STATUS_BAR_WIDGET_ALIGN_RIGHT,
    STATUS_BAR_WIDGET_ALIGN_CENTER
} status_bar_widget_align_t;

/**
 * @brief Status bar widget structure
 */
typedef struct {
    status_bar_widget_type_t type;
    lv_obj_t* obj;
    int32_t x_offset;
    int32_t width;
} status_bar_widget_t;

/**
 * @brief Create status bar
 * @param parent Parent object (usually root)
 * @param width Status bar width (usually view's ui_width)
 * @param font Font object for text display
 * @return Status bar object
 */
lv_obj_t* status_bar_create_with_pos(lv_obj_t* parent, int32_t width, const lv_font_t* font, status_bar_widget_pos_t pos);

/**
 * @brief Destroy status bar
 * @param status_bar Status bar object
 */
void status_bar_destroy(lv_obj_t* status_bar);

/**
 * @brief Update status bar width
 * @param status_bar Status bar object
 * @param width New width
 */
void status_bar_update_width(lv_obj_t* status_bar, int32_t width);

/**
 * @brief Update time display
 * @param status_bar Status bar object
 * @param time_str Time string (format: "HH:MM")
 */
void status_bar_update_time(lv_obj_t* status_bar, const char* time_str);

/**
 * @brief 设置状态栏时间文本显隐。
 * @param[in] status_bar 状态栏对象。
 * @param[in] visible `true` 表示显示时间，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void status_bar_set_time_visible(lv_obj_t* status_bar, bool visible);

/**
 * @brief Update battery level
 * @param status_bar Status bar object
 * @param level Battery level (0-100)
 */
void status_bar_update_battery(lv_obj_t* status_bar, uint8_t level);

/**
 * @brief Update charge state display
 * @param status_bar Status bar object
 * @param state Charge state (0-3)
 */
void status_bar_update_charge_state(lv_obj_t* status_bar, uint8_t state);


/**
 * @brief Add text to status bar
 * @param status_bar Status bar object
 * @param text Text content
 * @param x_offset X offset from left (after time display)
 * @param x_width Text width
 * @return Created label object, NULL if failed
 */
lv_obj_t* status_bar_add_text(lv_obj_t* status_bar, const char* text, status_bar_widget_align_t align, int32_t x_width);

/**
 * @brief Add image to status bar
 * @param status_bar Status bar object
 * @param src Image source
 * @param x_offset X offset from left (after time display)
 * @return Created image object, NULL if failed
 */
lv_obj_t* status_bar_add_image(lv_obj_t* status_bar, const void* src, status_bar_widget_align_t align);

/**
 * @brief Add text to status bar
 * @param status_bar Status bar object
 * @param text Text content
 * @param x_offset X offset from left (after time display)
 * @param x_width Text width
 * @return Created label object, NULL if failed
 */
lv_obj_t* status_bar_add_text_at(lv_obj_t* status_bar, const char* text, int32_t x_offset, int32_t x_width);

/**
 * @brief Add image to status bar
 * @param status_bar Status bar object
 * @param src Image source
 * @param x_offset X offset from left (after time display)
 * @return Created image object, NULL if failed
 */
lv_obj_t* status_bar_add_image_at(lv_obj_t* status_bar, const void* src, int32_t x_offset);

/**
 * @brief Update text widget content
 * @param status_bar Status bar object
 * @param widget Text widget object
 * @param text New text content
 */
void status_bar_update_text(lv_obj_t* status_bar, lv_obj_t* widget, const char* text);

/**
 * @brief Update image widget content
 * @param status_bar Status bar object
 * @param widget Image widget object
 * @param src New image source
 */
void status_bar_update_image(lv_obj_t* status_bar, lv_obj_t* widget, const void* src);

/**
 * @brief Set status bar visibility
 * @param status_bar Status bar object
 * @param visible Visibility flag
 */
void status_bar_set_visible(lv_obj_t* status_bar, bool visible);

/**
 * @brief Set widget visibility
 * @param status_bar Status bar object
 * @param widget Widget object
 * @param visible Visibility flag
 */
void status_bar_set_widget_visible(lv_obj_t* status_bar, lv_obj_t* widget, bool visible);

/**
 * @brief 设置佩戴检测图标占位槽位显隐。
 * @param[in] status_bar 状态栏对象。
 * @param[in] visible `true` 表示显示占位槽位，`false` 表示隐藏占位槽位。
 * @return 无返回值。
 */
void status_bar_set_wear_detection_visible(lv_obj_t* status_bar, bool visible);

void status_bar_clear_custom_widgets(lv_obj_t* status_bar);

/**
 * @brief Check if text slot is available
 * @param status_bar Status bar object
 * @param x_offset X offset from left (after time display)
 * @param x_width Text width
 * @return true Slot is available
 * @return false Slot is not available
 */
bool status_bar_text_slot_available(lv_obj_t* status_bar, int32_t x_offset, int32_t x_width);

/**
 * @brief Check if image slot is available
 * @param status_bar Status bar object
 * @param src Image source
 * @param x_offset X offset from left (after time display)
 * @return true Slot is available
 * @return false Slot is not available
 */
bool status_bar_img_slot_available(lv_obj_t* status_bar, int32_t x_offset);

/**
 * @brief Get slot availability
 * @param status_bar Status bar object
 * @param align Widget align
 * @param x_offset X offset from left (after time display)
 * @param x_width Text width
 */
bool status_bar_get_slot_available(lv_obj_t* status_bar, status_bar_widget_align_t align, int32_t *x_offset, int32_t *x_width);

#endif /* STATUS_BAR_H */
