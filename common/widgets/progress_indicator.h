#ifndef COMMON_WIDGETS_PROGRESS_INDICATOR_H
#define COMMON_WIDGETS_PROGRESS_INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "img.h"
#include "label.h"
#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 图文状态提示组件句柄。
 *
 * 组件负责垂直排布一个图标和一行提示文案。文案可显示百分比，
 * 也可显示连接中、转写中、翻译中等状态。
 */
typedef struct progress_indicator_t progress_indicator_t;

/**
 * @brief 图文状态提示组件配置项。
 */
typedef struct {
    int32_t x;              ///< 组件左上角 X 坐标。
    int32_t y;              ///< 组件左上角 Y 坐标。
    int32_t w;              ///< 组件宽度；可传 `LV_SIZE_CONTENT` 或 `LV_PCT()`。
    int32_t h;              ///< 组件高度；可传 `LV_SIZE_CONTENT` 或 `LV_PCT()`。
    int32_t gap;            ///< 图标与文字之间的垂直间距。
    img_cfg_t icon;         ///< 图标配置。
    label_cfg_t text;       ///< 提示文案配置。
} progress_indicator_cfg_t;

/**
 * @brief 获取默认的图文状态提示组件配置。
 *
 * 默认显示系统 `connecting.jpg` 图标和 `0%` 文本，适合作为下载、
 * 上传等进度场景的起点；调用方可直接改写 `text.text` 作为普通提示。
 *
 * @return 返回填充好默认值的配置结构体。
 */
progress_indicator_cfg_t progress_indicator_default_cfg(void);

/**
 * @brief 创建一个图文状态提示组件并挂载到指定父对象。
 *
 * @param parent 父对象；传 `NULL` 时退化到当前活动屏幕。
 * @param cfg 组件配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回组件句柄，失败返回 `NULL`。
 */
progress_indicator_t* progress_indicator_create(lv_obj_t* parent,
                                                const progress_indicator_cfg_t* cfg);

/**
 * @brief 按配置结构批量更新图文状态提示组件。
 *
 * @param indicator 目标组件句柄。
 * @param cfg 组件配置；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
void progress_indicator_apply_cfg(progress_indicator_t* indicator,
                                  const progress_indicator_cfg_t* cfg);

/**
 * @brief 设置提示文案。
 *
 * @param indicator 目标组件句柄。
 * @param text 新文本内容；传 `NULL` 时按空字符串处理。
 * @return 无返回值。
 */
void progress_indicator_set_text(progress_indicator_t* indicator, const char* text);

/**
 * @brief 按格式串设置提示文案。
 *
 * @param indicator 目标组件句柄。
 * @param fmt 文案格式串；传 `NULL` 时按空字符串处理。
 * @param ... 格式参数。
 * @return 无返回值。
 */
void progress_indicator_set_text_fmt(progress_indicator_t* indicator, const char* fmt, ...);

/**
 * @brief 设置提示文案字体信息。
 *
 * @param indicator 目标组件句柄。
 * @param font_info 字体信息；传 `NULL` 时回退为系统默认字体。
 * @return 无返回值。
 */
void progress_indicator_set_text_font_info(progress_indicator_t* indicator,
                                           const app_font_info_t* font_info);

/**
 * @brief 设置图标显隐。
 *
 * 隐藏时仅把图标透明度置为 0，保留原有占位，避免提示文案在
 * 说明态和等待态之间上下跳动。
 *
 * @param indicator 目标组件句柄。
 * @param visible `true` 表示显示图标，`false` 表示隐藏图标。
 * @return 无返回值。
 */
void progress_indicator_set_icon_visible(progress_indicator_t* indicator, bool visible);

/**
 * @brief 按百分比刷新提示文案。
 *
 * `percent` 会被夹紧到 0~100，并显示为 `xx%`。
 *
 * @param indicator 目标组件句柄。
 * @param percent 百分比进度。
 * @return 无返回值。
 */
void progress_indicator_set_percent(progress_indicator_t* indicator, int32_t percent);

/**
 * @brief 获取底层 LVGL 根对象。
 *
 * @param indicator 目标组件句柄。
 * @return 返回底层对象；组件无效时返回 `NULL`。
 */
lv_obj_t* progress_indicator_get_obj(progress_indicator_t* indicator);

#ifdef __cplusplus
}
#endif

#endif
