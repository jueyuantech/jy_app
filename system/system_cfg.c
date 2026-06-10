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
static bool config_touchpad_enabled       = false;
static bool config_notification_enabled   = false;
static bool config_keyword_spotting_enabled = true;
static bool config_idle_detection_enabled = false;
static system_head_gesture_config_t config_head_gesture = {0};
static uint8_t config_display_mode        = 0;
static uint16_t config_lcd_sleep_timeout  = 0;
static uint16_t config_deep_sleep_timeout = 0;
static uint16_t config_inactivity_timeout = 0;
static bool config_bl_auto                = false;
static uint8_t config_brightness          = 0;
static char* config_curlang               = NULL;
static char* config_home_app              = NULL;
system_lcd_t config_lcd                   = {
    .ui_x_begin = SYSTEM_LCD_UI_X_BEGIN,
    .ui_y_begin = SYSTEM_LCD_UI_Y_BEGIN,
    .ui_width = SYSTEM_LCD_UI_WIDTH,
    .ui_height = SYSTEM_LCD_UI_HEIGHT,
};

static bool simple_guide        = false;
static bool user_guide          = false;
static bool user_guide_finish   = false;
static bool play_audio          = false;
static uint32_t display_level       = 0;
static bool system_cfgfile_inited = false;

static bool system_cfgfile_mkdir_parent(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    const char* last = strrchr(path, '/');
    if (!last || last == path) {
        return false;
    }
    char dir[128] = {0};
    size_t n = (size_t)(last - path);
    if (n >= sizeof(dir)) {
        return false;
    }
    memcpy(dir, path, n);
    dir[n] = '\0';
    return floatair_fs_mkdirs(dir) == FLOATAIR_FS_OK;
}

static cJSON* system_cfgfile_create_default_root(void) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON* fontinfo = cJSON_AddObjectToObject(root, "fontinfo");
    if (!fontinfo) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(26));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(0));

    cJSON_AddItemToObject(root, "wearDetectionEnabled", cJSON_CreateBool(true));
    cJSON* head_gesture = cJSON_AddObjectToObject(root, "headGestureConfig");
    if (!head_gesture) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(head_gesture, "up_enabled", cJSON_CreateBool(true));
    cJSON_AddItemToObject(head_gesture, "down_enabled", cJSON_CreateBool(true));
    cJSON_AddItemToObject(head_gesture, "up_deg", cJSON_CreateNumber(25));
    cJSON_AddItemToObject(head_gesture, "down_deg", cJSON_CreateNumber(15));
    cJSON_AddItemToObject(head_gesture, "base_deg", cJSON_CreateNumber(0));

    cJSON_AddItemToObject(root, "touchpadEnabled", cJSON_CreateBool(true));
    cJSON_AddItemToObject(root, "notificationEnabled", cJSON_CreateBool(true));
    cJSON_AddItemToObject(root, "keywordSpottingEnabled", cJSON_CreateBool(true));
    cJSON_AddItemToObject(root, "idleDetectionEnabled", cJSON_CreateBool(true));
    cJSON_AddItemToObject(root, "displayMode", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(root, "lcd_sleep_timeout", cJSON_CreateNumber(30));
    cJSON_AddItemToObject(root, "deep_sleep_timeout", cJSON_CreateNumber(60));
    cJSON_AddItemToObject(root, "inactivity_timeout", cJSON_CreateNumber(120));
    cJSON_AddItemToObject(root, "bl_auto", cJSON_CreateBool(true));

    uint32_t jb = g_section_data.jyt_default_brightness;
    uint8_t brightness = (jb > UINT8_MAX) ? UINT8_MAX : (uint8_t)jb;
    if (brightness == 0) {
        brightness = 128;
    }
    cJSON_AddItemToObject(root, "brightness", cJSON_CreateNumber((double)brightness));

    cJSON_AddItemToObject(root, "curlang", cJSON_CreateString(""));
    cJSON_AddItemToObject(root, "home_app", cJSON_CreateString("home"));
    cJSON_AddItemToObject(root, "simpleguide", cJSON_CreateBool(true));
    cJSON_AddItemToObject(root, "userguide", cJSON_CreateBool(false));
    cJSON_AddItemToObject(root, "userguidefinish", cJSON_CreateBool(false));
    cJSON_AddItemToObject(root, "playaudio", cJSON_CreateBool(true));
    cJSON_AddItemToObject(root, "displaylevel", cJSON_CreateNumber(1));
    cJSON_AddItemToObject(root, "displaydistancelevel", cJSON_CreateNumber(1));

    return root;
}

bool system_cfgfile_rebuild_default(void) {
    const char* path = system_config_path();
    if (!path) {
        return false;
    }
    if (!system_cfgfile_mkdir_parent(path)) {
        return false;
    }
    cJSON* root = system_cfgfile_create_default_root();
    if (!root) {
        return false;
    }
    int ret = save_json(path, root);
    cJSON_Delete(root);
    return ret == 0;
}

static void system_cfgfile_clear_runtime_state(void) {
    if (config_curlang) {
        free(config_curlang);
        config_curlang = NULL;
    }
    if (config_home_app) {
        free(config_home_app);
        config_home_app = NULL;
    }
    system_cfgfile_inited = false;
}

bool system_cfgfile_reset_to_default(void) {
    if (!system_cfgfile_rebuild_default()) {
        return false;
    }
    system_cfgfile_clear_runtime_state();
    return system_cfgfile_load();
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

char* system_config_get_home_app(void) {
    floatair_dbg("home[%s]", config_home_app);
    return config_home_app;
}

bool system_config_set_home_app(char* home_app) {
    if (!home_app) {
        return false;
    }
    char* dup_home = strdup(home_app);
    floatair_assert(dup_home != NULL, "strdup home_app failed");
    if (config_home_app) {
        free(config_home_app);
        config_home_app = NULL;
    }
    config_home_app = dup_home;
    return system_cfgfile_update();
}

bool system_cfgfile_load(void) {
    if (system_cfgfile_inited) {
        floatair_dbg("system_cfgfile_load, inited");
        return true;
    }
    const char* cfg_path = system_config_path();
    cJSON* root = NULL;
    for (int attempt = 0; attempt < 2; attempt++) {
        root = load_json(cfg_path);
        if (!root) {
            floatair_err("load_json %s failed", cfg_path);
            if (!system_cfgfile_rebuild_default()) {
                return false;
            }
            continue;
        }
        break;
    }
    if (!root) {
        return false;
    }

    parse_bool_key(root, "wearDetectionEnabled", &config_wear_detection_enabled);
    parse_bool_key(root, "touchpadEnabled", &config_touchpad_enabled);
    parse_bool_key(root, "notificationEnabled", &config_notification_enabled);
    parse_bool_key(root, "keywordSpottingEnabled", &config_keyword_spotting_enabled);
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
    parse_string_key_dup(root, "home_app", &config_home_app);
    if (config_home_app == NULL || config_home_app[0] == '\0') {
        cJSON_Delete(root);
        system_cfgfile_clear_runtime_state();
        if (!system_cfgfile_rebuild_default()) {
            return false;
        }
        return system_cfgfile_load();
    }
    config_lcd.ui_x_begin = SYSTEM_LCD_UI_X_BEGIN;
    config_lcd.ui_y_begin = SYSTEM_LCD_UI_Y_BEGIN;
    config_lcd.ui_width = SYSTEM_LCD_UI_WIDTH;
    config_lcd.ui_height = SYSTEM_LCD_UI_HEIGHT;

    parse_bool_key(root, "simpleguide", &simple_guide);
    parse_bool_key(root, "userguide", &user_guide);
    parse_bool_key(root, "userguidefinish", &user_guide_finish);
    parse_bool_key(root, "playaudio", &play_audio);
    parse_u32_key(root, "displaylevel", &display_level);
    parse_u32_key(root, "displaydistancelevel", &display_level);

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
    if (config_home_app) {
        free(config_home_app);
        config_home_app = NULL;
    }
    system_cfgfile_inited = false;
    return true;
}

bool system_cfgfile_update(void) {
    if (!system_cfgfile_inited) {
        floatair_err("system_cfgfile_update, not inited");
        return false;
    }

    cJSON* root = load_json(system_config_path());
    if (!root) {
        floatair_err("load_json %s failed", system_config_path());
        if (!system_cfgfile_mkdir_parent(system_config_path())) {
            return false;
        }
        root = system_cfgfile_create_default_root();
        if (!root) {
            return false;
        }
    }

    cJSON_DeleteItemFromObjectCaseSensitive(root, "wearDetectionEnabled");
    cJSON_AddItemToObject(
        root, "wearDetectionEnabled", cJSON_CreateBool(config_wear_detection_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "touchpadEnabled");
    cJSON_AddItemToObject(root, "touchpadEnabled", cJSON_CreateBool(config_touchpad_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "notificationEnabled");
    cJSON_AddItemToObject(
        root, "notificationEnabled", cJSON_CreateBool(config_notification_enabled));

    cJSON_DeleteItemFromObjectCaseSensitive(root, "keywordSpottingEnabled");
    cJSON_AddItemToObject(
        root, "keywordSpottingEnabled", cJSON_CreateBool(config_keyword_spotting_enabled));

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
    }

    cJSON_DeleteItemFromObjectCaseSensitive(root, "home_app");
    if (config_home_app) {
        cJSON_AddItemToObject(root, "home_app", cJSON_CreateString(config_home_app));
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, "lcdinfo");

    cJSON_DeleteItemFromObjectCaseSensitive(root, "onlyCenterName");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "simpleguide");
    cJSON_AddItemToObject(root, "simpleguide", cJSON_CreateBool(simple_guide));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "userguide");
    cJSON_AddItemToObject(root, "userguide", cJSON_CreateBool(user_guide));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "userguidefinish");
    cJSON_AddItemToObject(root, "userguidefinish", cJSON_CreateBool(user_guide_finish));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "playaudio");
    cJSON_AddItemToObject(root, "playaudio", cJSON_CreateBool(play_audio));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "homestyle");

    cJSON_DeleteItemFromObjectCaseSensitive(root, "displaylevel");
    cJSON_AddItemToObject(
        root, "displaylevel", cJSON_CreateNumber((double)display_level));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "displaydistancelevel");
    cJSON_AddItemToObject(
        root, "displaydistancelevel", cJSON_CreateNumber((double)display_level));
    cJSON_DeleteItemFromObjectCaseSensitive(root, "displaydistance");

    int ret_code = save_json(system_config_path(), root);
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
    floatair_dbg("config_home_app: %s", config_home_app ? config_home_app : "NULL");
    floatair_dbg("config_ui_x_begin: %" PRIu32, config_lcd.ui_x_begin);
    floatair_dbg("config_ui_y_begin: %" PRIu32, config_lcd.ui_y_begin);
    floatair_dbg("config_ui_width: %" PRIu32, config_lcd.ui_width);
    floatair_dbg("config_ui_height: %" PRIu32, config_lcd.ui_height);
    floatair_dbg("simple_guide: %d", simple_guide);
    floatair_dbg("user_guide: %d", user_guide);
    floatair_dbg("user_guide_finish: %d", user_guide_finish);
    floatair_dbg("play_audio: %d", play_audio);
    floatair_dbg("display_level: %" PRIu32, display_level);
    floatair_dbg("End");
}

bool home_get_simple_guide(void) {
    floatair_dbg("simple_guide %d", simple_guide);
    return simple_guide;
}

void home_set_simple_guide(bool guide) {
    simple_guide = guide;
    system_cfgfile_update();
}

bool system_config_get_userguide(void) {
    floatair_dbg("user_guide %d", user_guide);
    return user_guide;
}

bool system_config_set_userguide(bool userguide) {
    user_guide = userguide;
    return system_cfgfile_update();
}

bool system_config_get_userguide_finish(void) {
    floatair_dbg("user_guide_finish %d", user_guide_finish);
    return user_guide_finish;
}

bool system_config_set_userguide_finish(bool finish) {
    user_guide_finish = finish;
    return system_cfgfile_update();
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

bool home_get_play_audio(void) {
    floatair_dbg("play_audio %d", play_audio);
    return play_audio;
}

void home_set_play_audio(bool play) {
    play_audio = play;
    system_cfgfile_update();
}
