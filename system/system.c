/**
 * @file system.c
 * @brief 系统公共入口与设备信息接口实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system/system.h"
#include "common/app_framework/app_router.h"
#include "system/system_config_json.h"
#include "system/system_runtime_input.h"
#include "system/system_runtime_ui.h"
#include "system/system_timer.h"
#include "app_lcd.h"
#include "floatair_fs.h"
#include "message.h"

#include <inttypes.h>
#include <string.h>

static bool s_wear_removed = false; ///< 当前是否处于摘下状态，摘下期间任何消息都不重置无交互定时器。
static app_sleep_screen_on_scope_t s_screen_on_scope = APP_SLEEP_SCREEN_ON_SCOPE_OFF; ///< 手机请求的临时常亮范围，仅保存在内存中。

/** jy_app 应用版本号，默认由 CMake 根据 git describe 与产品名注入。 */
#ifndef JY_APP_VERSION_STRING
#define JY_APP_VERSION_STRING "unknown"
#endif

/**
 * @brief 关闭屏幕并请求 OS 执行恢复出厂流程。
 * @return 请求成功发出返回 `true`，否则返回 `false`。
 */
bool system_factoryreset_execute(void) {
    floatair_lcd_set_state(LCD_OFF);
    return system_request_factory_reset();
}

/**
 * @brief 处理 system 应用消息回调。
 * @param[in] node 消息 payload 节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
static bool system_msg_cb(mpack_node_t node, msg_pack_t* msg) {
    return system_route_cmd(node, msg);
}

static app_message_t system_msg = {
    .id   = APP_MSG_ID_SYSTEM,
    .name = "system",
    .cb   = system_msg_cb,
};

/**
 * @brief 初始化 system 应用并注册消息处理器。
 * @return 无返回值。
 */
void system_init(void) {
    int ret = -1;
    ret     = app_msg_register(&system_msg);
    floatair_assert(ret == 0, "app_msg_register failed");
    system_timer_init();
    s_wear_removed = false;
    s_screen_on_scope = APP_SLEEP_SCREEN_ON_SCOPE_OFF;
    app_sleep_timer_init();
    system_runtime_input_reset_wearing_state();
    app_router_reset_state();
    system_sync_config_to_device();
    system_request_device_state();
}

/**
 * @brief 反初始化 system 应用并释放配置与运行时状态。
 * @return 无返回值。
 */
void system_deinit(void) {
    if (!app_router_deinit()) {
        floatair_warn("system deinit skipped app router cleanup");
    }
    app_msg_delete(APP_MSG_ID_SYSTEM);
    system_cfgfile_unload();
    system_timer_deinit();
}

/**
 * @brief 获取系统配置文件路径。
 * @return 返回系统配置文件绝对路径。
 */
const char* system_config_path(void) {
    return floatair_fs_get_system_config_file();
}

/**
 * @brief 判断文件名是否为受支持的图片格式。
 * @param[in] name 待检查的文件名。
 * @return `true` 表示是支持的图片格式，`false` 表示不是。
 */
bool system_is_image_file(const char *name) {
    size_t n = strlen(name);
    if (n < 4) {
        floatair_info("short name: %s", name);
        return false;
    }
    const char *ext = strrchr(name, '.');
    if (!ext) {
        floatair_info("no ext: %s", name);
        return false;
    }
    if (strncmp(ext, ".jpg", strlen(".jpg")) == 0) return true;
    if (strncmp(ext, ".jpeg", strlen(".jpeg")) == 0) return true;
    if (strncmp(ext, ".png", strlen(".png")) == 0)  {
        floatair_info("png file: %s", name);
        return false;
    }
    return false;
}

/**
 * @brief 重置息屏定时器。
 * @return 无返回值。
 */
void app_sleep_timer_reset(void) {
    if (floatair_lcd_is_off()) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep_timer_reset skipped: lcd off");
        return;
    }
    if (s_screen_on_scope != APP_SLEEP_SCREEN_ON_SCOPE_OFF) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep_timer_reset skipped: screen-on scope=%d",
                     (int)s_screen_on_scope);
        return;
    }
    if (s_wear_removed) {
        floatair_dbg("------- app sleep_timer_reset skipped: wear removed");
        return;
    }
/*
    if (!system_config_get_idle_detection_enabled()) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep_timer_reset skipped: idle detection disabled");
        return;
    }
*/
    uint16_t inactivity_timeout = system_config_get_inactivity_timeout();
    if (inactivity_timeout == 0) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep_timer_reset skipped: inactivity_timeout=0");
        return;
    }

    if (system_timer_sleep_reset()) {
        floatair_dbg("------- app sleep_timer_reset");
        return;
    }

    uint32_t sleep_ms = (uint32_t)inactivity_timeout * 1000u;
    if (!system_timer_sleep_start(sleep_ms)) {
        floatair_err("app sleep timer recreate failed: timeout=%" PRIu32 " ms", sleep_ms);
        return;
    }
    floatair_dbg("------- app sleep timer recreated [%" PRIu32 "]", sleep_ms);
}

/**
 * @brief 更新佩戴状态并按配置的无交互超时时间重新起算。
 * @param[in] removed `true` 表示摘下，`false` 表示已佩戴。
 * @return 无返回值。
 */
void app_sleep_timer_set_wear_removed(bool removed) {
    if (removed == s_wear_removed) {
        floatair_dbg("wear state unchanged: removed=%d", (int)removed);
        return;
    }

    s_wear_removed = removed;
    system_timer_sleep_deinit();
    app_sleep_timer_init();
    floatair_info("wear state changed, inactivity timer restarted: removed=%d", (int)removed);
}

/**
 * @brief 按当前配置初始化息屏定时器。
 * @return 无返回值。
 */
void app_sleep_timer_init(void) {
    if (floatair_lcd_is_off()) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep disabled while lcd off");
        return;
    }
    if (s_screen_on_scope != APP_SLEEP_SCREEN_ON_SCOPE_OFF) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep disabled by screen-on scope=%d",
                     (int)s_screen_on_scope);
        return;
    }
/*
    if (!system_config_get_idle_detection_enabled()) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep disabled");
        return;
    }
*/
    uint16_t inactivity_timeout = system_config_get_inactivity_timeout();
    if (inactivity_timeout == 0) {
        system_timer_sleep_deinit();
        floatair_dbg("------- app sleep disabled by inactivity_timeout=0");
        return;
    }

    uint32_t sleep_ms = (uint32_t)inactivity_timeout * 1000u;
    floatair_dbg("------- app sleep in [%" PRIu32 "]", sleep_ms);
    bool ok = system_timer_sleep_start(sleep_ms);
    floatair_assert(ok, "system_timer_sleep_start failed");
}

/**
 * @brief 设置手机请求的临时常亮范围，并同步启停无操作灭屏定时器。
 * @param[in] scope 临时常亮范围。
 * @return `true` 表示设置成功，`false` 表示范围无效。
 */
bool app_sleep_timer_set_screen_on_scope(app_sleep_screen_on_scope_t scope) {
    if (scope > APP_SLEEP_SCREEN_ON_SCOPE_CONNECTION) {
        floatair_err("invalid screen-on scope=%d", (int)scope);
        return false;
    }

    s_screen_on_scope = scope;
    if (scope == APP_SLEEP_SCREEN_ON_SCOPE_OFF) {
        app_sleep_timer_reset();
    } else {
        system_timer_sleep_deinit();
    }
    floatair_info("screen-on scope set to %d", (int)scope);
    return true;
}

/**
 * @brief 获取当前手机请求的临时常亮范围。
 * @return 返回当前仅保存在内存中的临时常亮范围。
 */
app_sleep_screen_on_scope_t app_sleep_timer_get_screen_on_scope(void) {
    return s_screen_on_scope;
}

/**
 * @brief 按运行时事件恢复临时常亮策略。
 * @param[in] reason App 切换或手机断连。
 * @return 无返回值。
 */
void app_sleep_timer_restore_screen_on_scope(app_sleep_screen_on_restore_t reason) {
    bool should_restore = false;

    switch (reason) {
        case APP_SLEEP_SCREEN_ON_RESTORE_APP_CHANGED:
            should_restore = s_screen_on_scope == APP_SLEEP_SCREEN_ON_SCOPE_APP;
            break;
        case APP_SLEEP_SCREEN_ON_RESTORE_DISCONNECTED:
            should_restore = s_screen_on_scope != APP_SLEEP_SCREEN_ON_SCOPE_OFF;
            break;
        default:
            floatair_warn("invalid screen-on restore reason=%d", (int)reason);
            return;
    }

    if (!should_restore) {
        return;
    }
    floatair_info("restore screen-on scope=%d for reason=%d",
                  (int)s_screen_on_scope,
                  (int)reason);
    (void)app_sleep_timer_set_screen_on_scope(APP_SLEEP_SCREEN_ON_SCOPE_OFF);
}

/**
 * @brief 打印当前蓝牙信息缓存。
 * @return 无返回值。
 */
void system_dump_bt_info(void) {
    floatair_dbg("-------system_dump_bt_info");
    floatair_dbg("bt_name: %s", g_bt_info.bt_name);
    floatair_dbg("bt_addr: %s", g_bt_info.bt_addr);
    floatair_dbg("ble_addr: %s", g_bt_info.ble_addr);
    floatair_dbg("ble_name: %s", g_bt_info.ble_name);
}

/**
 * @brief 将工厂区字节数组转换为可打印 C 字符串。
 * @param[in] src 原始字节数组。
 * @param[in] src_len 原始字节数组长度。
 * @param[out] out 输出缓冲区。
 * @param[in] out_len 输出缓冲区长度。
 * @return 返回转换后的字符串；输入无效时返回空字符串。
 */
static const char* section_bytes_to_cstr(const uint8_t* src, size_t src_len, char* out, size_t out_len) {
    if (!out || out_len == 0) return "";
    out[0] = '\0';
    if (!src) return out;
    size_t j = 0;
    for (size_t i = 0; i < src_len && (j + 1) < out_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\0') break;
        if (c < 0x20 || c > 0x7E) break;
        out[j++] = (char)c;
    }
    out[j] = '\0';
    while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '\t')) {
        out[--j] = '\0';
    }
    return out;
}

/**
 * @brief 获取并打印工厂区字符串字段。
 * @param[in] label 字段日志标签。
 * @param[in] src 原始字节数组。
 * @param[in] src_len 原始字节数组长度。
 * @param[out] out 输出缓冲区。
 * @param[in] out_len 输出缓冲区长度。
 * @return 返回转换后的 C 字符串。
 */
static const char* system_get_section_string(const char* label,
                                             const uint8_t* src,
                                             size_t src_len,
                                             char* out,
                                             size_t out_len) {
    const char* value = section_bytes_to_cstr(src, src_len, out, out_len);

    floatair_dbg("%s: %s", label, value);
    return value;
}

/**
 * @brief 打印工厂区关键设备信息。
 * @return 无返回值。
 */
void system_dump_jyt_section(void) {
    char edition[sizeof(g_section_data.jyt_edition) + 1];
    char manufacture[sizeof(g_section_data.jyt_manufacture) + 1];
    char model[sizeof(g_section_data.jyt_model) + 1];
    char psn[sizeof(g_section_data.jyt_psn) + 1];
    char ssn[sizeof(g_section_data.jyt_ssn) + 1];

    floatair_dbg("jyt_default_brightness: %d", g_section_data.jyt_default_brightness);
    floatair_dbg("jyt_default_volumn: %d", g_section_data.jyt_default_volumn);
    floatair_dbg("jyt_edition: %s", section_bytes_to_cstr(g_section_data.jyt_edition, sizeof(g_section_data.jyt_edition), edition, sizeof(edition)));
    floatair_dbg("jyt_manufacture: %s", section_bytes_to_cstr(g_section_data.jyt_manufacture, sizeof(g_section_data.jyt_manufacture), manufacture, sizeof(manufacture)));
    floatair_dbg("jyt_model: %s", section_bytes_to_cstr(g_section_data.jyt_model, sizeof(g_section_data.jyt_model), model, sizeof(model)));
    floatair_dbg("jyt_psn: %s", section_bytes_to_cstr(g_section_data.jyt_psn, sizeof(g_section_data.jyt_psn), psn, sizeof(psn)));
    floatair_dbg("jyt_ssn: %s", section_bytes_to_cstr(g_section_data.jyt_ssn, sizeof(g_section_data.jyt_ssn), ssn, sizeof(ssn)));
}

/**
 * @brief 获取当前系统亮屏状态。
 * @return `1` 表示亮屏，`0` 表示灭屏。
 */
uint8_t system_get_sys_state(void) {
    lcd_state_t lcd_state = floatair_lcd_get_state();
    uint8_t state = (uint8_t)lcd_state;

    floatair_info("get screen state: %u(%s)",
                  (unsigned)state,
                  floatair_lcd_state_name(lcd_state));
    return state;
}

/**
 * @brief 设置当前系统亮屏状态。
 * @param[in] state 目标系统状态，`0` 表示灭屏，`1` 表示亮屏。
 * @return 无返回值。
 */
void system_set_sys_state(uint8_t state) {
    lcd_state_t next_state = (lcd_state_t)state;
    lcd_state_t previous_state = floatair_lcd_get_state();

    if (!floatair_lcd_state_is_valid(next_state)) {
        floatair_err("reject invalid screen state: %u, expected 0(OFF) or 1(ON)",
                     (unsigned)state);
        return;
    }

    floatair_info("set screen state: %u(%s)",
                  (unsigned)next_state,
                  floatair_lcd_state_name(next_state));
    if (next_state == LCD_OFF) {
        system_timer_sleep_deinit();
        floatair_lcd_set_state(LCD_OFF);
    } else {
        floatair_lcd_set_state(LCD_ON);
        if (previous_state == LCD_OFF) {
            app_sleep_timer_init();
        } else {
            app_sleep_timer_reset();
        }
        system_ui_flush_pending_after_screen_on();
    }

    if (previous_state != next_state) {
        (void)system_runtime_input_notify_sys_state((uint8_t)next_state);
    }
}

/**
 * @brief 获取产品序列号字符串。
 * @return 返回产品序列号字符串。
 */
const char* system_get_sn(void) {
    static char sn[sizeof(g_section_data.jyt_psn) + 1];
    return system_get_section_string("system_get_sn", g_section_data.jyt_psn, sizeof(g_section_data.jyt_psn), sn, sizeof(sn));
}
/**
 * @brief 获取短序列号字符串。
 * @return 返回短序列号字符串。
 */
const char* system_get_ssn(void) {
    static char ssn[sizeof(g_section_data.jyt_ssn) + 1];
    return system_get_section_string("system_get_ssn", g_section_data.jyt_ssn, sizeof(g_section_data.jyt_ssn), ssn, sizeof(ssn));
}
/**
 * @brief 获取制造商字符串。
 * @return 返回制造商字符串。
 */
const char* system_get_manufacture(void) {
    static char manufacture[sizeof(g_section_data.jyt_manufacture) + 1];
    return system_get_section_string("system_get_manufacture",
                                     g_section_data.jyt_manufacture,
                                     sizeof(g_section_data.jyt_manufacture),
                                     manufacture,
                                     sizeof(manufacture));
}
/**
 * @brief 获取产品型号字符串。
 * @return 返回产品型号字符串。
 */
const char* system_get_model(void) {
    static char model[sizeof(g_section_data.jyt_model) + 1];
    return system_get_section_string("system_get_model", g_section_data.jyt_model, sizeof(g_section_data.jyt_model), model, sizeof(model));
}
/**
 * @brief 获取版本说明字符串。
 * @return 返回版本说明字符串。
 */
const char* system_get_edition(void) {
    static char edition[sizeof(g_section_data.jyt_edition) + 1];
    return system_get_section_string("system_get_edition", g_section_data.jyt_edition, sizeof(g_section_data.jyt_edition), edition, sizeof(edition));
}
/**
 * @brief 获取蓝牙名称。
 * @return 返回蓝牙名称字符串。
 */
const char* system_get_btname(void) { return g_bt_info.bt_name; }
/**
 * @brief 获取蓝牙地址。
 * @return 返回蓝牙地址字符串。
 */
const char* system_get_btaddr(void) { return g_bt_info.bt_addr; }
/**
 * @brief 获取 BLE 名称。
 * @return 返回 BLE 名称字符串。
 */
const char* system_get_blename(void) { return g_bt_info.ble_name; }
/**
 * @brief 获取 BLE 地址。
 * @return 返回 BLE 地址字符串。
 */
const char* system_get_bleaddr(void) { return g_bt_info.ble_addr; }

/**
 * @brief 获取应用固件版本号。
 * @return 返回应用固件版本字符串。
 */
const char* system_get_fwver(void) {
    return JY_APP_VERSION_STRING;
}

/**
 * @brief 获取底层系统版本号。
 * @return 返回底层系统版本字符串。
 */
const char* system_get_bthver(void) {
    return FLOATAIR_OS_VERSION_STRING;
}

/**
 * @brief 获取当前协议版本号。
 * @return 返回协议版本字符串。
 */
const char* system_get_protocolver(void) { return "H6V3"; }
/**
 * @brief 获取默认音量配置。
 * @return 返回默认音量值。
 */
uint8_t system_get_default_volumn(void) { return g_section_data.jyt_default_volumn; }
/**
 * @brief 获取默认亮度配置。
 * @return 返回默认亮度值。
 */
uint16_t system_get_default_brightness(void) { return g_section_data.jyt_default_brightness; }
/**
 * @brief 设置默认亮度配置。
 * @param[in] brightness 目标默认亮度。
 * @return 无返回值。
 */
void system_set_default_brightness(uint16_t brightness) { g_section_data.jyt_default_brightness = brightness; }

/**
 * @brief 将显示层级配置转换为实际距离值。
 * @param[in] level 显示层级配置值。
 * @return 返回对应的距离常量；不支持时回退正常距离常量。
 */
int system_get_displaylevel_value(uint32_t level) {
    floatair_dbg("display_level %" PRIu32, level);
    switch (level) {
        case 1:
            return FLOATAIR_ROOT_IMAGE_DISTANCE_NORMAL;
        case 2:
            return FLOATAIR_ROOT_IMAGE_DISTANCE_FAR;
        case 3:
            return FLOATAIR_ROOT_IMAGE_DISTANCE_NEAR;
        default:
            floatair_err("display_level %" PRIu32 " not support", level);
            return FLOATAIR_ROOT_IMAGE_DISTANCE_NORMAL;
    }
}
