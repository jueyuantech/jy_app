/**
 * @file rpmsgttf_client.c
 * @brief RPMSG TTF 字体远程渲染客户端 — APP侧实现
 *
 * 替代本地tiny_ttf(stb_truetype)引擎，通过RPMSG在M33上渲染glyph位图。
 * M55通过直接内存访问读取M33上的位图数据。
 *
 * 缓存全部在OS侧（rpmsgttf_client_lite.c）管理：
 *   - g_metric_cache (1024条): glyph metric缓存
 *   - g_cache (256条): glyph顶点+位图缓存
 * APP侧不做缓存，直接透传给OS层。
 */

#include "rpmsgttf_client.h"
#include "app_def.h"
#include "floatair_dbg.h"
#include "rpmsgttf.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* Include LVGL internals for draw_buf and cache types */
#include "lvgl/src/draw/lv_draw_buf.h"
#include "lvgl/src/core/lv_global.h"

/* Exported OS-side functions */
extern int  rpmsgttf_open_font(const char* path, int32_t font_size,
                               int32_t kerning, size_t cache_size,
                               int32_t* out_line_height, int32_t* out_base_line);
extern void rpmsgttf_close_font(int font_id);
extern int  rpmsgttf_get_glyph_dsc(int font_id, uint32_t unicode,
                                    uint32_t unicode_next,
                                    struct rpmsgttf_glyph_dsc_resp_s* out);
extern int  rpmsgttf_get_glyph_bmp(int font_id, uint32_t glyph_index,
                                    struct rpmsgttf_glyph_bmp_resp_s* out);
extern void rpmsgttf_release_glyph(int font_id, uint32_t pool_slot);

/* Font descriptor */
typedef struct {
    int32_t font_id;
    int32_t font_size;
    int32_t kerning;
    int32_t line_height;
    int32_t base_line;

    /* Bitmap wrapper for LVGL — points to OS-side buffer */
    lv_draw_buf_t bitmap_wrapper;
    uint32_t      current_pool_slot;
    uint8_t       wrapper_in_use;
} rpmsgttf_font_desc_t;

/* LVGL callbacks */
static bool rpmsgttf_get_glyph_dsc_cb(const lv_font_t* font,
                                       lv_font_glyph_dsc_t* dsc_out,
                                       uint32_t unicode_letter,
                                       uint32_t unicode_letter_next)
{
    rpmsgttf_font_desc_t* dsc = (rpmsgttf_font_desc_t*)font->dsc;

    if (unicode_letter < 0x20 ||
        unicode_letter == 0xf8ff ||
        unicode_letter == 0x200c) {
        dsc_out->box_w = 0;
        dsc_out->adv_w = 0;
        dsc_out->box_h = 0;
        dsc_out->ofs_x = 0;
        dsc_out->ofs_y = 0;
        dsc_out->format = LV_FONT_GLYPH_FORMAT_NONE;
        dsc_out->is_placeholder = false;
        dsc_out->entry = NULL;
        return true;
    }

    /* Ask OS layer (has its own metric cache) */
    struct rpmsgttf_glyph_dsc_resp_s resp;
    int ret = rpmsgttf_get_glyph_dsc(dsc->font_id, unicode_letter,
                                      unicode_letter_next, &resp);
    if (ret != 0) {
        floatair_err("[RTTF] glyph_dsc failed: font_id=%" PRId32 " U+%" PRIX32 " ret=%d",
                     dsc->font_id, unicode_letter, ret);
        return false;
    }

    /* Use kerned adv_w if available, otherwise base */
    if (font->kerning == LV_FONT_KERNING_NORMAL &&
        unicode_letter_next != 0 &&
        resp.adv_w_kerned != 0) {
        dsc_out->adv_w = resp.adv_w_kerned;
    } else {
        dsc_out->adv_w = resp.adv_w;
    }
    dsc_out->box_w = resp.box_w;
    dsc_out->box_h = resp.box_h;
    dsc_out->ofs_x = resp.ofs_x;
    dsc_out->ofs_y = resp.ofs_y;
    dsc_out->gid.index = resp.glyph_index;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->is_placeholder = false;
    dsc_out->entry = NULL;

    return true;
}

static const void* rpmsgttf_get_glyph_bitmap_cb(lv_font_glyph_dsc_t* g_dsc,
                                                  lv_draw_buf_t* draw_buf)
{
    LV_UNUSED(draw_buf);
    uint32_t glyph_index = g_dsc->gid.index;
    const lv_font_t* font = g_dsc->resolved_font;
    rpmsgttf_font_desc_t* dsc = (rpmsgttf_font_desc_t*)font->dsc;

    struct rpmsgttf_glyph_bmp_resp_s resp;
    int ret = rpmsgttf_get_glyph_bmp(dsc->font_id, glyph_index, &resp);
    if (ret != 0) {
        floatair_err("[RTTF] glyph_bmp failed: font_id=%" PRId32 " gi=%" PRIu32 " ret=%d",
                     dsc->font_id, glyph_index, ret);
        return NULL;
    }

    /* Set up bitmap wrapper pointing to OS-side buffer */
    dsc->bitmap_wrapper.header.w = resp.w;
    dsc->bitmap_wrapper.header.h = resp.h;
    dsc->bitmap_wrapper.header.stride = resp.stride;
    dsc->bitmap_wrapper.header.cf = LV_COLOR_FORMAT_A8;
    dsc->bitmap_wrapper.data_size = resp.data_size;
    dsc->bitmap_wrapper.data = (uint8_t*)(uintptr_t)resp.m33_addr;

    dsc->current_pool_slot = resp.pool_slot;
    dsc->wrapper_in_use = 1;

    g_dsc->entry = (lv_cache_entry_t*)&dsc->bitmap_wrapper;
    return &dsc->bitmap_wrapper;
}

static void rpmsgttf_release_glyph_cb(const lv_font_t* font,
                                       lv_font_glyph_dsc_t* g_dsc)
{
    rpmsgttf_font_desc_t* dsc = (rpmsgttf_font_desc_t*)font->dsc;

    if (dsc->wrapper_in_use) {
        rpmsgttf_release_glyph(dsc->font_id, dsc->current_pool_slot);
        dsc->wrapper_in_use = 0;
    }

    g_dsc->entry = NULL;
}

/* Public API */
lv_font_t* rpmsgttf_create_font(const char* path, int32_t font_size,
                                 lv_font_kerning_t kerning, size_t cache_size)
{
    LV_UNUSED(cache_size);

    if (path == NULL || font_size <= 0) {
        floatair_err("[RTTF] create_font: invalid args");
        return NULL;
    }

    rpmsgttf_font_desc_t* dsc = lv_malloc_zeroed(sizeof(rpmsgttf_font_desc_t));
    if (dsc == NULL) {
        floatair_err("[RTTF] create_font: out of memory");
        return NULL;
    }

    int32_t line_height = 0;
    int32_t base_line = 0;

    int font_id = rpmsgttf_open_font(path, font_size,
                                      (int32_t)kerning, cache_size,
                                      &line_height, &base_line);
    if (font_id < 0) {
        floatair_err("[RTTF] create_font: open_font failed: %d", font_id);
        lv_free(dsc);
        return NULL;
    }

    dsc->font_id = font_id;
    dsc->font_size = font_size;
    dsc->kerning = (int32_t)kerning;
    dsc->line_height = line_height;
    dsc->base_line = base_line;

    /* Create LVGL font object */
    lv_font_t* out_font = lv_malloc_zeroed(sizeof(lv_font_t));
    if (out_font == NULL) {
        rpmsgttf_close_font(font_id);
        lv_free(dsc);
        floatair_err("[RTTF] create_font: out of memory for font obj");
        return NULL;
    }

    out_font->get_glyph_dsc = rpmsgttf_get_glyph_dsc_cb;
    out_font->get_glyph_bitmap = rpmsgttf_get_glyph_bitmap_cb;
    out_font->release_glyph = rpmsgttf_release_glyph_cb;
    out_font->dsc = dsc;
    out_font->kerning = kerning;

    out_font->line_height = line_height;
    out_font->base_line = base_line;

    return out_font;
}

void rpmsgttf_destroy_font(lv_font_t* font)
{
    if (font == NULL) {
        return;
    }

    if (font->dsc != NULL) {
        rpmsgttf_font_desc_t* dsc = (rpmsgttf_font_desc_t*)font->dsc;
        if (dsc->wrapper_in_use) {
            rpmsgttf_release_glyph(dsc->font_id, dsc->current_pool_slot);
        }
        rpmsgttf_close_font(dsc->font_id);
        lv_free(dsc);
        font->dsc = NULL;
    }

    lv_free(font);
}
