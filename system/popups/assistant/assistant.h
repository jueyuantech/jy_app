/**
 * @file assistant.h
 * @brief Assistant 弹窗模块公共接口声明。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include <lvgl/lvgl.h>

/**
 * @brief 打开 assistant 弹窗层。
 * @return `true` 表示打开成功，`false` 表示打开失败。
 */
bool assistant_open(void);
/**
 * @brief 关闭 assistant 弹窗层。
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return `true` 表示关闭成功，`false` 表示关闭失败。
 */
bool assistant_close(bool report_close);
/**
 * @brief 查询 assistant 弹窗层是否正在显示。
 * @return `true` 表示正在显示，`false` 表示未显示。
 */
bool assistant_is_open(void);
/**
 * @brief 处理 assistant 弹窗输入事件。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示事件已被 assistant 弹窗消费，`false` 表示未消费。
 */
bool assistant_handle_event(lv_event_code_t code);

/**
 * @brief 处理 SystemControl.openAssistant 命令。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_open_cmd(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief 处理 SystemControl.updateAssistantSttInfo 命令。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_update_stt_info_cmd(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief 处理 SystemControl.closeAssistant 命令。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_close_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief 在 popup 层创建 assistant 视图。
 * @return `true` 表示创建成功，`false` 表示创建失败。
 */
bool assistant_popup_open(void);
/**
 * @brief 销毁 assistant popup 视图。
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return `true` 表示销毁成功，`false` 表示销毁失败。
 */
bool assistant_popup_close(bool report_close);
/**
 * @brief 通知 assistant popup 已被删除。
 *
 * 用于 popup 被外部对象树删除时补做生命周期清理，确保 STT 快照能恢复。
 *
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return 无返回值。
 */
void assistant_on_popup_deleted(bool report_close);

void assistant_stt_update(void);
/**
 * @brief 记录最近一次 assistant 文本更新的协议字段，用于区分当前问答区域。
 * @param[in] area 文本区域。
 * @return 无返回值。
 */
void assistant_stt_note_update(uint8_t area);
void assistant_stt_clear(void);

#ifdef __cplusplus
}
#endif
