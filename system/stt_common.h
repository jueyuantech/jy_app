/**
 * @file stt_common.h
 * @brief STT interaction common interfaces and data structures
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/** @ingroup app_system */

#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "mpack.h"
#include "system/system.h"

#include <stdio.h>

#define TRANSMODE_SHOW_ONLY_TRANS                   0  ///< show only translation
#define TRANSMODE_SHOW_DUAL                         1  ///< show dual translation
#define TRANSMODE_SHOW_ONLY_ORI                     2  ///< show only original text

#define TEXTMODE_DEFAULT                            0 ///< default text mode
#define TEXTMODE_HISTORY                            1 ///< history text mode
#define TEXTMODE_MEETING                            2 ///< meeting text mode

#define AUDIOSOURCE_GLASSES                         0  ///< audio source glasses
#define AUDIOSOURCE_PHONE                           1  ///< audio source phone

#define OMNIDIRECTIONAL                            0  ///< omnidirectional
#define DIRECTIONAL                                1  ///< directional

#define TEXT_DIRECTION_LTR                            0  ///< left to right direction
#define TEXT_DIRECTION_RTL                            1  ///< right to left direction

/**
 * @brief STT information structure
 *
 * Holds context of a speech-to-text and translation session
 */
#define STT_INFO_MAX_MSG_NUM 6
typedef struct {
    uint64_t createdAt; ///< Unix 秒级创建时间戳。
    uint32_t id;        ///< record ID
    uint32_t msgIdLen;  ///< message ID length
    uint32_t userLen;   ///< user length
    uint32_t transcribeLen; ///< transcription text length
    uint32_t translateLen; ///< translation text length
    char* msgId;        ///< message ID
    char* user;         ///< user
    char* transcribe;   ///< transcription text
    char* translate;    ///< translation text
    uint8_t area;       ///< area
    uint8_t msgType;    ///< message type
    uint8_t actionType; ///< action type
    uint8_t isFinal;    ///< whether final result
} stt_info_t;

/**
 * @brief STT 公共显示配置结构。
 *
 * 保存转写、翻译和 assistant 等 STT 视图共享的显示状态。
 */
 #define STT_CONFIG_MAX_LANGUAGE_LEN 64
 typedef struct {
    char language_source[STT_CONFIG_MAX_LANGUAGE_LEN]; ///< 源语言名称。
    char language_target[STT_CONFIG_MAX_LANGUAGE_LEN]; ///< 目标语言名称。
    uint8_t transMode; ///< 原文/译文显示模式。
    uint8_t textMode; ///< 文本展示模式。
    uint8_t micDirectional; ///< 麦克风指向模式。
    uint8_t language_hint; ///< 语言提示开关。
    uint8_t audioSourceIndicator; ///< 音频来源指示。
    uint8_t sourceTextDirection; ///< 源语言文本方向。
    uint8_t targetTextDirection; ///< 目标语言文本方向。
 } stt_config_t;

extern stt_config_t stt_config;
extern lv_style_t stt_stylecur;
extern lv_style_t stt_stylehis;
extern lv_style_t stt_stylebolder;

/**
 * @brief STT 服务全局状态快照。
 *
 * 用于临时 popup 独占 STT 全局资源时挂起原页面状态，恢复时会原样归还字体配置、
 * 文本缓冲和显示配置。
 */
typedef struct {
    stt_config_t config;                 ///< 被挂起的 STT 显示配置。
    uint32_t font_size;                  ///< 被挂起的实际字号。
    stt_info_t* buffer;                  ///< 被挂起的 STT 文本缓冲。
} stt_service_snapshot_t;

/**
 * @brief Get current STT buffer size
 * @return number of non-empty entries
 */
int stt_size(void);

/**
 * @brief 设置最近一次从应用消息队列读取到的待处理深度。
 * @param[in] pending Q-8 中仍等待消费的消息数量；负数表示未知。
 * @return 无返回值。
 */
void stt_set_flow_queue_pending(int pending);

/**
 * @brief Set font configuration
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @param[in] config_file configuration file
 * @return true on success
 */
bool stt_set_fontconfig(mpack_node_t node, msg_pack_t* msg, char* config_file);
/**
 * @brief Update STT information
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @param[out] stt_info STT information
 * @return true on success
 */
bool stt_update_sttinfo(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief 查询最近一次 `stt_update_sttinfo()` 是否因流控跳过页面操作。
 * @return `true` 表示最近一次 STT 数据已写入缓冲但页面操作被流控吸收，`false` 表示页面正常处理。
 */
bool stt_update_sttinfo_was_skipped(void);

/**
 * @brief Set text mode
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_textmode(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Set audio track state
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_audiotrackstate(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Set translation mode
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_transmode(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Set maximum lines
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_maxline(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Set audio source indicator
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_audiosourceindicator(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Set microphone directionality
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_micdirectional(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Set language hints
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_languagehint(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Set text direction
 * @param[in] node mpack node
 * @param[in,out] msg message pack
 * @return true on success
 */
bool stt_set_textdirection(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief Initialize STT service
 */
void stt_service_init(char *config_file);

/**
 * @brief Deinitialize STT service
 */
void stt_service_deinit(void);

/**
 * @brief 初始化 STT 服务状态快照。
 * @param[out] snapshot 快照对象。
 * @return 无返回值。
 */
void stt_service_snapshot_init(stt_service_snapshot_t* snapshot);
/**
 * @brief 挂起当前 STT 服务全局状态。
 * @param[out] snapshot 用于保存当前状态的快照对象。
 * @return `true` 表示挂起成功，`false` 表示参数无效或快照已被占用。
 */
bool stt_service_suspend(stt_service_snapshot_t* snapshot);
/**
 * @brief 恢复此前挂起的 STT 服务全局状态，并释放当前临时 STT 服务资源。
 * @param[in,out] snapshot 待恢复的快照对象。
 * @return 无返回值。
 */
void stt_service_resume(stt_service_snapshot_t* snapshot);

/**
 * @brief 获取当前 STT 使用的全局字体。
 * @return 返回全局字体指针；未初始化时返回 `NULL`。
 */
const lv_font_t* stt_get_font(void);

/**
 * @brief 获取当前 STT 使用的实际字号。
 * @return 返回已解析到全局字体表的字号。
 */
uint32_t stt_get_font_size(void);

/**
 * @brief Get STT font height
 * @return font height
 */
uint32_t stt_get_font_height(void);

/**
 * @brief Initialize STT buffer
 */
void stt_buffer_init(void);
/**
 * @brief Deinitialize STT buffer
 */
void stt_buffer_deinit(void);

/**
 * @brief Dump STT buffer
 */
void stt_buffer_dump(void);

/**
 * @brief Push STT info to buffer
 * @param[in,out] info STT info，成功写入后字符串所有权会转移到内部缓冲并清空该结构。
 * @return true on success
 */
bool stt_buffer_push(stt_info_t* info);

/**
 * @brief Get translate by index
 * @param[in] index index
 * @return translate
 */
const char* stt_buffer_get_translate_by_index(size_t index);

/**
 * @brief Get transcribe by index
 * @param[in] index index
 * @return transcribe
 */

const char* stt_buffer_get_transcribe_by_index(size_t index);
/**
 * @brief Get area by index
 * @param[in] index index
 * @return area
 */
uint32_t stt_buffer_get_area_by_index(size_t index);
/**
 * @brief Get is final by index
 * @param[in] index index
 * @return is final
 */
bool stt_buffer_get_is_final_by_index(size_t index);
/**
 * @brief Get message type by index
 * @param[in] index index
 * @return message type
 */
uint32_t stt_buffer_get_msg_type_by_index(size_t index);
/**
 * @brief Get action type by index
 * @param[in] index index
 * @return action type
 */
uint32_t stt_buffer_get_act_type_by_index(size_t index);

/**
 * @brief Initialize STT styles
 */
void stt_style_init(void);

/**
 * @brief Deinitialize STT styles
 */
void stt_style_deinit(void);

#ifdef __cplusplus
}
#endif
