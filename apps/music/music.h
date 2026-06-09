/**
 * @file music.h
 * @brief Music 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup app_music Music App @{ */

#include "message.h"
#include "common/app_framework/app_manager.h"
#include "system/system.h"
#include "floatair_fs.h"
#include <lvgl/lvgl.h>

typedef struct app_page_t app_page_t;

/**
 * @brief 注册 Music 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool music_app_register(void);

/**
 * @brief 获取 Music 页面描述符。
 * @return 返回 Music 页面描述符。
 */
const app_page_t* music_page_get(void);

/**
 * @brief Route music command
 * @param node Message node
 * @param msg Message pack instance
 * @return true Message parsed successfully
 * @return false Message parsed failed
 */
bool music_route_cmd(mpack_node_t node, msg_pack_t* msg);

void music_avrcp_clear(void);
void music_avrcp_update_lyric(const char* text);

/** @} */
#ifdef __cplusplus
}
#endif
