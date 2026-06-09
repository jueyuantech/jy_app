/**
 * @file paged_text.h
 * @brief 分页文本组件接口
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-22
 * @copyright JYTek
 */
#ifndef COMMON_WIDGETS_PAGED_TEXT_H
#define COMMON_WIDGETS_PAGED_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "label.h"
#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 分页文本组件句柄。
 */
typedef struct paged_text_t paged_text_t;

/**
 * @brief 翻页步进策略。
 */
typedef enum {
    PAGED_TEXT_STEP_LINE_PAGE = 0,   ///< 按当前可见整行数翻一页。
    PAGED_TEXT_STEP_VIEW_PERCENT,    ///< 按可视高度百分比滚动。
} paged_text_step_mode_t;

/**
 * @brief 分页文本高亮窗口配置。
 */
typedef struct {
    bool enabled;            ///< 是否启用上下遮罩和高亮框。
    lv_opa_t mask_opa;       ///< 上下遮罩透明度。
    int32_t border_width;    ///< 高亮框边框宽度。
    int32_t radius;          ///< 高亮框圆角半径。
    int32_t outset;          ///< 内部文本相对高亮窗口的内缩距离。
} paged_text_highlight_cfg_t;

/**
 * @brief 分页文本组件配置。
 */
typedef struct {
    label_cfg_t label;                 ///< 文本配置；位置尺寸用于分页组件外框，样式用于内部文本组件。
    paged_text_highlight_cfg_t highlight; ///< 高亮窗口配置。
    paged_text_step_mode_t step_mode;  ///< 翻页步进策略。
    uint8_t step_percent;              ///< 百分比步进值，策略为 `PAGED_TEXT_STEP_VIEW_PERCENT` 时有效。
} paged_text_cfg_t;

/**
 * @brief 获取默认分页文本配置。
 * @return 返回默认配置。
 */
paged_text_cfg_t paged_text_default_cfg(void);

/**
 * @brief 创建分页文本组件。
 * @param parent 父对象；传 `NULL` 时使用当前活动屏幕。
 * @param cfg 组件配置；传 `NULL` 时使用默认配置。
 * @return 成功返回组件句柄，失败返回 `NULL`。
 */
paged_text_t* paged_text_create(lv_obj_t* parent, const paged_text_cfg_t* cfg);

/**
 * @brief 设置显示文本并重置到第一页。
 *
 * 组件不会拷贝完整文本，只保存 `text` 指针并预计算分页边界；
 * LVGL 标签只渲染当前页文本。调用方需要保证文本内存在组件使用期间
 * 保持有效。
 *
 * @param paged_text 目标组件句柄。
 * @param text 新文本；传 `NULL` 时按空字符串处理。
 */
void paged_text_set_text(paged_text_t* paged_text, const char* text);

/**
 * @brief 直接显示当前窗口文本并跳过分页计算。
 *
 * 调用方已经完成窗口裁剪时使用该接口；组件仍记录为单页文本，
 * 后续页码查询会返回 1 页。
 *
 * @param paged_text 目标组件句柄。
 * @param text 当前窗口文本；传 `NULL` 时按空字符串处理。
 */
void paged_text_set_visible_text(paged_text_t* paged_text, const char* text);

/**
 * @brief 设置文本字体和间距。
 * @param paged_text 目标组件句柄。
 * @param font_info 字体配置；传 `NULL` 时使用系统默认字体。
 */
void paged_text_set_font_info(paged_text_t* paged_text, const app_font_info_t* font_info);

/**
 * @brief 设置文本对齐方式。
 * @param paged_text 目标组件句柄。
 * @param align 文本对齐方式。
 */
void paged_text_set_align(paged_text_t* paged_text, label_align_t align);

/**
 * @brief 设置翻页步进策略。
 * @param paged_text 目标组件句柄。
 * @param mode 翻页步进策略。
 * @param percent 百分比步进值；传 0 时回退为 100。
 */
void paged_text_set_step_mode(paged_text_t* paged_text,
                              paged_text_step_mode_t mode,
                              uint8_t percent);

/**
 * @brief 刷新分页统计。
 * @param paged_text 目标组件句柄。
 */
void paged_text_refresh(paged_text_t* paged_text);

/**
 * @brief 跳转到第一页。
 * @param paged_text 目标组件句柄。
 */
void paged_text_page_init(paged_text_t* paged_text);

/**
 * @brief 向上翻页。
 * @param paged_text 目标组件句柄。
 * @return `true` 表示滚动位置发生变化。
 */
bool paged_text_page_up(paged_text_t* paged_text);

/**
 * @brief 向下翻页。
 * @param paged_text 目标组件句柄。
 * @return `true` 表示滚动位置发生变化。
 */
bool paged_text_page_down(paged_text_t* paged_text);

/**
 * @brief 跳转到指定页索引。
 * @param paged_text 目标组件句柄。
 * @param page_idx 页索引，从 0 开始。
 * @return `true` 表示跳转成功。
 */
bool paged_text_set_page(paged_text_t* paged_text, uint32_t page_idx);

/**
 * @brief 按源文本偏移渲染文本。
 * @param paged_text 目标组件句柄。
 * @param offset 源文本中首个渲染字符的 UTF-8 字节偏移。
 */
void paged_text_apply_text_offset(paged_text_t* paged_text, uint32_t offset);

/**
 * @brief 按文本视口坐标刷新上下遮罩和高亮框。
 * @param paged_text 目标分页文本组件。
 * @param top_mask_height 顶部遮罩底边 y 坐标，单位像素。
 * @param bottom_mask_height 底部遮罩顶边 y 坐标，单位像素。
 */
void paged_text_set_highlight_window(paged_text_t* paged_text,
                                     int32_t top_mask_height,
                                     int32_t bottom_mask_height);

/**
 * @brief 隐藏上下遮罩和高亮框。
 * @param paged_text 目标分页文本组件。
 */
void paged_text_hide_highlight_window(paged_text_t* paged_text);

/**
 * @brief 获取当前页码。
 * @param paged_text 目标组件句柄。
 * @return 返回当前页码，从 1 开始。
 */
uint32_t paged_text_get_current_page(paged_text_t* paged_text);

/**
 * @brief 获取当前页索引。
 * @param paged_text 目标组件句柄。
 * @return 返回当前页索引，从 0 开始。
 */
uint32_t paged_text_get_current_page_index(paged_text_t* paged_text);

/**
 * @brief 获取总页数。
 * @param paged_text 目标组件句柄。
 * @return 返回总页数，最小为 1。
 */
uint32_t paged_text_get_total_pages(paged_text_t* paged_text);

/**
 * @brief 获取内部文本相对分页文本视口边缘的内缩距离。
 * @param paged_text 目标组件句柄。
 * @return 返回内部文本内缩距离，组件无效时返回 0。
 */
int32_t paged_text_get_text_inset(paged_text_t* paged_text);

/**
 * @brief 获取底层 LVGL 对象。
 * @param paged_text 目标组件句柄。
 * @return 返回组件根对象；无效时返回 `NULL`。
 */
lv_obj_t* paged_text_get_obj(paged_text_t* paged_text);

#ifdef __cplusplus
}
#endif

#endif
