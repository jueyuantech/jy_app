/**
 * @file system_notification.h
 * @brief System notification queue interfaces
 */
#pragma once

#include "app_def.h"
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

typedef struct {
    time_t  notify_time;                        ///< 通知时间戳。
    uint32_t id;                                ///< 通知业务 ID，用于 remove 判断当前活动单例。
    notify_mode_t mode;                         ///< 通知显示模式。
    bool has_title;                             ///< 是否存在可展示标题。
    bool has_image;                             ///< 是否存在原始 L8 图标数据。
    bool has_image_dsc;                         ///< 是否存在编译态 LVGL 图片描述符图标。
    bool has_image_path;                        ///< 是否存在图片路径图标。
    char title[MSG_STR_MAX_LEN];                ///< 标题文本缓存。
    uint32_t duration_ms;                       ///< 自动关闭时长，单位毫秒。
    uint8_t level;                              ///< 通知级别。
    uint8_t action;                             ///< 通知动作类型。
    size_t image_size;                          ///< `image` 原始图标数据长度。
    uint8_t image[SYSTEM_NOTIFICATION_IMAGE_BUF_SIZE]; ///< 32x32 L8 原始图标缓存。
    const lv_image_dsc_t* image_dsc;            ///< 编译态 LVGL 图片描述符指针。
    char image_path[SYSTEM_MAX_PATH_LEN];       ///< 系统图片完整路径缓存。
} system_notification_entry_t;

size_t system_notification_copy(system_notification_entry_t* out_items, size_t capacity);
bool system_notification_remove_at(size_t index);
void system_notification_clear(void);
bool system_notification_show_call(const char* title, const char* message);
bool system_notification_show_missed_call(const char* message);
void system_notification_dismiss_call(void);
bool system_notification_add_entry(const system_notification_entry_t* entry);
bool system_notification_update_entry(const system_notification_entry_t* entry);
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
