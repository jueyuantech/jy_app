#include <time.h>
#include <stdio.h>
#include "message.h"

#include "elf_common.h"
#include "floatair_dbg.h"
#include "app_def.h"

#include <inttypes.h>
#include <lvgl/lvgl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_router.h"
#include "product_app.h"
#include "common/widgets/toast.h"
#include "common/widgets/status_bar.h"
#include "system/system.h"
#include "system/system_avrcp.h"
#include "system/system_res.h"
#include "system/system_notification.h"
#include "system/system_runtime_state.h"
#include "system/system_runtime_ui.h"
#include "system/system_timer.h"
#include "sys_adapter.h"
#include "app_lcd.h"
#include "ui_res.h"

/* ------------------
 * Version constraints check
 * ------------------ */
#define EXPECTED_LVGL_VERSION_MAJOR 9
#define EXPECTED_LVGL_VERSION_MINOR 2

#if LVGL_VERSION_MAJOR != EXPECTED_LVGL_VERSION_MAJOR ||                                           \
    LVGL_VERSION_MINOR != EXPECTED_LVGL_VERSION_MINOR
#error "LVGL version mismatch! Expected 9.2.x, please check your lvgl source."
#endif

typedef struct list_node {
    struct list_node* prev;
    struct list_node* next;
} list_node;

#define LIST_INITIAL_CLEARED_VALUE { NULL, NULL }
#define APP_TOAST_ID_LOW_BATTERY 1U ///< 低电量报警 Toast 业务标识。

/**
 * @brief 灭屏期间低电量 Toast 等待亮屏执行的最终动作。
 */
typedef enum {
    APP_LOW_BATTERY_TOAST_PENDING_NONE = 0, ///< 无待处理动作。
    APP_LOW_BATTERY_TOAST_PENDING_SHOW,     ///< 亮屏后显示低电量 Toast。
    APP_LOW_BATTERY_TOAST_PENDING_DISMISS,  ///< 亮屏后关闭低电量 Toast。
} app_low_battery_toast_pending_action_t;

static app_low_battery_toast_pending_action_t s_low_battery_toast_pending =
    APP_LOW_BATTERY_TOAST_PENDING_NONE; ///< 灭屏期间最后一次低电量 Toast 动作。
static char s_pending_emerg_toast[MSG_STR_MAX_LEN] = {0}; ///< 灭屏期间等待亮屏显示的紧急消息。

static inline int list_is_clear(const list_node* list) {
    return list->next == NULL && list->prev == NULL;
}
static inline void list_initialize(list_node* list) {
    list->next = list;
    list->prev = list;
}
static inline void list_clear_node(list_node* list) {
    list->next = NULL;
    list->prev = NULL;
}
static inline void list_add_tail(list_node* list, list_node* item) {
    item->prev = list->prev;
    item->next = (list_node*)list;
    list->prev->next = item;
    list->prev       = item;
}
static inline void list_delete(list_node* item) {
    item->prev->next = item->next;
    item->next->prev = item->prev;
    item->next = item->prev = NULL;
}
#ifndef container_of
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_for_every(list, node) for ((node) = (list)->next; (node) != (list); (node) = (node)->next)
#define list_for_every_safe(list, node, tmp) \
    for ((node) = (list)->next, (tmp) = (node)->next; (node) != (list); (node) = (tmp), (tmp) = (node)->next)

static list_node list = LIST_INITIAL_CLEARED_VALUE;

/**
 * @brief 判断字符串是否命中给定名单。
 * @param[in] value 待匹配字符串。
 * @param[in] list_items 字符串名单。
 * @param[in] count 名单长度。
 * @return `true` 表示命中，`false` 表示未命中。
 */
static bool app_msg_string_in_list(const char* value, const char* const* list_items, size_t count) {
    if (value == NULL || list_items == NULL) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (strcmp(value, list_items[i]) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 判断新手引导是否尚未完成。
 * @return `true` 表示欢迎页或教学步骤尚未完成，`false` 表示已完成。
 */
static bool app_msg_guide_is_active(void) {
    return !system_config_is_userguide_finished();
}

/**
 * @brief 判断 SystemControl 命令是否会影响页面或当前业务展示。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示应受新手引导步骤限制，`false` 表示可按普通系统消息处理。
 */
static bool app_msg_guide_system_control_is_page_related(const msg_pack_t* msg) {
    static const char* const page_cmds[] = {
        "setView",
        "openAssistant",
        "updateAssistantSttInfo",
        "closeAssistant",
        "setProgressVisible",
        "setUploadProgressVisible",
    };

    if (msg == NULL || msg->id != APP_MSG_ID_SYSTEM) {
        return false;
    }
    if (strcmp(msg->biz, "SystemControl") != 0) {
        return false;
    }

    return app_msg_string_in_list(msg->cmd, page_cmds, sizeof(page_cmds) / sizeof(page_cmds[0]));
}

/**
 * @brief 判断 host 消息是否为第 5 步语音唤醒链路允许的 assistant 命令。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示允许继续处理，`false` 表示不是第 5 步允许消息。
 */
static bool app_msg_guide_is_step5_assistant_message(const msg_pack_t* msg) {
    static const char* const assistant_cmds[] = {
        "openAssistant",
        "updateAssistantSttInfo",
        "closeAssistant",
    };
    const char* progress = system_config_get_userguide();

    if (progress == NULL || strcmp(progress, SYSTEM_USERGUIDE_PROGRESS_STEP5) != 0) {
        return false;
    }
    if (msg == NULL || msg->id != APP_MSG_ID_SYSTEM) {
        return false;
    }
    if (strcmp(msg->biz, "SystemControl") != 0) {
        return false;
    }

    return app_msg_string_in_list(msg->cmd, assistant_cmds, sizeof(assistant_cmds) / sizeof(assistant_cmds[0]));
}

/**
 * @brief 判断 host 消息是否为新手引导开关控制命令。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示为新手引导开关命令，`false` 表示不是。
 */
static bool app_msg_guide_is_guide_control_message(const msg_pack_t* msg) {
    static const char* const guide_cmds[] = {
        "openGuide",
        "closeGuide",
    };

    if (msg == NULL || msg->id != APP_MSG_ID_SYSTEM) {
        return false;
    }
    if (strcmp(msg->biz, "SystemControl") != 0) {
        return false;
    }

    return app_msg_string_in_list(msg->cmd, guide_cmds, sizeof(guide_cmds) / sizeof(guide_cmds[0]));
}

/**
 * @brief 判断当前页面是否为非 Guide 的动态首页。
 * @return `true` 表示当前页面就是动态首页且不属于 Guide，`false` 表示仍需按 Guide 规则限制。
 */
static bool app_msg_guide_is_non_guide_home_view(void) {
    const char* current_app = app_router_get_app();
    const char* home_viewname = app_router_get_home_viewname();

    if (current_app == NULL || home_viewname == NULL) {
        return false;
    }
    if (product_app_name_has_capability(home_viewname, PRODUCT_APP_CAP_GUIDE)) {
        return false;
    }

    return strcmp(current_app, home_viewname) == 0;
}

/**
 * @brief 判断新手引导期间是否允许处理指定 host 消息。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示允许继续处理，`false` 表示应返回新手引导步骤不匹配错误。
 */
static bool app_msg_guide_host_message_allowed(const msg_pack_t* msg) {
    if (!app_msg_guide_is_active()) {
        return true;
    }
    if (app_msg_guide_is_non_guide_home_view()) {
        return true;
    }
    if (msg == NULL) {
        return false;
    }
    if (msg->type == MSG_TYPE_ACK || msg->type == MSG_TYPE_NAK) {
        return true;
    }
    if (app_msg_guide_is_step5_assistant_message(msg)) {
        return true;
    }
    if (app_msg_guide_is_guide_control_message(msg)) {
        return true;
    }
    if (msg->id == APP_MSG_ID_SYSTEM) {
        if (strcmp(msg->biz, "Notification") == 0) {
            return false;
        }
        if (app_msg_guide_system_control_is_page_related(msg)) {
            return false;
        }
        return true;
    }

    return false;
}

/**
 * @brief Registry entry wrapping app_message_t with list node
 */
typedef struct {
    list_node node; ///< list node
    app_message_t item;    ///< registered message item
} app_message_node_t;

static int app_msg_update_entry(app_message_node_t* entry, app_message_t* msg) {
    if (msg->name) {
        if (entry->item.name) {
            free(entry->item.name);
        }
        entry->item.name = strdup(msg->name);
        if (!entry->item.name) {
            return -1;
        }
    }
    entry->item.cb = msg->cb;
    floatair_info(
        "register id : [%" PRIu32 "][%s] success", msg->id, msg->name ? msg->name : "name null");
    return 0;
}
void app_msg_init(void) {
    if (list_is_clear(&list)) {
        list_initialize(&list);
    }
    floatair_info("do init");
}

void app_msg_deinit(void) {
    if (list_is_clear(&list)) {
        return;
    }
    list_node* node_iter;
    list_node* tmp_iter;
    list_for_every_safe(&list, node_iter, tmp_iter) {
        app_message_node_t* entry = list_entry(node_iter, app_message_node_t, node);
        if (entry->item.name) {
            free(entry->item.name);
            entry->item.name = NULL;
        }
        list_delete(&entry->node);
        free(entry);
    }
    list_clear_node(&list);
    floatair_info("list is clear");
}

int app_msg_register(app_message_t* msg) {
    if (!msg) {
        floatair_err("msg is NULL");
        return -1;
    }
    if (list_is_clear(&list)) {
        list_initialize(&list);
    }
    floatair_info("register id : [%" PRIu32 "][%s]", msg->id, msg->name ? msg->name : "name null");
    list_node* node_iter;
    list_for_every(&list, node_iter) {
        app_message_node_t* entry = list_entry(node_iter, app_message_node_t, node);
        if (entry->item.id == msg->id) {
            return app_msg_update_entry(entry, msg);
        }
    }
    app_message_node_t* node = (app_message_node_t*) malloc(sizeof(app_message_node_t));
    floatair_assert(node, "node err");
    memset(node, 0, sizeof(app_message_node_t));
    node->item.id   = msg->id;
    node->item.cb   = msg->cb;
    node->item.name = msg->name ? strdup(msg->name) : NULL;
    if (msg->name && !node->item.name) {
        free(node);
        node = NULL;
        floatair_err("malloc app_message_node_t failed");
        return -1;
    }
    list_add_tail(&list, &node->node);
    floatair_info(
        "register id : [%" PRIu32 "][%s] success", msg->id, msg->name ? msg->name : "name null");
    return 0;
}

int app_msg_delete(uint32_t msg_id) {
    if (list_is_clear(&list)) {
        floatair_err("list is clear");
        return -1;
    }
    floatair_info("delete id : [%" PRIu32 "]", msg_id);
    list_node* node_iter;
    list_for_every(&list, node_iter) {
        app_message_node_t* entry = list_entry(node_iter, app_message_node_t, node);
        if (entry->item.id == msg_id) {
            floatair_info("delete id : [%" PRIu32 "][%s] success",
                          msg_id,
                          entry->item.name ? entry->item.name : "name null");
            if (entry->item.name) {
                free(entry->item.name);
                entry->item.name = NULL;
            }
            list_delete(&entry->node);
            free(entry);
            return 0;
        }
    }
    floatair_err("id : [%" PRIu32 "] not found", msg_id);
    return -1;
}

int app_msg_update(app_message_t* msg) {
    if (!msg) {
        floatair_err("msg is NULL");
        return -1;
    }
    if (list_is_clear(&list)) {
        floatair_err("list is clear");
        return -1;
    }
    floatair_info("update id : [%" PRIu32 "][%s]", msg->id, msg->name ? msg->name : "name null");
    list_node* node_iter;
    list_for_every(&list, node_iter) {
        app_message_node_t* entry = list_entry(node_iter, app_message_node_t, node);
        if (entry->item.id == msg->id) {
            if (msg->name) {
                if (entry->item.name) {
                    free(entry->item.name);
                }
                entry->item.name = strdup(msg->name);
                if (!entry->item.name) {
                    return -1;
                }
            }
            entry->item.cb = msg->cb;
            floatair_info("update id : [%" PRIu32 "][%s] success",
                          msg->id,
                          msg->name ? msg->name : "name null");
            return 0;
        }
    }
    floatair_err("id : [%" PRIu32 "] not found", msg->id);
    return -1;
}

app_message_t* app_msg_query(uint32_t msg_id) {
    if (list_is_clear(&list)) {
        floatair_err("list is clear");
        return NULL;
    }
    floatair_info("query id : [%" PRIu32 "]", msg_id);
    list_node* node_iter;
    list_for_every(&list, node_iter) {
        app_message_node_t* entry = list_entry(node_iter, app_message_node_t, node);
        floatair_info("id : [%" PRIu32 "][%s]",
                      entry->item.id,
                      entry->item.name ? entry->item.name : "name null");
        if (entry->item.id == msg_id) {
            return &entry->item;
        }
    }
    return NULL;
}

bool app_mpack_msg_handle(char* msg, size_t msg_size) {
    uint32_t start_us = (uint32_t)GetTimeUs();
    mpack_tree_t tree;
    mpack_error_t err_stat;
    mpack_node_t node_root;
    mpack_node_t payload_node;
    mpack_node_t data_node;
    bool ret = false;
    bool tree_inited = false;
    msg_pack_t mpackmsg = {
        .id       = UINT32_MAX,
        .sequence = UINT32_MAX,
        .type     = MSG_TYPE_INVALID,
    };
    msg_pack_t header = {0};
    bool header_valid = false;

    if (!msg || msg_size == 0) {
        floatair_err("msg is NULL");
        goto out;
    }
    if (list_is_clear(&list)) {
        floatair_err("list is clear");
        goto out;
    }

    mpack_tree_init(&tree, msg, msg_size);
    tree_inited = true;
    mpack_tree_parse(&tree);
    err_stat = mpack_tree_error(&tree);
    if (err_stat != mpack_ok) {
        floatair_err("----, tree ERR : %d ", err_stat);
        goto out;
    }

    node_root = mpack_tree_root(&tree);
    if (mpack_node_is_missing(node_root) || mpack_node_is_nil(node_root)) {
        floatair_err("----, root node is missing or nil ");
        goto out;
    }

    if (!app_msg_get_u32(node_root, false, "id", &(mpackmsg.id))) {
        floatair_err("----, id node err ");
        goto out;
    }

    payload_node = mpack_node_map_cstr(node_root, "payload");
    if (mpack_node_is_missing(payload_node) || mpack_node_is_nil(payload_node) ||
        mpack_node_type(payload_node) != mpack_type_map) {
        floatair_err("----, payload err ");
        goto out;
    }

    if (!app_msg_parse_header(payload_node, &mpackmsg)) {
        floatair_err("----, header node err ");
        goto out;
    }
    header = mpackmsg;
    header_valid = true;
    if (floatair_lcd_get_state() == LCD_OFF &&
        !system_host_message_allowed_when_lcd_off(&mpackmsg)) {
        floatair_warn("block host mpack while lcd off, id=%" PRIu32 " biz=%s cmd=%s",
                      mpackmsg.id,
                      mpackmsg.biz,
                      mpackmsg.cmd);
        ret = app_mpack_send_ack(&mpackmsg, ErrScreenOff);
        goto out;
    }
    {
        app_t* current_app = app_manager_current();
        if (current_app != NULL &&
            current_app->use_top_layer &&
            current_app->on_host_message != NULL &&
            current_app->on_host_message(&mpackmsg)) {
            ret = true;
            goto out;
        }
    }
    if (!system_get_btconn_state()) {
        floatair_warn("block host mpack while bt disconnect overlay active, id=%" PRIu32 " biz=%s cmd=%s",
                      mpackmsg.id,
                      mpackmsg.biz,
                      mpackmsg.cmd);
        ret = app_mpack_send_ack(&mpackmsg, ErrNotReady);
        goto out;
    }
    if (!system_host_message_allowed_when_popup_active(&mpackmsg)) {
        floatair_warn("block host mpack while popup active, id=%" PRIu32 " biz=%s cmd=%s",
                      mpackmsg.id,
                      mpackmsg.biz,
                      mpackmsg.cmd);
        ret = app_mpack_send_ack(&mpackmsg, ErrNotReady);
        goto out;
    }
    if (!app_msg_guide_host_message_allowed(&mpackmsg)) {
        floatair_warn("block host mpack while guide active, progress=%s id=%" PRIu32 " biz=%s cmd=%s",
                      system_config_get_userguide(),
                      mpackmsg.id,
                      mpackmsg.biz,
                      mpackmsg.cmd);
        ret = app_mpack_send_ack(&mpackmsg, ErrGuideStepMismatch);
        goto out;
    }

    data_node = mpack_node_map_cstr_optional(payload_node, "data");
    if (mpack_node_is_missing(data_node) || mpack_node_is_nil(data_node)) {
        floatair_dbg("----, data is missing or nil, let it go");
        data_node = mpack_tree_missing_node(&tree);
    }

    app_message_t* msg_item = app_msg_query(mpackmsg.id);
    if (!msg_item) {
        floatair_err("----, id [%" PRIu32 "] not registered ", mpackmsg.id);
        ret = app_mpack_send_ack(&mpackmsg, ErrIDErr);
        goto out;
    }
    bool cb_ret = false;
    if (msg_item->cb) {
        cb_ret = msg_item->cb(data_node, &mpackmsg);
    } else {
        floatair_err("----, id [%" PRIu32 "] cb is NULL ", mpackmsg.id);
    }
    ret = cb_ret;
    if (ret) {
        if (system_notification_should_suppress_activity(data_node, &mpackmsg)) {
            floatair_dbg("mpack activity suppressed, skip sleep_timer_reset");
        } else {
            app_sleep_timer_reset();
        }
    }

out:
    if (tree_inited) {
        mpack_tree_destroy(&tree);
    }
    {
        uint32_t cost_us = (uint32_t)GetTimeUs() - start_us;
        if (header_valid) {
            floatair_dbg("mpack cost %lu us/%lu ms ret=%d id=%" PRIu32 " seq=%" PRIu32 " type=%u biz=%s cmd=%s",
                         (unsigned long)cost_us,
                         (unsigned long)(cost_us / 1000U),
                         ret,
                         header.id,
                         header.sequence,
                         header.type,
                         header.biz,
                         header.cmd);
        } else {
            floatair_dbg("mpack cost %lu us/%lu ms ret=%d",
                         (unsigned long)cost_us,
                         (unsigned long)(cost_us / 1000U),
                         ret);
        }
    }
    return ret;
}

static const char* system_event_type_to_str(uint16_t event_type) {
    switch (event_type) {
#define EVT_CASE(x) \
    case x:         \
        return #x;
        EVT_CASE(SET_IMU_SINGLE_TAP)
        EVT_CASE(SET_IMU_DOUBLE_TAP)
        EVT_CASE(SET_IMU_TILT)
        EVT_CASE(SET_IMU_WOM)
        EVT_CASE(SET_IMU_R2W)
        EVT_CASE(SET_IMU_SMD)
        EVT_CASE(SET_IMU_DATA_UPDATE)
        EVT_CASE(SET_BAT_SOC_CHANGED)
        EVT_CASE(SET_BAT_VOLT_CHANGED)
        EVT_CASE(SET_CHARGER_STATE_CHANGED)
        EVT_CASE(SET_SLIDE_FORWARD)
        EVT_CASE(SET_SLIDE_BACKWORD)
        EVT_CASE(SET_FORCE_SINGLE_CLICK)
        EVT_CASE(SET_FORCE_DOUBLE_CLICK)
        EVT_CASE(SET_FORCE_TRI_CLICK)
        EVT_CASE(SET_FORCE_LONG_PRESSED)
        EVT_CASE(SET_IED_WEAR_ON)
        EVT_CASE(SET_IED_REMOVED)
        EVT_CASE(SET_KWS_HIT)
        EVT_CASE(SET_REPORT_DEVICE_STATE)
        EVT_CASE(SET_ALS_RAW_DATA)
        EVT_CASE(SET_JYT_SIBLING_SYNC)
        EVT_CASE(SET_BT_CALL_SETUP_EVENT)
        EVT_CASE(SET_BT_AVRCP_POSITION_CHANGED)
        EVT_CASE(SET_TWS_LINK_BROKEN)
        EVT_CASE(SET_JYP_HOST_DISCONNECTED)
        EVT_CASE(SET_JYP_HOST_CONNECTED)
        EVT_CASE(SET_JYT_LOW_BATTERY_WARNING)
        EVT_CASE(SET_JYT_TIMER_TRIGGER)
        EVT_CASE(SET_JYT_BT_VISIBLE_CHANGED)
        EVT_CASE(SET_ANCS_EVENT)
        EVT_CASE(SET_JYT_REFRESH_UI_REQ)
        EVT_CASE(SET_JYT_ACC_TYPE_CHANGED)
        default: return "UNKNOWN_SYSTEM_EVENT";
    }
#undef EVT_CASE
}

/**
 * @brief 处理蓝牙可见性变化，退出可搜索时立即重新进入可搜索。
 * @param msg 系统事件消息，payload 首字节为 `SMMAN_BT_VISIBILITY`。
 * @return `true` 表示处理成功，`false` 表示消息格式错误或请求失败。
 */
static bool system_handle_bt_visible_changed_event(const JYT_ELF_MQ_MSG* msg) {
    uint8_t bt_visibility = 0;
    uint8_t target_visibility = JYT_BT_VIS_GENERAL_ACCESS;

    if (msg->payload_len < sizeof(bt_visibility)) {
        floatair_err("invalid bt visibility payload_len: %d", msg->payload_len);
        return false;
    }

    bt_visibility = msg->payload[0];
    floatair_info("bt visibility changed: %u", bt_visibility);
    if (bt_visibility != JYT_BT_VIS_CONNECTABLE_ONLY) {
        return true;
    }

    floatair_info("bt visibility exited discoverable, request general access again");
    return system_request_bt_visibility(target_visibility);
}

#define SYSTEM_ATTACHMENT_EVENT_PAYLOAD_LEN 2u ///< 附件事件负载长度：附件类型和主从侧身份。

/**
 * @brief 主从两侧附件状态及业务计数。
 */
typedef struct {
    uint8_t type_by_side[SYSTEM_ATTACHMENT_SIDE_COUNT]; ///< 各侧最近一次上报的附件类型。
    uint8_t headset_count;                              ///< 当前检测到扬声器附件的侧数。
    uint8_t glasses_case_count;                         ///< 当前检测到眼镜盒附件的侧数。
} system_attachment_state_t;

static system_attachment_state_t s_attachment_state = {
    .type_by_side = {JYT_ACC_NONE, JYT_ACC_NONE},
};

/**
 * @brief 获取主从两侧最近一次收到的附件类型快照。
 * @param[out] snapshot 返回仅保存在内存中的附件状态。
 * @return 无返回值。
 */
void system_get_attachment_state(system_attachment_snapshot_t* snapshot) {
    floatair_assert(snapshot != NULL, "snapshot is NULL");
    memcpy(snapshot->type_by_side,
           s_attachment_state.type_by_side,
           sizeof(snapshot->type_by_side));
}

/**
 * @brief 处理主从两侧附件变化，并按附件数量更新本机业务状态。
 * @param[in] msg 系统事件消息，payload[0] 为附件类型，payload[1] 为主从侧身份。
 * @return `true` 表示本机处理和手机上报均成功，`false` 表示消息无效或上报失败。
 */
static bool system_handle_attachment_changed_event(const JYT_ELF_MQ_MSG* msg) {
    uint8_t attachment_type = 0;
    system_attachment_side_t attachment_side = SYSTEM_ATTACHMENT_SIDE_MASTER;
    uint8_t previous_type = 0;
    uint8_t previous_glasses_case_count = 0;
    bool ret = true;

    if (msg == NULL || msg->payload_len != SYSTEM_ATTACHMENT_EVENT_PAYLOAD_LEN) {
        floatair_err("invalid attachment payload_len: %u",
                     msg == NULL ? 0u : (unsigned)msg->payload_len);
        return false;
    }

    attachment_type = msg->payload[0];
    attachment_side = (system_attachment_side_t)msg->payload[1];
    if (attachment_type > JYT_ACC_SPEAKER) {
        floatair_err("invalid attachment type: %u", (unsigned)attachment_type);
        return false;
    }
    if ((unsigned)attachment_side >= SYSTEM_ATTACHMENT_SIDE_COUNT) {
        floatair_err("invalid attachment side: %u", (unsigned)attachment_side);
        return false;
    }

    previous_type = s_attachment_state.type_by_side[attachment_side];
    previous_glasses_case_count = s_attachment_state.glasses_case_count;
    if (previous_type != attachment_type) {
        if (previous_type == JYT_ACC_SPEAKER && s_attachment_state.headset_count > 0u) {
            s_attachment_state.headset_count--;
        } else if (previous_type == JYT_ACC_GLASSES_CASE &&
                   s_attachment_state.glasses_case_count > 0u) {
            s_attachment_state.glasses_case_count--;
        }

        if (attachment_type == JYT_ACC_SPEAKER) {
            s_attachment_state.headset_count++;
        } else if (attachment_type == JYT_ACC_GLASSES_CASE) {
            s_attachment_state.glasses_case_count++;
        }
        s_attachment_state.type_by_side[attachment_side] = attachment_type;
    }

    floatair_info("attachment changed: side=%u type=%u headset_count=%u case_count=%u",
                  (unsigned)attachment_side,
                  (unsigned)attachment_type,
                  (unsigned)s_attachment_state.headset_count,
                  (unsigned)s_attachment_state.glasses_case_count);

    system_ui_update_headset_state(
        s_attachment_state.type_by_side[SYSTEM_ATTACHMENT_SIDE_SLAVE] == JYT_ACC_SPEAKER,
        s_attachment_state.type_by_side[SYSTEM_ATTACHMENT_SIDE_MASTER] == JYT_ACC_SPEAKER);
    ret = system_report_attachment_type(attachment_type, attachment_side);
    if (previous_glasses_case_count == 0u && s_attachment_state.glasses_case_count > 0u &&
        !floatair_lcd_is_off()) {
        system_set_sys_state(LCD_OFF);
        if (!system_report_sys_state(LCD_OFF, SYSTEM_SYS_STATE_TRIGGER_GLASSES_CASE)) {
            ret = false;
        }
    }
    return ret;
}

typedef enum {
    ANCS_EVT_ADDED = 0,
    ANCS_EVT_MODIFIED = 1,
    ANCS_EVT_REMOVED = 2,
} ancs_event_id_t;

typedef enum {
    ANCS_ATTR_APP_IDENTIFIER = 0,
    ANCS_ATTR_TITLE = 1,
    ANCS_ATTR_SUBTITLE = 2,
    ANCS_ATTR_MESSAGE = 3,
    ANCS_ATTR_MESSAGE_SIZE = 4,
    ANCS_ATTR_DATE = 5,
} ancs_attr_id_t;

typedef enum {
    ANCS_CATEGORY_OTHER = 0,
    ANCS_CATEGORY_INCOMING_CALL = 1,
    ANCS_CATEGORY_MISSED_CALL = 2,
    ANCS_CATEGORY_SOCIAL = 4,
} ancs_category_id_t;

#define ANCS_CMD_GET_NTF_ATTR 0u ///< Get Notification Attributes 命令 ID。
#define ANCS_APP_ID_MAX 96u ///< iOS AppIdentifier 本地缓存长度。
#define ANCS_SKELETON_EVENT_ID_OFF 0u ///< skeleton 中 EventID 的偏移。
#define ANCS_SKELETON_EVENT_FLAGS_OFF 1u ///< skeleton 中 EventFlags 的偏移。
#define ANCS_SKELETON_CATEGORY_ID_OFF 2u ///< skeleton 中 CategoryID 的偏移。
#define ANCS_SKELETON_UID_OFF 4u ///< skeleton 中 NotificationUID 的偏移。
#define ANCS_EVENT_FLAG_PRE_EXISTING 0x04u ///< iOS 连接后重放的历史通知标志。
#define ANCS_DETAIL_CMD_ID_OFF 0u ///< detail 中 CommandID 的偏移。
#define ANCS_DETAIL_UID_OFF 1u ///< detail 中 NotificationUID 的偏移。
#define ANCS_DETAIL_ATTRS_OFF 5u ///< detail 中属性链的起始偏移。
#define ANCS_DETAIL_ATTR_HEADER_LEN 3u ///< detail 属性头 `[attrId][attrLen u16 LE]` 的长度。
#define ANCS_DETAIL_ATTR_LEN_OFF 1u ///< detail 属性头中 attrLen 的偏移。
#ifndef ANCS_BATCH_TYPE_UNSUPPORTED
#define ANCS_BATCH_TYPE_UNSUPPORTED 2u ///< 当前 ACL 连接已熔断 ANCS 的批记录类型。
#endif
#ifndef ANCS_UNSUPPORTED_DATA_LEN
#define ANCS_UNSUPPORTED_DATA_LEN 0u ///< ANCS 熔断记录不携带 data。
#endif
#define ANCS_UNSUPPORTED_NOTIFICATION_ID 0x414E4353u ///< “ANCS”系统提示通知 ID。
#define ANCS_UNSUPPORTED_MESSAGE_KEY "NOTIFY_ANCS_UNSUPPORTED" ///< ANCS 熔断后的国际化用户指引键。

/**
 * @brief Get Notification Attributes 详情解析结果。
 */
typedef struct {
    char app_id[ANCS_APP_ID_MAX];     ///< detail 属性链中的 AppIdentifier。
    char title[MSG_STR_MAX_LEN];      ///< 通知标题。
    char subtitle[MSG_STR_MAX_LEN];   ///< 通知副标题。
    char message[MSG_STR_MAX_LEN];    ///< 通知正文。
    time_t notify_time;               ///< 解析成功的通知时间。
    bool has_time;                    ///< notify_time 是否来自有效 DATE 属性。
} ancs_notification_detail_t;

static system_notification_entry_t
    s_ancs_batch_entries[SYSTEM_NOTIFICATION_QUEUE_MAX] = {0}; ///< 当前批次去重后的待入队通知。
static uint32_t s_ancs_batch_warning_count = 0u; ///< 批量协议异常日志限频计数。

void app_message_reset_ancs_state(void) {
    s_ancs_batch_warning_count = 0u;
}

static uint16_t ancs_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ancs_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t ancs_internal_notification_id(uint32_t uid) {
    uint32_t destid = uid ^ 0xA5A5A5A5u;
    floatair_info("notification id=%" PRIu32 " -> destid=%" PRIu32, uid, destid);
    return destid;
}

static bool ancs_parse_date_time(const uint8_t* s, size_t len, time_t* out_time) {
    if (!s || !out_time || len < 15) {
        return false;
    }

    char buf[16] = {0};
    memcpy(buf, s, 15);
    if (buf[8] != 'T') {
        return false;
    }

    int year = 0;
    int mon = 0;
    int mday = 0;
    int hour = 0;
    int min = 0;
    int sec = 0;

    if (sscanf(buf, "%4d%2d%2dT%2d%2d%2d", &year, &mon, &mday, &hour, &min, &sec) != 6) {
        return false;
    }

    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    t.tm_isdst = -1;

    time_t ts = mktime(&t);
    if (ts == (time_t)-1) {
        return false;
    }

    *out_time = ts;
    return true;
}

static void ancs_copy_str(char* out, size_t out_cap, const uint8_t* s, size_t len) {
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!s || len == 0) {
        return;
    }
    size_t copy_len = len < (out_cap - 1) ? len : (out_cap - 1);
    memcpy(out, s, copy_len);
    out[copy_len] = '\0';
}

/**
 * @brief 从第三方来电标题中提取联系人名称。
 * @param[in,out] title ANCS 标题缓存。
 * @return 无返回值。
 */
static void ancs_trim_call_title(char* title) {
    char* invite = NULL;

    if (title == NULL || title[0] == '\0') {
        return;
    }
    invite = strstr(title, "邀请");
    if (invite != NULL) {
        *invite = '\0';
    }
}

/**
 * @brief 判断本次批量协议异常是否需要输出日志。
 * @return 首次异常及之后每 16 次异常返回 `true`。
 */
static bool ancs_batch_warning_due(void) {
    s_ancs_batch_warning_count++;
    return s_ancs_batch_warning_count == 1u ||
           (s_ancs_batch_warning_count % 16u) == 0u;
}

/**
 * @brief 解析 FULL 记录携带的 Get Notification Attributes 原始详情。
 * @param[in] detail Data Source 原始回包，可为空。
 * @param[in] detail_len detail 字节数。
 * @param[in] expected_uid skeleton 中的通知 UID。
 * @param[out] parsed 详情解析结果。
 * @return 格式合法返回 `true`。
 */
static bool ancs_parse_detail(const uint8_t* detail,
                              size_t detail_len,
                              uint32_t expected_uid,
                              ancs_notification_detail_t* parsed) {
    size_t offset = ANCS_DETAIL_ATTRS_OFF;

    if (parsed == NULL) {
        return false;
    }
    memset(parsed, 0, sizeof(*parsed));
    if (detail_len == 0u) {
        return true;
    }
    if (detail == NULL || detail_len < ANCS_DETAIL_ATTRS_OFF) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS detail too short len=%u", (unsigned)detail_len);
        }
        return false;
    }
    if (detail[ANCS_DETAIL_CMD_ID_OFF] != ANCS_CMD_GET_NTF_ATTR) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS detail unknown cmd=%u",
                          (unsigned)detail[ANCS_DETAIL_CMD_ID_OFF]);
        }
        return false;
    }
    if (ancs_u32_le(&detail[ANCS_DETAIL_UID_OFF]) != expected_uid) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS detail uid mismatch expected=%" PRIu32
                          " actual=%" PRIu32,
                          expected_uid,
                          ancs_u32_le(&detail[ANCS_DETAIL_UID_OFF]));
        }
        return false;
    }

    while (offset < detail_len) {
        uint8_t attr_id = 0u;
        uint16_t attr_len = 0u;
        const uint8_t* attr_data = NULL;

        if (offset + ANCS_DETAIL_ATTR_HEADER_LEN > detail_len) {
            if (ancs_batch_warning_due()) {
                floatair_warn("ANCS detail truncated attribute header off=%u len=%u",
                              (unsigned)offset,
                              (unsigned)detail_len);
            }
            return false;
        }
        attr_id = detail[offset];
        attr_len = ancs_u16_le(&detail[offset + ANCS_DETAIL_ATTR_LEN_OFF]);
        offset += ANCS_DETAIL_ATTR_HEADER_LEN;
        if (offset + attr_len > detail_len) {
            if (ancs_batch_warning_due()) {
                floatair_warn("ANCS detail truncated attr=%u attr_len=%u remain=%u",
                              (unsigned)attr_id,
                              (unsigned)attr_len,
                              (unsigned)(detail_len - offset));
            }
            return false;
        }

        attr_data = &detail[offset];
        switch (attr_id) {
            case ANCS_ATTR_APP_IDENTIFIER:
                ancs_copy_str(parsed->app_id,
                              sizeof(parsed->app_id),
                              attr_data,
                              attr_len);
                break;
            case ANCS_ATTR_TITLE:
                ancs_copy_str(parsed->title,
                              sizeof(parsed->title),
                              attr_data,
                              attr_len);
                break;
            case ANCS_ATTR_SUBTITLE:
                ancs_copy_str(parsed->subtitle,
                              sizeof(parsed->subtitle),
                              attr_data,
                              attr_len);
                break;
            case ANCS_ATTR_MESSAGE:
                ancs_copy_str(parsed->message,
                              sizeof(parsed->message),
                              attr_data,
                              attr_len);
                break;
            case ANCS_ATTR_DATE:
                parsed->has_time = ancs_parse_date_time(attr_data,
                                                        attr_len,
                                                        &parsed->notify_time);
                break;
            default:
                break;
        }
        offset += attr_len;
    }
    return true;
}

/**
 * @brief 解析一条 FULL 批量记录并执行来电分流。
 * @param[in] rec 记录起点，指向 item_len 字段。
 * @param[in] item_len type 与 data 的总长度。
 * @param[out] entry 普通通知条目。
 * @param[out] should_enqueue 是否需要将 entry 加入普通通知队列。
 * @param[out] notification_uid iOS 通知 UID。
 * @return 记录格式合法返回 `true`。
 */
static bool ancs_parse_full_record(const uint8_t* rec,
                                   uint16_t item_len,
                                   system_notification_entry_t* entry,
                                   bool* should_enqueue,
                                   uint32_t* notification_uid) {
    uint16_t data_len = 0u;
    uint32_t rec_total = 0u;
    uint32_t app_id_len_off = 0u;
    uint32_t detail_len_off = 0u;
    uint32_t detail_off = 0u;
    uint8_t name_len = 0u;
    uint8_t app_id_len = 0u;
    uint16_t detail_len = 0u;
    const uint8_t* skeleton = NULL;
    uint8_t event_id = 0u;
    uint8_t event_flags = 0u;
    uint8_t category = 0u;
    bool is_pre_existing = false;
    uint32_t uid = 0u;
    char app_id[ANCS_APP_ID_MAX] = {0};
    ancs_notification_detail_t parsed = {0};

    if (rec == NULL || entry == NULL || should_enqueue == NULL ||
        notification_uid == NULL || item_len < 1u) {
        return false;
    }
    *should_enqueue = false;
    *notification_uid = 0u;
    data_len = item_len - 1u;
    rec_total = ANCS_ITEM_WIRE_TOTAL(item_len);
    if (data_len < ANCS_FULL_DATA_LEN(0, 0, 0)) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS FULL too short data_len=%u", (unsigned)data_len);
        }
        return false;
    }

    skeleton = &rec[ANCS_REC_SKELETON_OFF];
    event_id = skeleton[ANCS_SKELETON_EVENT_ID_OFF];
    event_flags = skeleton[ANCS_SKELETON_EVENT_FLAGS_OFF];
    category = skeleton[ANCS_SKELETON_CATEGORY_ID_OFF];
    is_pre_existing = (event_flags & ANCS_EVENT_FLAG_PRE_EXISTING) != 0u;
    uid = ancs_u32_le(&skeleton[ANCS_SKELETON_UID_OFF]);
    *notification_uid = uid;
    if (event_id != ANCS_EVT_ADDED && event_id != ANCS_EVT_MODIFIED) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS FULL invalid event=%u uid=%" PRIu32,
                          (unsigned)event_id,
                          uid);
        }
        return false;
    }

    name_len = rec[ANCS_REC_NAME_LEN_OFF];
    app_id_len_off = ANCS_REC_APP_ID_LEN_OFF(name_len);
    if (app_id_len_off >= rec_total) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS FULL missing appIdLen name_len=%u item_len=%u",
                          (unsigned)name_len,
                          (unsigned)item_len);
        }
        return false;
    }
    app_id_len = rec[app_id_len_off];
    detail_len_off = ANCS_REC_DETAIL_LEN_OFF(name_len, app_id_len);
    detail_off = ANCS_REC_DETAIL_OFF(name_len, app_id_len);
    if (detail_len_off + sizeof(uint16_t) > rec_total || detail_off > rec_total) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS FULL missing detailLen name_len=%u app_id_len=%u",
                          (unsigned)name_len,
                          (unsigned)app_id_len);
        }
        return false;
    }
    detail_len = ancs_u16_le(&rec[detail_len_off]);
    if ((uint32_t)ANCS_ITEM_WIRE_LEN(
            ANCS_FULL_DATA_LEN(name_len, app_id_len, detail_len)) != rec_total) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS FULL length mismatch item_len=%u name=%u app=%u detail=%u",
                          (unsigned)item_len,
                          (unsigned)name_len,
                          (unsigned)app_id_len,
                          (unsigned)detail_len);
        }
        return false;
    }

    ancs_copy_str(app_id,
                  sizeof(app_id),
                  &rec[ANCS_REC_APP_ID_OFF(name_len)],
                  app_id_len);
    if (!ancs_parse_detail(&rec[detail_off], detail_len, uid, &parsed)) {
        return false;
    }
    if (app_id[0] == '\0' && parsed.app_id[0] != '\0') {
        ancs_copy_str(app_id,
                      sizeof(app_id),
                      (const uint8_t*)parsed.app_id,
                      strlen(parsed.app_id));
    } else if (app_id[0] != '\0' && parsed.app_id[0] != '\0' &&
               strcmp(app_id, parsed.app_id) != 0) {
        if (ancs_batch_warning_due()) {
            floatair_warn("ANCS AppID mismatch record=%s detail=%s", app_id, parsed.app_id);
        }
    }
    /* 系统电话由 HFP 统一生成来电和未接来电通知，避免 ANCS 再次入队。 */
    if (strcmp(app_id, "com.apple.mobilephone") == 0) {
        floatair_dbg("ANCS ignore phone notification uid=%" PRIu32, uid);
        return true;
    }

    if (category == ANCS_CATEGORY_INCOMING_CALL && !is_pre_existing) {
        const char* caller = NULL;

        ancs_trim_call_title(parsed.title);
        caller = parsed.title[0] != '\0' ? parsed.title : parsed.message;
        floatair_info("ANCS third-party incoming call uid=%" PRIu32 " app=%s caller=%s",
                      uid,
                      app_id,
                      caller != NULL ? caller : "");
        system_runtime_state_set_ancs_call_info(caller, app_id);
        return true;
    }

    memset(entry, 0, sizeof(*entry));
    entry->id = ancs_internal_notification_id(uid);
    entry->mode = NOTIFY_MODE_MESSAGE;
    entry->duration_ms = 3000u;
    entry->level = 1u;
    entry->action = 1u;
    entry->silent = is_pre_existing;
    if (category == ANCS_CATEGORY_MISSED_CALL) {
        entry->icon = SYSTEM_NOTIFICATION_ICON_MISSED_CALL;
    }

    if (parsed.subtitle[0] != '\0' && parsed.message[0] != '\0') {
        char merged[MSG_STR_MAX_LEN] = {0};

        snprintf(merged,
                 sizeof(merged),
                 "%s\n%s",
                 parsed.subtitle,
                 parsed.message);
        memcpy(parsed.message, merged, sizeof(parsed.message));
        parsed.message[sizeof(parsed.message) - 1u] = '\0';
    } else if (parsed.message[0] == '\0' && parsed.subtitle[0] != '\0') {
        memcpy(parsed.message, parsed.subtitle, sizeof(parsed.message));
        parsed.message[sizeof(parsed.message) - 1u] = '\0';
    }

    if (parsed.has_time) {
        entry->notify_time = parsed.notify_time;
    } else {
        time(&entry->notify_time);
    }
    if (parsed.title[0] != '\0' && parsed.message[0] != '\0') {
        snprintf(entry->title,
                 sizeof(entry->title),
                 "%s\n%s",
                 parsed.title,
                 parsed.message);
    } else if (parsed.title[0] != '\0') {
        ancs_copy_str(entry->title,
                      sizeof(entry->title),
                      (const uint8_t*)parsed.title,
                      strlen(parsed.title));
    } else if (parsed.message[0] != '\0') {
        ancs_copy_str(entry->title,
                      sizeof(entry->title),
                      (const uint8_t*)parsed.message,
                      strlen(parsed.message));
    }
    entry->has_title = entry->title[0] != '\0';
    *should_enqueue = true;
    return true;
}

/**
 * @brief 从当前批次待入队列表移除指定通知。
 * @param[in] id 内部通知 ID。
 * @param[in,out] count 当前待入队数量。
 * @return 无返回值。
 */
static void ancs_pending_remove(uint32_t id, size_t* count) {
    if (count == NULL) {
        return;
    }
    for (size_t i = 0u; i < *count; ++i) {
        if (s_ancs_batch_entries[i].id != id) {
            continue;
        }
        if (i + 1u < *count) {
            memmove(&s_ancs_batch_entries[i],
                    &s_ancs_batch_entries[i + 1u],
                    sizeof(s_ancs_batch_entries[0]) * (*count - i - 1u));
        }
        (*count)--;
        return;
    }
}

/**
 * @brief 将通知按 UID 去重后追加到当前批次，超量时保留最新条目。
 * @param[in] entry 待加入通知。
 * @param[in,out] count 当前待入队数量。
 * @return 无返回值。
 */
static void ancs_pending_upsert(const system_notification_entry_t* entry,
                                size_t* count) {
    if (entry == NULL || count == NULL) {
        return;
    }
    ancs_pending_remove(entry->id, count);
    if (*count == SYSTEM_NOTIFICATION_QUEUE_MAX) {
        memmove(&s_ancs_batch_entries[0],
                &s_ancs_batch_entries[1],
                sizeof(s_ancs_batch_entries[0]) *
                    (SYSTEM_NOTIFICATION_QUEUE_MAX - 1u));
        (*count)--;
    }
    s_ancs_batch_entries[*count] = *entry;
    (*count)++;
}

/**
 * @brief 在通知列表和弹窗追加 ANCS 熔断指引，不影响已有通知。
 * @return 指引通知成功入队并展示返回 `true`。
 */
static bool ancs_show_unsupported_notification(void) {
    system_notification_entry_t entry = {0};
    const char* message = app_get_str(ANCS_UNSUPPORTED_MESSAGE_KEY);

    entry.id = ANCS_UNSUPPORTED_NOTIFICATION_ID;
    entry.mode = NOTIFY_MODE_MESSAGE;
    entry.duration_ms = 3000u;
    entry.level = 1u;
    entry.action = 1u;
    time(&entry.notify_time);
    ancs_copy_str(entry.title,
                  sizeof(entry.title),
                  (const uint8_t*)message,
                  strlen(message));
    entry.has_title = true;

    floatair_warn("ANCS unsupported, preserve notifications and show reconnect guide");
    return system_notification_add_entry(&entry);
}

static bool system_handle_ancs_event(const JYT_ELF_MQ_MSG* msg) {
    const uint8_t* payload = NULL;
    uint16_t payload_len = 0u;
    uint16_t count = 0u;
    uint32_t offset = ANCS_BATCH_CNT_LEN;
    uint32_t full_count = 0u;
    uint32_t removed_count = 0u;
    uint32_t unsupported_count = 0u;
    size_t pending_count = 0u;

    if (msg == NULL || msg->payload_len < ANCS_BATCH_CNT_LEN) {
        floatair_err("invalid ANCS batch payload_len=%u",
                     msg != NULL ? (unsigned)msg->payload_len : 0u);
        return false;
    }
    if (!system_config_is_userguide_finished()) {
        floatair_info("userguide unfinished, ignore ANCS batch");
        return true;
    }

    if (!system_config_get_notification_enabled()) {
        floatair_info("ANCS notifications disabled, ignore ANCS batch");
        return true;
    }

    payload = msg->payload;
    payload_len = msg->payload_len;
    count = ancs_u16_le(payload);

    for (uint16_t i = 0u; i < count; ++i) {
        uint16_t item_len = 0u;
        uint16_t data_len = 0u;
        uint8_t type = 0u;
        uint32_t rec_total = 0u;
        const uint8_t* rec = NULL;

        if (offset + ANCS_BATCH_ITEM_HDR_LEN > payload_len) {
            if (ancs_batch_warning_due()) {
                floatair_warn("ANCS batch truncated header i=%u count=%u off=%u len=%u",
                              (unsigned)i,
                              (unsigned)count,
                              (unsigned)offset,
                              (unsigned)payload_len);
            }
            break;
        }
        rec = &payload[offset];
        item_len = ancs_u16_le(rec);
        rec_total = ANCS_ITEM_WIRE_TOTAL(item_len);
        if (item_len < 1u || rec_total > ANCS_MAX_ITEM_WIRE ||
            offset + rec_total > payload_len) {
            if (ancs_batch_warning_due()) {
                floatair_warn("ANCS batch invalid item i=%u item_len=%u off=%u len=%u",
                              (unsigned)i,
                              (unsigned)item_len,
                              (unsigned)offset,
                              (unsigned)payload_len);
            }
            break;
        }
        type = rec[sizeof(uint16_t)];
        data_len = item_len - 1u;

        if (type == ANCS_BATCH_TYPE_FULL) {
            system_notification_entry_t entry = {0};
            bool should_enqueue = false;
            uint32_t uid = 0u;

            if (ancs_parse_full_record(rec,
                                       item_len,
                                       &entry,
                                       &should_enqueue,
                                       &uid)) {
                full_count++;
                if (should_enqueue) {
                    ancs_pending_upsert(&entry, &pending_count);
                }
            }
        } else if (type == ANCS_BATCH_TYPE_REMOVED) {
            const uint8_t* skeleton = &rec[ANCS_REC_SKELETON_OFF];
            uint8_t event_id = UINT8_MAX;
            uint32_t uid = 0u;
            uint32_t id = 0u;

            if (data_len > ANCS_SKELETON_EVENT_ID_OFF) {
                event_id = skeleton[ANCS_SKELETON_EVENT_ID_OFF];
            }
            if (data_len != ANCS_REMOVED_DATA_LEN || event_id != ANCS_EVT_REMOVED) {
                if (ancs_batch_warning_due()) {
                    floatair_warn("ANCS REMOVED invalid i=%u data_len=%u event=%u",
                                  (unsigned)i,
                                  (unsigned)data_len,
                                  (unsigned)event_id);
                }
                offset += rec_total;
                continue;
            }
            uid = ancs_u32_le(&skeleton[ANCS_SKELETON_UID_OFF]);
            id = ancs_internal_notification_id(uid);
            ancs_pending_remove(id, &pending_count);
            (void)system_notification_remove_id(id);
            removed_count++;
        } else if (type == ANCS_BATCH_TYPE_UNSUPPORTED) {
            if (data_len != ANCS_UNSUPPORTED_DATA_LEN) {
                if (ancs_batch_warning_due()) {
                    floatair_warn("ANCS UNSUPPORTED invalid i=%u data_len=%u",
                                  (unsigned)i,
                                  (unsigned)data_len);
                }
                offset += rec_total;
                continue;
            }
            unsupported_count++;
        } else if (ancs_batch_warning_due()) {
            floatair_warn("ANCS batch unknown type=%u i=%u item_len=%u",
                          (unsigned)type,
                          (unsigned)i,
                          (unsigned)item_len);
        }
        offset += rec_total;
    }

    if (offset != payload_len && ancs_batch_warning_due()) {
        floatair_warn("ANCS batch trailing or dropped bytes off=%u len=%u count=%u",
                      (unsigned)offset,
                      (unsigned)payload_len,
                      (unsigned)count);
    }
    floatair_info("ANCS batch count=%u len=%u full=%u removed=%u unsupported=%u queued=%u",
                  (unsigned)count,
                  (unsigned)payload_len,
                  (unsigned)full_count,
                  (unsigned)removed_count,
                  (unsigned)unsupported_count,
                  (unsigned)pending_count);
    if (unsupported_count > 0u) {
        return ancs_show_unsupported_notification();
    }
    if (pending_count > 0u) {
        return system_notification_add_entries_batch(s_ancs_batch_entries,
                                                     pending_count);
    }
    return true;
}

bool app_system_msg_handle_payload(JYT_ELF_MQ_MSG* msg) {
    uint32_t start_us = (uint32_t)GetTimeUs();
    bool ret = true;
    uint16_t event_type = 0;
    if (!msg) {
        floatair_err("msg is NULL");
        ret = false;
        goto out;
    }
    event_type = msg->Header.event_type;
    if (msg->Header.msg_type != EMT_SYSTEM_EVENT_WITH_PAYLOAD) {
        floatair_err("msg type (%d) not EMT_SYSTEM_EVENT_WITH_PAYLOAD", msg->Header.msg_type);
        ret = false;
        goto out;
    }
    floatair_dbg("handle type (%d)(%s)", event_type, system_event_type_to_str(event_type));
    {
        app_t* current_app = app_manager_current();
        if (current_app != NULL &&
            current_app->use_top_layer &&
            current_app->on_system_event != NULL &&
            current_app->on_system_event(msg)) {
            ret = true;
            goto out;
        }
    }
    switch (event_type) {
        case SET_IMU_SINGLE_TAP:
        case SET_IMU_DOUBLE_TAP:
        {
            if (!system_config_get_imudoubletap_enabled()) {
                floatair_info("imu_double_tap_enabled is false, imu double tap disabled");
                break;
            }
            ret = system_imu_event_convert_to_touch((uint8_t)event_type);
            break;
        }
        case SET_IMU_TILT:
        {
            if (!system_config_get_imutilt_enabled()) {
                floatair_info("imu_tilt_enabled is false, imu tilt disabled");
                break;
            }
            ret = system_update_imu_tilt(msg);
            //floatair_info("IMU_TILT ignore");
            break;
        }
        case SET_IMU_WOM:
        case SET_IMU_R2W:
        case SET_IMU_SMD:
        case SET_IMU_DATA_UPDATE:
        {
            break;
        }
        case SET_BAT_SOC_CHANGED:
        {
            break;
        }
        case SET_BAT_VOLT_CHANGED:
        {
            ret = system_update_bat_status(msg);
            if (ret && system_get_charge_state() == 1) {
                if (floatair_lcd_is_off()) {
                    s_low_battery_toast_pending = APP_LOW_BATTERY_TOAST_PENDING_DISMISS;
                    floatair_info("low battery toast dismiss deferred: lcd off");
                } else {
                    toast_dismiss(APP_TOAST_ID_LOW_BATTERY);
                }
            }
            break;
        }
        case SET_CHARGER_STATE_CHANGED:
        {
            break;
        }
        case SET_SLIDE_FORWARD:
        case SET_SLIDE_BACKWORD:
        case SET_FORCE_SINGLE_CLICK:
        case SET_FORCE_DOUBLE_CLICK:
        case SET_FORCE_TRI_CLICK:
        case SET_FORCE_LONG_PRESSED:
        {
            ret = system_touch_event_convert((uint8_t)event_type);
            //floatair_info("touch event ignore");
            break;
        }
        case SET_IED_WEAR_ON:
        case SET_IED_REMOVED:
        {
            bool is_wear_on = (event_type == SET_IED_WEAR_ON);
            floatair_info("wear detection update[%d]", is_wear_on);

            if (!system_runtime_input_update_wearing_state(is_wear_on)) {
                ret = true;
                break;
            }
            if (system_config_get_wear_detection_enabled()) {
                app_sleep_timer_set_wear_removed(!is_wear_on);
                if (is_wear_on) {
                    if (system_get_sys_state() == LCD_OFF) {
                        system_set_sys_state(LCD_ON);
                        (void)system_report_sys_state(LCD_ON,
                                                      SYSTEM_SYS_STATE_TRIGGER_WEAR_ON);
                    }
                }
            }
            ret = true;
            break;
        }
        case SET_KWS_HIT:
        {
            ret = system_update_kws_state(msg);
            break;
        }
        case SET_REPORT_DEVICE_STATE:
        {
            ret = system_update_device_state(msg);
            break;
        }
        case SET_ALS_RAW_DATA:
        {
            ret = system_update_als_raw_data(msg);
            break;
        }
        case SET_JYT_SIBLING_SYNC:
        {
            break;
        }
        case SET_BT_CALL_SETUP_EVENT:
        {
            ret = system_handle_call_setup_event(msg);
            break;
        }
        case SET_BT_AVRCP_POSITION_CHANGED:
        {
            ret = system_avrcp_handle_event(msg);
            break;
        }
        case SET_TWS_LINK_BROKEN:
        {
            break;
        }
        case SET_JYP_HOST_DISCONNECTED:
        {
            system_set_btconn_state(false);
            ret = true;
            break;
        }
        case SET_JYP_HOST_CONNECTED:
        {
            system_set_btconn_state(true);
            ret = true;
            break;
        }
        case SET_JYT_LOW_BATTERY_WARNING:
        {
            toast_cfg_t toast_cfg = toast_default_cfg();

            floatair_info("low battery warning: battery=%u", (unsigned)system_get_battery());
            if (floatair_lcd_is_off()) {
                s_low_battery_toast_pending = APP_LOW_BATTERY_TOAST_PENDING_SHOW;
                floatair_info("low battery toast show deferred: lcd off");
                ret = true;
                break;
            }
            toast_cfg.id = APP_TOAST_ID_LOW_BATTERY;
            toast_cfg.duration_ms = 0;
            ret = (toast_show_localized("TOAST_LOW_BATTERY_WARNING", &toast_cfg) != NULL);
            break;
        }
        case SET_JYT_TIMER_TRIGGER:
        {
            if (msg->payload_len < sizeof(uint32_t)) {
                floatair_err("invalid payload_len: %d", msg->payload_len);
                ret = false;
                break;
            }
            uint32_t timer_id = 0;
            memcpy(&timer_id, msg->payload, sizeof(uint32_t));
            floatair_dbg("timer_id: %" PRIu32, timer_id);
            ret = system_timer_handle_trigger(timer_id);
            if (!ret) {
                floatair_err("invalid timer_id: %" PRIu32, timer_id);
            }
            break;
        }
        case SET_JYT_BT_VISIBLE_CHANGED:
        {
            ret = system_handle_bt_visible_changed_event(msg);
            break;
        }
        case SET_ANCS_EVENT:
        {
            ret = system_handle_ancs_event(msg);
            break;
        }
        case SET_JYT_REFRESH_UI_REQ:
        {
            ret = system_ui_request_screen_refresh();
            break;
        }
        case SET_JYT_ACC_TYPE_CHANGED:
        {
            ret = system_handle_attachment_changed_event(msg);
            break;
        }
        default:
        {
            floatair_info("default event ignore %s(%d)", system_event_type_to_str(event_type), event_type);
            ret = false;
            break;
        }
    }
out:
    {
        uint32_t cost_us = (uint32_t)GetTimeUs() - start_us;
        const char* evt = msg ? system_event_type_to_str(event_type) : "NULL_MSG";
        floatair_dbg("system event payload cost %lu us/%lu ms, evt=%s(%u) ret=%d",
                     (unsigned long)cost_us,
                     (unsigned long)(cost_us / 1000U),
                     evt,
                     event_type,
                     ret);
    }
    return ret;
}

bool app_emerg_msg_handle(char* msg, size_t msg_size) {
    size_t copy_len = 0;

    if (!msg || msg_size == 0) {
        floatair_err("msg is NULL or msg_size is 0");
        return false;
    }
    {
        app_t* current_app = app_manager_current();
        if (current_app != NULL &&
            current_app->use_top_layer &&
            current_app->on_emerg_message != NULL &&
            current_app->on_emerg_message(msg, msg_size)) {
            return true;
        }
    }
    if (floatair_lcd_is_off()) {
        copy_len = msg_size < sizeof(s_pending_emerg_toast) - 1U
                       ? msg_size
                       : sizeof(s_pending_emerg_toast) - 1U;
        memcpy(s_pending_emerg_toast, msg, copy_len);
        s_pending_emerg_toast[copy_len] = '\0';
        floatair_info("emergency toast deferred: lcd off, len=%u", (unsigned)copy_len);
        return true;
    }
    char* safe_msg = (char*)malloc(msg_size + 1);
    if (!safe_msg) {
        floatair_err("malloc safe_msg failed");
        return false;
    }
    memcpy(safe_msg, msg, msg_size);
    safe_msg[msg_size] = '\0';
    toast_show(safe_msg);
    free(safe_msg);
    return true;
}

/**
 * @brief 亮屏后显示灭屏期间延迟的系统 Toast 消息。
 * @return 无返回值。
 */
void app_message_flush_pending_after_screen_on(void) {
    if (floatair_lcd_is_off()) {
        return;
    }

    system_avrcp_flush_pending_after_screen_on();

    if (s_low_battery_toast_pending == APP_LOW_BATTERY_TOAST_PENDING_DISMISS) {
        toast_dismiss(APP_TOAST_ID_LOW_BATTERY);
    } else if (s_low_battery_toast_pending == APP_LOW_BATTERY_TOAST_PENDING_SHOW) {
        toast_cfg_t toast_cfg = toast_default_cfg();

        toast_cfg.id = APP_TOAST_ID_LOW_BATTERY;
        toast_cfg.duration_ms = 0;
        (void)toast_show_localized("TOAST_LOW_BATTERY_WARNING", &toast_cfg);
    }
    s_low_battery_toast_pending = APP_LOW_BATTERY_TOAST_PENDING_NONE;

    if (s_pending_emerg_toast[0] != '\0') {
        toast_show(s_pending_emerg_toast);
        s_pending_emerg_toast[0] = '\0';
    }
}

bool app_system_msg_handle(JYT_ELF_MQ_MSG* msg) {
    if (!msg) {
        floatair_err("msg is NULL");
        return false;
    }
    if (msg->Header.msg_type != EMT_SYSTEM_EVENT) {
        floatair_err("msg type (%d) not EMT_SYSTEM_EVENT", msg->Header.msg_type);
        return false;
    }
    floatair_err("type (%d) not supported", msg->Header.event_type);
    return false;
}

enum {
    APP_MSG_DUMP_CHUNK_SIZE = 384,   ///< 单次日志正文上限，给日志前缀预留空间。
    APP_MSG_DUMP_FORMAT_SIZE = 128,  ///< 数值等短字段的临时格式化缓冲区大小。
    APP_MSG_DUMP_LOCAL_DEPTH = 8,    ///< 常规消息使用的本地遍历栈深度。
};

/**
 * @brief MsgPack 紧凑分段输出器。
 */
typedef struct {
    char chunk[APP_MSG_DUMP_CHUNK_SIZE + 1]; ///< 当前等待输出的日志正文。
    size_t length;                           ///< 当前正文已使用字节数。
    size_t part;                             ///< 下一段日志的分段序号。
    const char* tag;                         ///< 本条 MsgPack 的日志标签。
} app_msg_dump_writer_t;

/**
 * @brief 输出当前缓冲区并开始下一段。
 * @param[in,out] writer 紧凑输出器。
 * @return 无返回值。
 */
static void app_msg_dump_flush(app_msg_dump_writer_t* writer) {
    if (writer == NULL || writer->length == 0) {
        return;
    }
    writer->chunk[writer->length] = '\0';
    floatair_dbg("[MPACK][%s][part=%zu] %s", writer->tag, writer->part, writer->chunk);
    writer->length = 0;
    writer->part++;
}

/**
 * @brief 追加任意长度内容，缓冲区写满时自动分段输出。
 * @param[in,out] writer 紧凑输出器。
 * @param[in] data 待追加内容。
 * @param[in] length 内容字节数。
 * @return 无返回值。
 */
static void app_msg_dump_append(app_msg_dump_writer_t* writer,
                                const char* data,
                                size_t length) {
    while (writer != NULL && data != NULL && length > 0) {
        size_t space = APP_MSG_DUMP_CHUNK_SIZE - writer->length;
        if (space == 0) {
            app_msg_dump_flush(writer);
            space = APP_MSG_DUMP_CHUNK_SIZE;
        }
        size_t copy_len = (length < space) ? length : space;
        memcpy(writer->chunk + writer->length, data, copy_len);
        writer->length += copy_len;
        data += copy_len;
        length -= copy_len;
    }
}

/**
 * @brief 原子追加一个短标记，避免 UTF-8 字符被拆到两段日志中。
 * @param[in,out] writer 紧凑输出器。
 * @param[in] data 待追加标记。
 * @param[in] length 标记字节数。
 * @return 无返回值。
 */
static void app_msg_dump_append_atomic(app_msg_dump_writer_t* writer,
                                       const char* data,
                                       size_t length) {
    if (writer == NULL || data == NULL || length == 0) {
        return;
    }
    if (length <= APP_MSG_DUMP_CHUNK_SIZE &&
        APP_MSG_DUMP_CHUNK_SIZE - writer->length < length) {
        app_msg_dump_flush(writer);
    }
    app_msg_dump_append(writer, data, length);
}

/**
 * @brief 追加一个以 NUL 结尾的短字符串。
 * @param[in,out] writer 紧凑输出器。
 * @param[in] text 待追加字符串。
 * @return 无返回值。
 */
static void app_msg_dump_append_cstr(app_msg_dump_writer_t* writer, const char* text) {
    if (text != NULL) {
        app_msg_dump_append_atomic(writer, text, strlen(text));
    }
}

/**
 * @brief 格式化并追加一个数值等短字段。
 * @param[in,out] writer 紧凑输出器。
 * @param[in] format printf 格式字符串。
 * @return 无返回值。
 */
static void app_msg_dump_append_format(app_msg_dump_writer_t* writer,
                                       const char* format,
                                       ...) {
    char formatted[APP_MSG_DUMP_FORMAT_SIZE];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(formatted, sizeof(formatted), format, args);
    va_end(args);
    if (written < 0) {
        app_msg_dump_append_cstr(writer, "<format-error>");
        return;
    }
    size_t length = (size_t)written;
    if (length >= sizeof(formatted)) {
        length = sizeof(formatted) - 1;
    }
    app_msg_dump_append_atomic(writer, formatted, length);
}

/**
 * @brief 追加完整字符串，并转义日志后端不能安全承载的字符。
 * @param[in,out] writer 紧凑输出器。
 * @param[in] str 字符串原始字节。
 * @param[in] length 字符串字节数。
 * @return 无返回值。
 */
static void app_msg_dump_append_string(app_msg_dump_writer_t* writer,
                                       const char* str,
                                       size_t length) {
    app_msg_dump_append_cstr(writer, "\"");
    for (size_t offset = 0; offset < length;) {
        const unsigned char value = (unsigned char)str[offset];
        const char* escaped = NULL;
        switch (value) {
            case '\"': escaped = "\\\""; break;
            case '\\': escaped = "\\\\"; break;
            case '\b': escaped = "\\b"; break;
            case '\f': escaped = "\\f"; break;
            case '\n': escaped = "\\n"; break;
            case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break;
            default: break;
        }
        if (escaped != NULL) {
            app_msg_dump_append_cstr(writer, escaped);
            offset++;
            continue;
        }
        if (value < 0x20U || value == 0x7FU) {
            app_msg_dump_append_format(writer, "\\u%04X", (unsigned)value);
            offset++;
            continue;
        }

        size_t utf8_len = 1;
        bool utf8_valid = true;
        if (value < 0x80U) {
            utf8_len = 1;
        } else if (value >= 0xC2U && value <= 0xDFU) {
            utf8_len = 2;
        } else if (value >= 0xE0U && value <= 0xEFU) {
            utf8_len = 3;
        } else if (value >= 0xF0U && value <= 0xF4U) {
            utf8_len = 4;
        } else {
            utf8_valid = false;
        }
        if (utf8_valid) {
            utf8_valid = offset + utf8_len <= length;
            for (size_t i = 1; utf8_valid && i < utf8_len; ++i) {
                utf8_valid = (((unsigned char)str[offset + i] & 0xC0U) == 0x80U);
            }
            if (utf8_valid && utf8_len >= 3) {
                const unsigned char second = (unsigned char)str[offset + 1];
                if ((value == 0xE0U && second < 0xA0U) ||
                    (value == 0xEDU && second > 0x9FU) ||
                    (value == 0xF0U && second < 0x90U) ||
                    (value == 0xF4U && second > 0x8FU)) {
                    utf8_valid = false;
                }
            }
        }
        if (!utf8_valid) {
            app_msg_dump_append_format(writer, "\\x%02X", (unsigned)value);
            offset++;
            continue;
        }
        app_msg_dump_append_atomic(writer, str + offset, utf8_len);
        offset += utf8_len;
    }
    app_msg_dump_append_cstr(writer, "\"");
}

/**
 * @brief 紧凑输出一个非容器 MsgPack 节点。
 * @param[in,out] writer 紧凑输出器。
 * @param[in] node 待输出节点。
 * @param[in] type 节点类型。
 * @return 已处理返回 true，数组或 Map 容器返回 false。
 */
static bool app_msg_dump_compact_scalar(app_msg_dump_writer_t* writer,
                                        mpack_node_t node,
                                        mpack_type_t type) {
    switch (type) {
        case mpack_type_missing:
            app_msg_dump_append_cstr(writer, "<missing>");
            return true;
        case mpack_type_nil:
            app_msg_dump_append_cstr(writer, "null");
            return true;
        case mpack_type_bool:
            app_msg_dump_append_cstr(writer, mpack_node_bool(node) ? "true" : "false");
            return true;
        case mpack_type_int:
            app_msg_dump_append_format(writer, "%" PRId64, (int64_t)mpack_node_i64(node));
            return true;
        case mpack_type_uint:
            app_msg_dump_append_format(writer, "%" PRIu64, (uint64_t)mpack_node_u64(node));
            return true;
        case mpack_type_float:
            app_msg_dump_append_format(writer, "%.9g", (double)mpack_node_float(node));
            return true;
        case mpack_type_double:
            app_msg_dump_append_format(writer, "%.17g", mpack_node_double(node));
            return true;
        case mpack_type_str: {
            const char* str = mpack_node_str(node);
            size_t length = mpack_node_strlen(node);
            if (str == NULL) {
                app_msg_dump_append_cstr(writer, "<null-string>");
            } else {
                app_msg_dump_append_string(writer, str, length);
            }
            return true;
        }
        case mpack_type_bin: {
            size_t length = mpack_node_bin_size(node);
            app_msg_dump_append_format(writer, "{\"$bin\":{\"len\":%zu}}", length);
            return true;
        }
#if MPACK_EXTENSIONS
        case mpack_type_ext: {
            size_t length = mpack_node_data_len(node);
            app_msg_dump_append_format(writer,
                                       "{\"$ext\":{\"type\":%d,\"len\":%zu}}",
                                       (int)mpack_node_exttype(node),
                                       length);
            return true;
        }
#endif
        case mpack_type_array:
        case mpack_type_map:
            return false;
        default:
            app_msg_dump_append_format(writer, "<unknown:%d>", (int)mpack_node_type(node));
            return true;
    }
}

/**
 * @brief MsgPack 容器节点的迭代遍历状态。
 */
typedef struct {
    mpack_node_t node; ///< 当前待输出节点。
    size_t index;      ///< 下一个待输出的数组元素或 Map 键值对索引。
    size_t count;      ///< 当前容器的元素或键值对数量。
    bool entered;      ///< 是否已经输出容器起始标记。
    bool map_value;    ///< Map 当前是否等待输出 value。
} app_msg_dump_frame_t;

/**
 * @brief 迭代输出完整 MsgPack 树，二进制节点仅输出长度。
 * @param[in,out] writer 紧凑输出器。
 * @param[in] root 根节点。
 * @return 完整输出返回 true，遍历栈扩容失败返回 false。
 */
static bool app_msg_dump_compact_tree(app_msg_dump_writer_t* writer, mpack_node_t root) {
    app_msg_dump_frame_t local_stack[APP_MSG_DUMP_LOCAL_DEPTH];
    app_msg_dump_frame_t* stack = local_stack;
    size_t capacity = APP_MSG_DUMP_LOCAL_DEPTH;
    size_t depth = 1;

    local_stack[0] = (app_msg_dump_frame_t){
        .node = root,
    };

    while (depth > 0) {
        app_msg_dump_frame_t* frame = &stack[depth - 1];
        mpack_type_t type = mpack_node_type(frame->node);
        mpack_node_t child;

        if (app_msg_dump_compact_scalar(writer, frame->node, type)) {
            depth--;
            continue;
        }

        if (!frame->entered) {
            frame->entered = true;
            if (type == mpack_type_array) {
                frame->count = mpack_node_array_length(frame->node);
                app_msg_dump_append_cstr(writer, "[");
            } else {
                frame->count = mpack_node_map_count(frame->node);
                app_msg_dump_append_cstr(writer, "{");
            }
        }

        if (type == mpack_type_array) {
            if (frame->index >= frame->count) {
                app_msg_dump_append_cstr(writer, "]");
                depth--;
                continue;
            }
            if (frame->index > 0) {
                app_msg_dump_append_cstr(writer, ",");
            }
            child = mpack_node_array_at(frame->node, frame->index);
            frame->index++;
        } else if (!frame->map_value) {
            if (frame->index >= frame->count) {
                app_msg_dump_append_cstr(writer, "}");
                depth--;
                continue;
            }
            if (frame->index > 0) {
                app_msg_dump_append_cstr(writer, ",");
            }
            child = mpack_node_map_key_at(frame->node, frame->index);
            frame->map_value = true;
        } else {
            app_msg_dump_append_cstr(writer, ":");
            child = mpack_node_map_value_at(frame->node, frame->index);
            frame->index++;
            frame->map_value = false;
        }

        if (depth == capacity) {
            size_t new_capacity = capacity * 2;
            app_msg_dump_frame_t* expanded = NULL;
            if (stack == local_stack) {
                expanded = (app_msg_dump_frame_t*)malloc(
                    new_capacity * sizeof(app_msg_dump_frame_t));
                if (expanded != NULL) {
                    memcpy(expanded, local_stack, depth * sizeof(app_msg_dump_frame_t));
                }
            } else {
                expanded = (app_msg_dump_frame_t*)realloc(
                    stack, new_capacity * sizeof(app_msg_dump_frame_t));
            }
            if (expanded == NULL) {
                if (stack != local_stack) {
                    free(stack);
                }
                return false;
            }
            stack = expanded;
            capacity = new_capacity;
        }
        stack[depth] = (app_msg_dump_frame_t){
            .node = child,
        };
        depth++;
    }

    if (stack != local_stack) {
        free(stack);
    }
    return true;
}

/**
 * @brief 紧凑分段输出 MsgPack 可读字段，二进制字段仅输出类型和长度。
 * @param[in] msg MsgPack 原始数据。
 * @param[in] msg_size 原始数据字节数。
 * @param[in] tag 日志标签，为空时使用默认标签。
 * @return 无返回值。
 */
void app_msg_dump(const char* msg, size_t msg_size, const char* tag) {
    if (!tag) {
        tag = "msg default";
    }
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, msg, msg_size);
    mpack_tree_parse(&tree);

    if (mpack_tree_error(&tree) != mpack_ok) {
        floatair_err("MsgPack parse error: %s", mpack_error_to_string(mpack_tree_error(&tree)));
        mpack_tree_destroy(&tree);
        return;
    }

    app_msg_dump_writer_t writer = {
        .length = 0,
        .part = 1,
        .tag = tag,
    };
    if (!app_msg_dump_compact_tree(&writer, mpack_tree_root(&tree))) {
        app_msg_dump_append_cstr(&writer, "<dump-stack-allocation-failed>");
        floatair_err("MsgPack dump stack allocation failed");
    }
    app_msg_dump_flush(&writer);
    floatair_dbg("[MPACK][%s][end] parts=%zu source_bytes=%zu", tag, writer.part - 1, msg_size);
    mpack_tree_destroy(&tree);
}

bool app_msg_parse_header(mpack_node_t node, msg_pack_t* msg) {
    if (!app_msg_get_u32(node, true, "seq", &(msg->sequence))) {
        return false;
    }
    if (!app_msg_get_u8(node, true, "type", &(msg->type))) {
        return false;
    }
    if (0 == app_msg_get_str(node, "cmd", msg->cmd, MSG_CMD_MAX_LEN)) {
        return false;
    }
    msg->biz[0] = '\0';
    mpack_node_t biz_node = mpack_node_map_cstr_optional(node, "biz");
    if (!mpack_node_is_missing(biz_node) && !mpack_node_is_nil(biz_node) &&
        mpack_node_type(biz_node) == mpack_type_str) {
        size_t biz_len = mpack_node_strlen(biz_node);
        size_t copy_len = biz_len;
        if (copy_len >= MSG_BIZ_MAX_LEN) {
            copy_len = MSG_BIZ_MAX_LEN - 1;
            floatair_err("str len (%zu) is greater than size (%d) key: biz", biz_len, MSG_BIZ_MAX_LEN);
        }
        memcpy(msg->biz, mpack_node_str(biz_node), copy_len);
        msg->biz[copy_len] = '\0';
    }
    return true;
}

/**
 * @brief 以摘要形式记录一条 MsgPack 消息头。
 *
 * @param msg 消息缓冲区。
 * @param msg_size 缓冲区长度。
 * @param tag 日志标签。
 * @return 无返回值。
 */
void app_msg_dump_summary(const char* msg, size_t msg_size, const char* tag) {
    mpack_tree_t tree;
    mpack_node_t node_root;
    mpack_node_t payload_node;
    msg_pack_t header = {
        .id       = UINT32_MAX,
        .sequence = UINT32_MAX,
        .type     = MSG_TYPE_INVALID,
    };

    if (msg == NULL || msg_size == 0) {
        floatair_err("%s summary input err", tag ? tag : "msg");
        return;
    }

    mpack_tree_init_data(&tree, msg, msg_size);
    mpack_tree_parse(&tree);
    if (mpack_tree_error(&tree) != mpack_ok) {
        floatair_err("%s summary parse err: %s",
                     tag ? tag : "msg",
                     mpack_error_to_string(mpack_tree_error(&tree)));
        mpack_tree_destroy(&tree);
        return;
    }

    node_root = mpack_tree_root(&tree);
    if (!mpack_node_is_missing(node_root) && !mpack_node_is_nil(node_root)) {
        (void)app_msg_get_u32(node_root, false, "id", &header.id);
        payload_node = mpack_node_map_cstr_optional(node_root, "payload");
        if (!mpack_node_is_missing(payload_node) && !mpack_node_is_nil(payload_node) &&
            mpack_node_type(payload_node) == mpack_type_map) {
            (void)app_msg_parse_header(payload_node, &header);
        }
    }

    floatair_dbg("%s size=%zu id=%" PRIu32 " seq=%" PRIu32 " type=%u biz=%s cmd=%s",
                 tag ? tag : "msg",
                 msg_size,
                 header.id,
                 header.sequence,
                 header.type,
                 header.biz,
                 header.cmd);
    mpack_tree_destroy(&tree);
}

msg_pack_t* app_mpackmsg_create(void) {
    msg_pack_t* msg = malloc(sizeof(msg_pack_t));
    floatair_assert(msg, "msg is NULL");
    memset(msg, 0, sizeof(msg_pack_t));
    msg->id       = UINT32_MAX;
    msg->sequence = UINT32_MAX;
    msg->type     = MSG_TYPE_INVALID;
    return msg;
}

void app_mpackmsg_destroy(msg_pack_t* msg) {
    if (!msg) {
        floatair_err("msg is NULL");
        return;
    }
    free(msg);
}

bool app_msg_get_u8(mpack_node_t node, bool optional, const char* key, uint8_t* data) {
    if (!key || !data) {
        floatair_err("input err");
        return false;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        if (optional) {
            floatair_info("missing optional key: %s", key);
            return true;
        }
        floatair_err("missing key: %s", key);
        return false;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) != mpack_type_uint) {
        floatair_err("type err[%s]", key);
        return false;
    }
    *data = mpack_node_u8(parse_node);
    return true;
}

bool app_msg_get_u16(mpack_node_t node, bool optional, const char* key, uint16_t* data) {
    if (!key || !data) {
        floatair_err("input err");
        return false;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        if (optional) {
            floatair_info("missing optional key: %s", key);
            return true;
        }
        floatair_err("missing key: %s", key);
        return false;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) != mpack_type_uint) {
        floatair_err("type err[%s]", key);
        return false;
    }
    *data = mpack_node_u16(parse_node);
    return true;
}

bool app_msg_get_32(mpack_node_t node, bool optional, const char* key, int32_t* data) {
    if (!key || !data) {
        floatair_err("input err");
        return false;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        if (optional) {
            floatair_info("missing optional key: %s", key);
            return true;
        }
        floatair_err("missing key: %s", key);
        return false;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) == mpack_type_uint) {
        uint32_t value = mpack_node_u32(parse_node);

        if (value > (uint32_t)INT32_MAX) {
            floatair_err("range err[%s]", key);
            return false;
        }
        *data = (int32_t)value;
        return true;
    }
    if (mpack_node_type(parse_node) != mpack_type_int) {
        floatair_err("type err[%s]", key);
        return false;
    }
    *data = mpack_node_i32(parse_node);
    return true;
}

bool app_msg_get_u32(mpack_node_t node, bool optional, const char* key, uint32_t* data) {
    if (!key || !data) {
        floatair_err("input err");
        return false;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        if (optional) {
            floatair_info("missing optional key: %s", key);
            return true;
        }
        floatair_err("missing key: %s", key);
        return false;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) != mpack_type_uint) {
        floatair_err("type err[%s]", key);
        return false;
    }
    *data = mpack_node_u32(parse_node);
    return true;
}

bool app_msg_get_u64(mpack_node_t node, bool optional, const char* key, uint64_t* data) {
    if (!key || !data) {
        floatair_err("input err");
        return false;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        if (optional) {
            floatair_info("missing optional key: %s", key);
            return true;
        }
        floatair_err("missing key: %s", key);
        return false;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) != mpack_type_uint) {
        floatair_err("type err[%s]", key);
        return false;
    }
    *data = mpack_node_u64(parse_node);
    return true;
}

bool app_msg_get_float(mpack_node_t node, bool optional, const char* key, float* data) {
    if (!key || !data) {
        floatair_err("input err");
        return false;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        if (optional) {
            floatair_info("missing optional key: %s", key);
            return true;
        }
        floatair_err("missing key: %s", key);
        return false;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) != mpack_type_float && mpack_node_type(parse_node) != mpack_type_double) {
        floatair_err("type err[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) == mpack_type_float) {
        *data = mpack_node_float(parse_node);
    } else {
        *data = (float)mpack_node_double(parse_node);
    }
    return true;
}

bool app_msg_get_bool(mpack_node_t node, bool optional, const char* key, bool* data) {
    if (!key || !data) {
        floatair_err("input err");
        return false;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        if (optional) {
            floatair_info("missing optional key: %s", key);
            return true;
        }
        floatair_err("missing key: %s", key);
        return false;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil[%s]", key);
        return false;
    }
    if (mpack_node_type(parse_node) != mpack_type_bool) {
        floatair_err("type err[%s]", key);
        return false;
    }
    *data = mpack_node_bool(parse_node);
    return true;
}

size_t app_msg_get_str(mpack_node_t node, const char* key, char* data, size_t size) {
    size_t ret = 0;
    if (!key || !data || size == 0) {
        floatair_err("input err");
        return ret;
    }

    mpack_node_t parse_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(parse_node)) {
        floatair_err("missing key: %s", key);
        return ret;
    }
    if (mpack_node_is_nil(parse_node)) {
        floatair_err("nil key: %s", key);
        return ret;
    }
    if (mpack_node_type(parse_node) != mpack_type_str) {
        floatair_err("type err key: %s", key);
        return ret;
    }
    size_t str_len = mpack_node_strlen(parse_node);
    if (str_len == 0) {
        floatair_err("str is empty key: %s", key);
        return ret;
    }
    ret = str_len;
    if (ret >= size) {
        ret = size - 1;
        floatair_err("str len (%zu) is greater than size (%zu) key: %s", str_len, size, key);
    }
    memcpy(data, mpack_node_str(parse_node), ret);
    data[ret] = '\0';
    return ret;
}

static msg_pack_nck_t msg_pack_nck_err[] = {
    {ErrBizErr, "Business Error"},
    {ErrCmdErr, "Command Error"},
    {ErrIDErr, "ID Error"},
    {ErrNameErr, "Name Error"},
    {ErrPayloadErr, "Payload Error"},
    {ErrSeqErr, "Sequence Error"},
    {ErrTypeErr, "Type Error"},
    {ErrDataErr, "Data Error"},
    {ErrBadParam, "Bad Parameter Error"},
    {ErrDataTypeMismatch, "Data Type Mismatch Error"},
    {ErrNotReady, "Not Ready Error"},
    {ErrCmdNotImplemented, "Command Not Implemented Error"},
    {ErrFontNotExistFailed, "Font Not Exist Failed Error"},
    {ErrFileNotExistFailed, "File Not Exist Failed Error"},
    {ErrBadFilePath, "Bad File Path Error"},
    {ErrBtErr, "Bluetooth Error"},
    {ErrBadCRC, "Bad CRC Error"},
    {ErrScreenOff, "Screen Off Error"},
    {ErrGuideStepMismatch, "Guide Step Mismatch Error"},
};

const char* app_msg_get_err_msg(uint32_t err_code) {
    for (size_t i = 0; i < sizeof(msg_pack_nck_err) / sizeof(msg_pack_nck_err[0]); i++) {
        if (msg_pack_nck_err[i].code == err_code) {
            return msg_pack_nck_err[i].msg;
        }
    }
    return "Unknown Error";
}

msg_pack_writer_t* app_mpack_create_writer(msg_pack_t* msg, uint8_t type) {
    floatair_assert(msg, "input err");
    msg_pack_writer_t* writer = malloc(sizeof(msg_pack_writer_t));
    floatair_assert(writer, "writer err");
    memset(writer, 0, sizeof(msg_pack_writer_t));
    mpack_writer_init_growable(&writer->writer, &writer->buffer, &writer->size);

    mpack_start_map(&writer->writer, 2);
    mpack_write_cstr(&writer->writer, "id");
    mpack_write_u32(&writer->writer, msg->id);

    mpack_write_cstr(&writer->writer, "payload");
    mpack_start_map(&writer->writer, 5);
    mpack_write_cstr(&writer->writer, "seq");
    mpack_write_u32(&writer->writer, msg->sequence);
    mpack_write_cstr(&writer->writer, "type");
    mpack_write_u8(&writer->writer, type);
    mpack_write_cstr(&writer->writer, "cmd");
    mpack_write_cstr(&writer->writer, msg->cmd);
    mpack_write_cstr(&writer->writer, "biz");
    if (msg->biz[0] != '\0') {
        mpack_write_cstr(&writer->writer, msg->biz);
    } else {
        mpack_write_nil(&writer->writer);
    }
    mpack_write_cstr(&writer->writer, "data");
    return writer;
}

void app_mpack_writer_destroy(msg_pack_writer_t* writer) {
    if (!writer) {
        return;
    }
    if (writer->buffer) {
        free(writer->buffer);
        writer->buffer = NULL;
    }
    free(writer);
    writer = NULL;
}

bool app_mpack_send_writer(msg_pack_writer_t* writer) {
    floatair_assert(writer != NULL, "writer != NULL");
    mpack_finish_map(&writer->writer); /* finish payload map */
    mpack_finish_map(&writer->writer); /* finish outer map { id, payload } */
    mpack_error_t werr = mpack_writer_destroy(&writer->writer);
    if (werr != mpack_ok) {
        floatair_err("mpack_writer_destroy err: %d", werr);
        app_mpack_writer_destroy(writer);
        return false;
    }

    if (writer->buffer && writer->size > 0) {
        app_msg_dump_summary(writer->buffer, writer->size, "send phone msg");
        send2host(writer->buffer, (uint32_t) writer->size);
        app_mpack_writer_destroy(writer);
        return true;
    }
    app_mpack_writer_destroy(writer);
    return false;
}

bool app_mpack_send_ack(msg_pack_t* msg, MsgDpErr err_code) {
    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }
    msg_pack_writer_t* writer = NULL;
    if (err_code == Dp_ErrNone) {
        writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
        floatair_assert(writer != NULL, "writer != NULL");
        mpack_start_map(&writer->writer, 0);
        mpack_finish_map(&writer->writer);
    } else {
        writer = app_mpack_create_writer(msg, MSG_TYPE_NAK);
        floatair_assert(writer != NULL, "writer != NULL");
        mpack_start_map(&writer->writer, 2);
        mpack_write_cstr(&writer->writer, "code");
        mpack_write_u32(&writer->writer, (uint32_t) err_code);
        mpack_write_cstr(&writer->writer, "msg");
        mpack_write_cstr(&writer->writer, app_msg_get_err_msg((uint32_t) err_code));
        mpack_finish_map(&writer->writer);
    }
    return app_mpack_send_writer(writer);
}
