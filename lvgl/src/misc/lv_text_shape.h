/**
 * @file lv_text_shape.h
 * @brief 复杂文本整形结果的数据结构和接口声明。
 */

#ifndef LV_TEXT_SHAPE_H
#define LV_TEXT_SHAPE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../font/lv_font.h"
#include "lv_bidi.h"
#include "lv_types.h"

/**********************
 *      TYPEDEFS
 **********************/

/**
 * 单个整形后字形的位置和映射信息。
 */
#ifdef JY_APP_SHAPE_ON_AP
#include "rpmsgshape.h"
typedef struct rpmsgshape_glyph_s lv_text_shape_glyph_t;
#else
typedef struct {
    uint32_t glyph_id;  /**< 字体内部的字形 ID */
    uint32_t cluster;   /**< 原始文本中的字节簇起点 */
    int32_t advance_x;  /**< 水平方向步进距离 */
    int32_t offset_x;   /**< 字形绘制的水平偏移 */
    int32_t offset_y;   /**< 字形绘制的垂直偏移 */
} lv_text_shape_glyph_t;
#endif

/**
 * 一段文本整形后的字形序列和总宽度。
 */
typedef struct {
    lv_text_shape_glyph_t * glyphs; /**< 整形后的字形数组，由调用方通过 destroy 释放 */
    uint32_t glyph_count;           /**< 字形数量 */
    int32_t width;                  /**< 整段文本的水平宽度 */
} lv_text_shape_run_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * 判断指定字体和文本是否可以使用复杂文本整形。
 * @param font 字体对象
 * @param text 待整形文本
 * @return 可整形时返回 true，否则返回 false
 */
bool lv_text_shape_available(const lv_font_t * font, const char * text);

/**
 * 判断指定字体和文本片段是否可以使用复杂文本整形。
 * @param font 字体对象
 * @param text 待整形文本
 * @param byte_len 待判断文本片段的字节长度
 * @return 可整形时返回 true，否则返回 false
 */
bool lv_text_shape_available_range(const lv_font_t * font, const char * text, uint32_t byte_len);

/**
 * 对一段文本执行复杂文本整形。
 * @param font 字体对象
 * @param text 待整形文本
 * @param byte_len 待整形文本的字节长度
 * @param base_dir 文本基础方向
 * @param run_out 输出整形结果，函数会先清零该结构
 * @return 整形成功返回 true，否则返回 false
 */
bool lv_text_shape_run(const lv_font_t * font, const char * text, uint32_t byte_len,
                       lv_base_dir_t base_dir, lv_text_shape_run_t * run_out);

/**
 * 判断字体对象是否支持复杂文本整形所需的表读取和 glyph id 绘制。
 * @param font 字体对象
 * @return 支持返回 true
 */
bool lv_text_shape_font_available(const lv_font_t * font);

/**
 * 按 glyph id 获取字形描述。
 * @param font 字体对象
 * @param dsc_out 字形描述输出
 * @param glyph_id 字体内部 glyph id
 * @param advance_override advance 宽度覆盖值，负数表示使用字体 metric
 * @return 成功返回 true
 */
bool lv_text_shape_get_glyph_dsc_by_gid(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out,
                                        uint32_t glyph_id, int32_t advance_override);

/**
 * 销毁整形结果并清空结构内容。
 * @param run 待销毁的整形结果
 */
void lv_text_shape_run_destroy(lv_text_shape_run_t * run);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_TEXT_SHAPE_H*/
