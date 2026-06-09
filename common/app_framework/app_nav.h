/**
 * @file app_nav.h
 * @brief App 内页面导航门面接口。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-21
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#pragma once

#include "common/app_framework/app_manager.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 当前 App 压入新页面。
 * @param[in] page 页面描述符。
 * @param[in] data 页面入参；无入参传 `NULL`。
 * @param[in] size 页面入参长度。
 * @return `true` 表示压栈成功，`false` 表示压栈失败。
 */
bool app_nav_push(app_page_t* page, const void* data, size_t size);

/**
 * @brief 当前 App 弹出当前页面。
 * @return `true` 表示弹栈成功，`false` 表示弹栈失败。
 */
bool app_nav_pop(void);

/**
 * @brief 当前 App 替换当前页面。
 * @param[in] page 页面描述符。
 * @param[in] data 页面入参；无入参传 `NULL`。
 * @param[in] size 页面入参长度。
 * @return `true` 表示替换成功，`false` 表示替换失败。
 */
bool app_nav_replace(app_page_t* page, const void* data, size_t size);

/**
 * @brief 当前 App 处理返回事件。
 * @return `true` 表示返回事件已处理，`false` 表示未处理。
 */
bool app_nav_back(void);

#ifdef __cplusplus
}
#endif
