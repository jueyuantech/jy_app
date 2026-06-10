/**
 * @file system_runtime_misc.c
 * @brief 系统运行时轻量杂项接口实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system/system.h"

#include "system/popups/assistant/assistant.h"
#include "system/popups/notify_list/notify_list.h"

#include <stdbool.h>
#include <string.h>

/**
 * @brief 判断命令是否命中给定白名单。
 * @param[in] cmd 待匹配命令名。
 * @param[in] allow_list 白名单数组。
 * @param[in] count 白名单长度。
 * @return `true` 表示命中，`false` 表示未命中。
 */
static bool system_runtime_misc_cmd_in_list(const char* cmd, const char* const* allow_list, size_t count) {
    if (cmd == NULL || allow_list == NULL) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (strcmp(cmd, allow_list[i]) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 判断 popup 期间是否允许处理指定 SystemControl 命令。
 * @param[in] msg 已解析的 host 消息头。
 * @param[in] extra_cmds 当前 popup 额外允许的 SystemControl 命令名单。
 * @param[in] extra_count `extra_cmds` 的命令数量。
 * @return `true` 表示允许继续处理，`false` 表示应直接返回 `ErrNotReady`。
 */
static bool system_runtime_misc_system_control_allowed(const msg_pack_t* msg,
                                                       const char* const* extra_cmds,
                                                       size_t extra_count) {
    static const char* const common_cmds[] = {
        "getView",
        "sendTouchEvent",
        "sendHeartbeat",
        "sendKeepAlive",
    };

    if (msg == NULL || msg->id != APP_MSG_ID_SYSTEM) {
        return false;
    }
    if (strcmp(msg->biz, "SystemControl") != 0) {
        return false;
    }

    return system_runtime_misc_cmd_in_list(msg->cmd,
                                           common_cmds,
                                           sizeof(common_cmds) / sizeof(common_cmds[0])) ||
           system_runtime_misc_cmd_in_list(msg->cmd, extra_cmds, extra_count);
}

/**
 * @brief 判断 assistant popup 激活时是否允许处理指定消息。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示允许处理，`false` 表示应直接返回 `ErrNotReady`。
 */
static bool system_runtime_misc_assistant_msg_allowed(const msg_pack_t* msg) {
    static const char* const assistant_cmds[] = {
        "openAssistant",
        "updateAssistantSttInfo",
        "closeAssistant",
    };

    return system_runtime_misc_system_control_allowed(
        msg,
        assistant_cmds,
        sizeof(assistant_cmds) / sizeof(assistant_cmds[0]));
}

/**
 * @brief 判断 notify_list popup 激活时是否允许处理指定消息。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示允许处理，`false` 表示应直接返回 `ErrNotReady`。
 */
static bool system_runtime_misc_notify_list_msg_allowed(const msg_pack_t* msg) {
    static const char* const notification_cmds[] = {
        "addNotification",
        "removeNotification",
    };

    return system_runtime_misc_system_control_allowed(msg, NULL, 0) ||
           (msg != NULL &&
            msg->id == APP_MSG_ID_SYSTEM &&
            strcmp(msg->biz, "Notification") == 0 &&
            system_runtime_misc_cmd_in_list(
                msg->cmd,
                notification_cmds,
                sizeof(notification_cmds) / sizeof(notification_cmds[0])));
}

/**
 * @brief 返回系统心跳处理结果。
 * @return 始终返回 `true`。
 */
bool system_heart_beat(void) {
    return true;
}

/**
 * @brief 返回系统保活处理结果。
 * @return 始终返回 `true`。
 */
bool system_keep_alive(void) {
    return true;
}

/**
 * @brief 判断当前 popup 状态下是否允许处理指定 host 消息。
 * @param[in] msg 已解析的 host 消息头。
 * @return `true` 表示允许继续处理，`false` 表示应直接返回 `ErrNotReady`。
 */
bool system_host_message_allowed_when_popup_active(const msg_pack_t* msg) {
    if (msg == NULL) {
        return false;
    }

    if (assistant_is_open()) {
        return system_runtime_misc_assistant_msg_allowed(msg);
    }
    if (notify_list_is_open()) {
        return system_runtime_misc_notify_list_msg_allowed(msg);
    }

    return true;
}
