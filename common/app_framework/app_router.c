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

#include "app_lcd.h"
#include "common/app_framework/app_manager.h"
#include "product_app_generated.h"
#if defined(APP_NAME_HOME)
#include "home/home.h"
#endif
#include "product_app.h"
#include "common/widgets/status_bar.h"
#include "system/popups/notify/notify.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <inttypes.h>
#include <string.h>

#define APP_ROUTER_PROTOCOL_HOME_VIEW "home" ///< 协议层固定使用的首页视图名称。

static char g_router_curapp[MSG_STR_MAX_LEN] = {0};                  ///< 当前显示的 app 名称
static app_router_entry_t g_router_entry_mode = APP_ROUTER_ENTRY_LOCAL;  ///< 当前 app 进入方式
static app_router_app_platform_t g_router_default_app_platform = APP_ROUTER_APP_PLATFORM_NONE; ///< 产品配置默认上位机平台
static app_router_app_platform_t g_router_app_platform = APP_ROUTER_APP_PLATFORM_NONE; ///< 本次连接的上位机平台
static bool g_router_initialized = false;                            ///< 路由初始化状态

/**
 * @brief 按当前 App 策略同步 KWS 软件拦截状态。
 * @param[in] app_name 当前 App 名称；为空时清除 App 拦截。
 * @return 无返回值。
 */
static void app_router_sync_keyword_spotting_policy(const char* app_name) {
    bool blocked = product_app_name_has_capability(
        app_name,
        PRODUCT_APP_CAP_ASSISTANT_OPEN_BLOCKED);

    system_runtime_state_set_kws_intercept(
        SYSTEM_KWS_INTERCEPT_REASON_APP_POLICY,
        blocked);
}

/**
 * @brief 清理底部状态栏上遗留的自定义组件。
 * @return 无返回值。
 */
static void app_router_clear_status_bar_widgets(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL) {
        status_bar = system_get_status_bar(STATUS_BAR_POS_TOP);
    }

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return;
    }

    status_bar_clear_custom_widgets(status_bar);
}

/**
 * @brief 同步目标 App 在底部状态栏最左侧的展示名称。
 * @param[in] targetapp 目标 App 协议名称。
 * @return 无返回值。
 */
static void app_router_sync_status_bar_app_name(const char* targetapp) {
    const char* home_app = product_app_role_name(PRODUCT_APP_ROLE_HOME);
    const char* display_name = NULL;

    if (targetapp != NULL && targetapp[0] != '\0' &&
        (home_app == NULL || strcmp(targetapp, home_app) != 0)) {
#if defined(APP_NAME_HOME)
        display_name = home_get_app_display_name(targetapp);
#endif
        if (display_name == NULL || display_name[0] == '\0') {
            display_name = targetapp;
        }
    }

    system_status_bar_set_app_name(display_name);
}

/**
 * @brief 获取目标 App 进入页面创建阶段前应使用的状态栏位置。
 * @param[in] targetapp 目标 App 协议名称。
 * @return 返回目标状态栏位置。
 */
static status_bar_widget_pos_t app_router_status_bar_position_for_app(
    const char* targetapp) {
    const char* home_app = product_app_role_name(PRODUCT_APP_ROLE_HOME);

    if (PRODUCT_HOME_STATUS_BAR_AT_TOP && targetapp != NULL &&
        home_app != NULL && strcmp(targetapp, home_app) == 0) {
        return STATUS_BAR_POS_TOP;
    }
    return STATUS_BAR_POS_BOTTOM;
}

/**
 * @brief 判断蓝牙断连状态是否应阻断切换。
 * @param[in] targetapp 目标 App 名称。
 * @return `true` 表示应阻断，`false` 表示允许继续。
 */
static bool app_router_should_block_by_bt_disconnect(const char* targetapp) {
    app_t* current_app = app_manager_current();
    const char* home_app = product_app_role_name(PRODUCT_APP_ROLE_HOME);

    if (system_get_btconn_state()) {
        return false;
    }
    if (home_app != NULL && targetapp != NULL && strcmp(targetapp, home_app) == 0) {
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
    const char* home = product_app_role_name(PRODUCT_APP_ROLE_HOME);

    if (!system_config_get_langselection_finish()) {
        home = product_app_role_name(PRODUCT_APP_ROLE_LANGSELECTION);
    } else if (g_router_app_platform == APP_ROUTER_APP_PLATFORM_NONE) {
        home = product_app_role_name(PRODUCT_APP_ROLE_HOME);
    } else if (g_router_app_platform == APP_ROUTER_APP_PLATFORM_WATCH) {
        home = product_app_role_name(PRODUCT_APP_ROLE_WATCH_HOME);
    } else if (!system_config_is_userguide_finished()) {
        home = product_app_role_name(PRODUCT_APP_ROLE_GUIDE);
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
bool app_router_init(void) {
    app_manager_config_t cfg = {0};

    if (g_router_initialized) {
        return true;
    }

    cfg.page_host = app_page_host_default_config(
        (int32_t)config_lcd.ui_width,
        (int32_t)system_ui_get_page_content_height());
    cfg.page_host.offset_y = (int32_t)system_ui_get_page_content_offset_y();
    if (!app_manager_init(&cfg)) {
        floatair_err("app manager init failed");
        return false;
    }

    if (!product_apps_register_all()) {
        floatair_err("product apps register failed");
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
    app_router_sync_keyword_spotting_policy(NULL);
    system_status_bar_set_app_name(NULL);
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

const char* app_router_protocol_to_app_name(const char* view_name) {
    if (view_name == NULL) {
        return NULL;
    }
    if (strcmp(view_name, APP_ROUTER_PROTOCOL_HOME_VIEW) == 0) {
        return app_router_resolve_home();
    }
    return view_name;
}

const char* app_router_app_to_protocol_name(const char* app_name) {
    const char* product_home = product_app_role_name(PRODUCT_APP_ROLE_HOME);

    if (app_name == NULL) {
        return NULL;
    }
    if (product_home != NULL && strcmp(app_name, product_home) == 0) {
        return APP_ROUTER_PROTOCOL_HOME_VIEW;
    }
    return app_name;
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
    return g_router_curapp;
}

void app_router_refresh_status_bar_app_name(void) {
    app_router_sync_status_bar_app_name(g_router_curapp);
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
        system_status_bar_set_position(
            app_router_status_bar_position_for_app(targetapp));
        app_router_sync_status_bar_app_name(targetapp);
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
    if (app_router_should_block_by_bt_disconnect(targetapp)) {
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
        app_sleep_timer_restore_screen_on_scope(
            APP_SLEEP_SCREEN_ON_RESTORE_APP_CHANGED);
    }

    /* 清理目标 App 可能残留的旧页面栈，保持迁移前“切 App 即重新安装”的语义。 */
    if (!app_manager_stop(targetapp)) {
        floatair_warn("router reset target app failed, target=%s previous=%s",
                      targetapp,
                      had_current_app ? previous_app : "N/A");
        return false;
    }

    app_router_clear_status_bar_widgets();
    system_status_bar_set_position(
        app_router_status_bar_position_for_app(targetapp));
    app_router_sync_status_bar_app_name(targetapp);
    g_router_entry_mode = mode;
    ret = app_manager_switch(targetapp);
    if (ret) {
        snprintf(g_router_curapp, sizeof(g_router_curapp), "%s", targetapp);
        app_router_sync_keyword_spotting_policy(targetapp);
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
        system_status_bar_set_app_name(NULL);
        app_router_sync_keyword_spotting_policy(NULL);
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
