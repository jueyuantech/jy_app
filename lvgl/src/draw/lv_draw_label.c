/**
 * @file lv_draw_label.c
 * @brief LVGL文字绘制任务、字符遍历及字形提交实现。
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_label_private.h"
#include "lv_draw_private.h"
#include "../misc/lv_area_private.h"
#include "lv_draw_vector_private.h"
#include "lv_draw_rect_private.h"
#include "../core/lv_obj.h"
#include "../misc/lv_math.h"
#include "../core/lv_obj_event.h"
#include "../misc/lv_bidi_private.h"
#include "../misc/lv_text_private.h"
#include "../misc/lv_text_shape.h"
#include "../misc/lv_assert.h"
#include "../libs/tiny_ttf/lv_tiny_ttf.h"
#include "../stdlib/lv_mem.h"
#include "../stdlib/lv_string.h"
#include "../core/lv_global.h"

/*********************
 *      DEFINES
 *********************/
#define LABEL_RECOLOR_PAR_LENGTH 6
#define LV_LABEL_HINT_UPDATE_TH 1024 /*Update the "hint" if the label's y coordinates have changed more then this*/

#define font_draw_buf_handlers &(LV_GLOBAL_DEFAULT()->font_draw_buf_handlers)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void draw_letter(lv_draw_unit_t * draw_unit, lv_draw_glyph_dsc_t * dsc,  const lv_point_t * pos,
                        const lv_font_t * font, uint32_t letter, bool is_glyph_id,
                        const lv_font_glyph_dsc_t * measured, lv_draw_glyph_cb_t cb);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * 按单行内容检测基础方向，避免整段首行影响后续行。
 *
 * @param text 待检测文本
 * @param byte_len 文本字节长度
 * @param fallback 未检测到强方向字符时使用的方向
 * @return 该行的基础方向
 */
static lv_base_dir_t lv_draw_label_detect_line_base_dir(const char * text, uint32_t byte_len, lv_base_dir_t fallback)
{
    uint32_t i = 0;

    if(text == NULL || byte_len == 0) return fallback;

    while(text[i] != '\0' && i < byte_len) {
        uint32_t before = i;
        uint32_t letter = lv_text_encoded_next(text, &i);

        if(i <= before) break;
        if(i > byte_len) break;

        if((letter >= 0x0590 && letter <= 0x05FF) ||
           (letter >= 0x0600 && letter <= 0x065F) ||
           (letter >= 0x0670 && letter <= 0x06EF) ||
           (letter >= 0x0700 && letter <= 0x08FF) ||
           (letter >= 0xFB50 && letter <= 0xFDFF) ||
           (letter >= 0xFE70 && letter <= 0xFEFF)) {
            return LV_BASE_DIR_RTL;
        }
        if((letter >= '0' && letter <= '9') ||
           (letter >= 'A' && letter <= 'Z') ||
           (letter >= 'a' && letter <= 'z') ||
           (letter >= 0x00C0 && letter <= 0x02AF) ||
           (letter >= 0x0370 && letter <= 0x052F) ||
           (letter >= 0x1E00 && letter <= 0x1EFF) ||
           (letter >= 0x3040 && letter <= 0x30FF) ||
           (letter >= 0x1780 && letter <= 0x18AF) ||
           (letter >= 0x3400 && letter <= 0x9FFF) ||
           (letter >= 0xAC00 && letter <= 0xD7AF)) {
            return LV_BASE_DIR_LTR;
        }
    }

    return fallback;
}

/**
 * 获取实际参与整形的单行字节长度，去掉 LVGL 行尾换行控制字符。
 *
 * @param text 待处理单行文本
 * @param byte_len 原始行字节长度
 * @return 去掉行尾换行控制字符后的字节长度
 */
static uint32_t lv_draw_label_shape_line_byte_len(const char * text, uint32_t byte_len)
{
    if(text == NULL) return 0;

    while(byte_len > 0) {
        char c = text[byte_len - 1];
        if(c != '\n' && c != '\r') {
            break;
        }
        byte_len--;
    }

    return byte_len;
}

void lv_draw_label_dsc_init(lv_draw_label_dsc_t * dsc)
{
    lv_memzero(dsc, sizeof(lv_draw_label_dsc_t));
    dsc->opa = LV_OPA_COVER;
    dsc->color = lv_color_black();
    dsc->font = LV_FONT_DEFAULT;
    dsc->sel_start = LV_DRAW_LABEL_NO_TXT_SEL;
    dsc->sel_end = LV_DRAW_LABEL_NO_TXT_SEL;
    dsc->sel_color = lv_color_black();
    dsc->sel_bg_color = lv_palette_main(LV_PALETTE_BLUE);
    dsc->bidi_dir = LV_BASE_DIR_LTR;
    dsc->base.dsc_size = sizeof(lv_draw_label_dsc_t);
}

lv_draw_label_dsc_t * lv_draw_task_get_label_dsc(lv_draw_task_t * task)
{
    return task->type == LV_DRAW_TASK_TYPE_LABEL ? (lv_draw_label_dsc_t *)task->draw_dsc : NULL;
}

void lv_draw_glyph_dsc_init(lv_draw_glyph_dsc_t * dsc)
{
    lv_memzero(dsc, sizeof(lv_draw_glyph_dsc_t));
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_label(lv_layer_t * layer, const lv_draw_label_dsc_t * dsc,
                                         const lv_area_t * coords)
{
    if(dsc->opa <= LV_OPA_MIN) return;
    if(dsc->text == NULL || dsc->text[0] == '\0') return;
    if(dsc->font == NULL) {
        LV_LOG_WARN("dsc->font == NULL");
        return;
    }

    LV_PROFILER_BEGIN;
    lv_draw_task_t * t = lv_draw_add_task(layer, coords);

    t->draw_dsc = lv_malloc(sizeof(*dsc));
    lv_memcpy(t->draw_dsc, dsc, sizeof(*dsc));
    t->type = LV_DRAW_TASK_TYPE_LABEL;

    /*The text is stored in a local variable so malloc memory for it*/
    if(dsc->text_local) {
        lv_draw_label_dsc_t * new_dsc = t->draw_dsc;
        new_dsc->text = lv_strdup(dsc->text);
    }

    lv_draw_finalize_task_creation(layer, t);
    LV_PROFILER_END;
}

void LV_ATTRIBUTE_FAST_MEM lv_draw_character(lv_layer_t * layer, lv_draw_label_dsc_t * dsc,
                                             const lv_point_t * point, uint32_t unicode_letter)
{
    if(dsc->opa <= LV_OPA_MIN) return;
    if(dsc->font == NULL) {
        LV_LOG_WARN("dsc->font == NULL");
        return;
    }

    if(lv_text_is_marker(unicode_letter)) return;

    LV_PROFILER_BEGIN;

    lv_font_glyph_dsc_t g;
    lv_font_get_glyph_dsc(dsc->font, &g, unicode_letter, 0);

    lv_area_t a;
    a.x1 = point->x;
    a.y1 = point->y;
    a.x2 = a.x1 + g.adv_w;
    a.y2 = a.y1 + lv_font_get_line_height(g.resolved_font ? g.resolved_font : dsc->font);

    /*lv_draw_label needs UTF8 text so convert the Unicode character to an UTF8 string */
    uint32_t letter_buf[2];
    letter_buf[0] = lv_text_unicode_to_encoded(unicode_letter);
    letter_buf[1] = '\0';

    const char * letter_buf_char = (const char *)letter_buf;

#if LV_BIG_ENDIAN_SYSTEM
    while(*letter_buf_char == 0) ++letter_buf_char;
#endif

    dsc->text = letter_buf_char;
    dsc->text_local = 1;

    lv_draw_label(layer, dsc, &a);
    LV_PROFILER_END;
}

/**
 * @brief 判断字形所在的完整 cluster 是否与逻辑字符选区相交。
 * @param dsc 标签样式；run 字形序列；index 字形下标；line_start/byte_len 行字节范围。
 * @return 相交时整个组合字形一起选中。
 */
static bool lv_draw_label_cluster_selected(const lv_draw_label_dsc_t * dsc,
                                          const lv_text_shape_run_t * run, uint32_t index,
                                          uint32_t line_start, uint32_t byte_len)
{
    if(dsc->sel_start == LV_DRAW_LABEL_NO_TXT_SEL || dsc->sel_end == LV_DRAW_LABEL_NO_TXT_SEL) return false;
    uint32_t start = run->glyphs[index].cluster;
    uint32_t end = byte_len;
    for(uint32_t i = 0; i < run->glyph_count; i++) {
        uint32_t next = run->glyphs[i].cluster;
        if(next > start && next < end) end = next;
    }
    uint32_t first = lv_text_encoded_get_char_id(dsc->text, line_start + start);
    uint32_t last = lv_text_encoded_get_char_id(dsc->text, line_start + end);
    uint32_t sel_start = LV_MIN(dsc->sel_start, dsc->sel_end);
    uint32_t sel_end = LV_MAX(dsc->sel_start, dsc->sel_end);
    return sel_start < sel_end && first < sel_end && last > sel_start;
}

/**
 * @brief 先绘制完整选区背景，再按同一整形结果绘字，避免背景覆盖组合音标。
 * @param draw_unit 绘制单元；draw_letter_dsc 字形样式；bg_coords 背景；pos 行位置。
 * @param x 起点；line_height 行高；font 字体；shaped_run 字形；letter_space 字间距。
 * @param dsc 标签样式；line_start/byte_len 原文范围；cb 绘制回调。
 * @return 绘制结束的水平位置。
 */
static int32_t lv_draw_label_draw_shaped_run(lv_draw_unit_t * draw_unit, lv_draw_glyph_dsc_t * draw_letter_dsc,
                                             lv_area_t * bg_coords, const lv_point_t * pos, int32_t x,
                                             int32_t line_height, const lv_font_t * font,
                                             const lv_text_shape_run_t * shaped_run, int32_t letter_space,
                                             const lv_draw_label_dsc_t * dsc, uint32_t line_start,
                                             uint32_t byte_len, lv_draw_glyph_cb_t cb)
{
    int32_t shaped_x = x;
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.opa = dsc->opa;
    fill.color = dsc->sel_bg_color;
    for(unsigned pass = 0; pass < 2; pass++) {
        shaped_x = x;
        for(uint32_t i = 0; i < shaped_run->glyph_count; i++) {
            int32_t advance = shaped_run->glyphs[i].advance_x;
            int32_t spacing = i + 1 < shaped_run->glyph_count &&
                             shaped_run->glyphs[i].cluster != shaped_run->glyphs[i + 1].cluster ? letter_space : 0;
            bool selected = lv_draw_label_cluster_selected(dsc, shaped_run, i, line_start, byte_len);
            bg_coords->x1 = shaped_x;
            bg_coords->y1 = pos->y;
            bg_coords->x2 = shaped_x + LV_MAX(advance + spacing, 1) - 1;
            bg_coords->y2 = pos->y + line_height - 1;
            if(pass == 0) {
                if(selected) cb(draw_unit, NULL, &fill, bg_coords);
            }
            else {
                lv_point_t glyph_pos;
                glyph_pos.x = shaped_x + shaped_run->glyphs[i].offset_x;
                glyph_pos.y = pos->y - shaped_run->glyphs[i].offset_y;
                draw_letter_dsc->color = selected ? dsc->sel_color : dsc->color;
                draw_letter(draw_unit, draw_letter_dsc, &glyph_pos, font, shaped_run->glyphs[i].glyph_id, true, NULL, cb);
            }
            shaped_x += advance + spacing;
        }
    }
    return shaped_x;
}

/**
 * 绘制文本装饰线。
 *
 * @param draw_unit 绘制单元
 * @param dsc 标签绘制描述符
 * @param fill_dsc 填充描述符
 * @param font 字体对象
 * @param start_x 装饰线起始 x 坐标
 * @param end_x 装饰线结束 x 坐标
 * @param y 文本行 y 坐标
 * @param underline_width 装饰线宽度
 * @param cb 填充绘制回调
 */
static void lv_draw_label_draw_decor(lv_draw_unit_t * draw_unit, const lv_draw_label_dsc_t * dsc,
                                     lv_draw_fill_dsc_t * fill_dsc, const lv_font_t * font,
                                     int32_t start_x, int32_t end_x, int32_t y, int32_t underline_width,
                                     lv_draw_glyph_cb_t cb)
{
    if(end_x < start_x) return;

    if(dsc->decor & LV_TEXT_DECOR_UNDERLINE) {
        lv_area_t fill_area;
        fill_area.x1 = start_x;
        fill_area.x2 = end_x;
        fill_area.y1 = y + font->line_height - font->base_line - font->underline_position;
        fill_area.y2 = fill_area.y1 + underline_width - 1;

        fill_dsc->color = dsc->color;
        cb(draw_unit, NULL, fill_dsc, &fill_area);
    }
    if(dsc->decor & LV_TEXT_DECOR_STRIKETHROUGH) {
        lv_area_t fill_area;
        fill_area.x1 = start_x;
        fill_area.x2 = end_x;
        fill_area.y1 = y + (font->line_height - font->base_line) * 2 / 3 + font->underline_thickness / 2;
        fill_area.y2 = fill_area.y1 + underline_width - 1;

        fill_dsc->color = dsc->color;
        cb(draw_unit, NULL, fill_dsc, &fill_area);
    }
}

void lv_draw_label_iterate_characters(lv_draw_unit_t * draw_unit, const lv_draw_label_dsc_t * dsc,
                                      const lv_area_t * coords,
                                      lv_draw_glyph_cb_t cb)
{
    const lv_font_t * font = dsc->font;
    int32_t w;

    lv_area_t clipped_area;
    bool clip_ok = lv_area_intersect(&clipped_area, coords, draw_unit->clip_area);
    if(!clip_ok) return;

    lv_text_align_t align = dsc->align;
    lv_base_dir_t base_dir = dsc->bidi_dir;

    lv_bidi_calculate_align(&align, &base_dir, dsc->text);

    if((dsc->flag & LV_TEXT_FLAG_EXPAND) == 0) {
        /*Normally use the label's width as width*/
        w = lv_area_get_width(coords);
    }
    else {
        /*If EXPAND is enabled then not limit the text's width to the object's width*/
        lv_point_t p;
        lv_text_get_size(&p, dsc->text, dsc->font, dsc->letter_space, dsc->line_space, LV_COORD_MAX,
                         dsc->flag);
        w = p.x;
    }

    int32_t line_height_font = lv_font_get_line_height(font);
    int32_t line_height = line_height_font + dsc->line_space;

    /*Init variables for the first line*/
    int32_t line_width = 0;
    lv_point_t pos;
    lv_point_set(&pos, coords->x1, coords->y1);

    int32_t x_ofs = 0;
    int32_t y_ofs = 0;
    x_ofs = dsc->ofs_x;
    y_ofs = dsc->ofs_y;
    pos.y += y_ofs;

    uint32_t line_start     = 0;
    int32_t last_line_start = -1;

    /*Check the hint to use the cached info*/
    if(dsc->hint && y_ofs == 0 && coords->y1 < 0) {
        /*If the label changed too much recalculate the hint.*/
        if(LV_ABS(dsc->hint->coord_y - coords->y1) > LV_LABEL_HINT_UPDATE_TH - 2 * line_height) {
            dsc->hint->line_start = -1;
        }
        last_line_start = dsc->hint->line_start;
    }

    /*Use the hint if it's valid*/
    if(dsc->hint && last_line_start >= 0) {
        line_start = last_line_start;
        pos.y += dsc->hint->y;
    }

    uint32_t line_end = line_start + lv_text_get_next_line(&dsc->text[line_start], font, dsc->letter_space, w, NULL,
                                                           dsc->flag);

    /*Go the first visible line*/
    while(pos.y + line_height_font < draw_unit->clip_area->y1) {
        /*Go to next line*/
        line_start = line_end;
        line_end += lv_text_get_next_line(&dsc->text[line_start], font, dsc->letter_space, w, NULL, dsc->flag);
        pos.y += line_height;

        /*Save at the threshold coordinate*/
        if(dsc->hint && pos.y >= -LV_LABEL_HINT_UPDATE_TH && dsc->hint->line_start < 0) {
            dsc->hint->line_start = line_start;
            dsc->hint->y          = pos.y - coords->y1;
            dsc->hint->coord_y    = coords->y1;
        }

        if(dsc->text[line_start] == '\0') return;
    }

    /*Align to middle*/
    if(align == LV_TEXT_ALIGN_CENTER) {
        line_width = lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, dsc->letter_space);

        pos.x += (lv_area_get_width(coords) - line_width) / 2;

    }
    /*Align to the right*/
    else if(align == LV_TEXT_ALIGN_RIGHT) {
        line_width = lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, dsc->letter_space);
        pos.x += lv_area_get_width(coords) - line_width;
    }

    uint32_t sel_start = dsc->sel_start;
    uint32_t sel_end = dsc->sel_end;
    if(sel_start > sel_end) {
        uint32_t tmp = sel_start;
        sel_start = sel_end;
        sel_end = tmp;
    }
    bool selection_active = sel_start != LV_DRAW_LABEL_NO_TXT_SEL && sel_end != LV_DRAW_LABEL_NO_TXT_SEL;

    lv_area_t bg_coords;
    lv_draw_glyph_dsc_t draw_letter_dsc;
    lv_draw_glyph_dsc_init(&draw_letter_dsc);
    draw_letter_dsc.opa = dsc->opa;
    draw_letter_dsc.bg_coords = &bg_coords;
    draw_letter_dsc.color = dsc->color;

    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.opa = dsc->opa;
    int32_t underline_width = font->underline_thickness ? font->underline_thickness : 1;
    int32_t line_start_x;
    uint32_t i;
    int32_t letter_w;

    /*Write out all lines*/
    while(dsc->text[line_start] != '\0') {
        const char * line_txt = dsc->text + line_start;
        uint32_t line_byte_len = line_end - line_start;
        uint32_t shape_byte_len = lv_draw_label_shape_line_byte_len(line_txt, line_byte_len);
        lv_base_dir_t line_base_dir = lv_draw_label_detect_line_base_dir(line_txt, shape_byte_len, base_dir);

        pos.x += x_ofs;
        line_start_x = pos.x;

        /*Write all letter of a line*/
        i = 0;

        bool line_shaped = false;
        if(lv_text_shape_available_range(font, line_txt, shape_byte_len)) {
            lv_text_shape_run_t shaped_run;
            if(lv_text_shape_run(font, line_txt, shape_byte_len, line_base_dir, &shaped_run)) {
                int32_t shaped_x = lv_draw_label_draw_shaped_run(draw_unit, &draw_letter_dsc, &bg_coords, &pos, pos.x,
                                                                 line_height, font, &shaped_run, dsc->letter_space,
                                                                 dsc, line_start, shape_byte_len, cb);

                lv_draw_label_draw_decor(draw_unit, dsc, &fill_dsc, font, line_start_x, shaped_x - 1, pos.y,
                                         underline_width, cb);

                lv_text_shape_run_destroy(&shaped_run);
                line_shaped = true;
            }
        }

#if LV_USE_BIDI
        char * bidi_txt = NULL;
#else
        const char * bidi_txt = line_txt;
#endif

        while(!line_shaped && i < line_end - line_start) {
#if LV_USE_BIDI
            if(bidi_txt == NULL) {
                bidi_txt = lv_malloc(line_end - line_start + 1);
                LV_ASSERT_MALLOC(bidi_txt);
                lv_bidi_process_paragraph(line_txt, bidi_txt, line_byte_len, line_base_dir, NULL, 0);
            }
#endif
            uint32_t logical_char_pos = 0;
            if(selection_active) {
#if LV_USE_BIDI
                logical_char_pos = lv_text_encoded_get_char_id(dsc->text, line_start);
                uint32_t t = lv_text_encoded_get_char_id(bidi_txt, i);
                logical_char_pos += lv_bidi_get_logical_pos(line_txt, NULL, line_byte_len, line_base_dir, t, NULL);
#else
                logical_char_pos = lv_text_encoded_get_char_id(dsc->text, line_start + i);
#endif
            }

            uint32_t letter;
            uint32_t letter_next;
            lv_text_encoded_letter_next_2(bidi_txt, &letter, &letter_next, &i);

            /* 测宽与绘制复用同一无字偶距描述；有字偶距字体仍按next=0重新取绘制度量。 */
            lv_font_glyph_dsc_t measured = {0};
            const lv_font_glyph_dsc_t * reusable = NULL;
            if(lv_text_is_marker(letter)) {
                letter_w = 0;
            }
            else {
                bool found = lv_font_get_glyph_dsc(font, &measured, letter, letter_next);
                letter_w = measured.adv_w;
                if(found && !measured.is_placeholder && measured.resolved_font &&
                   measured.resolved_font->kerning == LV_FONT_KERNING_NONE)
                    reusable = &measured;
            }

            /*Always set the bg_coordinates for placeholder drawing*/
            bg_coords.x1 = pos.x;
            bg_coords.y1 = pos.y;
            bg_coords.x2 = pos.x + letter_w - 1;
            bg_coords.y2 = pos.y + line_height - 1;

            if(i >= line_end - line_start) {
                if(dsc->decor & LV_TEXT_DECOR_UNDERLINE) {
                    lv_area_t fill_area;
                    fill_area.x1 = line_start_x;
                    fill_area.x2 = pos.x + letter_w - 1;
                    fill_area.y1 = pos.y + font->line_height - font->base_line - font->underline_position;
                    fill_area.y2 = fill_area.y1 + underline_width - 1;

                    fill_dsc.color = dsc->color;
                    cb(draw_unit, NULL, &fill_dsc, &fill_area);
                }
                if(dsc->decor & LV_TEXT_DECOR_STRIKETHROUGH) {
                    lv_area_t fill_area;
                    fill_area.x1 = line_start_x;
                    fill_area.x2 = pos.x + letter_w - 1;
                    fill_area.y1 = pos.y + (font->line_height - font->base_line) * 2 / 3 + font->underline_thickness / 2;
                    fill_area.y2 = fill_area.y1 + underline_width - 1;

                    fill_dsc.color = dsc->color;
                    cb(draw_unit, NULL, &fill_dsc, &fill_area);
                }
            }

            if(selection_active && logical_char_pos >= sel_start && logical_char_pos < sel_end) {
                draw_letter_dsc.color = dsc->sel_color;
                fill_dsc.color = dsc->sel_bg_color;
                cb(draw_unit, NULL, &fill_dsc, &bg_coords);
            }
            else {
                draw_letter_dsc.color = dsc->color;
            }

            draw_letter(draw_unit, &draw_letter_dsc, &pos, font, letter, false, reusable, cb);

            if(letter_w > 0) {
                pos.x += letter_w + dsc->letter_space;
            }
        }

#if LV_USE_BIDI
        lv_free(bidi_txt);
        bidi_txt = NULL;
#endif
        /* 先判断下一行可见性，避免退出前重复扫描不可见文本。 */
        pos.y += line_height;
        if(pos.y > draw_unit->clip_area->y2) break;

        /*Go to next line*/
        line_start = line_end;
        line_end += lv_text_get_next_line(&dsc->text[line_start], font, dsc->letter_space, w, NULL, dsc->flag);

        pos.x = coords->x1;
        /*Align to middle*/
        if(align == LV_TEXT_ALIGN_CENTER) {
            line_width =
                lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, dsc->letter_space);

            pos.x += (lv_area_get_width(coords) - line_width) / 2;
        }
        /*Align to the right*/
        else if(align == LV_TEXT_ALIGN_RIGHT) {
            line_width =
                lv_text_get_width(&dsc->text[line_start], line_end - line_start, font, dsc->letter_space);
            pos.x += lv_area_get_width(coords) - line_width;
        }
    }

    if(draw_letter_dsc._draw_buf) lv_draw_buf_destroy(draw_letter_dsc._draw_buf);

    LV_ASSERT_MEM_INTEGRITY();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * @brief 复用可用的测宽描述或按原规则查询字形，再裁剪和提交位图。
 * @param draw_unit/dsc/pos 绘制单元、样式与位置；font/letter/is_glyph_id为字体及码点或字形编号。
 * @param measured 已解析且不受字偶距影响的描述，可为NULL；cb为字形绘制回调。
 * @return 无返回值。
 */
static void draw_letter(lv_draw_unit_t * draw_unit, lv_draw_glyph_dsc_t * dsc,  const lv_point_t * pos,
                        const lv_font_t * font, uint32_t letter, bool is_glyph_id,
                        const lv_font_glyph_dsc_t * measured, lv_draw_glyph_cb_t cb)
{
    lv_font_glyph_dsc_t g = {0};

    if(!is_glyph_id && lv_text_is_marker(letter)) /*Markers are valid letters but should not be rendered.*/
        return;

    LV_PROFILER_BEGIN;
    bool g_ret = false;
    if(measured) {
        g = *measured;
        g_ret = true;
    }
    else if(is_glyph_id) {
        g_ret = lv_text_shape_get_glyph_dsc_by_gid(font, &g, letter, -1);
    }
    else {
        g_ret = lv_font_get_glyph_dsc(font, &g, letter, '\0');
    }

    if(g_ret == false) {
        /*Add warning if the dsc is not found*/
        if(is_glyph_id) {
            LV_LOG_WARN("lv_draw_letter: glyph dsc. not found for gid %" LV_PRIu32, letter);
        }
        else {
            LV_LOG_WARN("lv_draw_letter: glyph dsc. not found for U+%" LV_PRIX32, letter);
        }
        LV_PROFILER_END;
        return;
    }

    /*Don't draw anything if the character is empty. E.g. space*/
    if((g.box_h == 0) || (g.box_w == 0)) {
        LV_PROFILER_END;
        return;
    }

    lv_area_t letter_coords;
    letter_coords.x1 = pos->x + g.ofs_x;
    letter_coords.x2 = letter_coords.x1 + g.box_w - 1;
    letter_coords.y1 = pos->y + (font->line_height - font->base_line) - g.box_h - g.ofs_y;
    letter_coords.y2 = letter_coords.y1 + g.box_h - 1;

    /*If the letter is completely out of mask don't draw it*/
    if(lv_area_is_out(&letter_coords, draw_unit->clip_area, 0) &&
       lv_area_is_out(dsc->bg_coords, draw_unit->clip_area, 0)) {
        LV_PROFILER_END;
        return;
    }

    if(g.resolved_font) {
        lv_draw_buf_t * draw_buf = NULL;
        if(LV_FONT_GLYPH_FORMAT_NONE < g.format && g.format < LV_FONT_GLYPH_FORMAT_IMAGE) {
            /*Only check draw buf for bitmap glyph*/
            draw_buf = lv_draw_buf_reshape(dsc->_draw_buf, 0, g.box_w, g.box_h, LV_STRIDE_AUTO);
            if(draw_buf == NULL) {
                if(dsc->_draw_buf) lv_draw_buf_destroy(dsc->_draw_buf);

                uint32_t h = g.box_h;
                if(h * g.box_w < 64) h *= 2; /*Alloc a slightly larger buffer*/
                draw_buf = lv_draw_buf_create_ex(font_draw_buf_handlers, g.box_w, h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
                LV_ASSERT_MALLOC(draw_buf);
                draw_buf->header.h = g.box_h;
                dsc->_draw_buf = draw_buf;
            }
        }

        dsc->glyph_data = (void *) lv_font_get_glyph_bitmap(&g, draw_buf);
        dsc->format = dsc->glyph_data ? g.format : LV_FONT_GLYPH_FORMAT_NONE;
    }
    else {
        dsc->format = LV_FONT_GLYPH_FORMAT_NONE;
    }

    dsc->letter_coords = &letter_coords;
    dsc->g = &g;
    cb(draw_unit, dsc, NULL, NULL);

    lv_font_glyph_release_draw_data(&g);

    LV_PROFILER_END;
}
