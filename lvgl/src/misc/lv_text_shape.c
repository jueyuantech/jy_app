/**
 * @file lv_text_shape.c
 * @brief 复杂文本整形接口的 no-op fallback 和 HarfBuzz 转发入口。
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_text_shape.h"
#ifdef JY_APP_SHAPE_ON_AP
#include <stdlib.h>
#endif

#if LV_USE_TINY_TTF
#include "../libs/tiny_ttf/lv_tiny_ttf.h"
#endif
#if defined(CONFIG_RPMSG_TTF_CLIENT) && CONFIG_RPMSG_TTF_CLIENT
#include "rpmsgttf_client.h"
#endif
#include "../stdlib/lv_mem.h"
#include "../stdlib/lv_string.h"
#include "lv_text_private.h"
#include "lv_bidi_private.h"
#if LV_USE_HARFBUZZ_SHAPER
#include <SheenBidi/SheenBidi.h>
#endif

/**********************
 * GLOBAL PROTOTYPES
 **********************/
#if LV_USE_HARFBUZZ_SHAPER && !defined(JY_APP_SHAPE_ON_AP)
bool lv_text_shape_hb_run(const lv_font_t * font, const char * text, uint32_t byte_len,
                          lv_base_dir_t base_dir, lv_text_shape_run_t * run_out);
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/

#if LV_USE_HARFBUZZ_SHAPER

/**
 * 判断 Unicode 码点是否属于需要 HarfBuzz 整形或复杂标记定位的脚本。
 * @param letter Unicode 码点
 * @return 需要整形返回 true，否则返回 false
 */
static bool lv_text_shape_is_complex_letter(uint32_t letter)
{
    if(letter == 0x200C || letter == 0x200D) return true; /* ZWNJ / ZWJ */
    if(letter == 0x200E || letter == 0x200F || letter == 0x061C) return true;
    if(letter >= 0x0300 && letter <= 0x036F) return true; /* Combining Diacritical Marks */

    if(letter >= 0x0660 && letter <= 0x0669) return false; /* Arabic-Indic digits do not need shaping */
    if(letter >= 0x06F0 && letter <= 0x06F9) return false; /* Extended Arabic-Indic digits do not need shaping */

    if(letter >= 0x0590 && letter <= 0x08FF) return true; /* Hebrew, Arabic and related RTL scripts */

    if(letter >= 0x0900 && letter <= 0x0DFF) return true; /* Indic scripts and Sinhala */
    if(letter >= 0x0E00 && letter <= 0x0EFF) return true; /* Thai and Lao */
    if(letter >= 0x0F00 && letter <= 0x109F) return true; /* Tibetan and Myanmar */

    if(letter >= 0x1780 && letter <= 0x18AF) return true; /* Khmer and Mongolian */
    if(letter >= 0xA8E0 && letter <= 0xA8FF) return true; /* Devanagari Extended */
    if(letter >= 0xA9E0 && letter <= 0xA9FF) return true; /* Myanmar Extended-B */
    if(letter >= 0xAA60 && letter <= 0xAA7F) return true; /* Myanmar Extended-A */

    if(letter >= 0xFB50 && letter <= 0xFDFF) return true; /* Arabic Presentation Forms-A */
    if(letter >= 0xFE70 && letter <= 0xFEFF) return true; /* Arabic Presentation Forms-B */

    return false;
}

/**
 * 判断文本片段是否包含需要 HarfBuzz 介入的复杂文字。
 * @param text 待检查文本
 * @param byte_len 待检查文本片段的字节长度；传 UINT32_MAX 表示检查到字符串结束
 * @return 需要整形返回 true，否则返回 false
 */
static bool lv_text_shape_text_needs_harfbuzz(const char * text, uint32_t byte_len)
{
    uint32_t i = 0;

    if(text == NULL || text[0] == '\0' || byte_len == 0) {
        return false;
    }

    while(text[i] != '\0' && i < byte_len) {
        uint32_t before = i;
        uint32_t letter = lv_text_encoded_next(text, &i);

        if(i <= before) {
            break;
        }
        if(i > byte_len && byte_len != UINT32_MAX) {
            break;
        }
        if(lv_text_shape_is_complex_letter(letter)) {
            return true;
        }
    }

    return false;
}


/**
 * @brief 按脚本切分单一方向片段，并将字形合并为视觉顺序。
 * @param font 字体；text 全文；start/length 片段字节范围；dir 方向；out 累积输出。
 * @return 成功返回 true，失败由外层释放累积结果。
 */
static bool lv_text_shape_direction_run(const lv_font_t * font, const char * text,
                                        uint32_t start, uint32_t length, lv_base_dir_t dir,
                                        lv_text_shape_run_t * out)
{
    uint32_t end = start + length;
    uint32_t group_start = out->glyph_count;
    while(start < end) {
        uint32_t cursor = start;
        SBScript script = SBScriptNil;
        while(cursor < end) {
            uint32_t before = cursor;
            uint32_t letter = lv_text_encoded_next(text, &cursor);
            if(cursor <= before || cursor > end) return false;
            SBScript next = SBCodepointGetScript(letter);
            if(next > SBScriptZZZZ) {
                if(script != SBScriptNil && next != script) { cursor = before; break; }
                script = next;
            }
        }
        lv_text_shape_run_t part = {0};
#ifdef JY_APP_SHAPE_ON_AP
        bool ok = rpmsgttf_shape_run(font, text + start, cursor - start, dir, &part);
#else
        bool ok = lv_text_shape_hb_run(font, text + start, cursor - start, dir, &part);
#endif
        if(!ok) { lv_text_shape_run_destroy(&part); return false; }
        if(part.glyph_count > SIZE_MAX / sizeof(*out->glyphs) - out->glyph_count ||
           (int64_t)out->width + part.width > INT32_MAX ||
           (int64_t)out->width + part.width < INT32_MIN) {
            lv_text_shape_run_destroy(&part); return false;
        }
        /* 常见的单脚本文本直接转移所有权，避免额外复制整段字形。 */
        if(out->glyph_count == 0) {
            *out = part;
            for(uint32_t i = 0; i < out->glyph_count; i++) out->glyphs[i].cluster += start;
            start = cursor;
            continue;
        }
        size_t size = (out->glyph_count + part.glyph_count) * sizeof(*out->glyphs);
#ifdef JY_APP_SHAPE_ON_AP
        lv_text_shape_glyph_t * joined = realloc(out->glyphs, size);
#else
        lv_text_shape_glyph_t * joined = lv_realloc(out->glyphs, size);
#endif
        if(!joined) { lv_text_shape_run_destroy(&part); return false; }
        out->glyphs = joined;
        uint32_t at = dir == LV_BASE_DIR_RTL ? group_start : out->glyph_count;
        if(at < out->glyph_count) {
            lv_memmove(joined + at + part.glyph_count, joined + at,
                       (out->glyph_count - at) * sizeof(*joined));
        }
        for(uint32_t i = 0; i < part.glyph_count; i++) {
            joined[at + i] = part.glyphs[i];
            joined[at + i].cluster += start;
        }
        out->glyph_count += part.glyph_count;
        out->width += part.width;
        lv_text_shape_run_destroy(&part);
        start = cursor;
    }
    return true;
}

#endif

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool lv_text_shape_available(const lv_font_t * font, const char * text)
{
#if LV_USE_HARFBUZZ_SHAPER
    return lv_text_shape_available_range(font, text, UINT32_MAX);
#else
    LV_UNUSED(font);
    LV_UNUSED(text);

    return false;
#endif
}

bool lv_text_shape_available_range(const lv_font_t * font, const char * text, uint32_t byte_len)
{
#if LV_USE_HARFBUZZ_SHAPER
    return lv_text_shape_font_available(font) && lv_text_shape_text_needs_harfbuzz(text, byte_len);
#else
    LV_UNUSED(font);
    LV_UNUSED(text);
    LV_UNUSED(byte_len);

    return false;
#endif
}

bool lv_text_shape_run(const lv_font_t * font, const char * text, uint32_t byte_len,
                       lv_base_dir_t base_dir, lv_text_shape_run_t * run_out)
{
    if(run_out == NULL) {
        return false;
    }

    lv_memzero(run_out, sizeof(*run_out));

    if(font == NULL || text == NULL || byte_len == 0) {
        return false;
    }

#if LV_USE_HARFBUZZ_SHAPER
    bool ok = false;
#if LV_USE_BIDI
    lv_bidi_visual_runs_t runs;
    if(lv_bidi_get_visual_runs(text, byte_len, base_dir, &runs)) {
        ok = true;
        for(uint32_t i = 0; ok && i < runs.run_count; i++) {
            ok = lv_text_shape_direction_run(font, text, runs.runs[i].byte_start,
                                             runs.runs[i].byte_len, runs.runs[i].base_dir, run_out);
        }
        lv_bidi_visual_runs_destroy(&runs);
    }
#else
    ok = lv_text_shape_direction_run(font, text, 0, byte_len, base_dir, run_out);
#endif
    if(!ok) {
        lv_text_shape_run_destroy(run_out);
    }

    return ok;
#else
    LV_UNUSED(font);
    LV_UNUSED(text);
    LV_UNUSED(byte_len);
    LV_UNUSED(base_dir);

    return false;
#endif
}

bool lv_text_shape_font_available(const lv_font_t * font)
{
#if LV_USE_HARFBUZZ_SHAPER
#if LV_USE_TINY_TTF
    if(lv_tiny_ttf_is_font(font)) {
        return true;
    }
#endif
#if defined(CONFIG_RPMSG_TTF_CLIENT) && CONFIG_RPMSG_TTF_CLIENT
    if(rpmsgttf_is_font(font)) {
        return true;
    }
#endif
#else
    LV_UNUSED(font);
#endif

    return false;
}

bool lv_text_shape_get_glyph_dsc_by_gid(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out,
                                        uint32_t glyph_id, int32_t advance_override)
{
#if LV_USE_HARFBUZZ_SHAPER
#if LV_USE_TINY_TTF
    if(lv_tiny_ttf_get_glyph_dsc_by_gid(font, dsc_out, glyph_id, advance_override)) {
        return true;
    }
#endif
#if defined(CONFIG_RPMSG_TTF_CLIENT) && CONFIG_RPMSG_TTF_CLIENT
    if(rpmsgttf_get_glyph_dsc_by_gid(font, dsc_out, glyph_id, advance_override)) {
        return true;
    }
#endif
#else
    LV_UNUSED(font);
    LV_UNUSED(dsc_out);
    LV_UNUSED(glyph_id);
    LV_UNUSED(advance_override);
#endif

    return false;
}

void lv_text_shape_run_destroy(lv_text_shape_run_t * run)
{
    if(run == NULL) {
        return;
    }

    if(run->glyphs) {
#ifdef JY_APP_SHAPE_ON_AP
        free(run->glyphs); /* OS 返回系统堆对象，和 LVGL 分配器无关。 */
#else
        lv_free(run->glyphs);
#endif
    }

    lv_memzero(run, sizeof(*run));
}
