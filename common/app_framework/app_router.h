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
#include <stdint.h>

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
 * @brief 上位机平台类型。
 */
typedef enum {
    APP_ROUTER_APP_PLATFORM_NONE = 0,    ///< 尚未收到上位机平台配置。
    APP_ROUTER_APP_PLATFORM_ANDROID = 1, ///< Android 上位机。
    APP_ROUTER_APP_PLATFORM_IOS = 2,     ///< iOS 上位机。
    APP_ROUTER_APP_PLATFORM_MACOS = 3,   ///< macOS 上位机。
    APP_ROUTER_APP_PLATFORM_WINDOWS = 4, ///< Windows 上位机。
    APP_ROUTER_APP_PLATFORM_WATCH = 5,   ///< 手表上位机。
} app_router_app_platform_t;

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
 * @brief 获取当前路由配置解析出的首页应用名称。
 * @return 返回当前应作为首页的应用名称字符串。
 */
const char* app_router_get_home_viewname(void);

/**
 * @brief 应用上位机平台配置并按平台进入初始应用。
 * @param[in] app_platform 上位机平台类型，取值见 `app_router_app_platform_t`。
 * @return `true` 表示配置生效并完成路由决策，`false` 表示平台非法或路由失败。
 */
bool app_router_apply_app_config(uint32_t app_platform);

/**
 * @brief 设置产品配置中的默认上位机平台，但不主动进入首页应用。
 * @param[in] platform 上位机平台名称，空字符串表示等待 `setAppConfig`。
 * @return `true` 表示默认平台配置成功，`false` 表示平台名称非法。
 */
bool app_router_set_default_app_platform(const char* platform);

/**
 * @brief 判断本次连接是否已收到上位机平台配置。
 * @return `true` 表示已收到平台配置，`false` 表示仍需等待。
 */
bool app_router_has_app_config(void);

/**
 * @brief 清除本次连接收到的上位机平台配置。
 * @return 无返回值。
 */
void app_router_clear_app_config(void);

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
