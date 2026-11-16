/**
 * @file speech_runtime.h
 * @brief Speech 应用公共接口声明。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include "common/app_framework/app_manager.h"
#include "common/speech/speech_view.h"

/**
 * @brief Speech 外部入口配置。
 */
typedef struct {
    const char* app_name;          ///< App 名称。
    const char* config_name;       ///< 该逻辑 App 独立配置所在的 LFSD App 目录名。
    uint32_t msg_id;               ///< 消息 ID。
    const char* exit_message_key;  ///< 退出确认文案键。
    bool stt_icons;                ///< 是否显示固定的左右角色图标。
} speech_app_profile_t;

/**
 * @brief 向共享 Speech 运行时注册一个逻辑 App 入口。
 * @param[in] profile Speech 入口配置，实际类型为 `speech_app_profile_t`。
 * @return `true` 表示注册成功，`false` 表示配置无效或注册失败。
 */
bool speech_runtime_register(const void* profile);

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
const speech_app_profile_t* speech_profile_from_app_name(const char* app_name);

/**
 * @brief 按消息 ID 获取 Speech 外部入口配置。
 * @param[in] msg_id 消息 ID。
 * @return 返回入口配置；未匹配时返回 NULL。
 */
const speech_app_profile_t* speech_profile_from_msg_id(uint32_t msg_id);

/**
 * @brief 获取 Speech 双击退出是否需要二次确认。
 * @return `true` 表示弹出确认框，`false` 表示直接退出。
 */
bool speech_exit_double_click_confirm_enabled(void);

/**
 * @brief 获取 Speech 双击是否仅上报给手机处理。
 * @return `true` 表示只上报触摸事件，不在眼镜端执行退出逻辑。
 */
bool speech_report_double_click_enabled(void);

/**
 * @brief 获取当前 Speech 应用配置的左右带框气泡开关。
 * @return `true` 表示按消息区域左右显示带框气泡。
 */
bool speech_bordered_stt_enabled(void);

/**
 * @brief 修改当前 Speech 会话的左右带框气泡开关，并刷新页面。
 * @param[in] enabled `true` 启用左右气泡，`false` 使用无框正文。
 * @return 无返回值。
 */
void speech_set_bordered_stt_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
