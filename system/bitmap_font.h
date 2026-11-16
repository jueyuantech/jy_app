/**
 * @file bitmap_font.h
 * @brief 点阵字库加载与缩放算法接口
 *
 * 直接访问预生成点阵字库的只读文件映射，并通过软件插值缩放至任意字号。
 * 提供三种插值算法（最近邻 / 双线扫描线 / 双线性），每种算法均有：
 *   - scalar 纯 C 可移植版本
 *   - dsp    ARM DSP 扩展加速版本 (__ARM_FEATURE_DSP)
 *   - mve    ARM Helium MVE 向量加速版本 (__ARM_FEATURE_MVE)
 * 运行时可通过 bitmap_font_set_algo() 切换算法与加速路径，便于性能对比测试。
 *
 * @author jytek
 * @date 2026-10-13
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 点阵字库插值算法选择。 */
typedef enum {
    BITMAP_FONT_ALGO_NEAREST = 0,  /**< 最近邻插值 (Nearest Neighbor) */
    BITMAP_FONT_ALGO_LINE = 1,     /**< 双线扫描线插值 (Line/Column) */
    BITMAP_FONT_ALGO_BILINEAR = 2, /**< 双线性插值 (Bilinear) */
    BITMAP_FONT_ALGO_COUNT,
} bitmap_font_algo_t;

/** 插值算法硬件加速路径选择。 */
typedef enum {
    BITMAP_FONT_ACCEL_SCALAR = 0, /**< 纯 C 可移植版本 */
    BITMAP_FONT_ACCEL_DSP = 1,    /**< ARM DSP 扩展加速 */
    BITMAP_FONT_ACCEL_MVE = 2,    /**< ARM Helium MVE 向量加速 */
    BITMAP_FONT_ACCEL_COUNT,
} bitmap_font_accel_t;

/**
 * @brief 从文件创建一个点阵字库字体对象。
 *
 * @param[in] path        点阵字库二进制文件路径 (如 /romfs/system/font/font.bin)。
 * @param[in] font_size   目标字号 (像素)。
 * @return 成功时返回 lv_font_t 指针，失败时返回 NULL。
 */
lv_font_t *bitmap_font_create_file(const char *path, int32_t font_size);

/**
 * @brief 销毁点阵字库字体对象。
 * @param[in] font 由 bitmap_font_create_file() 创建的字体对象。
 */
void bitmap_font_destroy(lv_font_t *font);

/**
 * @brief 判断字体对象是否由点阵字库创建。
 */
bool bitmap_font_is_font(const lv_font_t *font);

/**
 * @brief 设置点阵字库的插值算法。
 * @param[in] font  点阵字库字体对象。
 * @param[in] algo  插值算法。
 * @return true 表示设置成功。
 */
bool bitmap_font_set_algo(lv_font_t *font, bitmap_font_algo_t algo);

/**
 * @brief 设置点阵字库的硬件加速路径。
 * @param[in] font   点阵字库字体对象。
 * @param[in] accel  加速路径。
 * @return true 表示设置成功 (若硬件不支持该加速路径则回退 scalar)。
 */
bool bitmap_font_set_accel(lv_font_t *font, bitmap_font_accel_t accel);

/**
 * @brief 获取当前插值算法。
 */
bitmap_font_algo_t bitmap_font_get_algo(const lv_font_t *font);

/**
 * @brief 获取当前加速路径。
 */
bitmap_font_accel_t bitmap_font_get_accel(const lv_font_t *font);

/**
 * @brief 获取点阵字库的基准字号。
 */
uint8_t bitmap_font_get_base_size(const lv_font_t *font);

#ifdef __cplusplus
}
#endif
