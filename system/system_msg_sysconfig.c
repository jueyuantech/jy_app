/**
 * @file system_msg_sysconfig.c
 * @brief 系统配置消息解析、配置写入和配置同步实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include <time.h>
#include <lvgl.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "system/system.h"
#include "system/system_runtime_state.h"
#include "system/system_timer.h"
#include "app_lcd.h"
#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_router.h"
#include "home/home.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "sys_adapter.h"

static bool system_home_menu_name_to_id(const char* name, uint32_t* id) {
    if (name == NULL || id == NULL) {
        return false;
    }
    for (size_t i = 0; i < g_home_units_count; ++i) {
        if (g_home_units_arr[i].name != NULL && strcmp(g_home_units_arr[i].name, name) == 0) {
            *id = g_home_units_arr[i].id;
            return true;
        }
    }
    return false;
}

static const char* system_home_menu_id_to_name(uint32_t id) {
    for (size_t i = 0; i < g_home_units_count; ++i) {
        if (g_home_units_arr[i].id == id) {
            return g_home_units_arr[i].name;
        }
    }
    return NULL;
}

static bool system_home_menu_is_supported_name(const char* name) {
    return name != NULL && home_is_supported_app(name);
}

static bool system_home_menu_names_contains(char** names, size_t count, const char* name) {
    if (names == NULL || name == NULL) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static const char* system_home_menu_visible_name_at(size_t index) {
    size_t configured_count = system_config_get_homeunits_count();

    if (configured_count > 0) {
        return system_config_get_homeunit(index);
    }
    return index < g_home_units_count ? g_home_units_arr[index].name : NULL;
}

static size_t system_home_menu_visible_count(void) {
    size_t configured_count = system_config_get_homeunits_count();

    return configured_count > 0 ? configured_count : g_home_units_count;
}

static bool system_home_menu_visible_first_occurrence(size_t index) {
    const char* name = system_home_menu_visible_name_at(index);

    if (!system_home_menu_is_supported_name(name)) {
        return false;
    }
    for (size_t i = 0; i < index; ++i) {
        const char* prev_name = system_home_menu_visible_name_at(i);

        if (prev_name != NULL && strcmp(prev_name, name) == 0) {
            return false;
        }
    }
    return true;
}

static size_t system_home_menu_count_name_ids(bool all_apps) {
    size_t count = all_apps ? g_home_units_count : system_home_menu_visible_count();
    size_t valid_count = 0;

    for (size_t i = 0; i < count; ++i) {
        const char* name = all_apps ? g_home_units_arr[i].name : system_home_menu_visible_name_at(i);
        uint32_t id = 0;

        if (system_home_menu_name_to_id(name, &id) &&
            (all_apps || system_home_menu_visible_first_occurrence(i))) {
            valid_count++;
        }
    }
    return valid_count;
}

static void system_home_menu_write_name_ids(msg_pack_writer_t* writer, const char* key, bool all_apps) {
    size_t count = all_apps ? g_home_units_count : system_home_menu_visible_count();

    mpack_write_cstr(&writer->writer, key);
    mpack_start_array(&writer->writer, (uint32_t)system_home_menu_count_name_ids(all_apps));
    for (size_t i = 0; i < count; ++i) {
        const char* name = all_apps ? g_home_units_arr[i].name : system_home_menu_visible_name_at(i);
        uint32_t id = 0;

        if (system_home_menu_name_to_id(name, &id) &&
            (all_apps || system_home_menu_visible_first_occurrence(i))) {
            mpack_write_u32(&writer->writer, id);
        }
    }
    mpack_finish_array(&writer->writer);
}

static uint32_t system_home_menu_get_selected_id(void) {
    const char* selected_name = home_view_get_selected_app_name();
    uint32_t selected_id = 0;

    if (system_home_menu_name_to_id(selected_name, &selected_id)) {
        return selected_id;
    }
    selected_name = system_home_menu_visible_name_at(0);
    if (system_home_menu_name_to_id(selected_name, &selected_id)) {
        return selected_id;
    }
    return 0;
}

static bool system_home_menu_parse_visible(mpack_node_t node, char*** out_names, size_t* out_count) {
    mpack_node_t visible_node = mpack_node_map_cstr_optional(node, "visible");
    char** names = NULL;
    size_t count = 0;

    if (out_names == NULL || out_count == NULL) {
        return false;
    }
    *out_names = NULL;
    *out_count = 0;
    if (mpack_node_is_missing(visible_node) || mpack_node_type(visible_node) != mpack_type_array) {
        floatair_err("visible is invalid");
        return false;
    }
    count = mpack_node_array_length(visible_node);
    if (count == 0) {
        floatair_err("visible is empty");
        return false;
    }
    names = (char**)calloc(count, sizeof(char*));
    if (names == NULL) {
        floatair_err("alloc visible failed");
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        mpack_node_t item = mpack_node_array_at(visible_node, i);
        uint32_t id = 0;
        const char* name = NULL;

        if (mpack_node_type(item) != mpack_type_uint) {
            floatair_err("visible[%u] type err", (unsigned)i);
            goto fail;
        }
        id = mpack_node_u32(item);
        name = system_home_menu_id_to_name(id);
        if (!system_home_menu_is_supported_name(name) ||
            system_home_menu_names_contains(names, i, name)) {
            floatair_err("visible[%u] unsupported or duplicate id=%" PRIu32, (unsigned)i, id);
            goto fail;
        }
        names[i] = strdup(name);
        if (names[i] == NULL) {
            floatair_err("visible[%u] dup failed", (unsigned)i);
            goto fail;
        }
    }
    *out_names = names;
    *out_count = count;
    return true;

fail:
    for (size_t i = 0; i < count; ++i) {
        free(names[i]);
    }
    free(names);
    return false;
}

static void system_home_menu_free_names(char** names, size_t count) {
    if (names == NULL) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(names[i]);
    }
    free(names);
}

static bool system_systemconfig_getall(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");

    time_t now = time(NULL);
    uint64_t ts = (uint64_t)now;
    char timebuf[32] = {0};
#if defined(__USE_POSIX) || defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    {
        struct tm tminfo_local;
        if (localtime_r(&now, &tminfo_local) != NULL) {
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tminfo_local);
        }
    }
#else
    struct tm* ptm = localtime(&now);
    if (ptm) {
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", ptm);
    }
#endif
    const char* timezone_str = "Asia/Shanghai";

    uint8_t bl = system_config_get_brightness();
    uint8_t display_mode = system_config_get_displayMode();
    const char* lang = system_config_get_curlang();
    uint16_t inactivity = system_config_get_inactivity_timeout();
    uint16_t poweroff   = system_config_get_deep_sleep_timeout();
    uint8_t wear_en     = system_config_get_wear_detection_enabled() ? 1 : 0;
    uint8_t touch_en    = system_config_get_touchpad_enabled() ? 1 : 0;
    uint8_t idle_en     = system_config_get_idle_detection_enabled() ? 1 : 0;
    uint8_t notif_en    = system_config_get_notification_enabled() ? 1 : 0;
    uint8_t kws_en      = system_config_get_keyword_spotting_enabled() ? 1 : 0;
    uint8_t auto_bl_en  = system_config_get_bl_auto() ? 1 : 0;
    uint8_t font_size   = get_system_font_size();
    uint32_t display_level = system_config_get_displaylevel();
    size_t homeunits_count = system_config_get_homeunits_count();
    system_head_gesture_config_t head_gesture = {0};
    (void)system_config_get_head_gesture_config(&head_gesture);

    mpack_start_map(&writer->writer, 17);
    mpack_write_cstr(&writer->writer, "time");
    mpack_write_u64(&writer->writer, ts);

    mpack_write_cstr(&writer->writer, "timeConfig");
    mpack_start_map(&writer->writer, 4);
    mpack_write_cstr(&writer->writer, "time");
    mpack_write_cstr(&writer->writer, timebuf[0] ? timebuf : "1970-01-01 00:00:00");
    mpack_write_cstr(&writer->writer, "timestamp");
    mpack_write_u64(&writer->writer, ts);
    mpack_write_cstr(&writer->writer, "timezone");
    mpack_write_cstr(&writer->writer, timezone_str);
    mpack_write_cstr(&writer->writer, "userFormat");
    mpack_write_cstr(&writer->writer, "yyyy-MM-dd HH:mm:ss");
    mpack_finish_map(&writer->writer);

    mpack_write_cstr(&writer->writer, "displayConfig");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "mode");
    mpack_write_u8(&writer->writer, display_mode);
    mpack_finish_map(&writer->writer);

    mpack_write_cstr(&writer->writer, "brightness");
    mpack_write_u8(&writer->writer, bl);

    mpack_write_cstr(&writer->writer, "autoBrightnessEnabled");
    mpack_write_u8(&writer->writer, auto_bl_en);

    mpack_write_cstr(&writer->writer, "fontSize");
    mpack_write_u8(&writer->writer, font_size);

    mpack_write_cstr(&writer->writer, "language");
    mpack_write_cstr(&writer->writer, lang ? lang : "NA");

    mpack_write_cstr(&writer->writer, "homeunits");
    mpack_start_array(&writer->writer, (uint32_t)homeunits_count);
    for (size_t i = 0; i < homeunits_count; ++i) {
        const char* homeunit = system_config_get_homeunit(i);
        mpack_write_cstr(&writer->writer, homeunit != NULL ? homeunit : "");
    }
    mpack_finish_array(&writer->writer);

    mpack_write_cstr(&writer->writer, "inactivityTimeout");
    mpack_write_u16(&writer->writer, inactivity);

    mpack_write_cstr(&writer->writer, "poweroffTimeout");
    mpack_write_u16(&writer->writer, poweroff);

    mpack_write_cstr(&writer->writer, "wearDetectionEnabled");
    mpack_write_u8(&writer->writer, wear_en);

    mpack_write_cstr(&writer->writer, "headGestureConfig");
    mpack_start_map(&writer->writer, 5);
    mpack_write_cstr(&writer->writer, "upEnabled");
    mpack_write_u8(&writer->writer, head_gesture.up_enabled ? 1 : 0);
    mpack_write_cstr(&writer->writer, "downEnabled");
    mpack_write_u8(&writer->writer, head_gesture.down_enabled ? 1 : 0);
    mpack_write_cstr(&writer->writer, "upDeg");
    mpack_write_i32(&writer->writer, head_gesture.up_deg);
    mpack_write_cstr(&writer->writer, "downDeg");
    mpack_write_i32(&writer->writer, head_gesture.down_deg);
    mpack_write_cstr(&writer->writer, "baseDeg");
    mpack_write_i32(&writer->writer, head_gesture.base_deg);
    mpack_finish_map(&writer->writer);

    mpack_write_cstr(&writer->writer, "touchpadEnabled");
    mpack_write_u8(&writer->writer, touch_en);

    mpack_write_cstr(&writer->writer, "idleDetectionEnabled");
    mpack_write_u8(&writer->writer, idle_en);

    mpack_write_cstr(&writer->writer, "displayDistanceLevel");
    mpack_write_u8(&writer->writer, display_level);

    mpack_write_cstr(&writer->writer, "keywordSpottingEnabled");
    mpack_write_u8(&writer->writer, kws_en);

    mpack_write_cstr(&writer->writer, "notificationEnabled");
    mpack_write_u8(&writer->writer, notif_en);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_settime(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    uint32_t time = 0;
    if (!app_msg_get_u32(node, false, "time", &time)) {
        floatair_err("time is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("time %" PRIu32, time);
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_gettimeconfig(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_settimeconfig(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");

    char time_str[MSG_STR_MAX_LEN] = {0};
    if (!app_msg_get_str(node, "time", time_str, sizeof(time_str))) {
        floatair_err("time is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("time %s", time_str);
    uint64_t timestamp = 0;
    if (!app_msg_get_u64(node, false, "timestamp", &timestamp)) {
        floatair_err("timestamp is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("timestamp %" PRIu64, timestamp);
    char timezone[MSG_STR_MAX_LEN] = {0};
    if (!app_msg_get_str(node, "timezone", timezone, sizeof(timezone))) {
        floatair_err("timezone is NULL, ignore");
    } else {
        floatair_info("timezone %s", timezone);
    }

    char user_format[MSG_STR_MAX_LEN] = {0};
    if (!app_msg_get_str(node, "userFormat", user_format, sizeof(user_format))) {
        floatair_err("userFormat is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("userFormat %s", user_format);

    floatair_info("sync time from phone: time=%s timestamp=%" PRIu64, time_str, timestamp);
    const char *format = "%Y-%m-%d %H:%M:%S";
    if (set_system_time_from_string(time_str, format) != 0) {
        floatair_err("set system time failed, time=%s", time_str);
        return app_mpack_send_ack(msg, ErrBizErr);
    }

    time_t  time_now = time(NULL);
    struct tm* ptm = localtime(&time_now);
    if (ptm) {
        floatair_info("time_now %llu %02d:%02d:%02d", (unsigned long long)time_now, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    } else {
        floatair_info("time_now %llu", (unsigned long long)time_now);
    }
    system_ui_set_time_reliable(true);
    if (!system_ui_update_time_from_epoch(time_now)) {
        floatair_err("refresh status bar time failed after phone sync");
    }
    if (!system_timer_lvgl_period_realign_to_minute(time_now)) {
        floatair_warn("realign lvgl minute timer failed after phone sync");
    }
    system_request_device_state();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getbrightness(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "brightness");
    mpack_write_u8(&writer->writer, system_config_get_brightness());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setbrightness(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t brightness = 0;
    if (system_config_get_bl_auto()) {
        floatair_warn("setBrightness rejected while auto brightness enabled");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_u8(node, false, "brightness", &brightness)) {
        floatair_err("brightness is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("setBrightness %u", (unsigned)brightness);
    floatair_lcd_set_brightness(brightness);
    if (!system_config_set_brightness(brightness)) {
        floatair_err("save brightness config failed");
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getfontsize(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_setfontsize(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_getrowspace(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_setrowspace(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_getlanguage(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "language");
    const char* lang = system_config_get_curlang();
    mpack_write_cstr(&writer->writer, lang ? lang : "NA");
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setlanguage(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char lang[MSG_STR_MAX_LEN] = {0};
    if (!app_msg_get_str(node, "language", lang, sizeof(lang))) {
        floatair_err("language is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("language %s", lang);
    if (!system_config_set_curlang(lang)) {
        floatair_err("set language failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (app_manager_current() != NULL && !app_manager_refresh_current()) {
        floatair_warn("refresh current app failed after language change");
        if (app_manager_current() == NULL) {
            app_router_reset_state();
        }
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_gethomeunits(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    size_t count = system_config_get_homeunits_count();

    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "homeunits");
    mpack_start_array(&writer->writer, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) {
        const char* homeunit = system_config_get_homeunit(i);
        mpack_write_cstr(&writer->writer, homeunit != NULL ? homeunit : "");
    }
    mpack_finish_array(&writer->writer);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_sethomeunits(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    mpack_node_t homeunits_node = mpack_node_map_cstr_optional(node, "homeunits");
    char** homeunits = NULL;
    bool ok = false;
    size_t count = 0;

    if (mpack_node_is_missing(homeunits_node) || mpack_node_type(homeunits_node) != mpack_type_array) {
        floatair_err("homeunits is invalid");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    count = mpack_node_array_length(homeunits_node);
    if (count > 0) {
        homeunits = (char**)calloc(count, sizeof(char*));
        if (homeunits == NULL) {
            floatair_err("alloc homeunits failed");
            return app_mpack_send_ack(msg, ErrBizErr);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        mpack_node_t item = mpack_node_array_at(homeunits_node, i);

        if (mpack_node_type(item) != mpack_type_str) {
            floatair_err("homeunits[%u] type err", (unsigned)i);
            for (size_t j = 0; j < i; ++j) {
                free(homeunits[j]);
            }
            free(homeunits);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
        homeunits[i] = mpack_node_utf8_cstr_alloc(item, MSG_STR_MAX_LEN);
        if (homeunits[i] == NULL) {
            floatair_err("homeunits[%u] alloc failed", (unsigned)i);
            for (size_t j = 0; j < i; ++j) {
                free(homeunits[j]);
            }
            free(homeunits);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }
    ok = system_config_set_homeunits((const char* const*)homeunits, count);
    for (size_t i = 0; i < count; ++i) {
        free(homeunits[i]);
    }
    free(homeunits);
    if (!ok) {
        floatair_err("set homeunits failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    home_view_reset_selection();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_gethomemenuconfig(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");

    mpack_start_map(&writer->writer, 3);
    system_home_menu_write_name_ids(writer, "all", true);
    system_home_menu_write_name_ids(writer, "visible", false);
    mpack_write_cstr(&writer->writer, "selected");
    mpack_write_u32(&writer->writer, system_home_menu_get_selected_id());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_sethomemenuconfig(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char** visible_names = NULL;
    size_t visible_count = 0;
    uint32_t selected_id = 0;
    const char* selected_name = NULL;
    bool ok = false;

    if (!system_home_menu_parse_visible(node, &visible_names, &visible_count)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(node, false, "selected", &selected_id)) {
        floatair_err("selected is invalid");
        system_home_menu_free_names(visible_names, visible_count);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    selected_name = system_home_menu_id_to_name(selected_id);
    if (!system_home_menu_names_contains(visible_names, visible_count, selected_name)) {
        floatair_err("selected not in visible, id=%" PRIu32, selected_id);
        system_home_menu_free_names(visible_names, visible_count);
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    ok = system_config_set_homeunits((const char* const*)visible_names, visible_count);
    system_home_menu_free_names(visible_names, visible_count);
    if (!ok) {
        floatair_err("set home menu config failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    home_view_reset_selection();
    if (!home_view_select_app_by_name(selected_name)) {
        floatair_err("select home menu failed: %s", selected_name != NULL ? selected_name : "NULL");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getdisplayconfig(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_setdisplayconfig(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_getdisplaydistancelevel(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "displayDistanceLevel");
    mpack_write_u32(&writer->writer, (uint32_t)system_config_get_displaylevel());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setdisplaydistancelevel(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    uint32_t level = 0;
    if (!app_msg_get_u32(node, false, "displayDistanceLevel", &level)) {
        floatair_err("displayDistanceLevel is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    if (level < 1 || level > 3) {
        floatair_err("displayDistanceLevel invalid: %" PRIu32, level);
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    if (level == system_config_get_displaylevel()) {
        floatair_info("displaylevel unchanged: %" PRIu32, level);
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }
    if (!system_config_set_displaylevel(level)) {
        floatair_err("set displaylevel failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    system_ui_refresh_display_distance_level();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getdisplaydistance(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_setdisplaydistance(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    uint32_t distance = 0;
    if (!app_msg_get_u32(node, false, "distance", &distance)) {
        floatair_err("distance is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("distance %" PRIu32, distance);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getdisplaypopupdepth(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool system_systemconfig_setdisplaypopupdepth(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    uint32_t depth = 0;
    if (!app_msg_get_u32(node, false, "depth", &depth)) {
        floatair_err("depth is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    floatair_info("depth %" PRIu32, depth);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getinactivitytimeout(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer_err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "inactivityTimeout");
    mpack_write_u16(&writer->writer, system_config_get_inactivity_timeout());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setinactivitytimeout(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint32_t timeout32 = 0;
    if (!app_msg_get_u32(node, false, "inactivityTimeout", &timeout32)) {
        floatair_err("inactivityTimeout is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    uint16_t timeout = (uint16_t) timeout32;
    floatair_info("inactivityTimeout %d", timeout);
    if (!system_config_set_inactivity_timeout(timeout)) {
        floatair_err("set inactivityTimeout failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    app_sleep_timer_init();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getpowerofftimeout(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "poweroffTimeout");
    mpack_write_u16(&writer->writer, system_config_get_deep_sleep_timeout());
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setpowerofftimeout(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint32_t timeout32 = 0;
    if (!app_msg_get_u32(node, false, "poweroffTimeout", &timeout32)) {
        floatair_err("poweroffTimeout is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    uint16_t timeout = (uint16_t) timeout32;
    floatair_info("poweroffTimeout %d", timeout);
    if (!system_config_set_deep_sleep_timeout(timeout)) {
        floatair_err("set poweroffTimeout failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getheadgestureconfig(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");

    system_head_gesture_config_t config = {0};
    if (!system_config_get_head_gesture_config(&config)) {
        floatair_err("get headGestureConfig failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }

    mpack_start_map(&writer->writer, 5);
    mpack_write_cstr(&writer->writer, "upEnabled");
    mpack_write_u8(&writer->writer, config.up_enabled ? 1 : 0);
    mpack_write_cstr(&writer->writer, "downEnabled");
    mpack_write_u8(&writer->writer, config.down_enabled ? 1 : 0);
    mpack_write_cstr(&writer->writer, "upDeg");
    mpack_write_i32(&writer->writer, config.up_deg);
    mpack_write_cstr(&writer->writer, "downDeg");
    mpack_write_i32(&writer->writer, config.down_deg);
    mpack_write_cstr(&writer->writer, "baseDeg");
    mpack_write_i32(&writer->writer, config.base_deg);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setheadgestureconfig(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");

    system_head_gesture_config_t config = {0};
    if (!system_config_get_head_gesture_config(&config)) {
        floatair_err("get current headGestureConfig failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }

    uint8_t up_enable = 0;
    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, "upEnabled");
    if (mpack_node_is_missing(parse_node)) {
        floatair_err("upEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (mpack_node_type(parse_node) != mpack_type_uint) {
        floatair_err("upEnabled type err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    up_enable = mpack_node_u8(parse_node);
    config.up_enabled = (up_enable != 0);

    uint8_t down_enable = 0;
    parse_node = mpack_node_map_cstr_optional(node, "downEnabled");
    if (mpack_node_is_missing(parse_node)) {
        floatair_err("downEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (mpack_node_type(parse_node) != mpack_type_uint) {
        floatair_err("downEnabled type err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    down_enable = mpack_node_u8(parse_node);
    config.down_enabled = (down_enable != 0);

    parse_node = mpack_node_map_cstr_optional(node, "upDeg");
    if (!mpack_node_is_missing(parse_node)) {
        if (mpack_node_type(parse_node) == mpack_type_int) {
            config.up_deg = mpack_node_i32(parse_node);
        } else if (mpack_node_type(parse_node) == mpack_type_uint) {
            uint64_t raw = mpack_node_u64(parse_node);
            if (raw > (uint64_t)INT32_MAX) {
                floatair_err("upDeg overflow=%" PRIu64, raw);
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            config.up_deg = (int32_t)raw;
        } else {
            floatair_err("upDeg invalid");
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }

    parse_node = mpack_node_map_cstr_optional(node, "downDeg");
    if (!mpack_node_is_missing(parse_node)) {
        if (mpack_node_type(parse_node) == mpack_type_int) {
            config.down_deg = mpack_node_i32(parse_node);
        } else if (mpack_node_type(parse_node) == mpack_type_uint) {
            uint64_t raw = mpack_node_u64(parse_node);
            if (raw > (uint64_t)INT32_MAX) {
                floatair_err("downDeg overflow=%" PRIu64, raw);
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            config.down_deg = (int32_t)raw;
        } else {
            floatair_err("downDeg invalid");
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }

    parse_node = mpack_node_map_cstr_optional(node, "baseDeg");
    if (!mpack_node_is_missing(parse_node)) {
        if (mpack_node_type(parse_node) == mpack_type_int) {
            config.base_deg = mpack_node_i32(parse_node);
        } else if (mpack_node_type(parse_node) == mpack_type_uint) {
            uint64_t raw = mpack_node_u64(parse_node);
            if (raw > (uint64_t)INT32_MAX) {
                floatair_err("baseDeg overflow=%" PRIu64, raw);
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            config.base_deg = (int32_t)raw;
        } else {
            floatair_err("baseDeg invalid");
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }

    floatair_info("headGestureConfig up_enable=%u down_enable=%u up=%" PRId32
                  " down=%" PRId32 " base=%" PRId32,
                  (unsigned)up_enable,
                  (unsigned)down_enable,
                  config.up_deg,
                  config.down_deg,
                  config.base_deg);

    if (!system_config_set_head_gesture_config(&config)) {
        floatair_err("set headGestureConfig failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }

    float heads_up_threshold = (float)(config.base_deg + config.up_deg);
    float heads_down_threshold = (float)(config.base_deg - config.down_deg);
    if (!system_request_imu_threshold(heads_up_threshold, heads_down_threshold)) {
        floatair_err("request headGestureConfig threshold failed");
        return app_mpack_send_ack(msg, ErrBizErr);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getwaredetectionenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer_err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "wearDetectionEnabled");
    mpack_write_u8(&writer->writer, system_config_get_wear_detection_enabled() ? 1 : 0);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setwaredetectionenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t tmp  = 0;
    if (!app_msg_get_u8(node, false, "wearDetectionEnabled", &tmp)) {
        floatair_err("wearDetectionEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!system_config_set_wear_detection_enabled(tmp != 0)) {
        floatair_err("set wearDetectionEnabled failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    dev_ctl_cmd_t cmd = {
        .dev_type = DEV_WEARING_CTRL,
        .control_code = tmp != 0 ? 1 : 0,
        .data = 0,
    };
    if (!system_request_device_control(&cmd)) {
        floatair_err("request wearDetectionEnabled failed");
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getautobrightnessenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "autoBrightnessEnabled");
    mpack_write_u8(&writer->writer, system_config_get_bl_auto() ? 1 : 0);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setautobrightnessenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t tmp  = 0;
    if (!app_msg_get_u8(node, false, "autoBrightnessEnabled", &tmp)) {
        floatair_err("autoBrightnessEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    bool enabled = (tmp != 0);
    if (!system_config_set_bl_auto(enabled)) {
        floatair_err("set autoBrightnessEnabled failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (enabled) {
        (void)system_runtime_state_apply_auto_brightness();
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_gettouchpadenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "touchpadEnabled");
    mpack_write_u8(&writer->writer, system_config_get_touchpad_enabled() ? 1 : 0);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_settouchpadenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t tmp  = 0;
    if (!app_msg_get_u8(node, false, "touchpadEnabled", &tmp)) {
        floatair_err("touchpadEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!system_config_set_touchpad_enabled(tmp != 0)) {
        floatair_err("set touchpadEnabled failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getidledetectionenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "idleDetectionEnabled");
    mpack_write_u8(&writer->writer, system_config_get_idle_detection_enabled() ? 1 : 0);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setidledetectionenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t tmp  = 0;
    if (!app_msg_get_u8(node, false, "idleDetectionEnabled", &tmp)) {
        floatair_err("idleDetectionEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!system_config_set_idle_detection_enabled(tmp != 0)) {
        floatair_err("set idleDetectionEnabled failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    app_sleep_timer_init();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getnotificationenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "notificationEnabled");
    mpack_write_u8(&writer->writer, system_config_get_notification_enabled() ? 1 : 0);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setnotificationenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t tmp  = 0;
    if (!app_msg_get_u8(node, false, "notificationEnabled", &tmp)) {
        floatair_err("notificationEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!system_config_set_notification_enabled(tmp != 0)) {
        floatair_err("set notificationEnabled failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_systemconfig_getkeywordspottingenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "keywordSpottingEnabled");
    mpack_write_u8(&writer->writer, system_config_get_keyword_spotting_enabled() ? 1 : 0);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

static bool system_systemconfig_setkeywordspottingenabled(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    uint8_t tmp  = 0;
    if (!app_msg_get_u8(node, false, "keywordSpottingEnabled", &tmp)) {
        floatair_err("keywordSpottingEnabled is NULL");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    bool enabled = (tmp != 0);
    if (!system_config_set_keyword_spotting_enabled(enabled)) {
        floatair_err("set keywordSpottingEnabled failed");
        return app_mpack_send_ack(msg, ErrDataErr);
    }

    if (!system_request_keyword_spotting_enabled(enabled)) {
        floatair_err("request keywordSpottingEnabled failed");
        return app_mpack_send_ack(msg, ErrBizErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

app_cmd_func_t system_systemconfig_cmd_funcs[] = {
    {"getAll", system_systemconfig_getall},
    {"setTime", system_systemconfig_settime},
    {"getTimeConfig", system_systemconfig_gettimeconfig},
    {"setTimeConfig", system_systemconfig_settimeconfig},
    {"getBrightness", system_systemconfig_getbrightness},
    {"setBrightness", system_systemconfig_setbrightness},
    {"getFontSize", system_systemconfig_getfontsize},
    {"setFontSize", system_systemconfig_setfontsize},
    {"getRowSpace", system_systemconfig_getrowspace},
    {"setRowSpace", system_systemconfig_setrowspace},
    {"getLanguage", system_systemconfig_getlanguage},
    {"setLanguage", system_systemconfig_setlanguage},
    {"getHomeUnits", system_systemconfig_gethomeunits},
    {"setHomeUnits", system_systemconfig_sethomeunits},
    {"getHomeMenuConfig", system_systemconfig_gethomemenuconfig},
    {"setHomeMenuConfig", system_systemconfig_sethomemenuconfig},
    {"getDisplayConfig", system_systemconfig_getdisplayconfig},
    {"setDisplayConfig", system_systemconfig_setdisplayconfig},
    {"getDisplayDistanceLevel", system_systemconfig_getdisplaydistancelevel},
    {"setDisplayDistanceLevel", system_systemconfig_setdisplaydistancelevel},
    {"getDisplayDistance", system_systemconfig_getdisplaydistance},
    {"setDisplayDistance", system_systemconfig_setdisplaydistance},
    {"getDisplayPopupDepth", system_systemconfig_getdisplaypopupdepth},
    {"setDisplayPopupDepth", system_systemconfig_setdisplaypopupdepth},
    {"getInactivityTimeout", system_systemconfig_getinactivitytimeout},
    {"setInactivityTimeout", system_systemconfig_setinactivitytimeout},
    {"getPoweroffTimeout", system_systemconfig_getpowerofftimeout},
    {"setPoweroffTimeout", system_systemconfig_setpowerofftimeout},
    {"getHeadGestureConfig", system_systemconfig_getheadgestureconfig},
    {"setHeadGestureConfig", system_systemconfig_setheadgestureconfig},
    {"getWearDetectionEnabled", system_systemconfig_getwaredetectionenabled},
    {"setWearDetectionEnabled", system_systemconfig_setwaredetectionenabled},
    {"getAutoBrightnessEnabled", system_systemconfig_getautobrightnessenabled},
    {"setAutoBrightnessEnabled", system_systemconfig_setautobrightnessenabled},
    {"getTouchpadEnabled", system_systemconfig_gettouchpadenabled},
    {"setTouchpadEnabled", system_systemconfig_settouchpadenabled},
    {"getIdleDetectionEnabled", system_systemconfig_getidledetectionenabled},
    {"setIdleDetectionEnabled", system_systemconfig_setidledetectionenabled},
    {"getKeywordSpottingEnabled", system_systemconfig_getkeywordspottingenabled},
    {"setKeywordSpottingEnabled", system_systemconfig_setkeywordspottingenabled},
    {"getNotificationEnabled", system_systemconfig_getnotificationenabled},
    {"setNotificationEnabled", system_systemconfig_setnotificationenabled}};
const size_t system_systemconfig_cmd_funcs_count =
    sizeof(system_systemconfig_cmd_funcs) / sizeof(system_systemconfig_cmd_funcs[0]);
