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
#include "common/app_framework/app_router.h"
#include "common/app_framework/app_stereo.h"
#include "common/product_app.h"
#if defined(APP_NAME_FACTORY)
#include "factory/factory.h"
#endif
#include "system/popups/notify/notify.h"
#include "common/widgets/toast.h"
#include "system/popups/assistant/assistant.h"
#include "system/popups/notify_list/notify_list.h"
#include "app_lcd.h"
#include "app_def.h"
#include "sys_adapter.h"
#include "ui_res.h"

#include <inttypes.h>
#include <time.h>

/* 未校时 RTC 的缺省日期早于产品有效日期；日历硬件支持到 2099 年。 */
#define SYSTEM_RTC_VALID_EPOCH_MIN INT64_C(1735689600)  ///< 2025-01-01 00:00:00 UTC。
#define SYSTEM_RTC_VALID_EPOCH_END INT64_C(4102444800)  ///< 2100-01-01 00:00:00 UTC，不包含。

static lv_obj_t* g_status_bar = NULL;                 ///< app 层当前状态栏对象
static lv_obj_t* g_bt_disconnect_status_bar = NULL;   ///< 蓝牙断连遮罩层底部状态栏对象
static bool g_status_bar_visible = true;              ///< 当前状态栏期望显隐状态
static bool g_status_bar_overlay = false;             ///< 状态栏是否叠加在全高页面内容之上
static status_bar_widget_pos_t g_status_bar_position = STATUS_BAR_POS_BOTTOM; ///< 当前状态栏位置
static lv_obj_t* g_bt_disconnect_overlay = NULL;      ///< 蓝牙断连全屏遮罩
static system_bt_disconnect_overlay_ui_t g_bt_disconnect_overlay_ui; ///< 蓝牙断连遮罩生成布局句柄
static bool g_bt_disconnect_overlay_visible = false;  ///< 蓝牙断连遮罩当前显隐状态
static uint32_t g_display_level_applied = UINT32_MAX; ///< 最近一次已应用的显示距离档位
static uint8_t g_status_bar_battery = DEFAULT_BATTERY_LEVEL; ///< 最近一次通过消息推送同步的电量
static uint8_t g_status_bar_charge_state = 0;         ///< 最近一次通过消息推送同步的充电状态
static bool g_status_bar_left_headset_connected = false;  ///< 最近一次同步的左侧耳机附件连接状态。
static bool g_status_bar_right_headset_connected = false; ///< 最近一次同步的右侧耳机附件连接状态。
static time_t g_status_bar_time_epoch = 0;            ///< 最近一次收到的设备时间戳
static bool g_status_bar_time_valid = false;          ///< 设备时间戳缓存是否有效
static bool g_status_bar_time_reliable = false;       ///< 时间是否已由手机校时或 RTC 恢复确认可靠
static bool g_status_bar_time_visible = true;         ///< 当前 App 是否允许状态栏展示时间
static bool g_status_bar_refresh_pending = false;     ///< 灭屏期间状态栏缓存变化后待亮屏刷新标记
static bool g_screen_refresh_pending = false;         ///< 等待下一应用刷新周期处理的强制刷屏标记
static uint32_t g_progress_hint_event_id = 0;         ///< 自定义进度提示事件 ID

static bool system_ui_sync_app_layer_scene(void);

uint32_t system_ui_get_progress_hint_event(void) {
    if (g_progress_hint_event_id == 0) {
        g_progress_hint_event_id = lv_event_register_id();
    }

    return g_progress_hint_event_id;
}

bool system_ui_send_progress_hint(const system_progress_hint_param_t* param) {
    lv_obj_t* root = NULL;

    if (param == NULL) {
        return false;
    }

    root = app_manager_current_content_root();
    if (root == NULL || !lv_obj_is_valid(root)) {
        floatair_err("current page root is NULL");
        return false;
    }

    (void)lv_obj_send_event(root, system_ui_get_progress_hint_event(), (void*)param);
    return true;
}

/**
 * @brief 判断蓝牙断连遮罩是否应按连接态显示。
 * @return `true` 表示允许显示断连遮罩，`false` 表示当前应让前置流程独占页面。
 */
static bool system_bt_disconnect_overlay_should_show(void) {
#if defined(APP_NAME_FACTORY)
    if (factory_aging_is_enabled()) {
        return false;
    }
#endif
    const char* app_name = assistant_is_open()
                               ? product_app_role_name(PRODUCT_APP_ROLE_ASSISTANT)
                               : app_manager_current_name();

    return system_config_get_langselection_finish() &&
           (!system_get_btconn_state() || !app_router_has_app_config()) &&
           (!PRODUCT_BT_DISCONNECT_POLICY_APP ||
            product_app_name_has_capability(app_name, PRODUCT_APP_CAP_REQUIRES_PHONE));
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
 * @brief 获取当前活动页面状态栏对象。
 * @return 成功返回当前页面状态栏，失败返回 `NULL`。
 */
static lv_obj_t* system_ui_get_current_status_bar(void) {
    if (g_status_bar == NULL || !lv_obj_is_valid(g_status_bar)) {
        return NULL;
    }

    return g_status_bar;
}

/**
 * @brief 依据状态栏模式计算页面内容区高度。
 * @param[in] visible `true` 表示状态栏可见，`false` 表示状态栏隐藏。
 * @return 返回内容区高度。
 */
static lv_coord_t system_ui_calc_page_content_height(bool visible) {
    lv_coord_t height = (lv_coord_t)config_lcd.ui_height;

    if (visible && !g_status_bar_overlay) {
        height -= STATUS_BAR_HEIGHT;
    }

    if (height < 0) {
        height = 0;
    }

    return height;
}

/**
 * @brief 依据状态栏模式计算页面内容区纵向偏移。
 * @param[in] visible `true` 表示状态栏可见。
 * @param[in] pos 状态栏位置。
 * @return 返回页面内容区相对 app 层顶部的偏移。
 */
static lv_coord_t system_ui_calc_page_content_offset_y(
    bool visible,
    status_bar_widget_pos_t pos) {
    return (visible && !g_status_bar_overlay && pos == STATUS_BAR_POS_TOP)
               ? STATUS_BAR_HEIGHT
               : 0;
}

/**
 * @brief 获取当前状态栏模式下的页面内容区高度。
 * @return 返回内容区高度。
 */
lv_coord_t system_ui_get_page_content_height(void) {
    return system_ui_calc_page_content_height(g_status_bar_visible);
}

lv_coord_t system_ui_get_page_content_offset_y(void) {
    return system_ui_calc_page_content_offset_y(g_status_bar_visible,
                                                g_status_bar_position);
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
 * @brief 同步 app 层尺寸、显示距离与垂直显示位置。
 * @return `true` 表示应用图层纵向位置发生变化，`false` 表示保持不变。
 */
static bool system_ui_sync_app_layer_scene(void) {
    const char* current_app = app_manager_current_name();
    system_display_position_t position = system_ui_get_display_position();
    bool move_app = product_app_name_has_capability(
        current_app,
        PRODUCT_APP_CAP_DISPLAY_POSITION);
    bool move_app_float = move_app || product_app_name_has_capability(
        current_app,
        PRODUCT_APP_CAP_DISPLAY_POSITION_FLOAT);
    int32_t available_shift = 0;
    int32_t position_offset_y = 0;
    int32_t app_offset_y = 0;
    int32_t app_float_offset_y = 0;

    app_layers_resize((int32_t)config_lcd.ui_width, (int32_t)config_lcd.ui_height);

    if (move_app_float) {
        available_shift = (int32_t)config_lcd.ui_height -
                          SYSTEM_UI_HALF_PAGE_HEIGHT -
                          STATUS_BAR_HEIGHT;
        if (available_shift < 0) {
            available_shift = 0;
        }

        if (position == SYSTEM_DISPLAY_POSITION_TOP) {
            position_offset_y = -available_shift;
        } else if (position == SYSTEM_DISPLAY_POSITION_MIDDLE) {
            position_offset_y = -(available_shift / 2);
        }
    }

    app_offset_y = move_app ? position_offset_y : 0;
    app_float_offset_y = move_app_float ? position_offset_y : 0;
    if (!app_layers_set_vertical_offsets(app_offset_y, app_float_offset_y)) {
        return false;
    }

    floatair_info("sync app vertical position: app=%s position=%d app_offset_y=%" PRId32
                  " app_float_offset_y=%" PRId32,
                  current_app != NULL ? current_app : "N/A",
                  (int)position,
                  app_offset_y,
                  app_float_offset_y);
    return true;
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
    status_bar_update_calendar(status_bar,
                               (uint8_t)ptm->tm_mday,
                               (uint8_t)(ptm->tm_mon + 1),
                               (uint8_t)ptm->tm_wday);
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
 * @brief 设置状态栏时间是否已由手机校时或 RTC 恢复确认可靠。
 * @param[in] reliable `true` 表示时间可靠并允许显示，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void system_ui_set_time_reliable(bool reliable) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_time_reliable = reliable;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("time reliable ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
    system_ui_set_status_bar_time_visible(status_bar,
                                          reliable && g_status_bar_time_visible);
    system_ui_set_status_bar_time_visible(g_bt_disconnect_status_bar, reliable);
}

/**
 * @brief 按当前遮罩状态同步当前页状态栏显隐。
 * @return 无返回值。
 */
static void system_bt_disconnect_overlay_sync_status_bar(void) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();
    const char* current_app = app_manager_current_name();
    bool show_status_bar = g_status_bar_visible;

    if (status_bar == NULL) {
        floatair_info("sync bt disconnect status bar skipped: status_bar=NULL, app=%s, current_app=%s, overlay_visible=%d, expected=%d",
                      app_router_get_app(),
                      (current_app != NULL) ? current_app : "N/A",
                      (int)g_bt_disconnect_overlay_visible,
                      (int)show_status_bar);
        return;
    }

    status_bar_set_visible(status_bar,
                           g_bt_disconnect_overlay_visible ? false : show_status_bar);
    floatair_info("sync bt disconnect status bar: status_bar=%p, current_app=%s, overlay_visible=%d, expected=%d",
                  status_bar,
                  (current_app != NULL) ? current_app : "N/A",
                  (int)g_bt_disconnect_overlay_visible,
                  (int)show_status_bar);

    if (!g_bt_disconnect_overlay_visible && show_status_bar) {
        lv_obj_move_foreground(status_bar);
    }
}

/**
 * @brief 创建 app 层状态栏。
 * @param[in] parent 状态栏父对象。
 * @return 无返回值。
 */
static void system_status_bar_create(lv_obj_t* parent) {
    if (parent == NULL || g_status_bar != NULL) {
        return;
    }

    g_status_bar = status_bar_create_with_pos(
        parent,
        (int32_t)config_lcd.ui_width,
        NULL,
        g_status_bar_position);
    floatair_assert(g_status_bar != NULL, "status bar create failed");
    system_ui_refresh_status_bar(g_status_bar);
    status_bar_set_visible(g_status_bar, g_status_bar_visible);
    lv_obj_move_foreground(g_status_bar);
    floatair_info("status bar created: parent=%p status_bar=%p position=%d",
                  parent,
                  g_status_bar,
                  (int)g_status_bar_position);
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
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("status bar refresh deferred: lcd off");
        return;
    }

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return;
    }

    status_bar_update_battery(status_bar, g_status_bar_battery);
    status_bar_update_charge_state(status_bar, g_status_bar_charge_state);
    status_bar_update_headset_state(status_bar,
                                    g_status_bar_left_headset_connected,
                                    g_status_bar_right_headset_connected);
    status_bar_update_bt_state(status_bar, system_get_btconn_state());
    system_ui_set_status_bar_time_visible(
        status_bar,
        g_status_bar_time_reliable &&
            (status_bar != g_status_bar || g_status_bar_time_visible));
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
    app_manager_sync_current_view_layout(
        (int32_t)config_lcd.ui_width,
        content_h,
        (int32_t)system_ui_get_page_content_offset_y());
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
    lv_obj_t* screen = NULL;

    if (floatair_lcd_get_state() == LCD_OFF) {
        g_screen_refresh_pending = true;
        floatair_dbg("refresh screen deferred: lcd off");
        return true;
    }

    screen = lv_screen_active();
    if (screen == NULL || !lv_obj_is_valid(screen)) {
        floatair_warn("refresh screen ignored: active screen invalid");
        return false;
    }

    (void)system_ui_sync_app_layer_scene();
    floatair_info("refresh screen now: screen=%p", screen);
    floatair_lcd_commit_frame(screen);
    return true;
}

bool system_ui_request_screen_refresh(void) {
    g_screen_refresh_pending = true;
    return true;
}

bool system_ui_apply_pending_screen_refresh(void) {
    lv_obj_t* screen = NULL;

    if (!g_screen_refresh_pending || floatair_lcd_get_state() == LCD_OFF) {
        return false;
    }

    screen = lv_screen_active();
    if (screen == NULL || !lv_obj_is_valid(screen)) {
        floatair_warn("pending refresh retained: active screen invalid");
        return false;
    }

    (void)system_ui_sync_app_layer_scene();
    g_screen_refresh_pending = false;
    lv_obj_invalidate(screen);
    floatair_dbg("pending refresh applied: screen=%p", screen);
    return true;
}

/**
 * @brief 获取当前持久化的应用垂直显示位置。
 * @return 返回顶部、中部或底部显示位置。
 */
system_display_position_t system_ui_get_display_position(void) {
    uint32_t position = system_config_get_displayposition();

    if (position < (uint32_t)SYSTEM_DISPLAY_POSITION_TOP ||
        position > (uint32_t)SYSTEM_DISPLAY_POSITION_BOTTOM) {
        return SYSTEM_DISPLAY_POSITION_BOTTOM;
    }
    return (system_display_position_t)position;
}

bool system_ui_set_display_position(system_display_position_t position) {
    system_display_position_t previous = system_ui_get_display_position();

    if (position < SYSTEM_DISPLAY_POSITION_TOP ||
        position > SYSTEM_DISPLAY_POSITION_BOTTOM) {
        floatair_err("invalid display position: %d", (int)position);
        return false;
    }

    if (position == previous) {
        return true;
    }

    if (!system_config_set_displayposition((uint32_t)position)) {
        floatair_err("persist display position failed: %d", (int)position);
        return false;
    }
    floatair_info("set display position: %d->%d", (int)previous, (int)position);
    return system_ui_request_screen_refresh();
}

bool system_ui_render_screen_off_frame(void) {
    lv_obj_t* parent = NULL;
    lv_obj_t* frame = NULL;

    if (floatair_lcd_get_state() == LCD_OFF) {
        return false;
    }

    parent = app_layers_get_top();
    if (parent == NULL || !lv_obj_is_valid(parent)) {
        floatair_warn("screen off frame unavailable: parent=%p", parent);
        return false;
    }

    frame = lv_obj_create(parent);
    if (frame == NULL) {
        floatair_warn("screen off frame create failed");
        return false;
    }
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, LV_PCT(100), LV_PCT(100));
    lv_obj_align(frame, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(frame, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_image_src(frame, UI_RES_IMAGE_PROCESSING, LV_PART_MAIN);
    lv_obj_set_style_bg_image_opa(frame, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_image_tiled(frame, false, LV_PART_MAIN);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(frame);
    lv_obj_update_layout(frame);

    floatair_lcd_commit_frame(frame);
    lv_obj_delete(frame);
    floatair_info("screen off processing frame committed");
    return true;
}

/**
 * @brief 将电量值同步到底部状态栏。
 * @param[in] battery 电量百分比。
 * @return 无返回值。
 */
void system_ui_update_battery(uint8_t battery) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_battery = battery;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("battery ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
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
    lv_obj_t* status_bar = NULL;

    g_status_bar_charge_state = charge_state;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("charge state ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_update_charge_state(status_bar, charge_state);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_update_charge_state(g_bt_disconnect_status_bar, charge_state);
    }
}

/**
 * @brief 将左右耳机附件连接状态同步到全局状态栏。
 * @param[in] left_connected `true` 表示左侧耳机附件已连接。
 * @param[in] right_connected `true` 表示右侧耳机附件已连接。
 * @return 无返回值。
 */
void system_ui_update_headset_state(bool left_connected, bool right_connected) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_left_headset_connected = left_connected;
    g_status_bar_right_headset_connected = right_connected;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("headset state ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_update_headset_state(status_bar, left_connected, right_connected);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_update_headset_state(g_bt_disconnect_status_bar,
                                        left_connected,
                                        right_connected);
    }
}

/**
 * @brief 获取当前设备时间，认可手机校时或 RTC 恢复的有效时间。
 * @param[out] reliable 时间是否已由手机校时或 RTC 恢复确认可靠；允许传入 `NULL`。
 * @return 当前设备时间戳。
 */
time_t system_ui_time_now(bool* reliable) {
    time_t now;
#if defined(BUILD_NATIVE) && BUILD_NATIVE
    now = simulator_system_time_now();
#else
    now = time(NULL);
    /* 固件启动已从 RTC 恢复系统时间，无需再次连接手机才允许显示。 */
    if ((int64_t)now >= SYSTEM_RTC_VALID_EPOCH_MIN &&
        (int64_t)now < SYSTEM_RTC_VALID_EPOCH_END) {
        g_status_bar_time_reliable = true;
    }
#endif
    if (reliable != NULL) {
        *reliable = g_status_bar_time_reliable;
    }
    return now;
}

/**
 * @brief 按指定时间戳刷新状态栏时间与日期。
 * @param[in] time_now 需要显示的时间戳。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
bool system_ui_update_time_from_epoch(time_t time_now) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_time_epoch = time_now;
    g_status_bar_time_valid = true;
    (void)system_ui_time_now(NULL);
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("time ui update deferred: lcd off");
        return true;
    }

    bool ok = true;
    status_bar = system_ui_get_current_status_bar();
    system_ui_set_status_bar_time_visible(status_bar,
                                          g_status_bar_time_reliable &&
                                              g_status_bar_time_visible);
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
 * @brief 亮屏后统一补刷灭屏期间延迟的系统 UI 更新。
 * @return 无返回值。
 */
void system_ui_flush_pending_after_screen_on(void) {
    if (floatair_lcd_get_state() == LCD_OFF) {
        return;
    }

    if (g_status_bar_refresh_pending) {
        g_status_bar_refresh_pending = false;
        system_ui_refresh_status_bar(g_status_bar);
        system_ui_refresh_status_bar(g_bt_disconnect_status_bar);
    }

    /* 强制刷屏请求由应用消息循环在下一刷新周期统一执行。 */
}

/**
 * @brief 在蓝牙断连遮罩显示时拦截页面输入事件，按 App 模式支持双击返回。
 * @param[in] code 归一化后的 LVGL 输入事件码。
 * @return `true` 表示事件已被遮罩吞掉，`false` 表示应继续分发。
 */
bool system_ui_try_intercept_bt_disconnect_overlay_input(lv_event_code_t code) {
    if (!system_config_get_langselection_finish()) {
        return false;
    }
    if (!system_bt_disconnect_overlay_is_active()) {
        return false;
    }

    if (PRODUCT_BT_DISCONNECT_POLICY_APP && code == LV_EVENT_DCLICKED) {
        if (assistant_is_open()) {
            (void)assistant_close(false);
        } else {
            (void)app_router_exit_current_app();
        }
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
    if (system_ui_sync_app_layer_scene()) {
        (void)system_ui_request_screen_refresh();
    }
    status_bar_update_bt_state(g_status_bar, bt_connected);
    status_bar_update_bt_state(g_bt_disconnect_status_bar, bt_connected);
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
        if (!PRODUCT_BT_DISCONNECT_POLICY_APP) {
            (void)assistant_close(false);
        }
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
                  (int)g_status_bar_visible);
}

/**
 * @brief 初始化系统 LVGL 根节点、页面容器和状态栏。
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

    system_status_bar_create(page_parent);
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
 * @return 返回对应位置的当前状态栏对象；位置不匹配时返回 `NULL`。
 */
lv_obj_t* system_get_status_bar(status_bar_widget_pos_t pos) {
    if (pos != g_status_bar_position) {
        return NULL;
    }

    return system_ui_get_current_status_bar();
}

/**
 * @brief 设置状态栏显示模式与位置，并同步页面布局与遮罩层级。
 * @param[in] visible `true` 表示显示状态栏，`false` 表示隐藏。
 * @param[in] pos 状态栏目标位置。
 * @return 无返回值。
 */
void system_status_bar_set_mode_at(bool visible, status_bar_widget_pos_t pos) {
    lv_coord_t content_h = 0;
    lv_coord_t content_offset_y = 0;
    bool prev_visible = g_status_bar_visible;
    status_bar_widget_pos_t prev_position = g_status_bar_position;

    if (pos != STATUS_BAR_POS_TOP && pos != STATUS_BAR_POS_BOTTOM) {
        floatair_err("invalid status bar position: %d", (int)pos);
        return;
    }

    g_status_bar_visible = visible;
    g_status_bar_position = pos;
    status_bar_set_position(g_status_bar, pos);
    floatair_info("set status bar mode: visible=%d->%d position=%d->%d current_app=%s",
                  (int)prev_visible,
                  (int)visible,
                  (int)prev_position,
                  (int)pos,
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A");

    if (prev_visible != visible || prev_position != pos) {
        content_h = system_ui_calc_page_content_height(visible);
        content_offset_y = system_ui_calc_page_content_offset_y(visible, pos);
        app_manager_sync_current_view_layout((int32_t)config_lcd.ui_width,
                                             content_h,
                                             content_offset_y);
    }

    if (visible) {
        system_ui_refresh_status_bar(g_status_bar);
    }
    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }
}

void system_status_bar_set_position(status_bar_widget_pos_t pos) {
    system_status_bar_set_mode_at(g_status_bar_visible, pos);
}

void system_status_bar_set_mode(bool show_bottom) {
    system_status_bar_set_mode_at(show_bottom, STATUS_BAR_POS_BOTTOM);
}

/**
 * @brief 切换状态栏占位模式，并同步当前页面承载区尺寸。
 * @param[in] overlay `true` 表示页面内容保持全高，`false` 表示为状态栏预留高度。
 * @return 无返回值。
 */
void system_status_bar_set_overlay(bool overlay) {
    lv_coord_t content_h = 0;
    lv_coord_t content_offset_y = 0;

    if (g_status_bar_overlay == overlay) {
        return;
    }

    g_status_bar_overlay = overlay;
    content_h = system_ui_calc_page_content_height(g_status_bar_visible);
    content_offset_y = system_ui_calc_page_content_offset_y(g_status_bar_visible,
                                                            g_status_bar_position);
    app_manager_sync_current_view_layout((int32_t)config_lcd.ui_width,
                                         content_h,
                                         content_offset_y);
    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }
}

/**
 * @brief 设置当前 App 状态栏的时间显示策略。
 * @param[in] visible `true` 表示时间可靠时允许显示，`false` 表示始终隐藏。
 * @return 无返回值。
 */
void system_status_bar_set_time_visible(bool visible) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();

    g_status_bar_time_visible = visible;
    system_ui_set_status_bar_time_visible(status_bar,
                                          visible && g_status_bar_time_reliable);
}

void system_status_bar_set_app_name(const char* app_name) {
    if (g_status_bar == NULL || !lv_obj_is_valid(g_status_bar)) {
        return;
    }

    status_bar_update_app_name(g_status_bar, app_name);
}
