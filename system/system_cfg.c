/**
 * @file system_cfg.c
 * @brief System application configuration read/write implementation
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include "floatair_dbg.h"
#include "system.h"
#include "system_config_json.h"
#include "system_runtime_ui.h"
#include "floatair_fs.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool config_wear_detection_enabled = false;
static bool config_imudoubletap_enabled   = false;
static bool config_imutilt_enabled        = false;
static bool config_touchpad_enabled       = false;
static bool config_notification_enabled   = false;
static bool config_keyword_spotting_enabled = true;
static uint32_t config_kws_hit_value      = 5; ///< 当前产品需要响应的 KWS 命中值。
#define SYSTEM_KWS_HIT_VALUES_MAX 8U
static uint32_t config_kws_hit_values[SYSTEM_KWS_HIT_VALUES_MAX] = {5};
static size_t config_kws_hit_values_count = 1;
static bool config_idle_detection_enabled = false;
static system_head_gesture_config_t config_head_gesture = {0};
static uint8_t config_display_mode        = 0;
static uint16_t config_lcd_sleep_timeout  = 0;
static uint16_t config_deep_sleep_timeout = 0;
static uint16_t config_inactivity_timeout = 0;
static bool config_bl_auto                = false;
static uint8_t config_brightness          = 0;
static char* config_curlang               = NULL;
static char** config_homeunits            = NULL;
static size_t config_homeunits_count      = 0;
system_lcd_t config_lcd                   = {
    .ui_x_begin = SYSTEM_LCD_UI_X_BEGIN,
    .ui_y_begin = SYSTEM_LCD_UI_Y_BEGIN,
    .ui_width = SYSTEM_LCD_UI_WIDTH,
    .ui_height = SYSTEM_LCD_UI_HEIGHT,
};


static char* user_guide         = NULL;
static uint32_t display_level       = 0;
static uint32_t display_position    = SYSTEM_DISPLAY_POSITION_BOTTOM;
static bool system_cfgfile_inited = false;

static void system_cfgfile_free_homeunits(void) {
    if (config_homeunits != NULL) {
        for (size_t i = 0; i < config_homeunits_count; ++i) {
            free(config_homeunits[i]);
        }
        free(config_homeunits);
    }
    config_homeunits = NULL;
    config_homeunits_count = 0;
}

static void system_cfgfile_set_userguide_runtime(const char* progress) {
    const char* value = (progress != NULL && progress[0] != '\0') ? progress : SYSTEM_USERGUIDE_PROGRESS_FALSE;
    char* copied = strdup(value);

    floatair_assert(copied != NULL, "strdup userguide failed");
    if (user_guide != NULL) {
        free(user_guide);
        user_guide = NULL;
    }
    user_guide = copied;
}

static bool system_cfgfile_is_valid_userguide(const char* progress) {
    return progress != NULL &&
           (strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_FALSE) == 0 ||
            strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP1) == 0 ||
            strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP2) == 0 ||
            strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP3) == 0 ||
            strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP4) == 0 ||
            strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP5) == 0 ||
            strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_TRUE) == 0);
}

static void system_cfgfile_parse_userguide(cJSON* root) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "userguide");
    const char* progress = SYSTEM_USERGUIDE_PROGRESS_FALSE;

    if (cJSON_IsString(item) && item->valuestring != NULL &&
        system_cfgfile_is_valid_userguide(item->valuestring)) {
        progress = item->valuestring;
    }
    system_cfgfile_set_userguide_runtime(progress);
}

static bool system_cfgfile_copy_homeunits(const char* const* homeunits,
                                          size_t count,
                                          char*** out_homeunits,
                                          size_t* out_count) {
    char** copied = NULL;
    size_t copied_count = 0;

    if (out_homeunits == NULL || out_count == NULL) {
        return false;
    }
    *out_homeunits = NULL;
    *out_count = 0;
    if (count == 0) {
        return true;
    }
    copied = (char**)calloc(count, sizeof(char*));
    if (copied == NULL) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const char* item = (homeunits != NULL) ? homeunits[i] : NULL;

        if (item == NULL || item[0] == '\0') {
            continue;
        }
        copied[copied_count] = strdup(item);
        if (copied[copied_count] == NULL) {
            for (size_t j = 0; j < copied_count; ++j) {
                free(copied[j]);
            }
            free(copied);
            return false;
        }
        copied_count++;
    }
    if (copied_count == 0) {
        free(copied);
        copied = NULL;
    }
    *out_homeunits = copied;
    *out_count = copied_count;
    return true;
}

static bool system_cfgfile_parse_homeunits(cJSON* root) {
    cJSON* items = cJSON_GetObjectItemCaseSensitive(root, "homeunits");
    char** parsed = NULL;
    size_t parsed_count = 0;

    if (!cJSON_IsArray(items)) {
        system_cfgfile_free_homeunits();
        return true;
    }
    size_t item_count = (size_t)cJSON_GetArraySize(items);
    if (item_count == 0) {
        system_cfgfile_free_homeunits();
        return true;
    }
    parsed = (char**)calloc(item_count, sizeof(char*));
    if (parsed == NULL) {
        return false;
    }
    for (size_t i = 0; i < item_count; ++i) {
        cJSON* item = cJSON_GetArrayItem(items, (int)i);

        if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0') {
            continue;
        }
        parsed[parsed_count] = strdup(item->valuestring);
        if (parsed[parsed_count] == NULL) {
            for (size_t j = 0; j < parsed_count; ++j) {
                free(parsed[j]);
            }
            free(parsed);
            return false;
        }
        parsed_count++;
    }
    system_cfgfile_free_homeunits();
    if (parsed_count == 0) {
        free(parsed);
        return true;
    }
    config_homeunits = parsed;
    config_homeunits_count = parsed_count;
    return true;
}

static void system_cfgfile_parse_kws_hit_value(cJSON* root) {
    cJSON* items = cJSON_GetObjectItemCaseSensitive(root, "kwsHitValue");
    size_t parsed_count = 0;

    if (cJSON_IsArray(items)) {
        size_t item_count = (size_t)cJSON_GetArraySize(items);
        for (size_t i = 0;
             i < item_count && parsed_count < SYSTEM_KWS_HIT_VALUES_MAX;
             ++i) {
            cJSON* item = cJSON_GetArrayItem(items, (int)i);
            if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||
                item->valuedouble > (double)UINT32_MAX) {
                continue;
            }
            uint32_t value = (uint32_t)item->valuedouble;
            bool duplicate = false;
            for (size_t j = 0; j < parsed_count; ++j) {
                if (config_kws_hit_values[j] == value) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                config_kws_hit_values[parsed_count++] = value;
            }
        }
    }

    if (parsed_count == 0) {
        config_kws_hit_values[0] = config_kws_hit_value;
        parsed_count = 1;
    }
    config_kws_hit_values_count = parsed_count;
    config_kws_hit_value = config_kws_hit_values[0];
}

static bool system_cfgfile_update_kws_hit_value(cJSON* root) {
    cJSON_DeleteItemFromObjectCaseSensitive(root, "kwsHitValue");
    cJSON* items = cJSON_AddArrayToObject(root, "kwsHitValue");
    if (items == NULL) {
        return false;
    }
    for (size_t i = 0; i < config_kws_hit_values_count; ++i) {
        cJSON* item = cJSON_CreateNumber((double)config_kws_hit_values[i]);
        if (item == NULL || !cJSON_AddItemToArray(items, item)) {
            cJSON_Delete(item);
            return false;
        }
    }
    return true;
}

static void system_cfgfile_clear_runtime_state(void) {
    if (config_curlang) {
        free(config_curlang);
        config_curlang = NULL;
    }
    if (user_guide) {
        free(user_guide);
        user_guide = NULL;
    }
    system_cfgfile_free_homeunits();
    display_position = SYSTEM_DISPLAY_POSITION_BOTTOM;
    system_cfgfile_inited = false;
}

/**
 * @brief 归一化历史 LCD 息屏字段与当前静置息屏字段。
 * @return 无返回值。
 */
static void system_cfg_normalize_sleep_timeout_fields(void) {
    if (config_lcd_sleep_timeout == 0 || config_inactivity_timeout == 0) {
        config_lcd_sleep_timeout = 0;
        config_inactivity_timeout = 0;
    }
}

/**
 * @brief 同步历史 LCD 息屏字段与当前静置息屏字段。
 * @param[in] timeout 超时时间，单位为秒。
 * @return 无返回值。
 */
static void system_cfg_assign_sleep_timeout_fields(uint16_t timeout) {
    config_lcd_sleep_timeout = timeout;
    config_inactivity_timeout = timeout;
}

bool system_config_get_wear_detection_enabled(void) {
    return config_wear_detection_enabled;
}

bool system_config_set_wear_detection_enabled(bool wear_detection_enabled) {
    config_wear_detection_enabled = wear_detection_enabled;
    return system_cfgfile_update();
}

bool system_config_get_imudoubletap_enabled(void){
	return config_imudoubletap_enabled;
}

bool system_config_set_imudoubletap_enabled(bool imudoubletap_enabled) {
    config_imudoubletap_enabled = imudoubletap_enabled;
    return system_cfgfile_update();
}


bool system_config_get_imutilt_enabled(void){
	return config_imutilt_enabled;
}

bool system_config_set_imutilt_enabled(bool imutilt_enabled) {
    config_imutilt_enabled = imutilt_enabled;
    return system_cfgfile_update();
}

bool system_config_get_touchpad_enabled(void) {
    return config_touchpad_enabled;
}

bool system_config_set_touchpad_enabled(bool touchpad_enabled) {
    config_touchpad_enabled = touchpad_enabled;
    return system_cfgfile_update();
}

bool system_config_get_notification_enabled(void) {
    return config_notification_enabled;
}

bool system_config_set_notification_enabled(bool notification_enabled) {
    config_notification_enabled = notification_enabled;
    return system_cfgfile_update();
}

bool system_config_get_keyword_spotting_enabled(void) {
    return config_keyword_spotting_enabled;
}

bool system_config_set_keyword_spotting_enabled(bool keyword_spotting_enabled) {
    config_keyword_spotting_enabled = keyword_spotting_enabled;
    return system_cfgfile_update();
}

uint32_t system_config_get_kws_hit_value(void) {
    return config_kws_hit_value;
}

bool system_config_matches_kws_hit_value(uint32_t kws_hit_value) {
    for (size_t i = 0; i < config_kws_hit_values_count; ++i) {
        if (config_kws_hit_values[i] == kws_hit_value) {
            return true;
        }
    }
    return false;
}

bool system_config_get_idle_detection_enabled(void) {
    return config_idle_detection_enabled;
}

bool system_config_set_idle_detection_enabled(bool idle_detection_enabled) {
    config_idle_detection_enabled = idle_detection_enabled;
    return system_cfgfile_update();
}

bool system_config_get_head_gesture_config(system_head_gesture_config_t* config) {
    if (config == NULL) {
        floatair_err("config is NULL");
        return false;
    }

    *config = config_head_gesture;
    return true;
}

bool system_config_set_head_gesture_config(const system_head_gesture_config_t* config) {
    if (config == NULL) {
        floatair_err("config is NULL");
        return false;
    }

    config_head_gesture = *config;
    return system_cfgfile_update();
}

uint8_t system_config_get_displayMode(void) {
    return config_display_mode;
}

bool system_config_set_displayMode(uint8_t displayMode) {
    config_display_mode = displayMode;
    return system_cfgfile_update();
}

uint32_t system_config_get_displaylevel(void) {
    floatair_dbg("displaylevel %" PRIu32, display_level);
    return display_level;
}

bool system_config_set_displaylevel(uint32_t level) {
    floatair_dbg("displaylevel %" PRIu32, level);
    display_level = level;
    return system_cfgfile_update();
}

uint32_t system_config_get_displayposition(void) {
    return display_position;
}

bool system_config_set_displayposition(uint32_t position) {
    uint32_t previous = display_position;

    if (position < (uint32_t)SYSTEM_DISPLAY_POSITION_TOP ||
        position > (uint32_t)SYSTEM_DISPLAY_POSITION_BOTTOM) {
        floatair_err("displayposition invalid: %" PRIu32, position);
        return false;
    }

    display_position = position;
    if (system_cfgfile_update()) {
        return true;
    }

    display_position = previous;
    return false;
}

uint16_t system_config_get_lcd_sleep_timeout(void) {
    return config_lcd_sleep_timeout;
}

bool system_config_set_lcd_sleep_timeout(uint16_t lcd_sleep_timeout) {
    system_cfg_assign_sleep_timeout_fields(lcd_sleep_timeout);
    return system_cfgfile_update();
}

uint16_t system_config_get_inactivity_timeout(void) {
    return config_inactivity_timeout;
}

bool system_config_set_inactivity_timeout(uint16_t inactivity_timeout) {
    system_cfg_assign_sleep_timeout_fields(inactivity_timeout);
    return system_cfgfile_update();
}

uint16_t system_config_get_deep_sleep_timeout(void) {
    return config_deep_sleep_timeout;
}

bool system_config_set_deep_sleep_timeout(uint16_t deep_sleep_timeout) {
    config_deep_sleep_timeout = deep_sleep_timeout;
    return system_cfgfile_update();
}

bool system_config_get_bl_auto(void) {
    return config_bl_auto;
}

bool system_config_set_bl_auto(bool bl_auto) {
    config_bl_auto = bl_auto;
    return system_cfgfile_update();
}

uint8_t system_config_get_brightness(void) {
    return config_brightness;
}

bool system_config_set_brightness(uint8_t brightness) {
    config_brightness = brightness;
    return system_cfgfile_update();
}

char* system_config_get_curlang(void) {
    return config_curlang;
}

bool system_config_set_curlang(char* curlang) {
    if (!curlang) {
        floatair_err("curlang is NULL");
        return false;
    }
    /* normalize: accept both "en-US" and "en-US.json" */
    char norm[64] = {0};
    size_t n = strlen(curlang);
    if (n >= 5 && strcmp(curlang + (n - 5), ".json") == 0) {
        size_t m = n - 5;
        if (m >= sizeof(norm)) m = sizeof(norm) - 1;
        memcpy(norm, curlang, m);
        norm[m] = '\0';
    } else {
        snprintf(norm, sizeof(norm), "%s", curlang);
    }
    char* dup_lang = strdup(norm);
    floatair_assert(dup_lang != NULL, "strdup curlang failed");
    if (config_curlang) {
        free(config_curlang);
        config_curlang = NULL;
    }
    config_curlang = dup_lang;
    bool saved = system_cfgfile_update();
    if (saved) {
        system_i18n_reload();
        system_ui_refresh_bt_disconnect_overlay_text();
    }
    return saved;
}

size_t system_config_get_homeunits_count(void) {
    return config_homeunits_count;
}

const char* system_config_get_homeunit(size_t index) {
    if (index >= config_homeunits_count || config_homeunits == NULL) {
        return NULL;
    }
    return config_homeunits[index];
}

bool system_config_set_homeunits(const char* const* homeunits, size_t count) {
    char** copied = NULL;
    size_t copied_count = 0;

    if (homeunits == NULL && count > 0) {
        return false;
    }
    if (!system_cfgfile_copy_homeunits(homeunits, count, &copied, &copied_count)) {
        return false;
    }
    system_cfgfile_free_homeunits();
    config_homeunits = copied;
    config_homeunits_count = copied_count;
    return system_cfgfile_update();
}

bool system_cfgfile_load(void) {
    if (system_cfgfile_inited) {
        floatair_dbg("system_cfgfile_load, inited");
        return true;
    }
    const char* cfg_path = system_config_path();
    cJSON* root = system_config_load_json(cfg_path);
    if (!root) {
        return false;
    }

    parse_bool_key(root, "wearDetectionEnabled", &config_wear_detection_enabled);
    parse_bool_key(root, "imudoubletapEnabled", &config_imudoubletap_enabled);
	parse_bool_key(root, "imutiltEnabled", &config_imutilt_enabled);
    parse_bool_key(root, "touchpadEnabled", &config_touchpad_enabled);
    parse_bool_key(root, "notificationEnabled", &config_notification_enabled);
    parse_bool_key(root, "keywordSpottingEnabled", &config_keyword_spotting_enabled);
    system_cfgfile_parse_kws_hit_value(root);
    parse_bool_key(root, "idleDetectionEnabled", &config_idle_detection_enabled);
    cJSON* head_gesture = cJSON_GetObjectItemCaseSensitive(root, "headGestureConfig");
    if (cJSON_IsObject(head_gesture)) {
        cJSON* up_enabled = cJSON_GetObjectItemCaseSensitive(head_gesture, "up_enabled");
        cJSON* down_enabled = cJSON_GetObjectItemCaseSensitive(head_gesture, "down_enabled");
        if (up_enabled || down_enabled) {
            parse_bool_key(head_gesture, "up_enabled", &config_head_gesture.up_enabled);
            parse_bool_key(head_gesture, "down_enabled", &config_head_gesture.down_enabled);
        } else {
            bool legacy_enable = false;
            parse_bool_key(head_gesture, "enable", &legacy_enable);
            config_head_gesture.up_enabled = legacy_enable;
            config_head_gesture.down_enabled = legacy_enable;
        }

        cJSON* item = cJSON_GetObjectItemCaseSensitive(head_gesture, "up_deg");
        if (cJSON_IsNumber(item)) {
            config_head_gesture.up_deg = (int32_t)item->valuedouble;
        }

        item = cJSON_GetObjectItemCaseSensitive(head_gesture, "down_deg");
        if (cJSON_IsNumber(item)) {
            config_head_gesture.down_deg = (int32_t)item->valuedouble;
        }

        item = cJSON_GetObjectItemCaseSensitive(head_gesture, "base_deg");
        if (cJSON_IsNumber(item)) {
            config_head_gesture.base_deg = (int32_t)item->valuedouble;
        }
    }
    parse_u8_key(root, "displayMode", &config_display_mode);
    parse_u16_key(root, "lcd_sleep_timeout", &config_lcd_sleep_timeout);
    parse_u16_key(root, "deep_sleep_timeout", &config_deep_sleep_timeout);
    parse_u16_key(root, "inactivity_timeout", &config_inactivity_timeout);
    system_cfg_normalize_sleep_timeout_fields();
    parse_bool_key(root, "bl_auto", &config_bl_auto);
    config_brightness = (g_section_data.jyt_default_brightness > UINT8_MAX)
                            ? UINT8_MAX
                            : (uint8_t)g_section_data.jyt_default_brightness;
    parse_u8_key(root, "brightness", &config_brightness);
    parse_string_key_dup(root, "curlang", &config_curlang);
    if (config_curlang && config_curlang[0] == '\0') {
        free(config_curlang);
        config_curlang = NULL;
    }
    if (config_curlang) {
        size_t n = strlen(config_curlang);
        if (n >= 5 && strcmp(config_curlang + (n - 5), ".json") == 0) {
            size_t m = n - 5;
            char* norm = (char*)malloc(m + 1);
            floatair_assert(norm != NULL, "malloc curlang failed");
            memcpy(norm, config_curlang, m);
            norm[m] = '\0';
            free(config_curlang);
            config_curlang = norm;
        }
    }
    if (!system_cfgfile_parse_homeunits(root)) {
        cJSON_Delete(root);
        system_cfgfile_clear_runtime_state();
        return false;
    }
    config_lcd.ui_x_begin = SYSTEM_LCD_UI_X_BEGIN;
    config_lcd.ui_y_begin = SYSTEM_LCD_UI_Y_BEGIN;
    config_lcd.ui_width = SYSTEM_LCD_UI_WIDTH;
    config_lcd.ui_height = SYSTEM_LCD_UI_HEIGHT;

   
    system_cfgfile_parse_userguide(root);
    parse_u32_key(root, "displaylevel", &display_level);
    parse_u32_key(root, "displaydistancelevel", &display_level);
    display_position = SYSTEM_DISPLAY_POSITION_BOTTOM;
    parse_u32_key(root, "displayposition", &display_position);
    if (display_position < (uint32_t)SYSTEM_DISPLAY_POSITION_TOP ||
        display_position > (uint32_t)SYSTEM_DISPLAY_POSITION_BOTTOM) {
        floatair_warn("displayposition invalid in config: %" PRIu32 ", use bottom", display_position);
        display_position = SYSTEM_DISPLAY_POSITION_BOTTOM;
    }
    cJSON* platform = cJSON_GetObjectItemCaseSensitive(root, "platform");
    if (platform != NULL && !cJSON_IsString(platform)) {
        floatair_err("platform is invalid");
        cJSON_Delete(root);
        system_cfgfile_clear_runtime_state();
        return false;
    }
    if (cJSON_IsString(platform) && !app_router_set_default_app_platform(platform->valuestring)) {
        cJSON_Delete(root);
        system_cfgfile_clear_runtime_state();
        return false;
    }

    cJSON_Delete(root);
    system_cfgfile_inited = true;
    system_cfgfile_dump();
    return true;
}

bool system_cfgfile_unload(void) {
    if (!system_cfgfile_inited) {
        floatair_err("system_cfgfile_unload, not inited");
        return false;
    }
    system_cfgfile_update();
    if (config_curlang) {
        free(config_curlang);
        config_curlang = NULL;
    }
    system_cfgfile_free_homeunits();
    system_cfgfile_inited = false;
    return true;
}

bool system_cfgfile_update(void) {
    if (!system_cfgfile_inited) {
        floatair_err("system_cfgfile_update, not inited");
        return false;
    }

    cJSON* root = system_config_load_json(system_config_path());
    if (!root) {
        floatair_err("load effective config failed: %s", system_config_path());
        return false;
    }

    cJSON_DeleteItemFromObjectCaseSensitive(root, "wearDetectionEnabled");
    cJSON_AddItemToObject(
        root, "wearDetectionEnabled", cJSON_CreateBool(config_wear_detection_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "imudoubletapEnabled");
    cJSON_AddItemToObject(root, "imudoubletapEnabled", cJSON_CreateBool(config_imudoubletap_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "imutiltEnabled");
    cJSON_AddItemToObject(root, "imutiltEnabled", cJSON_CreateBool(config_imutilt_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "touchpadEnabled");
    cJSON_AddItemToObject(root, "touchpadEnabled", cJSON_CreateBool(config_touchpad_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "notificationEnabled");
    cJSON_AddItemToObject(
        root, "notificationEnabled", cJSON_CreateBool(config_notification_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "keywordSpottingEnabled");
    cJSON_AddItemToObject(
        root, "keywordSpottingEnabled", cJSON_CreateBool(config_keyword_spotting_enabled));

    if (!system_cfgfile_update_kws_hit_value(root)) {
        cJSON_Delete(root);
        floatair_err("update kwsHitValue failed");
        return false;
    }

    cJSON_DeleteItemFromObjectCaseSensitive(root, "idleDetectionEnabled");
    cJSON_AddItemToObject(
        root, "idleDetectionEnabled", cJSON_CreateBool(config_idle_detection_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "headGestureConfig");
    cJSON* head_gesture_upd = cJSON_AddObjectToObject(root, "headGestureConfig");
    if (head_gesture_upd == NULL) {
        cJSON_Delete(root);
        floatair_err("create headGestureConfig failed");
        return false;
    }
    cJSON_AddItemToObject(
        head_gesture_upd, "up_enabled", cJSON_CreateBool(config_head_gesture.up_enabled));
    cJSON_AddItemToObject(
        head_gesture_upd, "down_enabled", cJSON_CreateBool(config_head_gesture.down_enabled));
    cJSON_AddItemToObject(
        head_gesture_upd, "up_deg", cJSON_CreateNumber((double)config_head_gesture.up_deg));
    cJSON_AddItemToObject(
        head_gesture_upd, "down_deg", cJSON_CreateNumber((double)config_head_gesture.down_deg));
    cJSON_AddItemToObject(
        head_gesture_upd, "base_deg", cJSON_CreateNumber((double)config_head_gesture.base_deg));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "displayMode");
    cJSON_AddItemToObject(root, "displayMode", cJSON_CreateNumber((double) config_display_mode));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "lcd_sleep_timeout");
    cJSON_AddItemToObject(
        root, "lcd_sleep_timeout", cJSON_CreateNumber((double) config_lcd_sleep_timeout));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "deep_sleep_timeout");
    cJSON_AddItemToObject(
        root, "deep_sleep_timeout", cJSON_CreateNumber((double) config_deep_sleep_timeout));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "inactivity_timeout");
    cJSON_AddItemToObject(
        root, "inactivity_timeout", cJSON_CreateNumber((double) config_inactivity_timeout));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "bl_auto");
    cJSON_AddItemToObject(root, "bl_auto", cJSON_CreateBool(config_bl_auto));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "brightness");
    cJSON_AddItemToObject(root, "brightness", cJSON_CreateNumber((double)config_brightness));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "curlang");
    if (config_curlang) {
        const char* v = config_curlang;
        size_t n = strlen(v);
        if (n >= 5 && strcmp(v + (n - 5), ".json") == 0) {
            size_t m = n - 5;
            char norm[64] = {0};
            if (m >= sizeof(norm)) m = sizeof(norm) - 1;
            memcpy(norm, v, m);
            norm[m] = '\0';
            cJSON_AddItemToObject(root, "curlang", cJSON_CreateString(norm));
        } else {
            cJSON_AddItemToObject(root, "curlang", cJSON_CreateString(v));
        }
    } else {
        cJSON_AddItemToObject(root, "curlang", cJSON_CreateString(""));
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, "homeunits");
    cJSON* homeunits = cJSON_AddArrayToObject(root, "homeunits");
    if (homeunits == NULL) {
        cJSON_Delete(root);
        floatair_err("create homeunits failed");
        return false;
    }
    for (size_t i = 0; i < config_homeunits_count; ++i) {
        cJSON_AddItemToArray(homeunits,
                             cJSON_CreateString(config_homeunits[i] != NULL ? config_homeunits[i] : ""));
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, "lcdinfo");

    cJSON_DeleteItemFromObjectCaseSensitive(root, "onlyCenterName");
    
    cJSON_DeleteItemFromObjectCaseSensitive(root, "userguide");
    cJSON_AddItemToObject(root, "userguide", cJSON_CreateString(system_config_get_userguide()));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "userguidefinish");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "homestyle");

    cJSON_DeleteItemFromObjectCaseSensitive(root, "displaylevel");
    cJSON_AddItemToObject(
        root, "displaylevel", cJSON_CreateNumber((double)display_level));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "displaydistance");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "displayposition");
    cJSON_AddItemToObject(
        root, "displayposition", cJSON_CreateNumber((double)display_position));

    int ret_code = system_config_save_json(system_config_path(), root);
    cJSON_Delete(root);
    return ret_code == 0;
}

void system_cfgfile_dump(void) {
    if (!system_cfgfile_inited) {
        floatair_err("system_cfgfile_dump, not inited");
        return;
    }
    floatair_dbg("Begin");
    floatair_dbg("config_wear_detection_enabled: %s",
                  config_wear_detection_enabled ? "true" : "false");
    floatair_dbg("config_touchpad_enabled: %s", config_touchpad_enabled ? "true" : "false");
    floatair_dbg("config_notification_enabled: %s",
                  config_notification_enabled ? "true" : "false");
    floatair_dbg("config_keyword_spotting_enabled: %s",
                  config_keyword_spotting_enabled ? "true" : "false");
    floatair_dbg("config_kws_hit_value: %" PRIu32, config_kws_hit_value);
    for (size_t i = 0; i < config_kws_hit_values_count; ++i) {
        floatair_dbg("config_kws_hit_values[%u]: %" PRIu32,
                     (unsigned)i,
                     config_kws_hit_values[i]);
    }
    floatair_dbg("config_idle_detection_enabled: %s",
                  config_idle_detection_enabled ? "true" : "false");
    floatair_dbg("config_head_gesture.up_enabled: %s",
                  config_head_gesture.up_enabled ? "true" : "false");
    floatair_dbg("config_head_gesture.down_enabled: %s",
                  config_head_gesture.down_enabled ? "true" : "false");
    floatair_dbg("config_head_gesture.up_deg: %" PRId32, config_head_gesture.up_deg);
    floatair_dbg("config_head_gesture.down_deg: %" PRId32, config_head_gesture.down_deg);
    floatair_dbg("config_head_gesture.base_deg: %" PRId32, config_head_gesture.base_deg);
    floatair_dbg("config_display_mode: %u", config_display_mode);
    floatair_dbg("config_lcd_sleep_timeout: %u", config_lcd_sleep_timeout);
    floatair_dbg("config_deep_sleep_timeout: %u", config_deep_sleep_timeout);
    floatair_dbg("config_inactivity_timeout: %u", config_inactivity_timeout);
    floatair_dbg("config_bl_auto: %s", config_bl_auto ? "true" : "false");
    floatair_dbg("config_brightness: %u", config_brightness);
    floatair_dbg("config_curlang: %s", config_curlang ? config_curlang : "NULL");
    floatair_dbg("config_homeunits_count: %u", (unsigned)config_homeunits_count);
    floatair_dbg("config_ui_x_begin: %" PRIu32, config_lcd.ui_x_begin);
    floatair_dbg("config_ui_y_begin: %" PRIu32, config_lcd.ui_y_begin);
    floatair_dbg("config_ui_width: %" PRIu32, config_lcd.ui_width);
    floatair_dbg("config_ui_height: %" PRIu32, config_lcd.ui_height);

    floatair_dbg("user_guide: %s", user_guide ? user_guide : "");
    floatair_dbg("display_level: %" PRIu32, display_level);
    floatair_dbg("display_position: %" PRIu32, display_position);
    floatair_dbg("End");
}



const char* system_config_get_userguide(void) {
    if (user_guide == NULL) {
        system_cfgfile_set_userguide_runtime(SYSTEM_USERGUIDE_PROGRESS_FALSE);
    }
    floatair_dbg("user_guide %s", user_guide);
    return user_guide;
}

bool system_config_set_userguide(const char* progress) {
    if (!system_cfgfile_is_valid_userguide(progress)) {
        floatair_err("invalid userguide progress: %s", progress != NULL ? progress : "NULL");
        return false;
    }
    system_cfgfile_set_userguide_runtime(progress);
    return system_cfgfile_update();
}

bool system_config_is_userguide_finished(void) {
    const char* progress = system_config_get_userguide();

    return strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_TRUE) == 0;
}

bool system_config_get_langselection_finish(void) {
    const char* curlang = system_config_get_curlang();
    floatair_dbg("curlang %s", curlang == NULL ? "NULL" : curlang);
    bool finished = (curlang != NULL && curlang[0] != '\0');
    floatair_dbg("langselection_finish %d", finished);
    return finished;
}

bool system_config_set_langselection_finish(const char* curlang) {
    if (!curlang) {
        floatair_err("curlang is NULL");
        return false;
    }
    return system_config_set_curlang((char*)curlang);
}
