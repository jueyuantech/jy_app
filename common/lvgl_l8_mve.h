/** @brief LVGL L8填充及A8蒙版混合的M55 Helium接入，不改变图像混合路径。 */
#ifndef COMMON_LVGL_L8_MVE_H
#define COMMON_LVGL_L8_MVE_H
#include "lvgl/src/draw/sw/blend/lv_draw_sw_blend_private.h"
#if defined(__ARM_FEATURE_MVE) && LV_USE_DRAW_SW && LV_DRAW_SW_SUPPORT_L8
/**
 * @brief 批量混合L8颜色和可选A8蒙版，保持LVGL阈值、透明度和取整顺序。
 * @param dsc 已由LVGL裁剪的目标、行跨度、颜色、透明度及可选蒙版。
 * @return 完成处理返回LV_RESULT_OK。
 */
lv_result_t lv_l8_mve_fill(const lv_draw_sw_blend_fill_dsc_t *dsc);
#define LV_DRAW_SW_COLOR_BLEND_TO_L8(dsc) lv_l8_mve_fill(dsc) /**< 不透明填充入口。 */
#define LV_DRAW_SW_COLOR_BLEND_TO_L8_WITH_OPA(dsc) lv_l8_mve_fill(dsc) /**< 固定透明度入口。 */
#define LV_DRAW_SW_COLOR_BLEND_TO_L8_WITH_MASK(dsc) lv_l8_mve_fill(dsc) /**< A8蒙版入口。 */
#define LV_DRAW_SW_COLOR_BLEND_TO_L8_MIX_MASK_OPA(dsc) lv_l8_mve_fill(dsc) /**< 蒙版与透明度入口。 */
#endif
#endif
