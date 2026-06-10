/**
 * @file ai.h
 * @brief AI 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup app_ai AI App @{ */

typedef struct app_page_t app_page_t;

#include "common/app_framework/app_manager.h"
#include "floatair_fs.h"
#include "message.h"
#include "system/system.h"
#include <lvgl/lvgl.h>

/**
 * @brief 注册 AI 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool ai_app_register(void);

/**
 * @brief 路由 AI 应用命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool ai_route_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief 刷新 AI STT 文本显示。
 * @return 无返回值。
 */
void ai_stt_update(void);

/**
 * @brief 记录 AI STT 文本更新对应的对话区域。
 * @param[in] area 文本区域，0 为回答、1 为提问。
 * @param[in] msg_type 消息类型，0 为正式文本。
 * @return 无返回值。
 */
void ai_stt_note_update(uint8_t area, uint8_t msg_type);

/**
 * @brief 清空 AI STT 文本显示。
 * @return 无返回值。
 */
void ai_stt_clear(void);

/**
 * @brief 处理 AI 字体配置变更。
 * @return 无返回值。
 */
void ai_on_fontconfig_changed(void);

/**
 * @brief 获取 AI 页面描述符。
 * @return 返回 AI 页面描述符。
 */
const app_page_t* ai_page_get(void);

/** @} */

#ifdef __cplusplus
}
#endif
