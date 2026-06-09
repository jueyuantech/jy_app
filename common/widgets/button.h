#ifndef COMMON_WIDGETS_BUTTON_H
#define COMMON_WIDGETS_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "label.h"
#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用按钮组件句柄。
 */
typedef struct button_t button_t;

/**
 * @brief 按钮组件配置项。
 */
typedef struct {
    int32_t x;                  ///< 按钮左上角 X 坐标。
    int32_t y;                  ///< 按钮左上角 Y 坐标。
    int32_t w;                  ///< 按钮宽度。
    int32_t h;                  ///< 按钮高度。
    int32_t radius;             ///< 按钮圆角半径。
    int32_t border_width;       ///< 按钮边框宽度。
    int32_t pad_hor;            ///< 左右内边距。
    int32_t pad_ver;            ///< 上下内边距。
    uint8_t opa;                ///< 按钮透明度，范围 0~255。
    label_cfg_t label;          ///< 内部文字组件配置。
} button_cfg_t;

/**
 * @brief 获取默认的按钮配置。
 *
 * @return 返回填充默认值后的配置结构体。
 */
button_cfg_t button_default_cfg(void);

/**
 * @brief 创建一个按钮组件并挂载到指定父对象。
 *
 * @param parent 父对象；传 `NULL` 时退化到当前活动屏幕。
 * @param cfg 按钮配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回按钮句柄，失败返回 `NULL`。
 */
button_t* button_create(lv_obj_t* parent, const button_cfg_t* cfg);

/**
 * @brief 使用默认配置和指定文本快速创建按钮组件。
 *
 * @param parent 父对象；传 `NULL` 时退化到当前活动屏幕。
 * @param text 按钮文本；传 `NULL` 时按空字符串处理。
 * @return 创建成功返回按钮句柄，失败返回 `NULL`。
 */
button_t* button_create_from_text(lv_obj_t* parent, const char* text);

/**
 * @brief 设置按钮显示文本。
 *
 * @param button 目标按钮句柄。
 * @param text 新文本内容；传 `NULL` 时按空字符串处理。
 * @return 无返回值。
 */
void button_set_text(button_t* button, const char* text);

/**
 * @brief 统一设置按钮可见前景元素的透明度。
 *
 * 当前会同步作用到按钮边框和内部文本，避免调用方感知按钮内部样式细节。
 *
 * @param button 目标按钮句柄。
 * @param opa 新透明度，范围 0~255。
 * @return 无返回值。
 */
void button_set_opacity(button_t* button, uint8_t opa);

/**
 * @brief 批量应用按钮配置。
 *
 * @param button 目标按钮句柄。
 * @param cfg 配置结构；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
void button_apply_cfg(button_t* button, const button_cfg_t* cfg);

/**
 * @brief 获取内部文本组件句柄。
 *
 * @param button 目标按钮句柄。
 * @return 返回内部 `label_t*`；无效按钮时返回 `NULL`。
 */
label_t* button_get_label(button_t* button);

/**
 * @brief 获取底层 LVGL 对象。
 *
 * @param button 目标按钮句柄。
 * @return 返回底层 `lv_obj_t*`；无效按钮时返回 `NULL`。
 */
lv_obj_t* button_get_obj(button_t* button);

#ifdef __cplusplus
}
#endif

#endif
