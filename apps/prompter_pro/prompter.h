/**
 * @file prompter.h
 * @brief Prompter 应用对外接口
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include "common/app_framework/app_manager.h"
#include "common/widgets/paged_text.h"
#include "system/system.h"
#include <lvgl/lvgl.h>

typedef struct app_page_t app_page_t;

extern app_font_info_t prompter_font_info;
extern const lv_font_t* prompter_font;

#define PROMPTER_MENU_LABEL_MAX_LEN 128 ///< Prompter 文件列表菜单项文案最大长度。

/**
 * @brief Prompter 文件列表菜单项。
 */
typedef struct {
    uint32_t id;                              ///< 菜单项 ID，用于选中后回传给手机端。
    char label[PROMPTER_MENU_LABEL_MAX_LEN]; ///< 菜单项显示文案。
} prompter_menu_item_t;

/**
 * @brief Prompter 外部同步预览状态。
 */
typedef struct {
    uint32_t offset;             ///< 源文本中首个渲染字符的 UTF-8 字节偏移。
    uint32_t length;             ///< 本次渲染需要读取的 UTF-8 字节长度。
    int32_t top_mask_height;     ///< 顶部半透明遮罩底边 y 坐标，单位像素。
    int32_t bottom_mask_height;  ///< 底部半透明遮罩顶边 y 坐标，单位像素。
} prompter_external_view_t;

/**
 * @brief Prompter 正文排版参数。
 */
typedef struct {
    bool break_all;               ///< 是否允许任意字符强制换行。
    uint32_t letter_space_px;     ///< 字符间距，单位像素。
    uint32_t line_space_px;       ///< 行间距，单位像素。
    uint32_t padding_horizontal_px; ///< 分页文本内部水平留白，单位像素。
    uint32_t padding_vertical_px; ///< 分页文本内部垂直留白，单位像素。
    uint32_t text_size_px;        ///< 运行时实际字号，单位像素。
    uint32_t total_width_px;      ///< 分页文本视口宽度，单位像素。
    uint32_t total_height_px;     ///< 分页文本视口高度，单位像素。
} prompter_text_layout_t;

/**
 * @brief Prompter 提词播放状态。
 */
typedef enum {
    PROMPTER_STATE_PAUSE = 0,   ///< 暂停态，底部提示点击开始和滑动翻页。
    PROMPTER_STATE_RUNNING = 1, ///< 运行态，底部提示点击暂停。
} prompter_state_t;

/** @defgroup app_prompter Prompter App @{ */

/**
 * @brief 重置 Prompter 视图状态。
 */
void prompter_view_reset(void);

/**
 * @brief 注册 Prompter 到新 App framework。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool prompter_app_register(void);

/**
 * @brief 路由 Prompter 命令
 * @param node mpack 节点
 * @param msg 消息包
 * @return `true` 表示处理成功
 */
bool prompter_route_cmd(mpack_node_t node, msg_pack_t* msg);

bool prompter_config_ensure(void);
bool prompter_config_reset_to_default(void);

/**
 * @brief 设置 Prompter 文件列表菜单。
 * @param menu_id 菜单 ID。
 * @param items 菜单项数组；`count` 为 0 时允许传 `NULL`。
 * @param count 菜单项数量。
 * @param default_item_id 默认选中菜单项 ID；为 0 或不存在时选中第一项。
 * @return `true` 表示设置成功，`false` 表示参数无效或内存不足。
 */
bool prompter_menu_set(uint32_t menu_id,
                       const prompter_menu_item_t* items,
                       uint32_t count,
                       uint32_t default_item_id);

/**
 * @brief 上报 Prompter 文件列表菜单选中项。
 * @param menu_id 菜单 ID。
 * @param selected_item_id 被选中的菜单项 ID。
 * @return `true` 表示发送成功，`false` 表示发送失败。
 */
bool prompter_report_menu_selected(uint32_t menu_id, uint32_t selected_item_id);

/**
 * @brief 获取 Prompter 正文排版参数。
 * @param layout 输出排版参数。
 * @return `true` 表示获取成功，`false` 表示参数无效。
 */
bool prompter_text_get_layout(prompter_text_layout_t* layout);

/**
 * @brief 从配置文件读取 Prompter 字体配置
 * @return `true` 表示读取并应用成功
 */
bool prompter_font_init_from_config(void);

/**
 * @brief Prompter 字体配置变化后刷新运行时字体和页面控件。
 * @return `true` 表示刷新成功。
 */
bool prompter_on_fontconfig_changed(void);

/**
 * @brief 设置 Prompter 文本文件路径
 * @param path 文件路径
 * @return `true` 表示加载成功
 */
bool prompter_text_set_file(const char* path);

/**
 * @brief 按 Android 同步状态刷新 Prompter 正文与遮罩
 * @param view 外部同步预览状态
 */
void prompter_text_apply_external_view(const prompter_external_view_t* view);

/**
 * @brief 设置 Prompter 提词计时秒数
 * @param tick_seconds 手机端同步的提词计时秒数
 * @return 无返回值
 */
void prompter_text_set_tick(uint32_t tick_seconds);

/**
 * @brief 设置 Prompter 提词播放状态
 * @param state 手机端同步的提词播放状态
 * @return 无返回值
 */
void prompter_text_set_state(prompter_state_t state);

/**
 * @brief 获取 Prompter 页面描述符。
 * @return 返回 Prompter 页面描述符。
 */
const app_page_t* prompter_page_get(void);

/** @} */
#ifdef __cplusplus
}
#endif
