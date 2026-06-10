/**
 * @file gallery.h
 * @brief Gallery 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include "common/app_framework/app_manager.h"

/** @defgroup app_gallery Gallery App @{ */

typedef struct app_page_t app_page_t;

#include "system/system.h"
#include <lvgl/lvgl.h>

void gallery_cfg_init(void);

/**
 * @brief 注册 Gallery 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool gallery_app_register(void);

/**
 * @brief Route gallery command
 * @param node mpack node
 * @param msg message pack
 * @return true on success
 */
bool gallery_route_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief Update gallery picture
 * @param img image path
 * @return true on success
 */
bool gallery_update_pic(const char* img);

/**
 * @brief Update gallery picture folder
 */
void gallery_update_pic_folder(void);

/**
 * @brief Clear gallery view
 */
void gallery_clear_view(void);

/**
 * @brief 获取 Gallery 页面描述符。
 * @return 返回 Gallery 页面描述符。
 */
const app_page_t* gallery_page_get(void);

/** @} */
#ifdef __cplusplus
}
#endif
