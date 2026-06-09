/**
 * @file ota.h
 * @brief OTA 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-03-14
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include "common/app_framework/app_manager.h"

/** @defgroup app_ota OTA App @{ */

#include "system/system.h"
#include <lvgl/lvgl.h>

typedef struct app_page_t app_page_t;

/**
 * @brief 注册 OTA 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool ota_app_register(void);

/**
 * @brief 获取 OTA 页面描述符。
 * @return 返回 OTA 页面描述符。
 */
const app_page_t* ota_page_get(void);

/**
 * @brief Route OTA command
 * @param node mpack node
 * @param msg message pack
 * @return true on success
 */
bool ota_route_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief Update OTA progress bar
 * @param progress Progress value (0-100)
 */
void ota_update_progress(uint8_t progress);

/** @} */
#ifdef __cplusplus
}
#endif
