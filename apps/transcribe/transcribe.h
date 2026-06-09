/**
 * @file transcribe.h
 * @brief Transcribe 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup app_transcribe Transcribe App @{ */

typedef struct app_page_t app_page_t;

#include "message.h"
#include "common/app_framework/app_manager.h"
#include "system/system.h"
#include <lvgl/lvgl.h>

/**
 * @brief 注册 Transcribe 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool transcribe_app_register(void);

/**
 * @brief Route transcribe command
 * @param node message node
 * @return true route succeeded
 * @return false route failed
 */
bool transcribe_route_cmd(mpack_node_t node, msg_pack_t* msg);  

/**
 * @brief Update STT information
 */
void transcribe_stt_update(void);

/**
 * @brief Clear STT view
 */
void transcribe_stt_clear(void);

/**
 * @brief Update language hint
 */
void transcribe_update_lang_hint(void);

void transcribe_on_fontconfig_changed(void);

/**
 * @brief 获取 Transcribe 页面描述符。
 * @return 返回 Transcribe 页面描述符。
 */
const app_page_t* transcribe_page_get(void);


/** @} */
#ifdef __cplusplus
}
#endif
