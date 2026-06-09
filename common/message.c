#include <time.h>
#include <stdio.h>
#include "message.h"

#include "elf_common.h"
#include "floatair_dbg.h"
#include "app_def.h"

#include <inttypes.h>
#include <lvgl/lvgl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "common/app_framework/app_manager.h"
#include "common/widgets/toast.h"
#include "common/widgets/status_bar.h"
#include "system/system.h"
#include "system/system_res.h"
#include "system/system_notification.h"
#include "system/system_runtime_ui.h"
#include "system/system_timer.h"
#include "sys_adapter.h"
#include "app_lcd.h"

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

    data_node = mpack_node_map_cstr_optional(payload_node, "data");
    if (mpack_node_is_missing(data_node) || mpack_node_is_nil(data_node)) {
        floatair_dbg("----, data is missing or nil, let it go");
        data_node = mpack_tree_missing_node(&tree);
    } else if (mpack_node_type(data_node) != mpack_type_map) {
        floatair_err("----, data type err ");
        ret = app_mpack_send_ack(&mpackmsg, ErrDataErr);
        goto out;
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

typedef enum {
    ANCS_EVT_ADDED = 0,
    ANCS_EVT_MODIFIED = 1,
    ANCS_EVT_REMOVED = 2,
} ancs_event_id_t;

typedef enum {
    ANCS_CMD_GET_NTF_ATTR = 0,
    ANCS_CMD_GET_APP_ATTR = 1,
} ancs_cmd_id_t;

typedef enum {
    ANCS_ATTR_APP_IDENTIFIER = 0,
    ANCS_ATTR_TITLE = 1,
    ANCS_ATTR_SUBTITLE = 2,
    ANCS_ATTR_MESSAGE = 3,
    ANCS_ATTR_MESSAGE_SIZE = 4,
    ANCS_ATTR_DATE = 5,
} ancs_attr_id_t;

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

static bool system_handle_ancs_event(const JYT_ELF_MQ_MSG* msg) {
    if (!msg || msg->payload_len < 2) {
        return false;
    }

    const uint8_t* p = msg->payload;
    uint16_t len = msg->payload_len;
    uint8_t type = p[0];
    const uint8_t* data = &p[1];
    uint16_t data_len = len - 1;

    if (type == ANCS_FWD_TYPE_NOTIFICATION) {
        if (data_len < 8) {
            floatair_err("invalid ancs notification payload_len: %d", data_len);
            return false;
        }
        uint8_t evt_id = data[0];
        uint32_t uid = ancs_u32_le(&data[4]);
        uint32_t id = ancs_internal_notification_id(uid);

        if (evt_id == ANCS_EVT_REMOVED) {
            (void)system_notification_remove_id(id);
            return true;
        }

        floatair_info("ANCS ignore notification evt_id=%u uid=%" PRIu32 " id=%" PRIu32, evt_id, uid, id);
        return true;
    }

    if (type == ANCS_FWD_TYPE_DATA_SOURCE) {
        if (data_len < 1) {
            floatair_err("invalid ancs data source payload_len: %d", data_len);
            return false;
        }

        uint8_t cmd_id = data[0];
        if (cmd_id != ANCS_CMD_GET_NTF_ATTR) {
            floatair_info("ANCS ignore data source cmd_id=%u", cmd_id);
            return true;
        }
        if (data_len < 5) {
            floatair_err("invalid ancs data source payload_len: %d", data_len);
            return false;
        }

        uint32_t uid = ancs_u32_le(&data[1]);
        uint32_t id = ancs_internal_notification_id(uid);

        system_notification_entry_t entry = {0};
        entry.id = id;
        entry.mode = NOTIFY_MODE_MESSAGE;
        entry.duration_ms = 3000;
        entry.level = 1;
        entry.action = 1;

        char app_id[MSG_STR_MAX_LEN] = {0};
        char title[MSG_STR_MAX_LEN] = {0};
        char subtitle[MSG_STR_MAX_LEN] = {0};
        char message[MSG_STR_MAX_LEN] = {0};

        time_t parsed_time = 0;
        bool has_time = false;

        size_t offset = 5;
        while (offset + 3 <= data_len) {
            uint8_t attr_id = data[offset];
            uint16_t attr_len = ancs_u16_le(&data[offset + 1]);
            offset += 3;

            if (offset + attr_len > data_len) {
                break;
            }

            const uint8_t* attr_data = &data[offset];
            switch (attr_id) {
                case ANCS_ATTR_APP_IDENTIFIER:
                    ancs_copy_str(app_id, sizeof(app_id), attr_data, attr_len);
                    break;
                case ANCS_ATTR_TITLE:
                    ancs_copy_str(title, sizeof(title), attr_data, attr_len);
                    break;
                case ANCS_ATTR_SUBTITLE:
                    ancs_copy_str(subtitle, sizeof(subtitle), attr_data, attr_len);
                    break;
                case ANCS_ATTR_MESSAGE:
                    ancs_copy_str(message, sizeof(message), attr_data, attr_len);
                    break;
                case ANCS_ATTR_DATE:
                    has_time = ancs_parse_date_time(attr_data, attr_len, &parsed_time);
                    break;
                default:
                    break;
            }

            offset += attr_len;
        }

        if (title[0] == '\0' && app_id[0] != '\0') {
            ancs_copy_str(title, sizeof(title), (const uint8_t*)app_id, strlen(app_id));
        }

        if (subtitle[0] != '\0' && message[0] != '\0') {
            char merged[MSG_STR_MAX_LEN] = {0};
            snprintf(merged, sizeof(merged), "%s\n%s", subtitle, message);
            memcpy(message, merged, sizeof(message));
            message[MSG_STR_MAX_LEN - 1] = '\0';
        } else if (message[0] == '\0' && subtitle[0] != '\0') {
            memcpy(message, subtitle, sizeof(message));
            message[MSG_STR_MAX_LEN - 1] = '\0';
        }

        if (has_time) {
            entry.notify_time = parsed_time;
        } else {
            time(&entry.notify_time);
        }

        entry.title[0] = '\0';
        if (entry.mode == NOTIFY_MODE_CALL) {
            const char* call_text = message[0] != '\0' ? message : title;
            if (call_text && call_text[0] != '\0') {
                strncpy(entry.title, call_text, sizeof(entry.title) - 1);
                entry.title[sizeof(entry.title) - 1] = '\0';
            }
        } else if (title[0] != '\0' && message[0] != '\0') {
            snprintf(entry.title, sizeof(entry.title), "%s\n%s", title, message);
        } else if (title[0] != '\0') {
            strncpy(entry.title, title, sizeof(entry.title) - 1);
            entry.title[sizeof(entry.title) - 1] = '\0';
        } else if (message[0] != '\0') {
            strncpy(entry.title, message, sizeof(entry.title) - 1);
            entry.title[sizeof(entry.title) - 1] = '\0';
        }
        entry.has_title = entry.title[0] != '\0';

        return system_notification_add_entry(&entry);
    }

    floatair_info("ANCS ignore unknown type=%u payload_len=%u", type, (unsigned)msg->payload_len);
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
            ret = system_imu_event_convert_to_touch((uint8_t)event_type);
            break;
        }
        case SET_IMU_TILT:
        {
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
                toast_dismiss(APP_TOAST_ID_LOW_BATTERY);
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
            floatair_info("IED_WEAR_ON ignore[%d]", is_wear_on);

            // system_runtime_input_set_wearing_state(is_wear_on);
            // system_ui_set_wear_detection_visible(is_wear_on);
            // if (is_wear_on && system_get_sys_state() == 0) {
            //     system_set_sys_state(1);
            //     (void)system_report_sys_state(1);
            // }
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
            break;
        }
        case SET_TWS_LINK_BROKEN:
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
            const char* toast_text = app_get_str("TOAST_LOW_BATTERY_WARNING");

            floatair_info("low battery warning: battery=%u", (unsigned)system_get_battery());
            toast_cfg.id = APP_TOAST_ID_LOW_BATTERY;
            toast_cfg.duration_ms = 0;
            ret = (toast_show_with_cfg(toast_text, &toast_cfg) != NULL);
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
            ret = system_ui_refresh_screen_now();
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

/**
 * @brief Stack frame used for iterative MsgPack traversal
 */
typedef struct {
    mpack_node_t node; ///< current node
    int indent;        ///< indentation level
    bool is_key;
} dump_frame_t;

static bool ensure_stack(dump_frame_t** stack_ref, size_t* cap_ref, size_t needed) {
    if (needed <= *cap_ref) {
        return true;
    }
    size_t new_cap          = needed * 2;
    dump_frame_t* new_stack = (dump_frame_t*) realloc(*stack_ref, new_cap * sizeof(dump_frame_t));
    if (!new_stack) {
        return false;
    }
    *stack_ref = new_stack;
    *cap_ref   = new_cap;
    return true;
}

static void dump_scalar_node(mpack_node_t node, const char* pad, bool is_key) {
    switch (mpack_node_type(node)) {
        case mpack_type_nil:
            floatair_dbg("%snil", pad);
            break;
        case mpack_type_bool:
            floatair_dbg("%sbool: %s", pad, mpack_node_bool(node) ? "true" : "false");
            break;
        case mpack_type_int:
            floatair_dbg("%sint: %" PRId64, pad, (int64_t) mpack_node_int(node));
            break;
        case mpack_type_uint:
            floatair_dbg("%suint: %" PRIu64, pad, (uint64_t) mpack_node_uint(node));
            break;
        case mpack_type_float:
            floatair_dbg("%sfloat: %f", pad, (double) mpack_node_float(node));
            break;
        case mpack_type_double:
            floatair_dbg("%sdouble: %lf", pad, mpack_node_double(node));
            break;
        case mpack_type_str: {
            size_t len = mpack_node_strlen(node);
            const char* str = mpack_node_str(node);
            if (is_key) {
                floatair_dbg("%sstr: %.*s", pad, (int) len, str);
                break;
            }
            const size_t chunk_len = 64;
            floatair_dbg("%sstr (length: %zu):", pad, len);
            if (len == 0) {
                floatair_dbg("%s  ", pad);
                break;
            }
            for (size_t offset = 0; offset < len; offset += chunk_len) {
                size_t remain = len - offset;
                size_t n      = (remain > chunk_len) ? chunk_len : remain;
                floatair_dbg("%s  [%zu..%zu): %.*s", pad, offset, offset + n, (int) n, str + offset);
            }
            break;
        }
        case mpack_type_bin: {
            size_t len = mpack_node_bin_size(node);
            const char* bin = mpack_node_bin_data(node);
            if (is_key) {
                floatair_dbg("%sbin (length: %zu)", pad, len);
                break;
            }
            enum { bytes_per_line = 16 };
            floatair_dbg("%sbin (length: %zu):", pad, len);
            if (len == 0) {
                floatair_dbg("%s  ", pad);
                break;
            }
            for (size_t offset = 0; offset < len; offset += bytes_per_line) {
                size_t remain = len - offset;
                size_t n      = (remain > bytes_per_line) ? bytes_per_line : remain;
                char hex[bytes_per_line * 3];
                size_t pos = 0;
                for (size_t i = 0; i < n; i++) {
                    unsigned char b = (unsigned char) bin[offset + i];
                    int w = snprintf(hex + pos, sizeof(hex) - pos, "%02X ", b);
                    if (w < 0) {
                        break;
                    }
                    pos += (size_t) w;
                    if (pos >= sizeof(hex)) {
                        pos = sizeof(hex) - 1;
                        break;
                    }
                }
                if (pos > 0) {
                    hex[pos - 1] = '\0';
                } else {
                    hex[0] = '\0';
                }
                floatair_dbg("%s  [%zu..%zu): %s", pad, offset, offset + n, hex);
            }
            break;
        }
        default:
            floatair_dbg("%sunknown type: %d", pad, mpack_node_type(node));
            break;
    }
}

static void mpack_dump_node_iter(mpack_node_t root_node) {
    dump_frame_t* stack = NULL;
    size_t cap          = 64;
    size_t stack_size   = 0;
    stack               = (dump_frame_t*) malloc(cap * sizeof(dump_frame_t));
    floatair_assert(stack, "stack err");
    stack[stack_size++] = (dump_frame_t){.node = root_node, .indent = 0, .is_key = false};
    while (stack_size > 0) {
        dump_frame_t frame = stack[--stack_size];
        char pad[64];
        int pad_len = frame.indent * 2;
        pad_len     = (pad_len > (int) sizeof(pad) - 1) ? (int) sizeof(pad) - 1 : pad_len;
        memset(pad, ' ', (size_t) pad_len);
        pad[pad_len] = '\0';
        switch (mpack_node_type(frame.node)) {
            case mpack_type_array: {
                size_t count = mpack_node_array_length(frame.node);
                floatair_dbg("%sarray (size: %zu):", pad, count);
                if (!ensure_stack(&stack, &cap, stack_size + count)) {
                    break;
                }
                for (size_t i = count; i > 0; i--) {
                    mpack_node_t child  = mpack_node_array_at(frame.node, i - 1);
                    stack[stack_size++] = (dump_frame_t){
                        .node = child, .indent = frame.indent + 1, .is_key = false};
                }
                break;
            }
            case mpack_type_map: {
                size_t count = mpack_node_map_count(frame.node);
                floatair_dbg("%smap (size: %zu):", pad, count);
                if (!ensure_stack(&stack, &cap, stack_size + count * 2)) {
                    break;
                }
                for (size_t i = count; i > 0; i--) {
                    mpack_node_t key_node  = mpack_node_map_key_at(frame.node, i - 1);
                    mpack_node_t val_node  = mpack_node_map_value_at(frame.node, i - 1);
                    dump_frame_t val_frame = {.node = val_node, .indent = frame.indent + 2, .is_key = false};
                    dump_frame_t key_frame = {.node = key_node, .indent = frame.indent + 1, .is_key = true};
                    stack[stack_size++]    = val_frame;
                    stack[stack_size++]    = key_frame;
                }
                break;
            }
            default:
                dump_scalar_node(frame.node, pad, frame.is_key);
                break;
        }
    }
    free(stack);
}

// Top-level wrapper: dump entire MsgPack data
void app_msg_dump(char* msg, size_t msg_size, const char* tag) {
    mpack_node_t root;
    mpack_node_t payload;
    mpack_node_t data;

    if (!tag) {
        tag = "msg default";
    }
    // Create mpack parse tree
    // Create mpack parse tree
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, msg, msg_size);
    mpack_tree_parse(&tree);

    // Check parse errors
    if (mpack_tree_error(&tree) != mpack_ok) {
        floatair_err("MsgPack parse error: %s", mpack_error_to_string(mpack_tree_error(&tree)));
        mpack_tree_destroy(&tree);
        return;
    }
    floatair_info("######################begin#########################");
    floatair_info("###################%s######################", tag);

    root = mpack_tree_root(&tree);
    payload = mpack_node_map_cstr_optional(root, "payload");
    data = mpack_tree_missing_node(&tree);
    if (!mpack_node_is_missing(payload) && !mpack_node_is_nil(payload) &&
        mpack_node_type(payload) == mpack_type_map) {
        data = mpack_node_map_cstr_optional(payload, "data");
    }

    if (!mpack_node_is_missing(data) && !mpack_node_is_nil(data)) {
        mpack_dump_node_iter(data);
    } else if (!mpack_node_is_missing(payload) && !mpack_node_is_nil(payload)) {
        mpack_dump_node_iter(payload);
    } else {
        mpack_dump_node_iter(root);
    }

    floatair_info("######################end#####  ####################");
    // Free resources
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
