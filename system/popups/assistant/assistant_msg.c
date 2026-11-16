/**
 * @file assistant_msg.c
 * @brief Assistant 弹窗的 SystemControl 消息处理。
 */
#include "assistant.h"

#include "app_lcd.h"
#include "common/app_framework/app_manager.h"
#include "common/stt/stt_view_common.h"
#include "message.h"
#include "system/stt_common.h"

enum {
    ASSISTANT_STT_AREA_ANSWER = 0,
    ASSISTANT_STT_AREA_QUESTION = 1,
    ASSISTANT_STT_MSG_TYPE_HINT = 2,
};

static stt_view_deferred_update_t s_assistant_stt_deferred_update;

static void assistant_stt_deferred_update_cb(void* user_data) {
    (void)user_data;
    assistant_stt_update();
}

/**
 * @brief 处理打开 assistant 弹窗命令。
 * @param[in] node 消息 payload 数据节点，本命令不使用。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_open_cmd(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");

    if (!assistant_open(false)) {
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
    uint8_t msg_type = 0;

    floatair_assert(msg != NULL, "msg is NULL");
    was_open = assistant_is_open();

    (void)app_msg_get_u8(node, true, "area", &area);
    (void)app_msg_get_u8(node, false, "msgType", &msg_type);

    if (!was_open) {
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    ret = stt_update_sttinfo(node, msg);
    if (ret || stt_update_sttinfo_was_skipped()) {
        if (ret && area == ASSISTANT_STT_AREA_ANSWER &&
            msg_type == ASSISTANT_STT_MSG_TYPE_HINT && stt_size() > 0 &&
            stt_buffer_get_area_by_index(0) == ASSISTANT_STT_AREA_ANSWER &&
            stt_buffer_get_msg_type_by_index(0) == ASSISTANT_STT_MSG_TYPE_HINT &&
            stt_buffer_get_transcribe_by_index(0) != NULL) {
            stt_buffer_remove_area(ASSISTANT_STT_AREA_QUESTION);
        }
        assistant_stt_note_update(area);
        stt_view_request_deferred_update(&s_assistant_stt_deferred_update,
                                         "assistant",
                                         assistant_stt_deferred_update_cb,
                                         NULL);
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
    lv_obj_t* page_root = NULL;

    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");

    stt_view_cancel_deferred_update(&s_assistant_stt_deferred_update);
    if (!assistant_close(false)) {
        floatair_err("assistant close failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    page_root = app_manager_current_content_root();
    if (page_root != NULL) {
        (void)lv_obj_send_event(page_root, assistant_get_close_event(), NULL);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}
