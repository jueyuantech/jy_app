/**
 * @file guide.h
 * @brief Guide 应用公共接口声明。
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

/** @defgroup app_guide Guide App @{ */

#include "system/system.h"
#include <lvgl/lvgl.h>

typedef struct app_page_t app_page_t;

/**
 * @brief 注册 Guide 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool guide_app_register(void);

/**
 * @brief 获取 Guide 页面描述符。
 * @return 返回 Guide 页面描述符。
 */
const app_page_t* guide_page_get(void);

/** @} */
#ifdef __cplusplus
}
#endif
