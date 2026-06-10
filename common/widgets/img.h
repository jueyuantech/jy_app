#ifndef COMMON_WIDGETS_IMG_H
#define COMMON_WIDGETS_IMG_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用图片组件句柄。
 */
typedef struct img_t img_t;

/**
 * @brief 图片组件配置项。
 */
typedef struct {
    int32_t x;             ///< 组件左上角 X 坐标。
    int32_t y;             ///< 组件左上角 Y 坐标。
    int32_t w;             ///< 组件宽度；可传 `LV_SIZE_CONTENT`。
    int32_t h;             ///< 组件高度；可传 `LV_SIZE_CONTENT`。
    int32_t offset_x;      ///< 图片内容在组件内的 X 偏移。
    int32_t offset_y;      ///< 图片内容在组件内的 Y 偏移。
    uint16_t zoom;         ///< 缩放倍率；`LV_SCALE_NONE` 表示 1 倍。
    int16_t rotation;      ///< 旋转角度，单位 0.1 度。
    uint8_t opa;           ///< 图片透明度，范围 0~255。
    const void* src;       ///< 图片源，可传 LVGL 图片描述符、路径或符号。
} img_cfg_t;

/**
 * @brief 获取默认图片配置。
 *
 * @return 返回填充默认值后的配置结构体。
 */
img_cfg_t img_default_cfg(void);

/**
 * @brief 创建一个图片组件并挂载到指定父对象。
 *
 * @param parent 父对象；传 `NULL` 时退化到当前活动屏幕。
 * @param cfg 图片配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回图片句柄，失败返回 `NULL`。
 */
img_t* img_create(lv_obj_t* parent, const img_cfg_t* cfg);

/**
 * @brief 使用默认配置和指定图片源快速创建图片组件。
 *
 * @param parent 父对象；传 `NULL` 时退化到当前活动屏幕。
 * @param src 图片源，可传 LVGL 图片描述符、路径或符号。
 * @return 创建成功返回图片句柄，失败返回 `NULL`。
 */
img_t* img_create_from_src(lv_obj_t* parent, const void* src);

/**
 * @brief 设置图片源。
 *
 * @param img 目标图片句柄。
 * @param src 新图片源。
 * @return 无返回值。
 */
void img_set_src(img_t* img, const void* src);

/**
 * @brief 使用 L8 原始位图数据设置图片源。
 *
 * 调用后图片组件会内部持有一份数据副本和对应的 LVGL 描述符；
 * 后续再次调用 `img_set_src()` 或本接口时会自动释放旧副本。
 *
 * @param img 目标图片句柄。
 * @param data 原始位图数据。
 * @param data_size 数据字节数。
 * @param width 图片宽度。
 * @param height 图片高度。
 * @return `true` 表示设置成功，`false` 表示参数非法或内存申请失败。
 */
bool img_set_l8_data(img_t* img,
                     const void* data,
                     size_t data_size,
                     int32_t width,
                     int32_t height);

/**
 * @brief 获取当前图片源。
 *
 * @param img 目标图片句柄。
 * @return 返回当前图片源；组件无效时返回 `NULL`。
 */
const void* img_get_src(img_t* img);

/**
 * @brief 设置图片内容偏移。
 *
 * @param img 目标图片句柄。
 * @param offset_x 新的 X 偏移。
 * @param offset_y 新的 Y 偏移。
 * @return 无返回值。
 */
void img_set_offset(img_t* img, int32_t offset_x, int32_t offset_y);

/**
 * @brief 设置图片缩放倍率。
 *
 * @param img 目标图片句柄。
 * @param zoom 新缩放倍率；`LV_SCALE_NONE` 表示 1 倍。
 * @return 无返回值。
 */
void img_set_zoom(img_t* img, uint16_t zoom);

/**
 * @brief 设置图片旋转角度。
 *
 * @param img 目标图片句柄。
 * @param rotation 新角度，单位 0.1 度。
 * @return 无返回值。
 */
void img_set_rotation(img_t* img, int16_t rotation);

/**
 * @brief 设置图片透明度。
 *
 * @param img 目标图片句柄。
 * @param opa 新透明度，范围 0~255。
 * @return 无返回值。
 */
void img_set_opacity(img_t* img, uint8_t opa);

/**
 * @brief 按配置结构批量更新图片组件。
 *
 * @param img 目标图片句柄。
 * @param cfg 配置结构；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
void img_apply_cfg(img_t* img, const img_cfg_t* cfg);

#ifdef __cplusplus
}
#endif

#endif
