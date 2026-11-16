/**
 * @file system_notification.h
 * @brief System notification queue interfaces
 */
#pragma once

#include "system/popups/notify/notify.h"
#include "message.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_NOTIFICATION_QUEUE_MAX 10
#define SYSTEM_NOTIFICATION_IMAGE_WIDTH 32
#define SYSTEM_NOTIFICATION_IMAGE_HEIGHT 32
#define SYSTEM_NOTIFICATION_IMAGE_BUF_SIZE \
    (SYSTEM_NOTIFICATION_IMAGE_WIDTH * SYSTEM_NOTIFICATION_IMAGE_HEIGHT)

/**
 * @brief 通知使用的内置 ROMFS 图标类型。
 */
typedef enum {
    SYSTEM_NOTIFICATION_ICON_DEFAULT = 0, ///< 根据通知模式选择电话或消息图标。
    SYSTEM_NOTIFICATION_ICON_MISSED_CALL, ///< 未接来电专用图标。
    SYSTEM_NOTIFICATION_ICON_NONE,        ///< 不显示图标，仅用于空列表等系统占位项。
} system_notification_icon_t;

typedef struct {
    time_t  notify_time;                        ///< 通知时间戳。
    uint32_t id;                                ///< 通知业务 ID，用于 remove 判断当前活动单例。
    notify_mode_t mode;                         ///< 通知显示模式。
    bool has_title;                             ///< 是否存在可展示标题。
    bool silent;                                ///< 仅入列表，不亮屏、不弹窗。
    bool has_image;                             ///< 是否存在手机下发的 32x32 L8 图标。
    char title[MSG_STR_MAX_LEN];                ///< 标题文本缓存。
    uint32_t duration_ms;                       ///< 自动关闭时长，单位毫秒。
    uint8_t level;                              ///< 通知级别。
    uint8_t action;                             ///< 通知动作类型。
    uint8_t icon;                               ///< `system_notification_icon_t`，绘制时解析为 ROMFS 路径。
    size_t image_size;                          ///< `image` 中的有效图标数据长度。
    uint8_t image[SYSTEM_NOTIFICATION_IMAGE_BUF_SIZE]; ///< 手机下发的 32x32 L8 图标缓存。
} system_notification_entry_t;

/**
 * @brief 获取通知弹窗或列表使用的内置 ROMFS 图标路径。
 * @param[in] entry 通知条目。
 * @return 内置图标路径；参数为空时返回 `NULL`。
 */
const void* system_notification_entry_icon_source(
    const system_notification_entry_t* entry);

size_t system_notification_copy(system_notification_entry_t* out_items, size_t capacity);
bool system_notification_remove_at(size_t index);
void system_notification_clear(void);
/**
 * @brief 显示指定阶段的电话通知。
 * @param[in] title 电话通知标题。
 * @param[in] message 电话号码或联系人文本。
 * @param[in] state 电话阶段。
 * @return `true` 表示显示成功，`false` 表示显示失败。
 */
bool system_notification_show_call(const char* title,
                                   const char* message,
                                   notify_call_state_t state);
bool system_notification_show_missed_call(const char* message);
/**
 * @brief 更新当前电话通知阶段。
 * @param[in] state 新的电话阶段。
 * @return `true` 表示产品 Notify 在更新后仍保持显示，`false` 表示没有活动电话通知或产品实现已关闭通知。
 */
bool system_notification_update_call_state(notify_call_state_t state);
/**
 * @brief 原地更新当前电话通知的联系人或号码。
 * @param[in] caller 新的单行显示文本。
 * @return 当前存在可更新的电话通知时返回 `true`。
 */
bool system_notification_update_call_caller(const char* caller);
/**
 * @brief 关闭电话通知；息屏时清理缓存并延迟到亮屏后销毁弹窗。
 * @return 无返回值。
 */
void system_notification_dismiss_call(void);
/**
 * @brief 亮屏后销毁已结束的电话弹窗，并取消其计时器；须在恢复延迟定时器前调用。
 * @return 无返回值。
 */
void system_notification_flush_pending_after_screen_on(void);
bool system_notification_add_entry(const system_notification_entry_t* entry);
/**
 * @brief 批量入队通知，并在整批完成后统一执行一次列表刷新与弹窗决策。
 * @param[in] entries 待入队通知数组。
 * @param[in] count 通知数量。
 * @return 至少有一条通知成功入队返回 `true`。
 */
bool system_notification_add_entries_batch(
    const system_notification_entry_t* entries,
    size_t count);
bool system_notification_update_entry(const system_notification_entry_t* entry);
/**
 * @brief 按通知 ID 删除通知详情。
 * @param[in] id 通知业务 ID。
 * @return 删除流程执行成功返回 `true`。
 */
bool system_notification_remove_id(uint32_t id);
/**
 * @brief 判断 Host 通知消息是否应跳过用户活动保活。
 * @param[in] node Host 通知命令 data 节点。
 * @param[in] msg Host MsgPack 消息头。
 * @return `true` 表示这条消息只静默入列表，不重置息屏定时器。
 */
bool system_notification_should_suppress_activity(mpack_node_t node, const msg_pack_t* msg);

#ifdef __cplusplus
}
#endif
