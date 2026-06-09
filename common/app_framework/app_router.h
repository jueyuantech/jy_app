/**
 * @file app_router.h
 * @brief App framework 路由门面接口声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-05-06
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief App 进入方式。
 */
typedef enum {
    APP_ROUTER_ENTRY_LOCAL = 0,  ///< 本地进入
    APP_ROUTER_ENTRY_REMOTE = 1, ///< 远端拉起
} app_router_entry_t;

/**
 * @brief 初始化App framework 路由。
 * @return `true` 表示初始化成功，`false` 表示初始化失败。
 */
bool app_router_init(void);

/**
 * @brief 反初始化App framework 路由，并释放 App framework 运行状态。
 * @return `true` 表示反初始化成功，`false` 表示当前忙碌。
 */
bool app_router_deinit(void);

/**
 * @brief 重置App framework 路由状态。
 * @return 无返回值。
 */
void app_router_reset_state(void);

/**
 * @brief 判断App framework 路由是否忙碌。
 * @return `true` 表示忙碌，`false` 表示空闲。
 */
bool app_router_is_busy(void);

/**
 * @brief 根据当前配置进入首页应用。
 * @return `true` 表示进入成功，`false` 表示进入失败。
 */
bool app_router_call_home(void);

/**
 * @brief 退出当前应用并返回首页。
 * @return `true` 表示退出成功，`false` 表示退出失败。
 */
bool app_router_exit_current_app(void);

/**
 * @brief 获取当前显示的应用名称。
 * @return 返回当前应用名称字符串。
 */
const char* app_router_get_app(void);

/**
 * @brief 切换到目标应用。
 * @param[in] targetapp 目标应用名称。
 * @param[in] mode 本次进入方式。
 * @return `true` 表示切换成功，`false` 表示切换失败。
 */
bool app_router_set_app(const char* targetapp, app_router_entry_t mode);

/**
 * @brief 设置当前应用进入方式。
 * @param[in] entry_mode 目标进入方式。
 * @return 无返回值。
 */
void app_router_set_entry_mode(app_router_entry_t entry_mode);

/**
 * @brief 获取当前应用进入方式。
 * @return 返回当前应用进入方式。
 */
app_router_entry_t app_router_get_entry_mode(void);

#ifdef __cplusplus
}
#endif
