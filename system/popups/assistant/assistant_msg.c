/**
 * @file assistant_msg.c
 * @brief Assistant 弹窗的 SystemControl 消息处理。
 */
#include "assistant.h"

#include "app_lcd.h"
#include "message.h"
#include "system/stt_common.h"

/**
 * @brief 处理打开 assistant 弹窗命令。
 * @param[in] node 消息 payload 数据节点，本命令不使用。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_open_cmd(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");

    if (!assistant_open()) {
        floatair_err("assistant open failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理 assistant 弹窗 STT 文本更新命令。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_update_stt_info_cmd(mpack_node_t node, msg_pack_t* msg) {
    bool was_open = false;
    bool ret = false;
    uint8_t area = 0;

    floatair_assert(msg != NULL, "msg is NULL");
    was_open = assistant_is_open();

    (void)app_msg_get_u8(node, true, "area", &area);

    if (!was_open) {
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    ret = stt_update_sttinfo(node, msg);
    if (ret && !stt_update_sttinfo_was_skipped()) {
        assistant_stt_note_update(area);
        assistant_stt_update();
    }
    return ret;
}

/**
 * @brief 处理关闭 assistant 弹窗命令。
 * @param[in] node 消息 payload 数据节点，本命令不使用。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_close_cmd(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");

    if (!assistant_close(false)) {
        floatair_err("assistant close failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}
