/**
 * @file lv_tiny_ttf.h
 *
 */

#ifndef LV_TINY_TTF_H
#define LV_TINY_TTF_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../../lv_conf_internal.h"

#if LV_USE_TINY_TTF

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

#if LV_TINY_TTF_FILE_SUPPORT != 0
/**
 * Create a font from the specified file or path with the specified line height.
 * @param path        the path or file name of the font
 * @param font_size   the font size in pixel
 * @return a font object
 */
lv_font_t * lv_tiny_ttf_create_file(const char * path, int32_t font_size);

/**
 * Create a font from the specified file or path with the specified line height with the specified cache size.
 * @param path        the path or file name of the font
 * @param font_size   the font size in pixel
 * @param kerning     kerning value in pixel
 * @param cache_size  the cache size in count
 * @return a font object
 */
lv_font_t * lv_tiny_ttf_create_file_ex(const char * path, int32_t font_size, lv_font_kerning_t kerning,
                                       size_t cache_size);
#endif

/**
 * Create a font from the specified data pointer with the specified line height.
 * @param data        the data pointer
 * @param data_size   the data size
 * @param font_size   the font size in pixel
 * @return a font object
 */
lv_font_t * lv_tiny_ttf_create_data(const void * data, size_t data_size, int32_t font_size);

/**
 * Create a font from the specified data pointer with the specified line height and the specified cache size.
 * @param data        the data pointer
 * @param data_size   the data size
 * @param font_size   the font size in pixel
 * @param kerning     kerning value in pixel
 * @param cache_size  the cache size in count
 * @return
 */
lv_font_t * lv_tiny_ttf_create_data_ex(const void * data, size_t data_size, int32_t font_size,
                                       lv_font_kerning_t kerning, size_t cache_size);

/**
 * Set the size of the font to a new font_size
 * @note the font bitmap cache and glyph cache will be flushed.
 * @param font        the font object
 * @param font_size   the font size in pixel
 */
void lv_tiny_ttf_set_size(lv_font_t * font, int32_t font_size);

/**
 * Destroy a font previously created with lv_tiny_ttf_create_xxxx()
 * @param font        the font object
 */
void lv_tiny_ttf_destroy(lv_font_t * font);

/**
 * 判断字体对象是否由 TinyTTF 创建。
 * @param font 字体对象
 * @return true 表示 TinyTTF 字体
 */
bool lv_tiny_ttf_is_font(const lv_font_t * font);

/**
 * Get a glyph descriptor by TinyTTF glyph id.
 * @param font              the font object
 * @param dsc_out           store the glyph descriptor here
 * @param glyph_id          glyph id in the TTF font
 * @param advance_override  advance width override in px, or a negative value to use font metrics
 * @return true if the glyph descriptor was found
 */
bool lv_tiny_ttf_get_glyph_dsc_by_gid(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t glyph_id,
                                      int32_t advance_override);

/**
 * Copy an SFNT table from the TinyTTF font.
 * @param font      the font object
 * @param tag       big-endian four-byte SFNT table tag
 * @param data_out  store the allocated table data here
 * @param size_out  store the table size here
 * @return true if the table was found and copied
 */
bool lv_tiny_ttf_get_table(const lv_font_t * font, uint32_t tag, uint8_t ** data_out, uint32_t * size_out);

/**
 * 获取当前 TinyTTF 字号对应的 HarfBuzz 26.6 scale。
 * @param font       字体对象
 * @param scale_out  保存 HarfBuzz scale
 * @return true 表示成功获取
 */
bool lv_tiny_ttf_get_hb_scale(const lv_font_t * font, int32_t * scale_out);

/**
 * Free table data returned by lv_tiny_ttf_get_table().
 * @param data  the table data
 */
void lv_tiny_ttf_free_table(uint8_t * data);

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_TINY_TTF*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_TINY_TTF_H*/
