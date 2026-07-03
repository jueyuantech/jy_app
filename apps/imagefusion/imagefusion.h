/**
 * @file imagefusion.h
 * @brief Imagefusion 应用公共接口声明。
 * @author jytek
 * @version 1.0.0
 * @date 2026-06-16
 * @copyright JYTek
 * @ingroup app_imagefusion
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common/app_framework/app_manager.h"
#include "message.h"

/**
 * @brief 注册 Imagefusion 到 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool imagefusion_app_register(void);

/**
 * @brief 获取 Imagefusion 页面描述符。
 * @return 返回 Imagefusion 页面描述符。
 */
const app_page_t* imagefusion_page_get(void);

/**
 * @brief 路由 Imagefusion 手机端命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool imagefusion_route_cmd(mpack_node_t node, msg_pack_t* msg);

#ifdef __cplusplus
}
#endif
