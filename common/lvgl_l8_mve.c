/** @brief M55 L8颜色填充和A8蒙版混合，每批8像素，尾部保留标量精确结果。 */
#include "lvgl_l8_mve.h"
#if defined(__ARM_FEATURE_MVE) && LV_USE_DRAW_SW && LV_DRAW_SW_SUPPORT_L8
#include <arm_mve.h>

lv_result_t lv_l8_mve_fill(const lv_draw_sw_blend_fill_dsc_t *dsc)
{
    uint8_t *dst = dsc->dest_buf;
    const uint8_t *mask = dsc->mask_buf;
    uint8_t color = lv_color_luminance(dsc->color);
    uint8_t opa = dsc->opa;
    if(dsc->dest_w <= 0 || dsc->dest_h <= 0) return LV_RESULT_OK;

    if(mask == NULL && opa >= LV_OPA_MAX) {
        uint8x16_t fill = vdupq_n_u8(color);
        for(int y = 0; y < dsc->dest_h; y++) {
            int x = 0;
            for(; x + 16 <= dsc->dest_w; x += 16) vst1q_u8(dst + x, fill);
            for(; x < dsc->dest_w; x++) dst[x] = color;
            dst += dsc->dest_stride;
        }
        return LV_RESULT_OK;
    }

    uint16x8_t color_v = vdupq_n_u16(color);
    for(int y = 0; y < dsc->dest_h; y++) {
        int x = 0;
        for(; x + 8 <= dsc->dest_w; x += 8) {
            uint16x8_t mix = mask ? vldrbq_u16(mask + x) : vdupq_n_u16(opa);
            if(mask && opa < LV_OPA_MAX) mix = vshrq_n_u16(vmulq_n_u16(mix, opa), 8);
            uint16x8_t old = vldrbq_u16(dst + x);
            uint16x8_t sum = vaddq_u16(vmulq_u16(color_v, mix),
                vmulq_u16(old, vsubq_u16(vdupq_n_u16(255), mix)));
            uint16x8_t result = vshrq_n_u16(sum, 8);
            result = vpselq_u16(color_v, result, vcmpcsq_n_u16(mix, LV_OPA_MAX));
            result = vpselq_u16(old, result, vcmpeqq_n_u16(mix, 0));
            vstrbq_u16(dst + x, result);
        }
        for(; x < dsc->dest_w; x++) {
            uint8_t mix = mask ? mask[x] : opa;
            if(mask && opa < LV_OPA_MAX) mix = (uint16_t)mix * opa >> 8;
            if(mix >= LV_OPA_MAX) dst[x] = color;
            else if(mix) dst[x] = ((uint16_t)color * mix + dst[x] * (255 - mix)) >> 8;
        }
        dst += dsc->dest_stride;
        if(mask) mask += dsc->mask_stride;
    }
    return LV_RESULT_OK;
}
#endif
