/**
 * @file system_notification.c
 * @brief 系统通知队列与 Notification 协议处理实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include <time.h>
#include <stdio.h>
#include <inttypes.h>

#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "app_lcd.h"
#include "system/popups/notify_list/notify_list.h"
#include "system/system.h"
#include "system/system_notification.h"
#include <lvgl.h>

#include <stdbool.h>
#include <string.h>
#include "ui_res.h"

typedef struct {
    system_notification_entry_t entry;
} notification_item_t;

static notification_item_t s_notification_queue[SYSTEM_NOTIFICATION_QUEUE_MAX] = {0};
static size_t s_notification_queue_count = 0;
static notification_item_t s_active_notification = {0};
static notify_call_state_t s_active_call_state = NOTIFY_CALL_STATE_RINGING;
static bool s_call_dismiss_pending = false; ///< 息屏期间请求关闭电话弹窗，亮屏后先销毁旧 UI。

/**
 * @brief 清空当前活动通知及其来电阶段缓存。
 * @return 无返回值。
 */
static void system_notification_clear_active(void) {
    memset(&s_active_notification, 0, sizeof(s_active_notification));
    s_active_call_state = NOTIFY_CALL_STATE_RINGING;
}

/**
 * @brief 通知操作 LVGL 前保持系统处于亮屏状态。
 *
 * 通知可能在 LCD 已息屏时到达，必须先复用系统亮屏入口，保证后续
 * Notify、通知列表等 LVGL 操作都发生在 LCD ON 之后。
 * @return 无返回值。
 */
static void system_notification_keep_screen_awake(void) {
    bool was_screen_off = (system_get_sys_state() == LCD_OFF);

    system_set_sys_state(LCD_ON);
    if (was_screen_off) {
        system_report_sys_state(LCD_ON, SYSTEM_SYS_STATE_TRIGGER_NOTIFICATION);
    }
}

/**
 * @brief 判断通知是否应静默进入通知列表。
 *
 * 条目显式标记静默时只进入列表；此外，通知提醒开关关闭时，来自手机 App
 * 或 ANCS 的普通用户消息也只进入列表，不亮屏、不弹悬浮通知。
 * @param[in] item 通知项。
 * @param[in] is_user_notification 是否来自用户通知通道。
 * @return `true` 表示只入列表，`false` 表示继续走弹框/亮屏流程。
 */
static bool system_notification_should_queue_silently(const notification_item_t* item,
                                                       bool is_user_notification) {
    if (item == NULL) {
        return false;
    }
    if (item->entry.silent) {
        return true;
    }
    if (!is_user_notification) {
        return false;
    }

    return !system_config_get_notification_enabled() &&
           item->entry.mode == NOTIFY_MODE_MESSAGE;
}

/**
 * @brief 判断 Host 通知消息是否应跳过用户活动保活。
 * @param[in] node Host 通知命令 data 节点。
 * @param[in] msg Host MsgPack 消息头。
 * @return `true` 表示这条消息只静默入列表，不重置息屏定时器。
 */
bool system_notification_should_suppress_activity(mpack_node_t node, const msg_pack_t* msg) {
    notification_item_t item = {0};
    uint8_t type = 0;
    mpack_node_t type_node;

    if (!msg || strcmp(msg->biz, "Notification") != 0) {
        return false;
    }

    if (strcmp(msg->cmd, "addNotification") != 0 &&
        strcmp(msg->cmd, "updateNotification") != 0) {
        return false;
    }

    if (mpack_node_is_missing(node) || mpack_node_is_nil(node) ||
        mpack_node_type(node) != mpack_type_map) {
        return false;
    }

    type_node = mpack_node_map_cstr_optional(node, "type");
    if (!mpack_node_is_missing(type_node) && !mpack_node_is_nil(type_node)) {
        if (mpack_node_type(type_node) != mpack_type_uint) {
            return false;
        }
        type = mpack_node_u8(type_node);
    }

    item.entry.mode =
        (type == NOTIFY_MODE_CALL) ? NOTIFY_MODE_CALL : NOTIFY_MODE_MESSAGE;
    return system_notification_should_queue_silently(&item, type == 0U);
}


const void* system_notification_entry_icon_source(
    const system_notification_entry_t* entry) {
    if (entry == NULL) {
        return NULL;
    }
    if (entry->icon == SYSTEM_NOTIFICATION_ICON_NONE) {
        return NULL;
    }
    if (entry->icon == SYSTEM_NOTIFICATION_ICON_MISSED_CALL) {
#ifdef UI_RES_IMAGE_MISSED_CALL
        return UI_RES_IMAGE_MISSED_CALL;
#else
        return UI_RES_IMAGE_INCOMING_CALL;
#endif
    }
    if (entry->mode == NOTIFY_MODE_CALL) {
        return UI_RES_IMAGE_INCOMING_CALL;
    }
    return UI_RES_IMAGE_IM_MESSAGE;
}

/**
 * @brief 解析手机下发的通知位图。
 * @param[in] node 通知数据 map。
 * @param[out] entry 目标通知条目。
 * @return 无返回值；字段缺失或无效时保留 ROMFS 兜底图标。
 */
static void system_notification_parse_bitmap(mpack_node_t node,
                                              system_notification_entry_t* entry) {
    mpack_node_t icon_node;

    if (entry == NULL) {
        return;
    }
    icon_node = mpack_node_map_cstr_optional(node, "iconBitmap");
    if (mpack_node_is_missing(icon_node)) {
        icon_node = mpack_node_map_cstr_optional(node, "iconBytes");
    }
    if (mpack_node_is_missing(icon_node) || mpack_node_is_nil(icon_node) ||
        mpack_node_type(icon_node) != mpack_type_bin ||
        mpack_node_data_len(icon_node) < SYSTEM_NOTIFICATION_IMAGE_BUF_SIZE) {
        return;
    }

    memcpy(entry->image,
           mpack_node_bin_data(icon_node),
           SYSTEM_NOTIFICATION_IMAGE_BUF_SIZE);
    entry->image_size = SYSTEM_NOTIFICATION_IMAGE_BUF_SIZE;
    entry->has_image = true;
}


/**
 * @brief 为未接来电通知补专用图标。
 *
 * 未接来电在交互上仍归类为消息通知，
 * 但图标需要保持与来电通知一致。
 * @param[out] entry 目标通知项。
 * @return 无返回值。
 */
static void system_notification_apply_missed_call_icon(system_notification_entry_t* entry) {
    if (entry == NULL) {
        return;
    }
    entry->icon = SYSTEM_NOTIFICATION_ICON_MISSED_CALL;
}

/**
 * @brief 跳过文本开头的换行符。
 * @param[in] text 原始文本。
 * @return 返回跳过前导换行后的文本指针。
 */
static const char* system_notification_skip_leading_newlines(const char* text) {
    if (!text) {
        return NULL;
    }

    while (*text == '\n' || *text == '\r') {
        text++;
    }

    return text;
}

static void system_notification_set_display_text(system_notification_entry_t* entry,
                                                 const char* title,
                                                 const char* message) {
    const char* clean_title = system_notification_skip_leading_newlines(title);
    const char* clean_message = system_notification_skip_leading_newlines(message);

    if (!entry) {
        return;
    }

    entry->title[0] = '\0';

    if (entry->mode == NOTIFY_MODE_CALL) {
        const char* call_text = (clean_message && clean_message[0] != '\0') ? clean_message : clean_title;

        if (call_text && call_text[0] != '\0') {
            strncpy(entry->title, call_text, sizeof(entry->title) - 1);
        }

        entry->has_title = entry->title[0] != '\0';
        return;
    }

    if (clean_title && clean_title[0] != '\0' && clean_message && clean_message[0] != '\0') {
        snprintf(entry->title, sizeof(entry->title), "%s\n%s", clean_title, clean_message);
    } else if (clean_title && clean_title[0] != '\0') {
        strncpy(entry->title, clean_title, sizeof(entry->title) - 1);
    } else if (clean_message && clean_message[0] != '\0') {
        strncpy(entry->title, clean_message, sizeof(entry->title) - 1);
    }

    entry->has_title = entry->title[0] != '\0';
}

static uint8_t system_notification_priority(notify_mode_t mode) {
    return (mode == NOTIFY_MODE_CALL) ? 2U : 1U;
}


static void system_notification_enqueue(const notification_item_t* item) {
    if (!item) {
        return;
    }

    if (item->entry.id != 0U) {
        for (size_t i = 0; i < s_notification_queue_count; i++) {
            if (s_notification_queue[i].entry.id == item->entry.id) {
                (void)system_notification_remove_at(i);
                break;
            }
        }
    }

    if (s_notification_queue_count < SYSTEM_NOTIFICATION_QUEUE_MAX) {
        s_notification_queue[s_notification_queue_count++] = *item;
        return;
    }

    memmove(&s_notification_queue[0],
            &s_notification_queue[1],
            sizeof(s_notification_queue[0]) * (SYSTEM_NOTIFICATION_QUEUE_MAX - 1));
    s_notification_queue[SYSTEM_NOTIFICATION_QUEUE_MAX - 1] = *item;
}

size_t system_notification_copy(system_notification_entry_t* out_items, size_t capacity) {
    size_t copied = 0;

    if (!out_items || capacity == 0 || s_notification_queue_count == 0) {
        return 0;
    }

    copied = s_notification_queue_count < capacity ? s_notification_queue_count : capacity;
    for (size_t i = 0; i < copied; i++) {
        out_items[i] = s_notification_queue[i].entry;
    }

    return copied;
}

bool system_notification_remove_at(size_t index) {
    if (index >= s_notification_queue_count) {
        return false;
    }

    if (index + 1 < s_notification_queue_count) {
        memmove(&s_notification_queue[index],
                &s_notification_queue[index + 1],
                sizeof(s_notification_queue[0]) * (s_notification_queue_count - index - 1));
    }

    memset(&s_notification_queue[s_notification_queue_count - 1], 0, sizeof(s_notification_queue[0]));
    s_notification_queue_count--;
    return true;
}

void system_notification_clear(void) {
    if (system_get_sys_state() == LCD_ON) {
        notify_dismiss();
    }
    system_notification_clear_active();
    memset(s_notification_queue, 0, sizeof(s_notification_queue));
    s_notification_queue_count = 0;
}

/**
 * @brief 在亮屏且通知列表已打开时刷新列表内容。
 * @return 已刷新列表返回 `true`，否则返回 `false`。
 */
static bool system_notification_refresh_open_list(void) {
    if (system_get_sys_state() != LCD_ON || !notify_list_is_open()) {
        return false;
    }

    notify_list_view_reload();
    return true;
}

static void system_notification_on_event(notify_t* notify, lv_event_code_t code, void* user_data) {
    notification_item_t* item = (notification_item_t*)user_data;

    (void)notify;
    floatair_info("notification event code=%d title=%s",
                  (int)code,
                  item && item->entry.has_title ? item->entry.title : "");

    switch (code) {
        case LV_EVENT_GESTURE_RIGHT:
            if (item && item->entry.mode == NOTIFY_MODE_CALL) {
                dev_ctl_cmd_t cmd = {
                    .dev_type = DEV_CALL_CTRL,
                    .control_code = JYT_CALL_OP_HUNGUP,
                    .data = 0,
                };

                system_request_device_control(&cmd);
                system_notification_dismiss_call();
            }
            break;
        case LV_EVENT_GESTURE_LEFT:
            if (item &&
                item->entry.mode == NOTIFY_MODE_CALL &&
                s_active_call_state == NOTIFY_CALL_STATE_RINGING) {
                dev_ctl_cmd_t cmd = {
                    .dev_type = DEV_CALL_CTRL,
                    .control_code = JYT_CALL_OP_ANSWER,
                    .data = 0,
                };

                system_request_device_control(&cmd);
                (void)system_notification_update_call_state(NOTIFY_CALL_STATE_CONNECTED);
            }
            break;
        case LV_EVENT_DCLICKED:
            if (item && item->entry.mode == NOTIFY_MODE_CALL) {
                dev_ctl_cmd_t cmd = {
                    .dev_type = DEV_CALL_CTRL,
                    .control_code = JYT_CALL_OP_HUNGUP,
                    .data = 0,
                };

                system_request_device_control(&cmd);
                system_notification_dismiss_call();
                break;
            }
            notify_dismiss();
            break;
        case LV_EVENT_CLICKED:
        case LV_EVENT_LONG_PRESSED:
            if (item && item->entry.mode == NOTIFY_MODE_CALL) {
                dev_ctl_cmd_t cmd = {
                    .dev_type = DEV_CALL_CTRL,
                    .control_code = s_active_call_state == NOTIFY_CALL_STATE_RINGING
                                        ? JYT_CALL_OP_ANSWER
                                        : JYT_CALL_OP_HUNGUP,
                    .data = 0,
                };

                system_request_device_control(&cmd);
                system_notification_dismiss_call();
                break;
            }
            if (item && item->entry.mode == NOTIFY_MODE_MESSAGE) {
                notify_dismiss();
                (void)notify_list_open();
            }
            break;
        default:
            break;
    }
}

static bool system_notification_show_item(notification_item_t* item) {
    notify_cfg_t cfg = notify_default_cfg();
    notify_t* notify = NULL;

    if (!item) {
        return false;
    }

    cfg.title = item->entry.has_title ? item->entry.title : NULL;
    if (item->entry.has_image) {
        cfg.image_src = item->entry.image;
        cfg.image_src_size = item->entry.image_size;
    } else
    {
        cfg.image_src = system_notification_entry_icon_source(&item->entry);
        cfg.image_src_size = 0;
    }
    cfg.mode = item->entry.mode;
    cfg.call_state = s_active_call_state;
    cfg.duration_ms = item->entry.duration_ms;

    if (!cfg.title && !cfg.image_src) {
        return false;
    }

    notify = notify_show_with_cfg(&cfg);
    if (!notify) {
        return false;
    }

    notify_set_callbacks(notify, system_notification_on_event, item);
    return true;
}

bool system_notification_show_call(const char* title,
                                   const char* message,
                                   notify_call_state_t state) {
    notification_item_t item = {0};

    item.entry.mode = NOTIFY_MODE_CALL;
    item.entry.duration_ms = 0;
    system_notification_set_display_text(&item.entry, title, message);
    if (!item.entry.has_title) {
        return false;
    }

    s_active_notification = item;
    s_active_call_state = state;
    if (s_active_call_state != NOTIFY_CALL_STATE_OUTGOING &&
        s_active_call_state != NOTIFY_CALL_STATE_CONNECTED) {
        s_active_call_state = NOTIFY_CALL_STATE_RINGING;
    }
    system_notification_keep_screen_awake();
    if (!system_notification_show_item(&s_active_notification)) {
        return false;
    }

    return true;
}

bool system_notification_update_call_state(notify_call_state_t state) {
    bool remains_visible = false;

    if (s_active_notification.entry.mode != NOTIFY_MODE_CALL) {
        return false;
    }

    s_active_call_state = state;
    if (s_active_call_state != NOTIFY_CALL_STATE_OUTGOING &&
        s_active_call_state != NOTIFY_CALL_STATE_CONNECTED) {
        s_active_call_state = NOTIFY_CALL_STATE_RINGING;
    }
    if (system_get_sys_state() == LCD_OFF) {
        return false;
    }

    remains_visible = notify_update_call_state(s_active_call_state);
    if (!remains_visible) {
        system_notification_clear_active();
    }
    return remains_visible;
}

bool system_notification_update_call_caller(const char* caller) {
    if (s_active_notification.entry.mode != NOTIFY_MODE_CALL ||
        caller == NULL || caller[0] == '\0') {
        return false;
    }
    system_notification_set_display_text(&s_active_notification.entry, NULL, caller);
    return notify_update_call_text(s_active_notification.entry.title);
}

bool system_notification_show_missed_call(const char* message) {
    notification_item_t item = {0};

    item.entry.mode = NOTIFY_MODE_MESSAGE;
    item.entry.duration_ms = notify_default_cfg().duration_ms;
    system_notification_set_display_text(&item.entry, NULL, message);
    /* 未接来电沿用消息模式展示，但图标固定使用来电图标。 */
    system_notification_apply_missed_call_icon(&item.entry);
    if (!item.entry.has_title) {
        return false;
    }

    /* 未接来电只保留列表项，不保留当前悬浮通知弹框。 */
    if (system_get_sys_state() == LCD_ON) {
        notify_dismiss();
    }
    system_notification_clear_active();

    system_notification_enqueue(&item);
    if (system_notification_refresh_open_list()) {
        return true;
    }

    floatair_info("missed call queued only, no popup");
    return true;
}

void system_notification_dismiss_call(void) {
    notify_mode_t active_mode = NOTIFY_MODE_MESSAGE;
    bool has_active_call_notify = false;
    bool has_cached_call_notify = s_active_notification.entry.mode == NOTIFY_MODE_CALL;

    if (system_get_sys_state() == LCD_OFF) {
        s_call_dismiss_pending = true;
        if (has_cached_call_notify) {
            system_notification_clear_active();
        }
        s_active_call_state = NOTIFY_CALL_STATE_RINGING;
        return;
    }

    s_call_dismiss_pending = false;
    has_active_call_notify = notify_get_active_mode(&active_mode) &&
                             active_mode == NOTIFY_MODE_CALL;
    if (has_active_call_notify || has_cached_call_notify) {
        notify_dismiss();
        system_notification_clear_active();
    }
    s_active_call_state = NOTIFY_CALL_STATE_RINGING;
}

void system_notification_flush_pending_after_screen_on(void) {
    notify_mode_t active_mode = NOTIFY_MODE_MESSAGE;

    if (floatair_lcd_is_off() || !s_call_dismiss_pending) {
        return;
    }

    s_call_dismiss_pending = false;
    if (notify_get_active_mode(&active_mode) && active_mode == NOTIFY_MODE_CALL) {
        floatair_info("dismiss ended call popup after screen on");
        notify_dismiss();
    }
    /* 新来电可能已写入缓存并触发亮屏，此处只销毁旧 UI，不清理新来电缓存。 */
}

static bool system_notification_prepare_entry(notification_item_t* item,
                                              const system_notification_entry_t* entry) {
    if (!item || !entry) {
        return false;
    }

    memset(item, 0, sizeof(*item));
    item->entry = *entry;
    item->entry.has_title = item->entry.title[0] != '\0';
    if (item->entry.has_image &&
        item->entry.image_size != SYSTEM_NOTIFICATION_IMAGE_BUF_SIZE) {
        return false;
    }

    if (item->entry.mode == NOTIFY_MODE_CALL) {
        item->entry.duration_ms = 0;
    } else if (item->entry.duration_ms == 0) {
        item->entry.duration_ms = notify_default_cfg().duration_ms;
    }

    if (!item->entry.has_title &&
        !item->entry.has_image &&
        system_notification_entry_icon_source(&item->entry) == NULL) {
        return false;
    }

    return true;
}

bool system_notification_add_entry(const system_notification_entry_t* entry) {
    notification_item_t item = {0};
    bool should_queue_silently = false;
    bool list_open = false;

    if (!system_notification_prepare_entry(&item, entry)) {
        floatair_err("notification prepare entry failed");
        return false;
    }

    floatair_info("notification add id=%" PRIu32 " mode=%d duration=%" PRIu32,
                  item.entry.id,
                  (int)item.entry.mode,
                  item.entry.duration_ms);

    should_queue_silently = system_notification_should_queue_silently(&item, true);
    system_notification_enqueue(&item);

    if (should_queue_silently) {
        (void)system_notification_refresh_open_list();
        floatair_info("user notification queued silently: id=%" PRIu32 " mode=%d",
                      item.entry.id,
                      (int)item.entry.mode);
        return true;
    }

    system_notification_keep_screen_awake();
    list_open = system_notification_refresh_open_list();

    if (list_open) {
        if (item.entry.mode == NOTIFY_MODE_MESSAGE) {
            floatair_info("notification queued without popup on notify_list popup: mode=%d",
                          (int)item.entry.mode);
            return true;
        }
    }

    {
        notify_mode_t active_mode = NOTIFY_MODE_MESSAGE;
        bool has_active_notify = notify_get_active_mode(&active_mode);

        if (has_active_notify &&
            system_notification_priority(item.entry.mode) < system_notification_priority(active_mode)) {
            floatair_info("notification queued without preemption: incoming=%d active=%d",
                          (int)item.entry.mode,
                          (int)active_mode);
            return true;
        }
    }

    s_active_notification = item;
    floatair_info("notification show active id=%" PRIu32 " mode=%d duration=%" PRIu32,
                  item.entry.id,
                  (int)item.entry.mode,
                  item.entry.duration_ms);

    if (!system_notification_show_item(&s_active_notification)) {
        system_notification_clear_active();
        return false;
    }

    return true;
}

bool system_notification_add_entries_batch(
    const system_notification_entry_t* entries,
    size_t count) {
    notification_item_t selected = {0};
    size_t prepared_count = 0u;
    bool has_selected = false;
    bool list_open = false;

    if (entries == NULL || count == 0u) {
        return false;
    }

    for (size_t i = 0u; i < count; ++i) {
        notification_item_t item = {0};
        bool should_queue_silently = false;

        if (!system_notification_prepare_entry(&item, &entries[i])) {
            floatair_warn("notification batch prepare failed index=%u",
                          (unsigned)i);
            continue;
        }
        should_queue_silently = system_notification_should_queue_silently(&item,
                                                                          true);
        floatair_dbg("notification batch enqueue id=%" PRIu32 " mode=%d duration=%" PRIu32,
                     item.entry.id,
                     (int)item.entry.mode,
                     item.entry.duration_ms);
        system_notification_enqueue(&item);
        prepared_count++;

        if (!should_queue_silently &&
            (!has_selected ||
             system_notification_priority(item.entry.mode) >=
                 system_notification_priority(selected.entry.mode))) {
            selected = item;
            has_selected = true;
        }
    }

    if (prepared_count == 0u) {
        return false;
    }

    if (!has_selected) {
        (void)system_notification_refresh_open_list();
        floatair_info("notification batch queued silently count=%u",
                      (unsigned)prepared_count);
        return true;
    }

    system_notification_keep_screen_awake();
    list_open = system_notification_refresh_open_list();
    if (list_open && selected.entry.mode == NOTIFY_MODE_MESSAGE) {
        floatair_info("notification batch queued without popup on notify_list count=%u",
                      (unsigned)prepared_count);
        return true;
    }

    {
        notify_mode_t active_mode = NOTIFY_MODE_MESSAGE;
        bool has_active_notify = notify_get_active_mode(&active_mode);

        if (has_active_notify &&
            system_notification_priority(selected.entry.mode) <
                system_notification_priority(active_mode)) {
            floatair_info("notification batch queued without preemption incoming=%d active=%d",
                          (int)selected.entry.mode,
                          (int)active_mode);
            return true;
        }
    }

    s_active_notification = selected;
    floatair_info("notification batch show active id=%" PRIu32
                  " mode=%d duration=%" PRIu32 " count=%u",
                  selected.entry.id,
                  (int)selected.entry.mode,
                  selected.entry.duration_ms,
                  (unsigned)prepared_count);
    if (!system_notification_show_item(&s_active_notification)) {
        system_notification_clear_active();
        return false;
    }
    return true;
}

bool system_notification_update_entry(const system_notification_entry_t* entry) {
    notification_item_t item = {0};
    bool should_queue_silently = false;
    bool list_open = false;

    if (!system_notification_prepare_entry(&item, entry)) {
        floatair_err("notification prepare entry failed");
        return false;
    }


    floatair_info("notification update id=%" PRIu32 " mode=%d duration=%" PRIu32,
                  item.entry.id,
                  (int)item.entry.mode,
                  item.entry.duration_ms);

    should_queue_silently = system_notification_should_queue_silently(&item, true);
    if (should_queue_silently && system_get_sys_state() == LCD_OFF) {
        for (size_t i = 0; i < s_notification_queue_count; i++) {
            if (s_notification_queue[i].entry.id == item.entry.id) {
                (void)system_notification_remove_at(i);
                break;
            }
        }
        system_notification_enqueue(&item);
        if (s_active_notification.entry.id == item.entry.id) {
            system_notification_clear_active();
        }
        floatair_info("user notification update queued silently: id=%" PRIu32,
                      item.entry.id);
        return true;
    }
    if (!should_queue_silently) {
        system_notification_keep_screen_awake();
    }
    if (notify_get_active_mode(NULL) && s_active_notification.entry.id == item.entry.id) {
        bool queue_updated = false;

        for (size_t i = 0; i < s_notification_queue_count; i++) {
            if (s_notification_queue[i].entry.id == item.entry.id) {
                s_notification_queue[i] = item;
                queue_updated = true;
                break;
            }
        }
        if (!queue_updated) {
            system_notification_enqueue(&item);
        }

        if (should_queue_silently) {
            if (system_get_sys_state() == LCD_ON) {
                notify_dismiss();
            }
            system_notification_clear_active();
            (void)system_notification_refresh_open_list();
            floatair_info("user notification update queued silently: id=%" PRIu32,
                          item.entry.id);
            return true;
        }

        (void)system_notification_refresh_open_list();

        s_active_notification = item;
        floatair_info("notification update active id=%" PRIu32 " mode=%d duration=%" PRIu32,
                      item.entry.id,
                      (int)item.entry.mode,
                      item.entry.duration_ms);

        if (!system_notification_show_item(&s_active_notification)) {
            system_notification_clear_active();
            return false;
        }

        return true;
    }

    for (size_t i = 0; i < s_notification_queue_count; i++) {
        if (s_notification_queue[i].entry.id == item.entry.id) {
            (void)system_notification_remove_at(i);
            break;
        }
    }

    system_notification_enqueue(&item);

    if (should_queue_silently) {
        (void)system_notification_refresh_open_list();
        floatair_info("user notification update queued silently: id=%" PRIu32 " mode=%d",
                      item.entry.id,
                      (int)item.entry.mode);
        return true;
    }

    list_open = system_notification_refresh_open_list();

    if (list_open) {
        if (item.entry.mode == NOTIFY_MODE_MESSAGE) {
            floatair_info("notification queued without popup on notify_list popup: mode=%d",
                          (int)item.entry.mode);
            return true;
        }
    }

    {
        notify_mode_t active_mode = NOTIFY_MODE_MESSAGE;
        bool has_active_notify = notify_get_active_mode(&active_mode);

        if (has_active_notify &&
            system_notification_priority(item.entry.mode) < system_notification_priority(active_mode)) {
            floatair_info("notification queued without preemption: incoming=%d active=%d",
                          (int)item.entry.mode,
                          (int)active_mode);
            return true;
        }
    }

    s_active_notification = item;
    floatair_info("notification show active id=%" PRIu32 " mode=%d duration=%" PRIu32,
                  item.entry.id,
                  (int)item.entry.mode,
                  item.entry.duration_ms);

    if (!system_notification_show_item(&s_active_notification)) {
        system_notification_clear_active();
        return false;
    }

    return true;
}

bool system_notification_remove_id(uint32_t id) {
    floatair_info("notification remove id=%" PRIu32, id);
    for (size_t i = 0; i < s_notification_queue_count;) {
        if (s_notification_queue[i].entry.id == id) {
            (void)system_notification_remove_at(i);
            continue;
        }
        i++;
    }


    if (system_get_sys_state() == LCD_OFF) {
        if (s_active_notification.entry.id == id) {
            system_notification_clear_active();
        }
        return true;
    }

    if (notify_get_active_mode(NULL) && s_active_notification.entry.id == id) {
        notify_dismiss();
        system_notification_clear_active();
    }

    return true;
}

static bool system_notification_add(mpack_node_t node, msg_pack_t* msg) {
    notify_cfg_t default_notify_cfg = notify_default_cfg();
    notification_item_t item = {0};
    uint8_t type = 0;
    char title[MSG_STR_MAX_LEN] = {0};
    char message[MSG_STR_MAX_LEN] = {0};
    const char* title_ptr = NULL;
    const char* message_ptr = NULL;
    uint8_t level = 0;
    uint8_t action = 0;
    mpack_node_t duration_node;
    bool is_user_notification = false;
    bool should_queue_silently = false;
    bool list_open = false;

    floatair_assert(msg != NULL, "msg is NULL");

    app_msg_get_u8(node, true, "type", &type);
    if (!app_msg_get_u32(node, false, "id", &item.entry.id)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    app_msg_get_str(node, "title", title, MSG_STR_MAX_LEN);
    title_ptr = title[0] ? title : NULL;
    app_msg_get_str(node, "msg", message, MSG_STR_MAX_LEN);
    message_ptr = message[0] ? message : NULL;
    system_notification_parse_bitmap(node, &item.entry);

    {
        mpack_node_t level_node = mpack_node_map_cstr_optional(node, "level");
        if (!mpack_node_is_missing(level_node) && !mpack_node_is_nil(level_node)) {
            if (mpack_node_type(level_node) != mpack_type_uint) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            level = mpack_node_u8(level_node);
        }
    }

    {
        mpack_node_t action_node = mpack_node_map_cstr_optional(node, "action");
        if (!mpack_node_is_missing(action_node) && !mpack_node_is_nil(action_node)) {
            if (mpack_node_type(action_node) != mpack_type_uint) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            action = mpack_node_u8(action_node);
        }
    }

    item.entry.mode =
        (type == NOTIFY_MODE_CALL) ? NOTIFY_MODE_CALL : NOTIFY_MODE_MESSAGE;
    item.entry.duration_ms = default_notify_cfg.duration_ms;
    item.entry.level = level;
    item.entry.action = action;
    duration_node = mpack_node_map_cstr_optional(node, "duration");
    if (!mpack_node_is_missing(duration_node)) {
        if (mpack_node_type(duration_node) == mpack_type_uint) {
            uint32_t duration = mpack_node_u32(duration_node);

            if (duration > UINT32_MAX / 1000U) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            item.entry.duration_ms = duration * 1000U;
        } else if (mpack_node_type(duration_node) == mpack_type_int) {
            int32_t duration = mpack_node_i32(duration_node);

            if (duration < 0 || (uint32_t)duration > UINT32_MAX / 1000U) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            item.entry.duration_ms = (uint32_t)duration * 1000U;
        } else {
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }
    system_notification_set_display_text(&item.entry, title_ptr, message_ptr);

    if (!item.entry.has_title &&
        !item.entry.has_image &&
        system_notification_entry_icon_source(&item.entry) == NULL) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    is_user_notification = (type == 0U);
    should_queue_silently =
        system_notification_should_queue_silently(&item, is_user_notification);
    system_notification_enqueue(&item);

    if (should_queue_silently) {
        (void)system_notification_refresh_open_list();
        floatair_info("user notification queued silently: id=%" PRIu32 " type=%u",
                      item.entry.id,
                      (unsigned)type);
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    system_notification_keep_screen_awake();
    list_open = system_notification_refresh_open_list();

    if (list_open) {
        if (item.entry.mode == NOTIFY_MODE_MESSAGE) {
            floatair_info("notification queued without popup on notify_list popup: mode=%d",
                          (int)item.entry.mode);
            return app_mpack_send_ack(msg, Dp_ErrNone);
        }
    }

    {
        notify_mode_t active_mode = NOTIFY_MODE_MESSAGE;
        bool has_active_notify = notify_get_active_mode(&active_mode);

        if (has_active_notify &&
            system_notification_priority(item.entry.mode) < system_notification_priority(active_mode)) {
            floatair_info("notification queued without preemption: incoming=%d active=%d",
                          (int)item.entry.mode,
                          (int)active_mode);
            return app_mpack_send_ack(msg, Dp_ErrNone);
        }
    }

    s_active_notification = item;
    floatair_info("notification show active id=%" PRIu32 " mode=%d duration=%" PRIu32,
                  item.entry.id,
                  (int)item.entry.mode,
                  item.entry.duration_ms);

    if (!system_notification_show_item(&s_active_notification)) {
        system_notification_clear_active();
        return app_mpack_send_ack(msg, ErrBizErr);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_notification_update(mpack_node_t node, msg_pack_t* msg) {
    notify_cfg_t default_notify_cfg = notify_default_cfg();
    notification_item_t item = {0};
    uint8_t type = 0;
    char title[MSG_STR_MAX_LEN] = {0};
    char message[MSG_STR_MAX_LEN] = {0};
    const char* title_ptr = NULL;
    const char* message_ptr = NULL;
    uint8_t level = 0;
    uint8_t action = 0;
    mpack_node_t duration_node;
    bool is_user_notification = false;
    bool should_queue_silently = false;
    bool list_open = false;

    floatair_assert(msg != NULL, "msg is NULL");

    app_msg_get_u8(node, true, "type", &type);
    if (!app_msg_get_u32(node, false, "id", &item.entry.id)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    app_msg_get_str(node, "title", title, MSG_STR_MAX_LEN);
    title_ptr = title[0] ? title : NULL;
    app_msg_get_str(node, "msg", message, MSG_STR_MAX_LEN);
    message_ptr = message[0] ? message : NULL;
    system_notification_parse_bitmap(node, &item.entry);

    {
        mpack_node_t level_node = mpack_node_map_cstr_optional(node, "level");
        if (!mpack_node_is_missing(level_node) && !mpack_node_is_nil(level_node)) {
            if (mpack_node_type(level_node) != mpack_type_uint) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            level = mpack_node_u8(level_node);
        }
    }

    {
        mpack_node_t action_node = mpack_node_map_cstr_optional(node, "action");
        if (!mpack_node_is_missing(action_node) && !mpack_node_is_nil(action_node)) {
            if (mpack_node_type(action_node) != mpack_type_uint) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            action = mpack_node_u8(action_node);
        }
    }

    item.entry.mode =
        (type == NOTIFY_MODE_CALL) ? NOTIFY_MODE_CALL : NOTIFY_MODE_MESSAGE;
    item.entry.duration_ms = default_notify_cfg.duration_ms;
    item.entry.level = level;
    item.entry.action = action;
    duration_node = mpack_node_map_cstr_optional(node, "duration");
    if (!mpack_node_is_missing(duration_node)) {
        if (mpack_node_type(duration_node) == mpack_type_uint) {
            uint32_t duration = mpack_node_u32(duration_node);

            if (duration > UINT32_MAX / 1000U) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            item.entry.duration_ms = duration * 1000U;
        } else if (mpack_node_type(duration_node) == mpack_type_int) {
            int32_t duration = mpack_node_i32(duration_node);

            if (duration < 0 || (uint32_t)duration > UINT32_MAX / 1000U) {
                return app_mpack_send_ack(msg, ErrBadParam);
            }
            item.entry.duration_ms = (uint32_t)duration * 1000U;
        } else {
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }
    system_notification_set_display_text(&item.entry, title_ptr, message_ptr);

    if (!item.entry.has_title &&
        !item.entry.has_image &&
        system_notification_entry_icon_source(&item.entry) == NULL) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    is_user_notification = (type == 0U);
    should_queue_silently =
        system_notification_should_queue_silently(&item, is_user_notification);
    if (should_queue_silently && system_get_sys_state() == LCD_OFF) {
        for (size_t i = 0; i < s_notification_queue_count; i++) {
            if (s_notification_queue[i].entry.id == item.entry.id) {
                (void)system_notification_remove_at(i);
                break;
            }
        }
        system_notification_enqueue(&item);
        if (s_active_notification.entry.id == item.entry.id) {
            system_notification_clear_active();
        }
        floatair_info("user notification update queued silently: id=%" PRIu32 " type=%u",
                      item.entry.id,
                      (unsigned)type);
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }
    if (!should_queue_silently) {
        system_notification_keep_screen_awake();
    }
    if (notify_get_active_mode(NULL) && s_active_notification.entry.id == item.entry.id) {
        bool queue_updated = false;

        for (size_t i = 0; i < s_notification_queue_count; i++) {
            if (s_notification_queue[i].entry.id == item.entry.id) {
                s_notification_queue[i] = item;
                queue_updated = true;
                break;
            }
        }
        if (!queue_updated) {
            system_notification_enqueue(&item);
        }

        if (should_queue_silently) {
            if (system_get_sys_state() == LCD_ON) {
                notify_dismiss();
            }
            system_notification_clear_active();
            (void)system_notification_refresh_open_list();
            floatair_info("user notification update queued silently: id=%" PRIu32 " type=%u",
                          item.entry.id,
                          (unsigned)type);
            return app_mpack_send_ack(msg, Dp_ErrNone);
        }

        if (notify_list_is_open()) {
            notify_list_view_reload();
        }

        s_active_notification = item;
        floatair_info("notification update active id=%" PRIu32 " mode=%d duration=%" PRIu32,
                      item.entry.id,
                      (int)item.entry.mode,
                      item.entry.duration_ms);

        if (!system_notification_show_item(&s_active_notification)) {
            system_notification_clear_active();
            return app_mpack_send_ack(msg, ErrBizErr);
        }

        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    for (size_t i = 0; i < s_notification_queue_count; i++) {
        if (s_notification_queue[i].entry.id == item.entry.id) {
            (void)system_notification_remove_at(i);
            break;
        }
    }

    system_notification_enqueue(&item);

    if (should_queue_silently) {
        (void)system_notification_refresh_open_list();
        floatair_info("user notification update queued silently: id=%" PRIu32 " type=%u",
                      item.entry.id,
                      (unsigned)type);
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    list_open = system_notification_refresh_open_list();

    if (list_open) {
        if (item.entry.mode == NOTIFY_MODE_MESSAGE) {
            floatair_info("notification queued without popup on notify_list popup: mode=%d",
                          (int)item.entry.mode);
            return app_mpack_send_ack(msg, Dp_ErrNone);
        }
    }

    {
        notify_mode_t active_mode = NOTIFY_MODE_MESSAGE;
        bool has_active_notify = notify_get_active_mode(&active_mode);

        if (has_active_notify &&
            system_notification_priority(item.entry.mode) < system_notification_priority(active_mode)) {
            floatair_info("notification queued without preemption: incoming=%d active=%d",
                          (int)item.entry.mode,
                          (int)active_mode);
            return app_mpack_send_ack(msg, Dp_ErrNone);
        }
    }

    s_active_notification = item;
    floatair_info("notification show active id=%" PRIu32 " mode=%d duration=%" PRIu32,
                  item.entry.id,
                  (int)item.entry.mode,
                  item.entry.duration_ms);

    if (!system_notification_show_item(&s_active_notification)) {
        system_notification_clear_active();
        return app_mpack_send_ack(msg, ErrBizErr);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}


static bool system_notification_remove(mpack_node_t node, msg_pack_t* msg) {
    uint32_t id = 0;

    floatair_assert(msg != NULL, "msg is NULL");

    if (!app_msg_get_u32(node, false, "id", &id)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    if (!system_notification_remove_id(id)) {
        return app_mpack_send_ack(msg, ErrBizErr);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

app_cmd_func_t system_notification_cmd_funcs[] = {
    {"addNotification", system_notification_add},
    {"updateNotification", system_notification_update},
    {"removeNotification", system_notification_remove},
};
const size_t system_notification_cmd_funcs_count =
    sizeof(system_notification_cmd_funcs) / sizeof(system_notification_cmd_funcs[0]);
