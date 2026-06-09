/**
 * @file poweron.h
 * @brief Poweron 应用公共接口声明。
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

/** @defgroup app_poweron Poweron App @{ */

#include "system/system.h"
#include <lvgl/lvgl.h>

typedef struct app_page_t app_page_t;

/**
 * @brief 注册 Poweron 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool poweron_app_register(void);

const app_page_t* poweron_page_get(void);

/** @} */
#ifdef __cplusplus
}
#endif
