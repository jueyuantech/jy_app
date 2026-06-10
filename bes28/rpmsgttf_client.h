/**
 * @file rpmsgttf_client.h
 * @brief RPMSG TTF 字体远程渲染客户端 API
 *
 * 通过RPMSG将TTF字体渲染委托给M33核心，M55通过直接内存访问读取位图数据。
 */
#pragma once

#include <lvgl/lvgl.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 创建一个通过RPMSG远程渲染的TTF字体对象。
 * @param[in] path TTF文件路径（M33本地文件系统路径）。
 * @param[in] font_size 字号。
 * @param[in] kerning kerning模式。
 * @param[in] cache_size glyph缓存数量。
 * @return 成功时返回字体对象，失败时返回NULL。
 */
lv_font_t* rpmsgttf_create_font(const char* path, int32_t font_size,
                                 lv_font_kerning_t kerning, size_t cache_size);

/**
 * @brief 销毁一个通过RPMSG远程渲染的TTF字体对象。
 * @param[in] font 待销毁的字体对象。
 */
void rpmsgttf_destroy_font(lv_font_t* font);
