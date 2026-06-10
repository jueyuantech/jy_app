#ifndef COMMON_WIDGETS_OVERLAY_H
#define COMMON_WIDGETS_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "label.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用叠加层组件句柄。
 */
typedef struct overlay_t overlay_t;

/**
 * @brief 单个 overlay 点位数据。
 */
typedef struct {
    int32_t x;       ///< 点位中心的 X 坐标。
    int32_t y;       ///< 点位中心的 Y 坐标。
    int32_t size;    ///< 点位圆点尺寸；传 0 时使用默认尺寸。
    uint8_t opa;     ///< 点位透明度；传 0 时使用默认透明度。
} overlay_point_t;

/**
 * @brief 叠加层组件配置。
 */
typedef struct {
    label_cfg_t text;         ///< 标注文本默认配置。
    overlay_point_t point;    ///< 默认点位配置；`x/y` 不参与默认样式计算。
    uint16_t max_items;       ///< 最多可同时显示的点位/文本数量。
} overlay_cfg_t;

/**
 * @brief 获取默认叠加层配置。
 *
 * @return 返回填充好默认值的配置结构体。
 */
overlay_cfg_t overlay_default_cfg(void);

/**
 * @brief 创建一个叠加层组件。
 *
 * @param parent 父对象；传 `NULL` 时退化到当前活动屏幕。
 * @param cfg 组件配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回组件句柄，失败返回 `NULL`。
 */
overlay_t* overlay_create(lv_obj_t* parent, const overlay_cfg_t* cfg);

/**
 * @brief 判断叠加层句柄及内部对象是否有效。
 *
 * @param overlay 目标组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool overlay_is_valid(overlay_t* overlay);

/**
 * @brief 销毁叠加层组件。
 *
 * @param overlay 目标组件句柄。
 * @return 无返回值。
 */
void overlay_destroy(overlay_t* overlay);

/**
 * @brief 批量设置 overlay 点位。
 *
 * @param overlay 目标组件句柄。
 * @param points 点位数组。
 * @param count 点位数量。
 * @return 无返回值。
 */
void overlay_set_points(overlay_t* overlay, const overlay_point_t* points, uint16_t count);

/**
 * @brief 追加一个 overlay 点位到下一个可用槽位。
 *
 * @param overlay 目标组件句柄。
 * @param point 点位数据。
 * @return `true` 表示追加成功，`false` 表示已满或组件无效。
 */
bool overlay_add_point(overlay_t* overlay, const overlay_point_t* point);

/**
 * @brief 批量设置 overlay 文本。
 *
 * @param overlay 目标组件句柄。
 * @param texts 文本数组。
 * @param count 文本数量。
 * @return 无返回值。
 */
void overlay_set_texts(overlay_t* overlay, const label_cfg_t* texts, uint16_t count);

/**
 * @brief 追加一个 overlay 文本到下一个可用槽位。
 *
 * @param overlay 目标组件句柄。
 * @param text 文本数据。
 * @return `true` 表示追加成功，`false` 表示已满或组件无效。
 */
bool overlay_add_text(overlay_t* overlay, const label_cfg_t* text);

/**
 * @brief 使用文本和字号快速追加一个 overlay 文本到下一个可用槽位。
 *
 * @param overlay 目标组件句柄。
 * @param text 文本内容；传 `NULL` 时按空字符串处理。
 * @param font_size 文本字号；传小于等于 0 时保持默认字号配置。
 * @return `true` 表示追加成功，`false` 表示已满或组件无效。
 */
bool overlay_add_text_from_font(overlay_t* overlay, const char* text, int32_t font_size);

/**
 * @brief 清空所有点位与文本。
 *
 * @param overlay 目标组件句柄。
 * @return 无返回值。
 */
void overlay_clear(overlay_t* overlay);

/**
 * @brief 获取组件根对象。
 *
 * @param overlay 目标组件句柄。
 * @return 返回底层 `lv_obj_t*`；组件无效时返回 `NULL`。
 */
lv_obj_t* overlay_get_obj(overlay_t* overlay);

#ifdef __cplusplus
}
#endif

#endif
