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

#include "app_build_config.h"
#include "app_def.h"
#include "app_lcd.h"
#include "common/app_framework/app_manager.h"
#include "common/widgets/status_bar.h"
#include "system/popups/notify/notify.h"
#if APP_BUILD_AI
#include "ai/ai.h"
#endif
#if APP_BUILD_GALLERY
#include "gallery/gallery.h"
#endif
#if APP_BUILD_GUIDE
#include "guide/guide.h"
#endif
#include "home/home.h"
#if APP_BUILD_NAVIGATION
#include "navigation/navigation.h"
#endif
#if APP_BUILD_OTA
#include "ota/ota.h"
#endif
#if APP_BUILD_POWEROFF
#include "poweroff/poweroff.h"
#endif
#if APP_BUILD_POWERON
#include "poweron/poweron.h"
#endif
#if APP_BUILD_PROMPTER
#include "prompter/prompter.h"
#endif
#include "system/system.h"
#include "system/system_runtime_ui.h"
#if APP_BUILD_TRANSLATE
#include "translate/translate.h"
#endif
#if APP_BUILD_TRANSCRIBE
#include "transcribe/transcribe.h"
#endif
#if APP_BUILD_MUSIC
#include "music/music.h"
#endif
#if APP_BUILD_READER
#include "reader/reader.h"
#endif
#if APP_BUILD_LANGSELECTION
#include "langselection/langselection.h"
#endif

#include <string.h>

static char g_router_curapp[MSG_STR_MAX_LEN] = {0};                  ///< 当前显示的 app 名称
static app_router_entry_t g_router_entry_mode = APP_ROUTER_ENTRY_LOCAL;  ///< 当前 app 进入方式
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
    char* home = system_config_get_home_app();

    floatair_assert(home != NULL, "home app name is null");
    if (!system_config_get_langselection_finish()) {
        home = APP_NAME_LANGSELECTION;
    } else if (system_config_get_userguide() && !system_config_get_userguide_finish()) {
        home = APP_NAME_GUIDE;
    }

    return home;
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
#if APP_BUILD_PROMPTER
    if (!prompter_app_register()) {
        floatair_err("prompter app register failed");
        return false;
    }
#endif
#if APP_BUILD_TRANSLATE
    if (!translate_app_register()) {
        floatair_err("translate app register failed");
        return false;
    }
#endif
#if APP_BUILD_GALLERY
    if (!gallery_app_register()) {
        floatair_err("gallery app register failed");
        return false;
    }
#endif
#if APP_BUILD_NAVIGATION
    if (!navigation_app_register()) {
        floatair_err("navigation app register failed");
        return false;
    }
#endif
#if APP_BUILD_GUIDE
    if (!guide_app_register()) {
        floatair_err("guide app register failed");
        return false;
    }
#endif
#if APP_BUILD_MUSIC
    if (!music_app_register()) {
        floatair_err("music app register failed");
        return false;
    }
#endif
#if APP_BUILD_OTA
    if (!ota_app_register()) {
        floatair_err("ota app register failed");
        return false;
    }
#endif
#if APP_BUILD_POWEROFF
    if (!poweroff_app_register()) {
        floatair_err("poweroff app register failed");
        return false;
    }
#endif
#if APP_BUILD_POWERON
    if (!poweron_app_register()) {
        floatair_err("poweron app register failed");
        return false;
    }
#endif
#if APP_BUILD_READER
    if (!reader_app_register()) {
        floatair_err("reader app register failed");
        return false;
    }
#endif
#if APP_BUILD_LANGSELECTION
    if (!langselection_app_register()) {
        floatair_err("langselection app register failed");
        return false;
    }
#endif
#if APP_BUILD_AI
    if (!ai_app_register()) {
        floatair_err("ai app register failed");
        return false;
    }
#endif
#if APP_BUILD_TRANSCRIBE
    if (!transcribe_app_register()) {
        floatair_err("transcribe app register failed");
        return false;
    }
#endif

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
    floatair_info("app router reset");
}

bool app_router_call_home(void) {
    const char* home = app_router_resolve_home();

    floatair_info("router call home resolved target=%s", home);
    if (!app_manager_has_app(home)) {
        return false;
    }

    floatair_lcd_set_state(LCD_ON);
    system_report_sys_state(1);
    app_sleep_timer_reset();
    return app_router_set_app(home, APP_ROUTER_ENTRY_LOCAL);
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
        if (g_router_entry_mode == APP_ROUTER_ENTRY_LOCAL) {
            system_report_view_change(targetapp);
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
