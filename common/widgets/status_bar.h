/**
 * @file status_bar.h
 * @brief Product-selected status bar API
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
#define DEFAULT_BATTERY_LEVEL UINT8_MAX ///< 尚未收到 OS 电量上报，不显示电池。

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
 * @brief Update status bar position inside its parent.
 * @param status_bar Status bar object.
 * @param pos Target top or bottom position.
 */
void status_bar_set_position(lv_obj_t* status_bar, status_bar_widget_pos_t pos);

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
 * @brief Update the localized calendar text used by a top status bar.
 * @param status_bar Status bar object.
 * @param day Day of month, range 1-31.
 * @param month Month, range 1-12.
 * @param weekday Weekday, range 0-6 where 0 is Sunday.
 */
void status_bar_update_calendar(lv_obj_t* status_bar,
                                uint8_t day,
                                uint8_t month,
                                uint8_t weekday);

/**
 * @brief Update the fixed app name shown in the leftmost bottom slot.
 * @param status_bar Status bar object.
 * @param app_name Display name, or NULL/empty to hide it.
 */
void status_bar_update_app_name(lv_obj_t* status_bar, const char* app_name);

/**
 * @brief 设置状态栏时间文本显隐。
 * @param[in] status_bar 状态栏对象。
 * @param[in] visible `true` 表示显示时间，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void status_bar_set_time_visible(lv_obj_t* status_bar, bool visible);

/**
 * @brief 更新电量，未上报时隐藏电池图标及百分比。
 * @param[in] status_bar 状态栏对象。
 * @param[in] level 电量百分比（0-100），DEFAULT_BATTERY_LEVEL 表示未上报。
 * @return 无返回值。
 */
void status_bar_update_battery(lv_obj_t* status_bar, uint8_t level);

/**
 * @brief Update charge state display
 * @param status_bar Status bar object
 * @param state Charge state (0-3)
 */
void status_bar_update_charge_state(lv_obj_t* status_bar, uint8_t state);

/**
 * @brief Update Bluetooth connection state display.
 * @param status_bar Status bar object
 * @param connected `true` when Bluetooth is connected
 */
void status_bar_update_bt_state(lv_obj_t* status_bar, bool connected);

/**
 * @brief 更新左右耳机附件连接状态显示。
 * @param[in] status_bar 状态栏对象。
 * @param[in] left_connected `true` 表示左侧耳机附件已连接。
 * @param[in] right_connected `true` 表示右侧耳机附件已连接。
 * @return 无返回值。
 */
void status_bar_update_headset_state(lv_obj_t* status_bar,
                                     bool left_connected,
                                     bool right_connected);

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
