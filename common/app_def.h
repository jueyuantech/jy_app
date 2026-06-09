#pragma once
#include <stddef.h>
#include <stdint.h>
#include "floatair_lcd.h"

#define SYSTEM_FILE_TYPE_DIR   0
#define SYSTEM_FILE_TYPE_FILE  1
#define SYSTEM_FILE_TYPE_OTHER 2

#define SYSTEM_MAX_PATH_LEN 256

typedef void (*floatair_minute_cb_t)(void);

#define APP_NAME_ASSISTANT "assistant"
/** AI 应用名称。 */
#define APP_NAME_AI "ai"
#define APP_NAME_GALLERY "gallery"
#define APP_NAME_HOME "home"
#define APP_NAME_GUIDE "guide"
#define APP_NAME_MUSIC "music"
#define APP_NAME_NAVIGATION "navigation"
#define APP_NAME_OTA "ota"
#define APP_NAME_POWEROFF "poweroff"
#define APP_NAME_POWERON "poweron"
#define APP_NAME_PROMPTER "prompter"
#define APP_NAME_READER "reader"
#define APP_NAME_TRANSCRIBE "transcribe"
#define APP_NAME_TRANSLATE "translate"
#define APP_NAME_SYSTEM "system"
#define APP_NAME_LANGSELECTION "langselection"

#define FLOATAIR_ROOT_IMAGE_DISTANCE_NORMAL (0)
#define FLOATAIR_ROOT_IMAGE_DISTANCE_FAR (-10)
#define FLOATAIR_ROOT_IMAGE_DISTANCE_NEAR (15)

/** App 选中内容浮层的双目视差距离。 */
#define FLOATAIR_APP_FLOAT_DISTANCE (6)
/** Popup 弹窗层的双目视差距离。 */
#define FLOATAIR_POPUP_FLOAT_DISTANCE (12)

/**
 * @brief Initialize floatair
 */
void floatair_load(void);
/**
 * @brief Unload floatair
 */
void floatair_unload(void);

/**
 * @brief LVGL tick handler
 */
void floatair_lvgl_tick(void);
/**
 * @brief LVGL period minute handler
 */
void floatair_lvgl_period_minute(void);

void floatair_register_minute_cb(floatair_minute_cb_t cb);
