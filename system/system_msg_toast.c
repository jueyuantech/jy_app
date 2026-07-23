/**
 * @file system_msg_toast.c
 * @brief Toast 系统协议处理实现。
 */
#include "message.h"
#include "common/widgets/toast.h"

#include <string.h>

#define SYSTEM_TOAST_DEFAULT_DURATION_MS 3000U ///< Toast 默认显示时长。
#define SYSTEM_TOAST_TEXT_MAX_LEN MSG_STR_MAX_LEN ///< Toast 文本最大缓存长度。

/**
 * @brief 解析 Toast 显示位置。
 * @param[in] node 消息 data 节点。
 * @param[out] position 解析后的 Toast 位置。
 * @return 无返回值。
 */
static void system_toast_parse_position(mpack_node_t node, toast_position_t* position) {
    mpack_node_t position_node = mpack_node_map_cstr_optional(node, "position");
    uint32_t raw_position = 0;

    if (position == NULL) {
        return;
    }
    *position = TOAST_POSITION_CENTER;
    if (mpack_node_is_missing(position_node)) {
        position_node = mpack_node_map_cstr_optional(node, "postion");
    }
    if (mpack_node_is_missing(position_node) || mpack_node_is_nil(position_node) ||
        mpack_node_type(position_node) != mpack_type_uint) {
        return;
    }
    raw_position = mpack_node_u32(position_node);
    if (raw_position == TOAST_POSITION_TOP ||
        raw_position == TOAST_POSITION_CENTER ||
        raw_position == TOAST_POSITION_BOTTOM) {
        *position = (toast_position_t)raw_position;
    }
}

/**
 * @brief 解析 Toast 显示时长。
 * @param[in] node 消息 data 节点。
 * @return 返回合法显示时长，非法或缺省时返回默认值。
 */
static uint32_t system_toast_parse_duration(mpack_node_t node) {
    uint32_t duration_ms = SYSTEM_TOAST_DEFAULT_DURATION_MS;
    mpack_node_t duration_node = mpack_node_map_cstr_optional(node, "duration");

    if (mpack_node_is_missing(duration_node) || mpack_node_is_nil(duration_node)) {
        return duration_ms;
    }
    if (mpack_node_type(duration_node) == mpack_type_uint) {
        duration_ms = mpack_node_u32(duration_node);
    } else if (mpack_node_type(duration_node) == mpack_type_int) {
        int32_t signed_duration = mpack_node_i32(duration_node);
        if (signed_duration > 0) {
            duration_ms = (uint32_t)signed_duration;
        }
    }
    return duration_ms == 0 ? SYSTEM_TOAST_DEFAULT_DURATION_MS : duration_ms;
}

/**
 * @brief 处理 Toast.showToast 命令。
 * @param[in] node 消息 data 节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
static bool system_toast_show(mpack_node_t node, msg_pack_t* msg) {
    char text[SYSTEM_TOAST_TEXT_MAX_LEN] = {0};
    toast_cfg_t cfg = toast_default_cfg();

    if (msg == NULL) {
        return false;
    }
    if (!app_msg_get_str(node, "text", text, sizeof(text))) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    cfg.duration_ms = system_toast_parse_duration(node);
    system_toast_parse_position(node, &cfg.position);

    if (toast_show_with_cfg(text, &cfg) == NULL) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

app_cmd_func_t system_toast_cmd_funcs[] = {
    {"showToast", system_toast_show},
};
const size_t system_toast_cmd_funcs_count =
    sizeof(system_toast_cmd_funcs) / sizeof(system_toast_cmd_funcs[0]);
