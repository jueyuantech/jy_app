#ifndef COMMON_WIDGETS_MSGBOX_H
#define COMMON_WIDGETS_MSGBOX_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "button.h"
#include "container.h"
#include "label.h"
#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用消息框组件句柄。
 */
typedef struct msgbox_t msgbox_t;

/**
 * @brief 消息框确认结果。
 */
typedef enum {
    MSGBOX_KEY_NONE = 0,        ///< 当前没有确认选择。
    MSGBOX_KEY_LEFT = 1 << 0,   ///< 当前确认了左侧按钮。
    MSGBOX_KEY_RIGHT = 1 << 1,  ///< 当前确认了右侧按钮。
    MSGBOX_KEY_CANCEL = 1 << 2, ///< 当前确认了取消按钮。
    MSGBOX_KEY_SAVE = 1 << 3,   ///< 当前确认了保存按钮。
    MSGBOX_KEY_OK = 1 << 4,     ///< 当前确认了确定按钮。
    MSGBOX_KEY_CONFIRM = 1 << 5,///< 当前确认了确认按钮。
    MSGBOX_KEY_YES = 1 << 6,    ///< 当前确认了是按钮。
    MSGBOX_KEY_NO = 1 << 7,     ///< 当前确认了否按钮。
    MSGBOX_KEY_LEAVE = 1 << 8,  ///< 当前确认了离开按钮。
} msgbox_key_t;

/**
 * @brief 消息框确认结果回调。
 * @param box 触发确认的消息框。
 * @param key 本次确认结果。
 * @param user_data 用户透传数据。
 * @return 无返回值。
 */
typedef void (*msgbox_event_cb_t)(msgbox_t* box, msgbox_key_t key, void* user_data);

/**
 * @brief 消息框按钮配置。
 */
typedef struct {
    msgbox_key_t key;  ///< 按钮语义；未定义时回退为左右侧语义。
    const char* text;  ///< 按钮文案；为空时按语义使用默认文案。
} msgbox_action_t;

/**
 * @brief 消息框组件配置项。
 *
 * 消息框固定居中显示，不再支持自定义位置。
 */
typedef struct {
    container_cfg_t dlg;      ///< 弹窗主容器配置。
    label_cfg_t title;        ///< 标题文本默认配置。
    container_cfg_t actions;  ///< 按钮行容器配置。
    button_cfg_t button;      ///< 按钮的默认样式配置。
    int32_t title_width;      ///< 标题文本布局宽度；小于等于 0 或 `LV_SIZE_CONTENT` 时按弹窗宽度自动计算。
    int32_t top_pad;          ///< 标题距离弹窗顶部的间距。
    int32_t bottom_pad;        ///< 按钮区域距离弹窗底部的间距。
    int32_t title_button_pad; ///< 标题文本举例按钮的间距。
    int32_t button_x_pad;     ///< 按钮距左右边缘的横向边距。
    const char* message;      ///< 顶部提示文本；传 `NULL` 时内部按空白占位。
    msgbox_action_t left;     ///< 左侧按钮配置。
    msgbox_action_t right;    ///< 右侧按钮配置。
} msgbox_cfg_t;

/**
 * @brief 获取默认消息框配置。
 *
 * 推荐先调用本函数获取一份默认配置，再按需修改少数字段后传给 `msgbox_show_with_cfg()`。
 *
 * @return 返回填充好默认值的消息框配置结构体。
 */
msgbox_cfg_t msgbox_default_cfg(void);

/**
 * @brief 销毁消息框组件。
 *
 * @param box 目标消息框组件句柄。
 * @return 无返回值。
 */
void msgbox_destroy(msgbox_t* box);
/**
 * @brief 设置消息框确认回调。
 * @param box 目标消息框组件句柄。
 * @param cb 确认回调；传 `NULL` 表示清空。
 * @param user_data 用户透传数据。
 * @return 无返回值。
 */
void msgbox_set_callback(msgbox_t* box, msgbox_event_cb_t cb, void* user_data);

/**
 * @brief 使用消息文本和按钮语义快速显示消息框。
 *
 * `keys` 支持按位组合，例如 `MSGBOX_KEY_CANCEL | MSGBOX_KEY_SAVE`。
 * 当传入的语义键不在预定义集合内时，返回结果会回退为 `MSGBOX_KEY_LEFT`
 * 或 `MSGBOX_KEY_RIGHT`。
 *
 * @param box 调用者当前持有的消息框对象；传 `NULL` 时会新建。
 * @param msg 提示文本；传 `NULL` 时内部显示单个空格占位。
 * @param keys 按钮语义位掩码。
 * @return 返回可继续持有的消息框对象，失败返回 `NULL`。
 */
msgbox_t* msgbox_show(msgbox_t* box, const char* msg, msgbox_key_t keys);

/**
 * @brief 创建或复用一个消息框，并立即显示。
 *
 * 推荐用法：
 * `msgbox_cfg_t cfg = msgbox_default_cfg();`
 * 按需修改 `cfg.message / cfg.left / cfg.right`
 * 然后调用 `msgbox_show_with_cfg(box, &cfg);`
 *
 * @param box 调用者当前持有的消息框对象；传 `NULL` 时会新建。
 * @param cfg 消息框配置；传 `NULL` 时使用默认配置。
 * @return 返回可继续持有的消息框对象，失败返回 `NULL`。
 */
msgbox_t* msgbox_show_with_cfg(msgbox_t* box, const msgbox_cfg_t* cfg);

/**
 * @brief 只更新消息框提示文本。
 *
 * @param box 目标消息框组件句柄。
 * @param message 新的提示文本；为空时内部显示单个空格占位。
 * @return 无返回值。
 */
void msgbox_set_text(msgbox_t* box, const char* message);

/**
 * @brief 设置消息框显隐状态。
 *
 * @param box 目标消息框组件句柄。
 * @param show `true` 表示显示，`false` 表示隐藏。
 * @return 无返回值。
 */
void msgbox_set_visible(msgbox_t* box, bool show);

/**
 * @brief 判断消息框当前是否隐藏。
 *
 * @param box 目标消息框组件句柄。
 * @return `true` 表示隐藏或对象无效，`false` 表示当前可见。
 */
bool msgbox_is_hidden(const msgbox_t* box);

/**
 * @brief 判断消息框句柄及内部对象是否有效。
 *
 * @param box 目标消息框组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool msgbox_is_valid(const msgbox_t* box);
/**
 * @brief 处理当前活动消息框输入事件。
 * @param code LVGL 事件码。
 * @return `true` 表示事件已被活动消息框消费，`false` 表示未消费。
 */
bool msgbox_handle_active_event(lv_event_code_t code);

#ifdef __cplusplus
}
#endif

#endif
