/**
 * @file product_app.h
 * @brief 产品 App 清单的公共查询与注册接口。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 产品 App 在公共逻辑中承担的通用角色。
 */
typedef enum {
    PRODUCT_APP_ROLE_HOME = 0,         ///< 默认首页。
    PRODUCT_APP_ROLE_LANGSELECTION,    ///< 语言选择页。
    PRODUCT_APP_ROLE_WATCH_HOME,       ///< 手表连接场景首页。
    PRODUCT_APP_ROLE_GUIDE,            ///< 新手引导页。
    PRODUCT_APP_ROLE_ASSISTANT,         ///< Assistant 配置主体。
    PRODUCT_APP_ROLE_COUNT,             ///< 角色数量。
} product_app_role_t;

/**
 * @brief 产品 App 向公共逻辑声明的通用能力。
 */
typedef enum {
    PRODUCT_APP_CAP_UPLOAD_PROGRESS = 1u << 0,      ///< 支持显示上传进度。
    PRODUCT_APP_CAP_GUIDE = 1u << 1,                ///< 新手引导页。
    PRODUCT_APP_CAP_STT_QUESTION_NO_SKIP = 1u << 2, ///< STT 问题区更新不可丢弃。
    PRODUCT_APP_CAP_ASSISTANT_OPEN_BLOCKED = 1u << 3, ///< 当前 App 阻止 KWS 打开 Assistant。
    PRODUCT_APP_CAP_GUIDE_SIMPLE = 1u << 4,         ///< 使用 Guide 内部自闭环的轻量引导。
    PRODUCT_APP_CAP_GUIDE_RIGHT_TEMPLE = 1u << 8,   ///< 四步引导使用右镜腿提示及对应原尺寸示意图。
    PRODUCT_APP_CAP_DISPLAY_POSITION = 1u << 5,     ///< 支持按系统显示位置搬移应用图层。
    PRODUCT_APP_CAP_DISPLAY_POSITION_FLOAT = 1u << 6, ///< 仅支持按系统显示位置搬移应用浮层。
    PRODUCT_APP_CAP_REQUIRES_PHONE = 1u << 7,       ///< 依赖手机连接，按 App 策略下断连时显示待连接。
} product_app_capability_t;

/**
 * @brief 产品 App 从 Home 菜单激活时执行的动作。
 */
typedef enum {
    PRODUCT_APP_HOME_ACTION_SET_VIEW = 0,      ///< 切换到 App 对应页面。
    PRODUCT_APP_HOME_ACTION_OPEN_ASSISTANT,    ///< 在当前页面打开 Assistant 弹层。
} product_app_home_action_t;

/**
 * @brief 产品 App 入口描述符注册函数。
 * @param[in] profile 具体 App 入口提供的静态配置。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
typedef bool (*product_app_profile_register_fn)(const void* profile);

/**
 * @brief 产品 App 入口描述符。
 */
typedef struct {
    product_app_profile_register_fn register_profile; ///< 共享运行时的注册函数。
    const void* profile;                              ///< 具体 App 入口静态配置。
} product_app_module_t;

/**
 * @brief 注册当前产品清单中参与构建的全部 App 模块。
 * @return `true` 表示全部注册成功，`false` 表示至少一个模块注册失败。
 */
bool product_apps_register_all(void);

/**
 * @brief 获取当前产品指定通用角色对应的 App 名称。
 * @param[in] role App 通用角色。
 * @return 返回 App 名称；角色未配置时返回 `NULL`。
 */
const char* product_app_role_name(product_app_role_t role);

/**
 * @brief 判断指定 App 名称是否声明了某项通用能力。
 * @param[in] name App 名称。
 * @param[in] capability 待查询能力。
 * @return `true` 表示支持，`false` 表示不支持或名称不存在。
 */
bool product_app_name_has_capability(const char* name, product_app_capability_t capability);

/**
 * @brief 判断指定消息 ID 对应的 App 是否声明了某项通用能力。
 * @param[in] msg_id App 消息 ID。
 * @param[in] capability 待查询能力。
 * @return `true` 表示支持，`false` 表示不支持或消息 ID 不存在。
 */
bool product_app_msg_id_has_capability(uint32_t msg_id, product_app_capability_t capability);

/**
 * @brief 获取指定 App 的 Home 菜单激活动作。
 * @param[in] name App 名称。
 * @return 返回产品清单声明的动作；名称不存在时回退为切换页面。
 */
product_app_home_action_t product_app_get_home_action(const char* name);
