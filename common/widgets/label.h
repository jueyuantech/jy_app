#ifndef COMMON_WIDGETS_LABEL_H
#define COMMON_WIDGETS_LABEL_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "system/system.h"
#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用文本组件句柄。
 *
 * 业务层通过该句柄操作文本组件，不需要直接调用 LVGL 原生 `lv_label_*`
 * 接口。
 */
typedef struct label_t label_t;

/**
 * @brief 文本对齐方式。
 */
typedef enum {
    LABEL_ALIGN_LEFT = 0,   ///< 文本左对齐。
    LABEL_ALIGN_CENTER,     ///< 文本居中对齐。
    LABEL_ALIGN_RIGHT,      ///< 文本右对齐。
} label_align_t;

/**
 * @brief 文本超出显示区域时的处理方式。
 */
typedef enum {
    LABEL_OVERFLOW_CLIP = 0,         ///< 超出部分直接裁剪。
    LABEL_OVERFLOW_WRAP,             ///< 超出后自动换行。
    LABEL_OVERFLOW_SCROLL,           ///< 超出后单向滚动显示。
    LABEL_OVERFLOW_SCROLL_CIRCULAR,  ///< 超出后循环滚动显示。
} label_overflow_t;

/**
 * @brief 文本组件配置项。
 *
 * 创建组件时建议先通过 `label_default_cfg()` 获取一份默认值，再按需修改。
 */
typedef struct {
    int32_t x;                  ///< 组件左上角 X 坐标。
    int32_t y;                  ///< 组件左上角 Y 坐标。
    int32_t w;                  ///< 组件宽度；可传 `LV_SIZE_CONTENT`。
    int32_t h;                  ///< 组件高度；可传 `LV_SIZE_CONTENT`。
    int32_t radius;             ///< 背景圆角半径。
    int32_t border_width;       ///< 边框宽度，0 表示无边框。
    int32_t pad_hor;            ///< 左右内边距。
    int32_t pad_ver;            ///< 上下内边距。
    uint8_t opa;                ///< 文字以及边框透明度，范围 0~255。
    label_align_t align;        ///< 文本对齐方式。
    label_overflow_t overflow;  ///< 文本溢出处理方式。
    uint32_t max_lines;         ///< 最大显示行数；传 0 表示不限制。
    app_font_info_t font;       ///< 字体信息，包含字号、字间距、行间距。
    const char* text;           ///< 初始文本；传 `NULL` 时会按空字符串处理。
} label_cfg_t;

/**
 * @brief 获取默认的文本组件配置。
 *
 * 默认配置为黑底白字，尺寸按内容自适应，适合作为基础文本直接使用。
 *
 * @return 返回填充好默认值的配置结构体。
 */
label_cfg_t label_default_cfg(void);

/**
 * @brief 创建一个文本组件并挂载到指定父对象。
 *
 * @param parent 父对象；传 `NULL` 时会退化为当前活动屏幕。
 * @param cfg 组件配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回组件句柄，失败返回 `NULL`。
 */
label_t* label_create(lv_obj_t* parent, const label_cfg_t* cfg);

/**
 * @brief 使用默认配置和指定文本快速创建文本组件。
 *
 * @param parent 父对象；传 `NULL` 时会退化为当前活动屏幕。
 * @param text 初始文本；传 `NULL` 时按空字符串处理。
 * @return 创建成功返回组件句柄，失败返回 `NULL`。
 */
label_t* label_create_from_text(lv_obj_t* parent, const char* text);

/**
 * @brief 使用默认配置、指定文本和字号快速创建文本组件。
 *
 * `font_size` 会写入 `label_cfg_t.font.weight`；当字号非法时会回退到
 * 默认字体行为。
 *
 * @param parent 父对象；传 `NULL` 时会退化为当前活动屏幕。
 * @param text 初始文本；传 `NULL` 时按空字符串处理。
 * @param font_size 文本字号。
 * @return 创建成功返回组件句柄，失败返回 `NULL`。
 */
label_t* label_create_from_text_and_font_size(lv_obj_t* parent, const char* text, int32_t font_size);

/**
 * @brief 设置显示文本。
 *
 * @param label 目标组件句柄。
 * @param text 新文本内容；传 `NULL` 时按空字符串处理。
 * @return 无返回值。
 */
void label_set_text(label_t* label, const char* text);

/**
 * @brief 获取当前显示文本。
 *
 * @param label 目标组件句柄。
 * @return 返回当前文本指针；组件无效时返回空字符串。
 */
const char* label_get_text(label_t* label);

/**
 * @brief 在当前文本末尾追加内容。
 *
 * 仅适用于标签内部文本由组件自身管理的场景；
 * 若 `text` 为空串或 `NULL`，则不执行任何操作。
 *
 * @param label 目标组件句柄。
 * @param text 待追加文本；传 `NULL` 时忽略。
 * @return 无返回值。
 */
void label_append_text(label_t* label, const char* text);

/**
 * @brief 设置组件内边距。
 *
 * @param label 目标组件句柄。
 * @param pad_hor 左右内边距。
 * @param pad_ver 上下内边距。
 * @return 无返回值。
 */
void label_set_padding(label_t* label, int32_t pad_hor, int32_t pad_ver);

/**
 * @brief 设置组件圆角半径。
 *
 * @param label 目标组件句柄。
 * @param radius 圆角半径。
 * @return 无返回值。
 */
void label_set_radius(label_t* label, int32_t radius);

/**
 * @brief 设置组件边框宽度。
 *
 * @param label 目标组件句柄。
 * @param border_width 边框宽度。
 * @return 无返回值。
 */
void label_set_border_width(label_t* label, int32_t border_width);

/**
 * @brief 设置文字透明度。
 *
 * @param label 目标组件句柄。
 * @param opa 新透明度，范围 0~255。
 * @return 无返回值。
 */
void label_set_opacity(label_t* label, uint8_t opa);

/**
 * @brief 设置文本对齐方式。
 *
 * @param label 目标组件句柄。
 * @param align 新对齐方式。
 * @return 无返回值。
 */
void label_set_align(label_t* label, label_align_t align);

/**
 * @brief 设置文本溢出处理方式。
 *
 * @param label 目标组件句柄。
 * @param overflow 新溢出处理方式。
 * @return 无返回值。
 */
void label_set_overflow(label_t* label, label_overflow_t overflow);

/**
 * @brief 设置最大显示行数。
 *
 * `0` 表示取消限制，恢复为按内容高度显示。
 *
 * @param label 目标组件句柄。
 * @param max_lines 最大显示行数。
 * @return 无返回值。
 */
void label_set_max_lines(label_t* label, uint32_t max_lines);

/**
 * @brief 设置字体信息。
 *
 * 会根据 `font_info` 中的字号绑定系统字体注册表中的最近字号字体。
 *
 * @param label 目标组件句柄。
 * @param font_info 字体配置；传 `NULL` 时回退为系统默认字体和默认间距。
 * @return 无返回值。
 */
void label_set_font_info(label_t* label, const app_font_info_t* font_info);


/**
 * @brief 获取组件父对象。
 *
 * @param label 目标组件句柄。
 * @return 返回父对象；组件无效时返回 `NULL`。
 */
lv_obj_t* label_get_parent(label_t* label);

/**
 * @brief 按配置结构批量更新组件。
 *
 * @param label 目标组件句柄。
 * @param cfg 新配置；传 `NULL` 时使用默认配置覆盖。
 * @return 无返回值。
 */
void label_apply_cfg(label_t* label, const label_cfg_t* cfg);

/**
 * @brief 获取底层 LVGL 对象。
 *
 * 这是一个兜底接口，只有在现有封装不够用时才建议调用。
 *
 * @param label 目标组件句柄。
 * @return 返回底层 `lv_obj_t*`；无效组件时返回 `NULL`。
 */
lv_obj_t* label_get_obj(label_t* label);

#ifdef __cplusplus
}
#endif

#endif
