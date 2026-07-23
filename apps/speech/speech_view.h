/**
 * @file speech_view.h
 * @brief 语音文本业务页面公共接口声明。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common/app_framework/app_manager.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 语音文本页面业务模式。
 */
typedef enum {
    SPEECH_VIEW_MODE_TRANSCRIBE = 0, ///< 转写页面模式。
    SPEECH_VIEW_MODE_TRANSLATE = 1,  ///< 翻译页面模式。
    SPEECH_VIEW_MODE_ONLINE_CHAT = 2, ///< 在线聊天页面模式。
} speech_view_mode_t;

/**
 * @brief 获取语音文本页面描述符。
 * @return 返回页面描述符。
 */
const app_page_t* speech_page_get(void);

/**
 * @brief 刷新语音文本页面的 STT 内容。
 * @return 无返回值。
 */
void speech_stt_update(void);

/**
 * @brief 清空语音文本页面的 STT 内容。
 * @return 无返回值。
 */
void speech_stt_clear(void);

/**
 * @brief 刷新语音文本页面的语言提示。
 * @return 无返回值。
 */
void speech_update_lang_hint(void);

/**
 * @brief 更新语音文本页面底部操作提示状态。
 * @param[in] state 状态值，0 表示停止，1 表示开始。
 * @return `true` 表示状态合法并已处理，`false` 表示状态非法。
 */
bool speech_set_state(uint8_t state);

/**
 * @brief 字体配置变化后刷新语音文本页面布局。
 * @return 无返回值。
 */
void speech_on_fontconfig_changed(void);

#ifdef __cplusplus
}
#endif
