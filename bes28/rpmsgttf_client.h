/**
 * @file rpmsgttf_client.h
 * @brief LVGL 本地字体服务与 AP 排版适配 API
 *
 * M55 保留字形绘制和缓存；复杂文本通过独立 RPMSG 服务交给 AP 排版。
 */
#pragma once

#include <lvgl/lvgl.h>
#include "lvgl/src/misc/lv_text_shape.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 创建一个使用 M55 本地字体服务的 TTF 字体对象。
 * @param[in] path TTF文件路径（两端只读 ROMFS 路径）。
 * @param[in] font_size 字号。
 * @param[in] kerning kerning模式。
 * @param[in] cache_size glyph缓存数量。
 * @return 成功时返回字体对象，失败时返回NULL。
 */
lv_font_t* rpmsgttf_create_font(const char* path, int32_t font_size,
                                 lv_font_kerning_t kerning, size_t cache_size);

/**
 * @brief 销毁一个使用 M55 本地字体服务的 TTF 字体对象。
 * @param[in] font 待销毁的字体对象。
 */
void rpmsgttf_destroy_font(lv_font_t* font);

/**
 * @brief 判断字体对象是否由 RPMSG TTF 客户端创建。
 * @param[in] font 字体对象。
 * @return 是 RPMSG TTF 字体返回 true。
 */
bool rpmsgttf_is_font(const lv_font_t* font);

/**
 * @brief 按 TTF glyph id 获取字形描述，供 HarfBuzz 整形后绘制使用。
 * @param[in] font 字体对象。
 * @param[out] dsc_out 字形描述输出。
 * @param[in] glyph_id TTF 内部 glyph id。
 * @param[in] advance_override advance 宽度覆盖值，负数表示使用字体原始 metric。
 * @return 成功返回 true。
 */
bool rpmsgttf_get_glyph_dsc_by_gid(const lv_font_t* font,
                                    lv_font_glyph_dsc_t* dsc_out,
                                    uint32_t glyph_id,
                                    int32_t advance_override);

/**
 * @brief 将完整逻辑文本交给 AP 排版，返回数组由 lv_text_shape_run_destroy 释放。
 * @param font 字体对象。
 * @param text UTF-8 文本。
 * @param length 文本字节数。
 * @param direction 文本方向。
 * @param out 字形序列和宽度。
 * @return 排版成功返回 true。
 */
bool rpmsgttf_shape_run(const lv_font_t* font, const char* text, uint32_t length,
                       lv_base_dir_t direction, lv_text_shape_run_t* out);
