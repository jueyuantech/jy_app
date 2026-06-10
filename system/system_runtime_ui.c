/**
 * @file system_runtime_ui.c
 * @brief 系统运行时 UI 壳层实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system/system_runtime_ui.h"

#include "system/system.h"
#include "system/system_config_json.h"
#include "system/system_notification.h"
#include "system/system_res.h"
#include "system_bt_disconnect_overlay_ui.h"
#include "floatair_lcd.h"
#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_layers.h"
#include "common/app_framework/app_stereo.h"
#include "system/popups/notify/notify.h"
#include "common/widgets/toast.h"
#include "system/popups/assistant/assistant.h"
#include "system/popups/notify_list/notify_list.h"
#include "app_lcd.h"

#include <inttypes.h>
#include <time.h>

static lv_obj_t* g_status_bar_bottom = NULL;          ///< app 层底部状态栏对象
static lv_obj_t* g_bt_disconnect_status_bar = NULL;   ///< 蓝牙断连遮罩层底部状态栏对象
static bool g_status_bar_bottom_visible = true;       ///< 底部状态栏期望显隐状态
static lv_obj_t* g_bt_disconnect_overlay = NULL;      ///< 蓝牙断连全屏遮罩
static system_bt_disconnect_overlay_ui_t g_bt_disconnect_overlay_ui; ///< 蓝牙断连遮罩生成布局句柄
static bool g_bt_disconnect_overlay_visible = false;  ///< 蓝牙断连遮罩当前显隐状态
static uint32_t g_display_level_applied = UINT32_MAX; ///< 最近一次已应用的显示距离档位
static uint8_t g_status_bar_battery = DEFAULT_BATTERY_LEVEL; ///< 最近一次通过消息推送同步的电量
static uint8_t g_status_bar_charge_state = 0;         ///< 最近一次通过消息推送同步的充电状态
static bool g_status_bar_wear_detection_visible = false; ///< 最近一次同步的佩戴检测图标显隐状态
static time_t g_status_bar_time_epoch = 0;            ///< 最近一次收到的设备时间戳
static bool g_status_bar_time_valid = false;          ///< 设备时间戳缓存是否有效
static bool g_status_bar_time_reliable = false;       ///< 时间是否已通过手机对表确认可靠

static void system_ui_sync_app_layer_scene(void);

/**
 * @brief 判断蓝牙断连遮罩是否应按连接态显示。
 * @return `true` 表示允许显示断连遮罩，`false` 表示当前应让前置流程独占页面。
 */
static bool system_bt_disconnect_overlay_should_show(void) {
    return !system_get_btconn_state() && system_config_get_langselection_finish();
}

/**
 * @brief 刷新蓝牙断连遮罩的提示文案与蓝牙名称。
 * @return 无返回值。
 */
void system_ui_refresh_bt_disconnect_overlay_text(void) {
    const char* bt_name = system_get_btname();
    lv_obj_t* notice_obj = NULL;
    lv_obj_t* name_obj = NULL;

    if (!system_config_get_langselection_finish()) {
        return;
    }

    notice_obj = label_get_obj(g_bt_disconnect_overlay_ui.notice);
    name_obj = label_get_obj(g_bt_disconnect_overlay_ui.name);

    if (notice_obj == NULL || name_obj == NULL) {
        return;
    }

    label_set_text(g_bt_disconnect_overlay_ui.notice, app_get_str("IDLE_NOTICE_UNBOND"));
    label_set_text(g_bt_disconnect_overlay_ui.name, bt_name != NULL ? bt_name : "");
    lv_obj_update_layout(notice_obj);
    lv_obj_update_layout(name_obj);
}

/**
 * @brief 判断蓝牙断连遮罩当前是否处于激活状态。
 * @return `true` 表示遮罩有效且正在显示，`false` 表示遮罩未激活。
 */
static bool system_bt_disconnect_overlay_is_active(void) {
    return g_bt_disconnect_overlay_visible &&
           g_bt_disconnect_overlay != NULL &&
           lv_obj_is_valid(g_bt_disconnect_overlay);
}

/**
 * @brief 获取当前活动页面底部状态栏对象。
 * @return 成功返回当前页面状态栏，失败返回 `NULL`。
 */
static lv_obj_t* system_ui_get_current_status_bar(void) {
    if (g_status_bar_bottom == NULL || !lv_obj_is_valid(g_status_bar_bottom)) {
        return NULL;
    }

    return g_status_bar_bottom;
}

/**
 * @brief 依据状态栏模式计算页面内容区高度。
 * @param[in] show_bottom `true` 表示底部状态栏占位，`false` 表示内容区铺满全屏。
 * @return 返回内容区高度。
 */
static lv_coord_t system_ui_calc_page_content_height(bool show_bottom) {
    lv_coord_t height = (lv_coord_t)config_lcd.ui_height;

    if (show_bottom) {
        height -= STATUS_BAR_HEIGHT;
    }

    if (height < 0) {
        height = 0;
    }

    return height;
}

/**
 * @brief 获取当前状态栏模式下的页面内容区高度。
 * @return 返回内容区高度。
 */
lv_coord_t system_ui_get_page_content_height(void) {
    return system_ui_calc_page_content_height(g_status_bar_bottom_visible);
}

/**
 * @brief 仅在 display level 变化时刷新底层 3D 根距离。
 * @return `true` 表示本次实际更新了根距离，`false` 表示当前档位未变化。
 */
static bool system_ui_apply_display_distance_level_if_needed(void) {
    uint32_t level = system_config_get_displaylevel();
    int32_t distance = system_get_displaylevel_value(level);

    if (g_display_level_applied == level) {
        return false;
    }

    floatair_info("apply display distance level: prev=%" PRIu32 " next=%" PRIu32 " distance=%" PRId32,
                  (uint32_t)g_display_level_applied,
                  (uint32_t)level,
                  distance);
    jyt_dual_screen_set_root_distance((int)distance);
    g_display_level_applied = level;
    system_ui_sync_app_layer_scene();
    return true;
}

/**
 * @brief 同步 app 层尺寸与显示距离位移。
 * @return 无返回值。
 */
static void system_ui_sync_app_layer_scene(void) {
    app_layers_resize((int32_t)config_lcd.ui_width, (int32_t)config_lcd.ui_height);
}

/**
 * @brief 使用指定时间戳刷新状态栏时间显示。
 * @param[in] status_bar 目标状态栏对象。
 * @param[in] time_now 需要显示的时间戳。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
static bool system_ui_refresh_status_bar_time(lv_obj_t* status_bar, time_t time_now) {
    char timebuf[8] = {0};
    struct tm* ptm = NULL;

    if (floatair_lcd_get_state() == LCD_OFF) {
        floatair_dbg("status bar time refresh skipped: lcd off");
        return true;
    }

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return true;
    }

    ptm = localtime(&time_now);
    if (ptm == NULL) {
        floatair_err("localtime failed");
        return false;
    }

    snprintf(timebuf, sizeof(timebuf), "%02d:%02d", ptm->tm_hour, ptm->tm_min);
    status_bar_update_time(status_bar, timebuf);
    return true;
}

/**
 * @brief 设置单个状态栏时间文本显隐。
 * @param[in] status_bar 目标状态栏对象。
 * @param[in] visible `true` 表示显示时间，`false` 表示隐藏时间。
 * @return 无返回值。
 */
static void system_ui_set_status_bar_time_visible(lv_obj_t* status_bar, bool visible) {
    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return;
    }

    status_bar_set_time_visible(status_bar, visible);
}

/**
 * @brief 设置状态栏时间是否已通过手机对表确认可靠。
 * @param[in] reliable `true` 表示时间可靠并允许显示，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void system_ui_set_time_reliable(bool reliable) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();

    g_status_bar_time_reliable = reliable;
    system_ui_set_status_bar_time_visible(status_bar, reliable);
    system_ui_set_status_bar_time_visible(g_bt_disconnect_status_bar, reliable);
}

/**
 * @brief 按当前遮罩状态同步当前页底部状态栏显隐。
 * @return 无返回值。
 */
static void system_bt_disconnect_overlay_sync_status_bar(void) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();
    const char* current_app = app_manager_current_name();
    bool show_bottom = g_status_bar_bottom_visible;

    if (status_bar == NULL) {
        floatair_info("sync bt disconnect status bar skipped: status_bar=NULL, app=%s, current_app=%s, overlay_visible=%d, bottom_expected=%d",
                      app_router_get_app(),
                      (current_app != NULL) ? current_app : "N/A",
                      (int)g_bt_disconnect_overlay_visible,
                      (int)show_bottom);
        return;
    }

    status_bar_set_visible(status_bar,
                           g_bt_disconnect_overlay_visible ? false : show_bottom);
    floatair_info("sync bt disconnect status bar: status_bar=%p, current_app=%s, overlay_visible=%d, bottom_expected=%d",
                  status_bar,
                  (current_app != NULL) ? current_app : "N/A",
                  (int)g_bt_disconnect_overlay_visible,
                  (int)show_bottom);

    if (!g_bt_disconnect_overlay_visible && show_bottom) {
        lv_obj_move_foreground(status_bar);
    }
}

/**
 * @brief 创建 app 层底部状态栏。
 * @param[in] parent 状态栏父对象。
 * @return 无返回值。
 */
static void system_status_bar_bottom_create(lv_obj_t* parent) {
    if (parent == NULL || g_status_bar_bottom != NULL) {
        return;
    }

    g_status_bar_bottom = status_bar_create_with_pos(
        parent,
        (int32_t)config_lcd.ui_width,
        NULL,
        STATUS_BAR_POS_BOTTOM);
    floatair_assert(g_status_bar_bottom != NULL, "bottom status bar create failed");
    system_ui_refresh_status_bar(g_status_bar_bottom);
    status_bar_set_visible(g_status_bar_bottom, g_status_bar_bottom_visible);
    lv_obj_move_foreground(g_status_bar_bottom);
    floatair_info("bottom status bar created: parent=%p status_bar=%p", parent, g_status_bar_bottom);
}

/**
 * @brief 创建系统级蓝牙断连全屏遮罩。
 * @param[in] parent 遮罩挂载的父节点。
 * @return 无返回值。
 */
static void system_bt_disconnect_overlay_create(lv_obj_t* parent) {
    lv_obj_t* status_bar_host = NULL;

    if (parent == NULL || g_bt_disconnect_overlay != NULL) {
        return;
    }

    floatair_assert(system_bt_disconnect_overlay_init_ui(parent, &g_bt_disconnect_overlay_ui),
                    "bt disconnect overlay init failed");
    g_bt_disconnect_overlay = container_get_obj(g_bt_disconnect_overlay_ui.overlay);
    floatair_assert(g_bt_disconnect_overlay != NULL, "bt disconnect overlay NULL");
    lv_obj_remove_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);

    status_bar_host = container_get_obj(g_bt_disconnect_overlay_ui.status_bar_host);
    floatair_assert(status_bar_host != NULL, "bt disconnect status bar host NULL");
    g_bt_disconnect_status_bar = status_bar_create_with_pos(
        status_bar_host,
        (int32_t)config_lcd.ui_width,
        NULL,
        STATUS_BAR_POS_BOTTOM);
    floatair_assert(g_bt_disconnect_status_bar != NULL, "bt disconnect status bar create failed");
    system_ui_refresh_status_bar(g_bt_disconnect_status_bar);

    system_ui_refresh_bt_disconnect_overlay_text();
    floatair_info("bt disconnect overlay created: parent=%p overlay=%p size=%ux%u",
                  parent,
                  g_bt_disconnect_overlay,
                  (unsigned)config_lcd.ui_width,
                  (unsigned)config_lcd.ui_height);
}

/**
 * @brief 立即刷新指定状态栏的电量、充电态与缓存时间显示。
 * @param[in] status_bar 目标状态栏对象。
 * @return 无返回值。
 */
void system_ui_refresh_status_bar(lv_obj_t* status_bar) {
    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return;
    }

    status_bar_update_battery(status_bar, g_status_bar_battery);
    status_bar_update_charge_state(status_bar, g_status_bar_charge_state);
    status_bar_set_wear_detection_visible(status_bar, g_status_bar_wear_detection_visible);
    system_ui_set_status_bar_time_visible(status_bar, g_status_bar_time_reliable);
    if (g_status_bar_time_reliable && g_status_bar_time_valid) {
        (void)system_ui_refresh_status_bar_time(status_bar, g_status_bar_time_epoch);
    }
}

/**
 * @brief 在显示距离档位变化后同步刷新前后台页面场景布局。
 * @return 无返回值。
 */
void system_ui_refresh_display_distance_level(void) {
    lv_coord_t content_h = system_ui_get_page_content_height();

    if (!system_ui_apply_display_distance_level_if_needed()) {
        return;
    }

    floatair_info("refresh display distance level: app=%s content_h=%d",
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A",
                  (int)content_h);
    app_manager_sync_current_view_layout((int32_t)config_lcd.ui_width, content_h);
    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }
}

/**
 * @brief 立即将当前活动屏幕标记为脏并触发一次 LVGL 刷屏。
 * @return `true` 表示已触发刷屏，`false` 表示当前没有有效活动屏幕。
 */
bool system_ui_refresh_screen_now(void) {
    lv_obj_t* screen = lv_screen_active();

    if (screen == NULL || !lv_obj_is_valid(screen)) {
        floatair_warn("refresh screen ignored: active screen invalid");
        return false;
    }

    floatair_info("refresh screen now: screen=%p", screen);
    lv_obj_invalidate(screen);
    lv_refr_now(NULL);
    return true;
}

/**
 * @brief 将电量值同步到底部状态栏。
 * @param[in] battery 电量百分比。
 * @return 无返回值。
 */
void system_ui_update_battery(uint8_t battery) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();

    g_status_bar_battery = battery;

    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_update_battery(status_bar, battery);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_update_battery(g_bt_disconnect_status_bar, battery);
    }
}

/**
 * @brief 将充电状态同步到底部状态栏。
 * @param[in] charge_state 充电状态值。
 * @return 无返回值。
 */
void system_ui_update_charge_state(uint8_t charge_state) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();

    g_status_bar_charge_state = charge_state;

    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_update_charge_state(status_bar, charge_state);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_update_charge_state(g_bt_disconnect_status_bar, charge_state);
    }
}

/**
 * @brief 设置底部状态栏佩戴检测图标显隐。
 * @param[in] visible `true` 表示显示图标占位，`false` 表示隐藏图标占位。
 * @return 无返回值。
 */
void system_ui_set_wear_detection_visible(bool visible) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();

    g_status_bar_wear_detection_visible = visible;
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_set_wear_detection_visible(status_bar, visible);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_set_wear_detection_visible(g_bt_disconnect_status_bar, visible);
    }
}

/**
 * @brief 按指定时间戳刷新底部状态栏时间。
 * @param[in] time_now 需要显示的时间戳。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
bool system_ui_update_time_from_epoch(time_t time_now) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();

    g_status_bar_time_epoch = time_now;
    g_status_bar_time_valid = true;

    bool ok = true;
    system_ui_set_status_bar_time_visible(status_bar, g_status_bar_time_reliable);
    system_ui_set_status_bar_time_visible(g_bt_disconnect_status_bar, g_status_bar_time_reliable);
    if (!g_status_bar_time_reliable) {
        return true;
    }

    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        ok = system_ui_refresh_status_bar_time(status_bar, time_now);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        ok = system_ui_refresh_status_bar_time(g_bt_disconnect_status_bar, time_now) && ok;
    }
    return ok;
}

/**
 * @brief 在蓝牙断连遮罩显示时拦截页面输入事件。
 * @return `true` 表示事件已被遮罩吞掉，`false` 表示应继续分发。
 */
bool system_ui_try_intercept_bt_disconnect_overlay_input(void) {
    if (!system_config_get_langselection_finish()) {
        return false;
    }
    if (!system_bt_disconnect_overlay_is_active()) {
        return false;
    }

    floatair_info("bt disconnect overlay intercepted input");
    return true;
}

/**
 * @brief 按当前系统运行时状态统一同步系统壳层显隐与层级。
 * @return 无返回值。
 */
void system_ui_sync_shell_state(void) {
    bool bt_connected = system_get_btconn_state();
    bool overlay_visible = system_bt_disconnect_overlay_should_show();

    floatair_info("sync shell state: bt_connected=%d, overlay_target=%d, app=%s, current_app=%s, overlay_prev=%d",
                  (int)bt_connected,
                  (int)overlay_visible,
                  app_router_get_app(),
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A",
                  (int)g_bt_disconnect_overlay_visible);
    system_ui_set_bt_disconnect_overlay_visible(overlay_visible);
}

/**
 * @brief 控制蓝牙断连遮罩显隐，并同步状态栏层级。
 * @param[in] visible `true` 表示显示遮罩，`false` 表示隐藏遮罩。
 * @return 无返回值。
 */
void system_ui_set_bt_disconnect_overlay_visible(bool visible) {
    bool overlay_valid = g_bt_disconnect_overlay != NULL && lv_obj_is_valid(g_bt_disconnect_overlay);
    bool hidden_before = false;

    if (!system_config_get_langselection_finish()) {
        visible = false;
    }

    if (overlay_valid) {
        hidden_before = lv_obj_has_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    floatair_info("set bt disconnect overlay visible: req=%d, prev=%d, valid=%d, hidden_before=%d, app=%s, current_app=%s",
                  (int)visible,
                  (int)g_bt_disconnect_overlay_visible,
                  (int)overlay_valid,
                  (int)hidden_before,
                  app_router_get_app(),
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A");

    if (g_bt_disconnect_overlay == NULL || !lv_obj_is_valid(g_bt_disconnect_overlay)) {
        floatair_warn("set bt disconnect overlay visible ignored: overlay invalid, req=%d", (int)visible);
        return;
    }

    system_ui_refresh_bt_disconnect_overlay_text();

    if (hidden_before == !visible) {
        floatair_info("set bt disconnect overlay visible skipped: unchanged req=%d", (int)visible);
        g_bt_disconnect_overlay_visible = visible;
        return;
    }

    g_bt_disconnect_overlay_visible = visible;
    if (visible) {
        uint32_t level = system_config_get_displaylevel();
        int32_t distance = system_get_displaylevel_value(level);

        system_notification_clear();
        toast_dismiss_active();
        (void)notify_list_close();
        (void)assistant_close(false);
        jyt_dual_screen_set_root_distance((int)distance);
        g_display_level_applied = level;
        system_ui_sync_app_layer_scene();
        lv_obj_remove_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_bt_disconnect_overlay);
        system_ui_sync_app_layer_scene();
    } else {
        lv_obj_add_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);
        system_ui_sync_app_layer_scene();
    }

    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }

    floatair_info("set bt disconnect overlay visible done: req=%d, hidden_after=%d, active=%d, status_bar_expected=%d",
                  (int)visible,
                  (int)lv_obj_has_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN),
                  (int)system_bt_disconnect_overlay_is_active(),
                  (int)g_status_bar_bottom_visible);
}

/**
 * @brief 初始化系统 LVGL 根节点、页面容器和底部状态栏。
 * @return 返回当前活动屏幕根对象，失败时触发断言。
 */
lv_obj_t* system_init_lvgl_fb(void) {
    lv_obj_t* p_root = lv_screen_active();
    lv_obj_t* page_parent = NULL;
    int32_t output_width = 0;
    int32_t output_height = 0;
    floatair_assert(p_root != NULL, "lv_screen_active failed");
    config_lcd.ui_x_begin = SYSTEM_LCD_UI_X_BEGIN;
    config_lcd.ui_y_begin = SYSTEM_LCD_UI_Y_BEGIN;
    config_lcd.ui_width = SYSTEM_LCD_UI_WIDTH;
    config_lcd.ui_height = SYSTEM_LCD_UI_HEIGHT;
    output_width = app_stereo_get_output_width();
    output_height = app_stereo_get_output_height();
    floatair_info("init lvgl fb: root=%p logical=%ux%u output=%dx%d",
                  p_root,
                  (unsigned)config_lcd.ui_width,
                  (unsigned)config_lcd.ui_height,
                  (int)output_width,
                  (int)output_height);
    lv_obj_remove_style_all(p_root);
    lv_obj_set_style_border_width(p_root, 0, LV_PART_MAIN);
    lv_obj_set_size(p_root, output_width, output_height);
    lv_obj_set_style_bg_color(p_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(p_root, true, LV_PART_MAIN);

    if (app_layers_init(p_root, (int32_t)config_lcd.ui_width, (int32_t)config_lcd.ui_height)) {
        page_parent = app_layers_get_app();
    } else {
        floatair_warn("app framework layers init failed");
        page_parent = p_root;
    }

    system_status_bar_bottom_create(page_parent);
    system_bt_disconnect_overlay_create(
        (app_layers_get_overlay() != NULL) ? app_layers_get_overlay() : p_root);
    floatair_info("init lvgl fb: defer shell sync until first page load");
    (void)system_ui_apply_display_distance_level_if_needed();
    system_ui_sync_app_layer_scene();
    return p_root;
}

/**
 * @brief 获取指定位置的系统状态栏对象。
 * @param[in] pos 状态栏位置。
 * @return 返回对应位置的状态栏对象；当前仅支持底部状态栏。
 */
lv_obj_t* system_get_status_bar(status_bar_widget_pos_t pos) {
    if (pos != STATUS_BAR_POS_BOTTOM) {
        return NULL;
    }

    return system_ui_get_current_status_bar();
}

/**
 * @brief 设置底部状态栏显示模式，并同步页面布局与遮罩层级。
 * @param[in] show_bottom `true` 表示显示底部状态栏，`false` 表示隐藏。
 * @return 无返回值。
 */
void system_status_bar_set_mode(bool show_bottom) {
    lv_coord_t content_h = 0;
    bool prev_show_bottom = true;

    prev_show_bottom = g_status_bar_bottom_visible;
    g_status_bar_bottom_visible = show_bottom;
    floatair_info("set status bar mode: prev=%d next=%d current_app=%s",
                  (int)prev_show_bottom,
                  (int)show_bottom,
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A");

    if (prev_show_bottom == show_bottom) {
        return;
    }

    content_h = system_ui_calc_page_content_height(show_bottom);
    app_manager_sync_current_view_layout((int32_t)config_lcd.ui_width, content_h);
    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }
}
