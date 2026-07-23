/**
 * @file speech.h
 * @brief Speech 应用公共接口声明。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include "common/app_framework/app_manager.h"
#include "speech_view.h"

#define SPEECH_CONFIG_APP_NAME "speech" ///< Speech 共享配置目录名称。

/**
 * @brief Speech 外部入口配置。
 */
typedef struct {
    const char* app_name;          ///< App 名称。
    uint32_t msg_id;               ///< 消息 ID。
    speech_view_mode_t view_mode;  ///< 页面业务模式。
} speech_app_entry_t;

/**
 * @brief 注册 Speech 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool speech_app_register(void);

/**
 * @brief 路由 Speech 命令。
 * @param[in] node 消息节点。
 * @param[in] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool speech_route_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief 按 App 名称获取 Speech 外部入口配置。
 * @param[in] app_name App 名称。
 * @return 返回入口配置；未匹配时返回 NULL。
 */
const speech_app_entry_t* speech_entry_from_app_name(const char* app_name);

/**
 * @brief 按消息 ID 获取 Speech 外部入口配置。
 * @param[in] msg_id 消息 ID。
 * @return 返回入口配置；未匹配时返回 NULL。
 */
const speech_app_entry_t* speech_entry_from_msg_id(uint32_t msg_id);

/**
 * @brief 获取 Speech 双击退出是否需要二次确认。
 * @return `true` 表示弹出确认框，`false` 表示直接退出。
 */
bool speech_exit_double_click_confirm_enabled(void);

#ifdef __cplusplus
}
#endif
