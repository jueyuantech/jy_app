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
#include <stddef.h>
#include <stdint.h>

#define SPEECH_FUNCTION_MENU_LABEL_MAX_LEN 64

/**
 * @brief 手机下发的单个 Speech 功能选项。
 */
typedef struct {
    uint32_t id;                                      ///< 手机侧定义的稳定选项 ID。
    char label[SPEECH_FUNCTION_MENU_LABEL_MAX_LEN];   ///< 眼镜端直接展示的选项文案。
} speech_function_menu_item_t;

/**
 * @brief 手机下发的 Speech 功能选择菜单。
 */
typedef struct {
    uint32_t menu_id;                                 ///< 手机侧定义的菜单实例/版本 ID。
    uint32_t selected_item_id;                        ///< 初始高亮选项 ID。
    size_t item_count;                                ///< 有效选项数量。
    speech_function_menu_item_t* items;               ///< 动态选项数组。
} speech_function_menu_t;

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
 * @brief 请求在下一次 LVGL 刷新节拍合并更新语音文本页面。
 *
 * 连续调用只会保留一个待执行任务，页面刷新时读取最新的 STT 缓冲区。
 *
 * @return 无返回值。
 */
void speech_stt_request_update(void);

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
 * @brief 激活语言提示并隐藏互斥的顶部文本。
 * @return 无返回值。
 */
void speech_activate_language_hint(void);

/**
 * @brief 更新语音文本页面底部操作提示状态。
 * @param[in] state 状态值，0 表示停止，1 表示开始。
 * @return `true` 表示状态合法并已处理，`false` 表示状态非法。
 */
bool speech_set_state(uint8_t state);

/**
 * @brief 更新 Speech 页面顶部文本。
 * @param[in] text 手机下发的提示或结果文本；隐藏时允许为 NULL。
 * @param[in] visible 是否显示顶部文本。
 * @return `true` 表示页面已更新，`false` 表示页面未就绪或文本无效。
 */
bool speech_set_header_text(const char* text, bool visible);

/**
 * @brief 显示手机协议驱动的功能选择遮罩。
 * @param[in] menu 完整菜单数据。
 * @return `true` 表示数据合法且已显示，`false` 表示页面未就绪或数据非法。
 */
bool speech_function_menu_show(const speech_function_menu_t* menu);

/**
 * @brief 隐藏功能选择遮罩并恢复当前 Speech 页面状态。
 * @return 无返回值。
 */
void speech_function_menu_hide(void);

/**
 * @brief 查询功能选择遮罩是否正在显示。
 * @return `true` 表示正在显示。
 */
bool speech_function_menu_is_visible(void);

/**
 * @brief 字体配置变化后刷新语音文本页面布局。
 * @return 无返回值。
 */
void speech_on_fontconfig_changed(void);

/**
 * @brief 气泡开关变化后刷新正文行间距和现有 STT 内容。
 * @return 无返回值。
 */
void speech_on_bordered_stt_changed(void);

#ifdef __cplusplus
}
#endif
