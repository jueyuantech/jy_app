/**
 * @file app_manager.h
 * @brief 轻量 App 与页面栈框架接口。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-21
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#pragma once

#include "common/app_framework/app_page_host.h"
#include "message.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MANAGER_MAX_APPS 16       ///< 可注册 App 最大数量
#define APP_PAGE_STACK_MAX 8          ///< 单个 App 页面栈最大深度

typedef struct app_page_t app_page_t;
typedef struct app_t app_t;

/**
 * @brief 页面入参缓存。
 */
typedef struct {
    void* ptr;     ///< 数据指针
    size_t size;   ///< 数据长度
} app_page_data_t;

/**
 * @brief 页面描述符。
 */
struct app_page_t {
    const char* name;   ///< 页面名称

    /**
     * @brief 创建页面视图。
     * @param[in] parent 页面内容根对象。
     * @param[in] data 页面入参缓存。
     * @return 无返回值。
     */
    void (*on_create)(lv_obj_t* parent, const app_page_data_t* data);

    /**
     * @brief 页面变为可见。
     * @param[in] root 页面内容根对象。
     * @return 无返回值。
     */
    void (*on_appear)(lv_obj_t* root);

    /**
     * @brief 页面即将不可见。
     * @return 无返回值。
     */
    void (*on_disappear)(void);

    /**
     * @brief 页面视图即将销毁。
     * @return 无返回值。
     */
    void (*on_destroy)(void);

    /**
     * @brief 页面从页面栈移除。
     * @return 无返回值。
     */
    void (*on_unload)(void);

    /**
     * @brief 页面自定义返回处理。
     * @return `true` 表示页面已消费返回事件，`false` 表示交给框架默认返回。
     */
    bool (*on_back)(void);
};

/**
 * @brief 页面栈项。
 */
typedef struct {
    app_page_t* page;        ///< 页面描述符
    app_page_data_t data;    ///< 页面入参缓存
    app_page_view_t view;    ///< 当前视图对象集合
    bool view_created;       ///< 当前视图是否已创建
} app_page_entry_t;

/**
 * @brief App 描述符。
 */
struct app_t {
    const char* name;                            ///< App 名称
    app_page_entry_t stack[APP_PAGE_STACK_MAX];  ///< App 独立页面栈
    int stack_top;                               ///< 页面栈顶下标，空栈为 -1
    bool use_top_layer;                          ///< 是否将页面承载到最高优先级 top 层

    /**
     * @brief App 首次启动。
     * @return 无返回值。
     */
    void (*on_start)(void);

    /**
     * @brief App 从后台恢复。
     * @return 无返回值。
     */
    void (*on_resume)(void);

    /**
     * @brief App 进入后台。
     * @return 无返回值。
     */
    void (*on_pause)(void);

    /**
     * @brief App 停止并销毁。
     * @return 无返回值。
     */
    void (*on_stop)(void);

    /**
     * @brief App 根页面返回处理。
     * @return `true` 表示 App 已消费返回事件，`false` 表示框架不处理。
     */
    bool (*on_back)(void);

    /**
     * @brief 当前 App 作为 top 层时预处理 host MsgPack 消息。
     * @param[in,out] msg 已解析的消息头。
     * @return `true` 表示 App 已处理并阻止后续分发，`false` 表示继续正常分发。
     */
    bool (*on_host_message)(msg_pack_t* msg);

    /**
     * @brief 当前 App 作为 top 层时预处理底层系统事件。
     * @param[in] msg 底层系统事件消息。
     * @return `true` 表示 App 已处理并阻止后续分发，`false` 表示继续正常分发。
     */
    bool (*on_system_event)(JYT_ELF_MQ_MSG* msg);

    /**
     * @brief 当前 App 作为 top 层时预处理底层紧急文本消息。
     * @param[in] msg 消息内容缓冲区。
     * @param[in] msg_size 消息长度。
     * @return `true` 表示 App 已处理并阻止后续分发，`false` 表示继续正常分发。
     */
    bool (*on_emerg_message)(const char* msg, size_t msg_size);
};

/**
 * @brief App 框架配置。
 */
typedef struct {
    lv_obj_t* app_layer;                 ///< 页面层父对象；传 `NULL` 时使用 app_layers_get_app()
    app_page_host_config_t page_host;    ///< 页面承载层配置
} app_manager_config_t;

/**
 * @brief 初始化 App 框架。
 * @param[in] cfg App 框架配置。
 * @return `true` 表示初始化成功，`false` 表示初始化失败。
 */
bool app_manager_init(const app_manager_config_t* cfg);

/**
 * @brief 反初始化 App 框架，停止已启动 App 并清空注册表。
 * @return `true` 表示反初始化成功或当前尚未初始化，`false` 表示当前忙碌。
 */
bool app_manager_deinit(void);

/**
 * @brief 注册 App。
 * @param[in] app App 描述符。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool app_manager_register(app_t* app);

/**
 * @brief 判断指定 App 是否已注册。
 * @param[in] name App 名称。
 * @return `true` 表示已注册，`false` 表示未注册。
 */
bool app_manager_has_app(const char* name);

/**
 * @brief 切换到指定 App。
 * @param[in] name App 名称。
 * @return `true` 表示切换成功，`false` 表示切换失败。
 */
bool app_manager_switch(const char* name);

/**
 * @brief 刷新当前 App 的栈顶页面视图。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
bool app_manager_refresh_current(void);

/**
 * @brief 停止指定 App，并清空其页面栈。
 * @param[in] name App 名称。
 * @return `true` 表示停止成功，`false` 表示停止失败。
 */
bool app_manager_stop(const char* name);

/**
 * @brief 获取当前 App。
 * @return 返回当前 App；未切换 App 时返回 `NULL`。
 */
app_t* app_manager_current(void);

/**
 * @brief 获取当前 App 名称。
 * @return 返回当前 App 名称；未切换 App 时返回 `NULL`。
 */
const char* app_manager_current_name(void);

/**
 * @brief 获取当前 App 栈顶页面内容根对象。
 * @return 返回当前页面内容根对象；无当前页面时返回 `NULL`。
 */
lv_obj_t* app_manager_current_content_root(void);

/**
 * @brief 同步当前 App 栈顶页面承载层尺寸。
 * @param[in] width 页面宽度。
 * @param[in] height 页面高度。
 * @return 无返回值。
 */
void app_manager_sync_current_view_layout(int32_t width, int32_t height);

/**
 * @brief 判断 App 框架是否正在执行切换。
 * @return `true` 表示忙碌，`false` 表示空闲。
 */
bool app_manager_is_busy(void);

/**
 * @brief 当前 App 压入新页面。
 * @param[in] page 页面描述符。
 * @param[in] data 页面入参；无入参传 `NULL`。
 * @param[in] size 页面入参长度。
 * @return `true` 表示压栈成功，`false` 表示压栈失败。
 */
bool app_page_push(app_page_t* page, const void* data, size_t size);

/**
 * @brief 当前 App 弹出当前页面。
 * @return `true` 表示弹栈成功，`false` 表示弹栈失败。
 */
bool app_page_pop(void);

/**
 * @brief 当前 App 替换当前页面。
 * @param[in] page 页面描述符。
 * @param[in] data 页面入参；无入参传 `NULL`。
 * @param[in] size 页面入参长度。
 * @return `true` 表示替换成功，`false` 表示替换失败。
 */
bool app_page_replace(app_page_t* page, const void* data, size_t size);

/**
 * @brief 当前 App 处理返回事件。
 * @return `true` 表示返回事件已处理，`false` 表示未处理。
 */
bool app_page_back(void);

#ifdef __cplusplus
}
#endif
