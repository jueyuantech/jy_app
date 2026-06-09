#ifndef COMMON_WIDGETS_ROLLER_H
#define COMMON_WIDGETS_ROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "label.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用滚轮组件句柄。
 *
 * 对外抽象为选项滚轮，不承诺具体可视行数；调用方只通过选项、选中项、
 * 回调和输入处理接口使用滚轮，不依赖内部展示结构。
 */
typedef struct roller_t roller_t;

/**
 * @brief 滚轮当前项的文本溢出处理方式。
 */
typedef enum {
    ROLLER_OVERFLOW_SCROLL = 0,         ///< 当前项文本超出时循环滚动显示。
    ROLLER_OVERFLOW_EXPAND_HEIGHT = 1,  ///< 当前项文本超出时裁剪，高度按行高展开。
} roller_overflow_mode_t;

/**
 * @brief 滚轮组件配置。
 *
 * 创建组件时建议先通过 `roller_default_cfg()` 获取一份默认值，再按需修改。
 * 新接口以 `roller_t*` 作为主句柄，完整配置统一收拢在这里。
 * 位置、尺寸、显隐等通用能力统一由 `ui_widget` 层负责。
 */
typedef struct {
    const char** items;                  ///< 初始选项文本数组。
    uint32_t count;                      ///< 初始选项数量。
    label_cfg_t label;                   ///< 内部文本默认配置。
    app_font_info_t selected_font;        ///< 当前项字体信息；字号为 0 时沿用 `label.font`。
    roller_overflow_mode_t overflow_mode;///< 当前项文本溢出处理方式。
    int32_t row_height;                  ///< 每一行的固定高度；小于等于 0 时按字体行高自动计算。
    int32_t row_gap;                     ///< 相邻行之间的垂直间距；小于 0 时自动计算。
    int32_t selected_pad_ver;            ///< 当前项上下额外留白。
    int32_t radius;                      ///< 当前项圆角半径。
    int32_t border_width;                ///< 当前项边框宽度。
    uint8_t opa_normal;                  ///< 非选中项文字与边框透明度。
    uint8_t opa_selected;                ///< 当前项文字与边框透明度。
} roller_cfg_t;

/**
 * @brief 选中项变化回调。
 *
 * @param roller 目标滚轮组件句柄。
 * @param selected 当前选中项索引。
 * @param user_data 用户透传数据。
 */
typedef void (*roller_selected_cb_t)(roller_t* roller, uint32_t selected, void* user_data);

/**
 * @brief 当前项激活回调。
 *
 * @param roller 目标滚轮组件句柄。
 * @param selected 当前选中项索引。
 * @param code 触发激活的事件码。
 * @param user_data 用户透传数据。
 */
typedef void (*roller_activate_cb_t)(roller_t* roller, uint32_t selected, lv_event_code_t code, void* user_data);

/**
 * @brief 获取默认滚轮配置。
 *
 * @return 返回填充好默认值的滚轮配置结构体。
 */
roller_cfg_t roller_default_cfg(void);

/**
 * @brief 创建滚轮组件。
 *
 * @param parent 父对象；传 `NULL` 时退化为当前活动屏幕。
 * @param cfg 滚轮配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回滚轮组件句柄，失败返回 `NULL`。
 */
roller_t* roller_create(lv_obj_t* parent, const roller_cfg_t* cfg);

/**
 * @brief 销毁滚轮组件。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 无返回值。
 */
void roller_destroy(roller_t* roller);

/**
 * @brief 判断滚轮句柄及内部对象是否有效。
 *
 * @param roller 目标滚轮组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool roller_is_valid(roller_t* roller);

/**
 * @brief 批量应用滚轮配置。
 *
 * @param roller 目标滚轮组件句柄。
 * @param cfg 配置结构；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
void roller_apply_cfg(roller_t* roller, const roller_cfg_t* cfg);

/**
 * @brief 设置滚轮回调。
 *
 * 这里注册的是新接口回调；旧版 `lv_obj_t*` 形态的回调请使用兼容层接口。
 *
 * @param roller 目标滚轮组件句柄。
 * @param on_selected_changed 选中项变化回调。
 * @param on_activate 当前项激活回调。
 * @param user_data 用户透传数据。
 * @return 无返回值。
 */
void roller_set_callbacks(roller_t* roller,
                          roller_selected_cb_t on_selected_changed,
                          roller_activate_cb_t on_activate,
                          void* user_data);

/**
 * @brief 处理滚轮的按键或手势事件。
 *
 * @param roller 目标滚轮组件句柄。
 * @param code 事件码。
 * @return `true` 表示事件已被滚轮消费，`false` 表示未处理。
 */
bool roller_key_handler(roller_t* roller, lv_event_code_t code);

/**
 * @brief 更新滚轮选项列表。
 *
 * @param roller 目标滚轮组件句柄。
 * @param items 文本数组。
 * @param count 文本数量。
 * @return 无返回值。
 */
void roller_set_items(roller_t* roller, const char** items, uint32_t count);

/**
 * @brief 设置当前选中项。
 *
 * @param roller 目标滚轮组件句柄。
 * @param index 目标索引。
 * @param anim 是否带动画；当前实现保留参数但不执行动画。
 * @return 无返回值。
 */
void roller_set_selected(roller_t* roller, uint32_t index, bool anim);

/**
 * @brief 获取当前选中项索引。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回当前选中项索引；组件无效时返回 `0`。
 */
uint32_t roller_get_selected(roller_t* roller);

/**
 * @brief 获取当前选中项文本。
 *
 * @param roller 目标滚轮组件句柄。
 * @param buf 输出缓冲区。
 * @param size 缓冲区大小。
 * @return 无返回值。
 */
void roller_get_selected_text(roller_t* roller, char* buf, uint32_t size);

/**
 * @brief 获取滚轮当前选项总数。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回当前选项总数；组件无效时返回 `0`。
 */
uint32_t roller_get_count(roller_t* roller);

/**
 * @brief 获取滚轮底层 LVGL 对象。
 *
 * 主要用于需要直接接入 LVGL 能力，或与旧版 `roller_widget_*` 调用边界衔接的场景。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回底层 `lv_obj_t*`；组件无效时返回 `NULL`。
 */
lv_obj_t* roller_get_obj(roller_t* roller);

/**
 * @brief 获取滚轮内部当前项文本组件。
 *
 * @param roller 目标滚轮组件句柄。
 * @return 返回当前项文本组件句柄；组件无效时返回 `NULL`。
 */
label_t* roller_get_selected_label(roller_t* roller);

/* -------- 兼容旧接口：roller_widget_*（仅保留签名桥接，不扩展新能力） -------- */

/**
 * @brief 旧版滚轮样式结构体。
 *
 * 仅用于兼容现有业务代码；新代码请优先使用 `roller_cfg_t` 中的样式字段。
 * 兼容层会把可用字段映射到 `roller_cfg_t`，其余保留字段仅维持旧签名。
 */
typedef struct {
    lv_coord_t row_height;           ///< 映射到 `roller_cfg_t::row_height`。
    lv_coord_t row_gap;              ///< 映射到 `roller_cfg_t::row_gap`。
    lv_coord_t selected_pad_ver;     ///< 映射到 `roller_cfg_t::selected_pad_ver`。
    lv_coord_t radius;               ///< 映射到 `roller_cfg_t::radius`。
    lv_coord_t border_width;         ///< 映射到 `roller_cfg_t::border_width`。
    lv_opa_t bg_opa;                 ///< 保留旧字段；当前兼容层不消费。
    lv_opa_t border_opa_normal;      ///< 作为普通态透明度回退值。
    lv_opa_t border_opa_selected;    ///< 作为选中态透明度回退值。
    lv_opa_t text_opa_normal;        ///< 优先映射到 `roller_cfg_t::opa_normal`。
    lv_opa_t text_opa_selected;      ///< 优先映射到 `roller_cfg_t::opa_selected`。
} roller_widget_style_t;

/**
 * @brief 旧版选中变化回调类型。
 */
typedef void (*roller_widget_selected_cb_t)(lv_obj_t* roller, uint32_t selected, void* user_data);

/**
 * @brief 旧版激活回调类型。
 */
typedef void (*roller_widget_activate_cb_t)(lv_obj_t* roller, uint32_t selected, lv_event_code_t code, void* user_data);

/**
 * @brief 兼容旧版签名创建滚轮。
 *
 * 等价于新接口的 `roller_create()`，但返回旧代码习惯使用的底层 `lv_obj_t*`。
 * `font_normal` / `font_selected` 仅在兼容层内部保存，不属于新接口主配置。
 *
 * @param parent 父对象。
 * @param items 文本数组。
 * @param count 文本数量。
 * @param font_normal 非选中项字体。
 * @param font_selected 当前项字体。
 * @param overflow_mode 当前项溢出模式。
 * @return 返回滚轮底层对象指针；创建失败返回 `NULL`。
 */
lv_obj_t* roller_widget_create(lv_obj_t* parent,
                               const char** items,
                               uint32_t count,
                               const lv_font_t* font_normal,
                               const lv_font_t* font_selected,
                               roller_overflow_mode_t overflow_mode);

/**
 * @brief 兼容旧版签名设置样式。
 *
 * 兼容层会把旧样式字段映射到内部 `roller_cfg_t`；不再存在独立的旧样式存储。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @param style 旧版样式结构体；传 `NULL` 时回退到新接口默认样式值。
 * @return 无返回值。
 */
void roller_widget_set_style(lv_obj_t* roller, const roller_widget_style_t* style);

/**
 * @brief 兼容旧版签名设置回调。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @param on_selected_changed 旧版选中变化回调。
 * @param on_activate 旧版激活回调。
 * @param user_data 用户透传数据。
 * @return 无返回值。
 */
void roller_widget_set_callbacks(lv_obj_t* roller,
                                 roller_widget_selected_cb_t on_selected_changed,
                                 roller_widget_activate_cb_t on_activate,
                                 void* user_data);

/**
 * @brief 兼容旧版签名处理输入事件。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @param code 事件码。
 * @return `true` 表示事件已被滚轮消费，`false` 表示未处理。
 */
bool roller_widget_key_handler(lv_obj_t* roller, lv_event_code_t code);

/**
 * @brief 兼容旧版签名更新滚轮选项列表。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @param items 文本数组。
 * @param count 文本数量。
 * @return 无返回值。
 */
void roller_widget_set_items(lv_obj_t* roller, const char** items, uint32_t count);

/**
 * @brief 兼容旧版签名设置当前选中项。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @param index 目标索引。
 * @param anim 是否带动画；语义与新接口保持一致。
 * @return 无返回值。
 */
void roller_widget_set_selected(lv_obj_t* roller, uint32_t index, bool anim);

/**
 * @brief 兼容旧版签名获取当前选中项索引。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @return 返回当前选中项索引；对象无效时返回 `0`。
 */
uint32_t roller_widget_get_selected(lv_obj_t* roller);

/**
 * @brief 兼容旧版签名获取当前选中项文本。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @param buf 输出缓冲区。
 * @param size 缓冲区大小。
 * @return 无返回值。
 */
void roller_widget_get_selected_text(lv_obj_t* roller, char* buf, uint32_t size);

/**
 * @brief 兼容旧版签名获取当前选项数量。
 *
 * @param roller 旧接口使用的滚轮底层对象。
 * @return 返回当前选项总数；对象无效时返回 `0`。
 */
uint32_t roller_widget_get_count(lv_obj_t* roller);

#ifdef __cplusplus
}
#endif

#endif
