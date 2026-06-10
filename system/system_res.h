/**
 * @file system_res.h
 * @brief 系统资源模块对外接口声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once
/** @ingroup app_system */

#include <lvgl/lvgl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "i18n.h"
#include "system_def.h"

/** @brief 应用层允许配置的最小字体字号。 */
#define APP_FONT_SIZE_MIN 14
/** @brief 应用层允许配置的最大字体字号。 */
#define APP_FONT_SIZE_MAX 60
/** @brief 应用层允许配置的最小行间距。 */
#define APP_FONT_ROWSPACE_MIN 0
/** @brief 应用层允许配置的最大行间距。 */
#define APP_FONT_ROWSPACE_MAX 40
/** @brief 应用层允许配置的最小字间距。 */
#define APP_FONT_WORDSPACE_MIN 0
/** @brief 应用层允许配置的最大字间距。 */
#define APP_FONT_WORDSPACE_MAX 8

/**
 * @brief 检查字号是否位于应用允许的配置范围内。
 * @param[in] font_size 待检查的字号。
 * @return `true` 表示字号在允许范围内，`false` 表示超出范围。
 */
bool app_fontsize_valid(int32_t font_size);

/**
 * @brief 检查行间距是否位于应用允许的配置范围内。
 * @param[in] row_space 待检查的行间距。
 * @return `true` 表示行间距有效，`false` 表示行间距无效。
 */
bool app_font_rowspace_valid(int32_t row_space);

/**
 * @brief 检查字间距是否位于应用允许的配置范围内。
 * @param[in] word_space 待检查的字间距。
 * @return `true` 表示字间距有效，`false` 表示字间距无效。
 */
bool app_font_wordspace_valid(int32_t word_space);

/**
 * @brief 创建一个应用层 Tiny TTF 字体对象。
 * @param[in] font_size 字号。
 * @param[in] cache_cnt Tiny TTF 缓存数量。
 * @return 成功时返回字体对象，失败时返回 `NULL`。
 */
lv_font_t* app_font_create(int32_t font_size, size_t cache_cnt);

/**
 * @brief 销毁一个应用层字体对象。
 * @param[in] font 待销毁字体对象。
 * @return 无返回值。
 */
void app_font_destroy(lv_font_t* font);

/**
 * @brief 根据图片文件名拼接系统图片完整路径。
 * @param[in] img 图片文件名。
 * @param[out] path 输出的完整路径缓冲区。
 * @param[in] size `path` 缓冲区大小。
 * @return `true` 表示成功拼接且目标文件存在，`false` 表示失败或文件不存在；失败时 `path` 会保持为空串。
 */
bool app_images_path(const char *img, char *path, size_t size);

/**
 * @brief 检查图片路径是否存在且可用。
 * @param[in] path 待检查的图片路径。
 * @return `true` 表示路径有效，`false` 表示路径无效。
 */
bool app_image_path_valid(const char *path);

/**
 * @brief 检查图片文件是否可以被当前平台解码。
 * @param[in] path 图片路径。
 * @return `true` 表示支持解码，`false` 表示当前平台不支持。
 */
bool app_image_decode_supported(const char *path);

/**
 * @brief 查询当前系统语言下的国际化字符串。
 * @param[in] key 国际化字符串键值。
 * @return 返回查询结果字符串；输入无效或查询失败时返回空串兜底，避免污染界面显示。
 */
const char *app_get_str(const char *key);

/**
 * @brief 初始化系统字体注册表并加载系统默认字体。
 * @return `true` 表示初始化成功，`false` 表示初始化失败；失败时会回滚本次已创建的字体对象。
 */
bool system_font_init(void);

/**
 * @brief 释放系统字体注册表中持有的全部字体对象。
 * @return `true` 表示释放成功，`false` 表示释放失败。
 */
bool system_font_deinit(void);

/**
 * @brief 检查指定字号是否已注册到系统字体注册表中。
 * @param[in] size 待检查的字号。
 * @return `true` 表示该字号已注册，`false` 表示该字号未注册。
 */
bool system_font_size_supported(uint32_t size);

/**
 * @brief 按精确字号获取系统注册字体。
 * @param[in] size 目标字号。
 * @return 成功时返回对应字体指针，未注册时返回 `NULL`。
 */
const lv_font_t* get_font_by_size(uint32_t size);

/**
 * @brief 按最近字号获取系统注册字体。
 * @param[in] size 目标字号。
 * @return 返回最近的已注册字体指针；若距离相同则选择更大的字号。
 */
const lv_font_t* get_font_by_size_near(uint32_t size);

/**
 * @brief 按最近字号获取系统最终采用的注册字号。
 * @param[in] size 目标字号。
 * @return 返回最近的已注册字号；若距离相同则选择更大的字号。
 */
uint32_t get_font_size_near(uint32_t size);

/**
 * @brief 获取当前系统默认字体对象。
 * @return 返回系统默认字体指针。
 */
const lv_font_t* get_system_font(void);

/**
 * @brief 获取当前系统默认字体的实际字号。
 * @return 返回系统默认字体的实际字号。
 */
uint32_t get_system_font_size(void);

/**
 * @brief 获取指定字体的单行高度。
 * @param[in] font 待查询的字体对象。
 * @return 成功时返回字体高度，失败时返回 `0`。
 */
uint32_t get_font_height(const lv_font_t* font);

/**
 * @brief 获取指定字体在给定字间距下的单行文本宽度。
 * @param[in] font 待查询的字体对象。
 * @param[in] text 待测量的文本内容。
 * @param[in] letter_space 文本字间距。
 * @return 成功时返回文本宽度，失败时返回 `0`。
 */
int32_t get_font_text_width(const lv_font_t* font, const char* text, int32_t letter_space);

/**
 * @brief 获取指定字体在给定排版参数下的文本尺寸。
 * @param[in] font 待查询的字体对象。
 * @param[out] out 输出的文本宽高结果。
 * @param[in] text 待测量的文本内容。
 * @param[in] letter_space 文本字间距。
 * @param[in] line_space 文本行间距。
 * @param[in] max_width 文本布局允许的最大宽度。
 * @return `true` 表示测量成功，`false` 表示输入参数无效。
 */
bool get_font_text_size(
    const lv_font_t* font,
    lv_point_t* out,
    const char* text,
    int32_t letter_space,
    int32_t line_space,
    int32_t max_width);

/**
 * @brief 为 LVGL 文本对象设置字体。
 * @param[in] obj 目标 LVGL 对象。
 * @param[in] font 目标字体对象。
 * @return 无返回值。
 */
void obj_set_text_font(lv_obj_t* obj, const lv_font_t* font);

/**
 * @brief 为 LVGL 文本对象同时设置字体、字间距和行间距。
 * @param[in] obj 目标 LVGL 对象。
 * @param[in] font 目标字体对象。
 * @param[in] letter_space 文本字间距。
 * @param[in] line_space 文本行间距。
 * @return 无返回值。
 */
void obj_set_text_style(lv_obj_t* obj, const lv_font_t* font, int32_t letter_space, int32_t line_space);

/**
 * @brief 获取当前系统默认字体的单行高度。
 * @return 返回系统默认字体高度。
 */
uint32_t get_system_font_height(void);

/**
 * @brief 获取当前系统默认字体的行间距配置。
 * @return 返回系统默认字体的行间距。
 */
uint32_t get_system_font_row_space(void);

/**
 * @brief 获取当前系统默认字体的字间距配置。
 * @return 返回系统默认字体的字间距。
 */
uint32_t get_system_font_word_space(void);

/**
 * @brief 按当前系统语言重新加载国际化资源。
 * @return `true` 表示重载成功，`false` 表示重载失败。
 */
bool system_i18n_reload(void);
