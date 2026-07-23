/**
 * @file app_router.c
 * @brief App framework 路由门面实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-05-06
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#include "common/app_framework/app_router.h"

#include "app_def.h"
#include "app_lcd.h"
#include "ai/ai.h"
#include "common/app_framework/app_manager.h"
#include "common/widgets/status_bar.h"
#include "gallery/gallery.h"
#include "guide/guide.h"
#include "home/home.h"
#include "imagefusion/imagefusion.h"
#include "langselection/langselection.h"
#include "music/music.h"
#include "navigation/navigation.h"
#include "prompter_pro/prompter.h"
#include "reader/reader.h"
#include "speech/speech.h"
#include "system/popups/notify/notify.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <inttypes.h>
#include <string.h>

static char g_router_curapp[MSG_STR_MAX_LEN] = {0};                  ///< 当前显示的 app 名称
static app_router_entry_t g_router_entry_mode = APP_ROUTER_ENTRY_LOCAL;  ///< 当前 app 进入方式
static app_router_app_platform_t g_router_default_app_platform = APP_ROUTER_APP_PLATFORM_NONE; ///< 产品配置默认上位机平台
static app_router_app_platform_t g_router_app_platform = APP_ROUTER_APP_PLATFORM_NONE; ///< 本次连接的上位机平台
static bool g_router_initialized = false;                            ///< 路由初始化状态

/**
 * @brief 清理底部状态栏上遗留的自定义组件。
 * @return 无返回值。
 */
static void app_router_clear_status_bar_widgets(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return;
    }

    status_bar_clear_custom_widgets(status_bar);
}

/**
 * @brief 判断蓝牙断连状态是否应阻断切换。
 * @return `true` 表示应阻断，`false` 表示允许继续。
 */
static bool app_router_should_block_by_bt_disconnect(void) {
    app_t* current_app = app_manager_current();

    if (system_get_btconn_state()) {
        return false;
    }
    if (!system_config_get_langselection_finish()) {
        return false;
    }
    if (g_router_curapp[0] == '\0') {
        return false;
    }
    if (current_app != NULL && current_app->use_top_layer) {
        return false;
    }

    return true;
}

/**
 * @brief 按当前配置解析首页应用名称。
 * @return 返回首页应用名称；配置异常时返回 `NULL`。
 */
static const char* app_router_resolve_home(void) {
    const char* home = APP_NAME_HOME;

    if (!system_config_get_langselection_finish()) {
        home = APP_NAME_LANGSELECTION;
    } else if (g_router_app_platform == APP_ROUTER_APP_PLATFORM_NONE) {
        home = APP_NAME_HOME;
    } else if (g_router_app_platform == APP_ROUTER_APP_PLATFORM_WATCH) {
        home = APP_NAME_TRANSCRIBE;
    } else if (!system_config_is_userguide_finished()) {
        home = APP_NAME_GUIDE;
    }
    return home;
}

/**
 * @brief 将产品配置中的平台名称转换为路由平台枚举。
 * @param[in] platform 产品配置中的平台名称。
 * @param[out] out_platform 转换后的平台枚举。
 * @return `true` 表示转换成功，`false` 表示平台名称非法。
 */
static bool app_router_parse_platform_name(const char* platform, app_router_app_platform_t* out_platform) {
    if (out_platform == NULL) {
        return false;
    }
    if (platform == NULL || platform[0] == '\0') {
        *out_platform = APP_ROUTER_APP_PLATFORM_NONE;
        return true;
    }
    if (strcmp(platform, "android") == 0) {
        *out_platform = APP_ROUTER_APP_PLATFORM_ANDROID;
    } else if (strcmp(platform, "ios") == 0) {
        *out_platform = APP_ROUTER_APP_PLATFORM_IOS;
    } else if (strcmp(platform, "macos") == 0) {
        *out_platform = APP_ROUTER_APP_PLATFORM_MACOS;
    } else if (strcmp(platform, "windows") == 0) {
        *out_platform = APP_ROUTER_APP_PLATFORM_WINDOWS;
    } else if (strcmp(platform, "watch") == 0) {
        *out_platform = APP_ROUTER_APP_PLATFORM_WATCH;
    } else {
        floatair_err("router app platform name invalid: %s", platform);
        return false;
    }
    return true;
}

/**
 * @brief 注册全部业务 App。
 * @return `true` 表示全部注册成功，`false` 表示至少一个 App 注册失败。
 */
static bool app_router_register_apps(void) {
    if (!home_app_register()) {
        floatair_err("home app register failed");
        return false;
    }
    if (!prompter_app_register()) {
        floatair_err("prompter app register failed");
        return false;
    }
    if (!speech_app_register()) {
        floatair_err("speech app register failed");
        return false;
    }
    if (!gallery_app_register()) {
        floatair_err("gallery app register failed");
        return false;
    }
    if (!navigation_app_register()) {
        floatair_err("navigation app register failed");
        return false;
    }
    if (!guide_app_register()) {
        floatair_err("guide app register failed");
        return false;
    }
    if (!music_app_register()) {
        floatair_err("music app register failed");
        return false;
    }
    if (!reader_app_register()) {
        floatair_err("reader app register failed");
        return false;
    }
    if (!langselection_app_register()) {
        floatair_err("langselection app register failed");
        return false;
    }
    if (!ai_app_register()) {
        floatair_err("ai app register failed");
        return false;
    }
    if (!imagefusion_app_register()) {
        floatair_err("imagefusion app register failed");
        return false;
    }

    return true;
}

bool app_router_init(void) {
    app_manager_config_t cfg = {0};

    if (g_router_initialized) {
        return true;
    }

    cfg.page_host = app_page_host_default_config(
        (int32_t)config_lcd.ui_width,
        (int32_t)system_ui_get_page_content_height());
    if (!app_manager_init(&cfg)) {
        floatair_err("app manager init failed");
        return false;
    }

    if (!app_router_register_apps()) {
        if (!app_manager_deinit()) {
            floatair_warn("app router init rollback failed, manager busy");
        }
        app_router_reset_state();
        return false;
    }

    g_router_initialized = true;
    return true;
}

bool app_router_deinit(void) {
    if (!app_manager_deinit()) {
        floatair_warn("app router deinit failed, manager busy");
        return false;
    }
    g_router_initialized = false;
    app_router_reset_state();
    return true;
}

void app_router_reset_state(void) {
    memset(g_router_curapp, 0, sizeof(g_router_curapp));
    g_router_entry_mode = APP_ROUTER_ENTRY_LOCAL;
    g_router_app_platform = g_router_default_app_platform;
    floatair_info("app router reset, platform=%d", (int)g_router_app_platform);
}

bool app_router_call_home(void) {
    const char* home = app_router_resolve_home();

    floatair_info("router call home resolved target=%s", home);
    if (!app_manager_has_app(home)) {
        return false;
    }

    return app_router_set_app(home, APP_ROUTER_ENTRY_LOCAL);
}

const char* app_router_get_home_viewname(void) {
    return app_router_resolve_home();
}

bool app_router_apply_app_config(uint32_t app_platform) {
    app_router_app_platform_t prev_app_platform = g_router_app_platform;

    switch (app_platform) {
        case APP_ROUTER_APP_PLATFORM_ANDROID:
        case APP_ROUTER_APP_PLATFORM_IOS:
        case APP_ROUTER_APP_PLATFORM_MACOS:
        case APP_ROUTER_APP_PLATFORM_WINDOWS:
        case APP_ROUTER_APP_PLATFORM_WATCH:
            g_router_app_platform = (app_router_app_platform_t)app_platform;
            break;
        default:
            floatair_err("router app platform invalid: %" PRIu32, app_platform);
            return false;
    }

    floatair_info("router apply app config platform=%" PRIu32, app_platform);
    if (!app_router_call_home()) {
        g_router_app_platform = prev_app_platform;
        system_ui_sync_shell_state();
        return false;
    }
    return true;
}

bool app_router_set_default_app_platform(const char* platform) {
    app_router_app_platform_t default_app_platform = APP_ROUTER_APP_PLATFORM_NONE;

    if (!app_router_parse_platform_name(platform, &default_app_platform)) {
        return false;
    }
    g_router_default_app_platform = default_app_platform;
    g_router_app_platform = default_app_platform;
    floatair_info("router default app platform=%d", (int)default_app_platform);
    return true;
}

bool app_router_has_app_config(void) {
    return g_router_app_platform != APP_ROUTER_APP_PLATFORM_NONE;
}

void app_router_clear_app_config(void) {
    g_router_app_platform = g_router_default_app_platform;
    floatair_info("router clear app config, platform=%d", (int)g_router_app_platform);
}

bool app_router_exit_current_app(void) {
    floatair_info("router exit current app: current=%s entry=%d",
                  g_router_curapp[0] ? g_router_curapp : "N/A",
                  (int)g_router_entry_mode);

    if (app_router_is_busy()) {
        floatair_warn("router exit app failed, router busy");
        return false;
    }

    return app_router_call_home();
}

const char* app_router_get_app(void) {
    floatair_dbg("router get app %s", g_router_curapp);
    return g_router_curapp;
}

bool app_router_is_busy(void) {
    return app_manager_is_busy();
}

bool app_router_set_app(const char* targetapp, app_router_entry_t mode) {
    notify_mode_t active_notify_mode = NOTIFY_MODE_MESSAGE;
    bool ret = false;
    bool had_current_app = false;
    bool suppress_view_change_report = false;
    char previous_app[MSG_STR_MAX_LEN] = {0};

    floatair_assert(targetapp != NULL, "targetapp is NULL");
    floatair_info("router set app [%s]-->[%s] mode[%d]",
                  g_router_curapp,
                  targetapp,
                  (int)mode);

    if (!g_router_initialized) {
        floatair_warn("router set app failed, router not initialized");
        return false;
    }
    if (app_router_is_busy()) {
        floatair_warn("router set app failed, router busy");
        return false;
    }
    if (!app_manager_has_app(targetapp)) {
        floatair_err("router app %s not found", targetapp);
        return false;
    }
    if (strcmp(g_router_curapp, targetapp) == 0) {
        g_router_entry_mode = mode;
        system_ui_sync_shell_state();
        floatair_info("router set app skipped, already current");
        return true;
    }
    if (g_router_curapp[0] != '\0' &&
        notify_get_active_mode(&active_notify_mode) &&
        active_notify_mode == NOTIFY_MODE_CALL) {
        floatair_warn("router set app blocked by active call notify, current=%s target=%s mode=%d",
                      g_router_curapp,
                      targetapp,
                      (int)mode);
        return false;
    }
    if (app_router_should_block_by_bt_disconnect()) {
        floatair_warn("router set app blocked by bt disconnect overlay, current=%s target=%s mode=%d",
                      g_router_curapp,
                      targetapp,
                      (int)mode);
        return false;
    }

    had_current_app = (g_router_curapp[0] != '\0');
    if (had_current_app) {
        snprintf(previous_app, sizeof(previous_app), "%s", g_router_curapp);
        if (!app_manager_stop(previous_app)) {
            floatair_warn("router stop current app failed, current=%s target=%s",
                          previous_app,
                          targetapp);
            return false;
        }
        memset(g_router_curapp, 0, sizeof(g_router_curapp));
        g_router_entry_mode = APP_ROUTER_ENTRY_LOCAL;
    }

    /* 清理目标 App 可能残留的旧页面栈，保持迁移前“切 App 即重新安装”的语义。 */
    if (!app_manager_stop(targetapp)) {
        floatair_warn("router reset target app failed, target=%s previous=%s",
                      targetapp,
                      had_current_app ? previous_app : "N/A");
        return false;
    }

    app_router_clear_status_bar_widgets();
    g_router_entry_mode = mode;
    ret = app_manager_switch(targetapp);
    if (ret) {
        snprintf(g_router_curapp, sizeof(g_router_curapp), "%s", targetapp);
        suppress_view_change_report = !system_config_is_userguide_finished();
        if (g_router_entry_mode == APP_ROUTER_ENTRY_LOCAL &&
            !suppress_view_change_report) {
            system_report_view_change(targetapp);
        } else if (suppress_view_change_report) {
            floatair_info("router suppress guide view change report for app %s", targetapp);
        } else {
            floatair_info("router suppress view change report for remote app %s", targetapp);
        }
        system_ui_sync_shell_state();
    } else {
        g_router_entry_mode = APP_ROUTER_ENTRY_LOCAL;
    }
    return ret;
}

void app_router_set_entry_mode(app_router_entry_t entry_mode) {
    g_router_entry_mode = entry_mode;
}

app_router_entry_t app_router_get_entry_mode(void) {
    return g_router_entry_mode;
}
