/**
 * @file lv_text_shape_hb.cc
 * @brief 基于 HarfBuzz 的复杂文本整形后端。
 */

extern "C" {
#include "lv_text_shape.h"
#include "../libs/tiny_ttf/lv_tiny_ttf.h"
#include "../stdlib/lv_mem.h"
}

#include "hb.h"
#include "hb-ot.h"

#include <limits.h>
#include <stdint.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/
static hb_blob_t * lv_text_shape_hb_reference_table(hb_face_t * face, hb_tag_t tag, void * user_data);
static void lv_text_shape_hb_destroy_table(void * data);
static hb_direction_t lv_text_shape_hb_direction(lv_base_dir_t base_dir);
static bool lv_text_shape_hb_to_px(hb_position_t value, int32_t * px_out);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static hb_blob_t * lv_text_shape_hb_reference_table(hb_face_t * face, hb_tag_t tag, void * user_data)
{
    LV_UNUSED(face);

    const lv_font_t * font = static_cast<const lv_font_t *>(user_data);
    uint8_t * table_data = NULL;
    uint32_t table_size = 0;

    if(!lv_tiny_ttf_get_table(font, tag, &table_data, &table_size)) {
        return hb_blob_get_empty();
    }

    return hb_blob_create(reinterpret_cast<const char *>(table_data),
                          static_cast<unsigned int>(table_size),
                          HB_MEMORY_MODE_READONLY,
                          table_data,
                          lv_text_shape_hb_destroy_table);
}

static void lv_text_shape_hb_destroy_table(void * data)
{
    lv_free(data);
}

static hb_direction_t lv_text_shape_hb_direction(lv_base_dir_t base_dir)
{
    if(base_dir == LV_BASE_DIR_RTL) {
        return HB_DIRECTION_RTL;
    }

    if(base_dir == LV_BASE_DIR_LTR) {
        return HB_DIRECTION_LTR;
    }

    return HB_DIRECTION_INVALID;
}

static bool lv_text_shape_hb_to_px(hb_position_t value, int32_t * px_out)
{
    if(px_out == NULL) {
        return false;
    }

    int64_t px = value;
    if(px >= 0) {
        px = (px + 32) / 64;
    }
    else {
        px = (px - 32) / 64;
    }

    if(px > INT32_MAX || px < INT32_MIN) {
        return false;
    }

    *px_out = static_cast<int32_t>(px);
    return true;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

extern "C" bool lv_text_shape_hb_run(const lv_font_t * font, const char * text, uint32_t byte_len,
                                     lv_base_dir_t base_dir, lv_text_shape_run_t * run_out)
{
    if(font == NULL || text == NULL || run_out == NULL || byte_len == 0 || byte_len > INT_MAX) {
        return false;
    }

    int32_t hb_scale = 0;
    if(!lv_tiny_ttf_get_hb_scale(font, &hb_scale)) {
        return false;
    }

    hb_face_t * face = hb_face_create_for_tables(lv_text_shape_hb_reference_table,
                                                 const_cast<lv_font_t *>(font),
                                                 NULL);
    if(face == NULL) {
        return false;
    }

    hb_font_t * hb_font = hb_font_create(face);
    hb_face_destroy(face);
    if(hb_font == NULL) {
        return false;
    }

    hb_font_set_scale(hb_font, hb_scale, hb_scale);
    hb_ot_font_set_funcs(hb_font);

    hb_buffer_t * buffer = hb_buffer_create();
    if(buffer == NULL) {
        hb_font_destroy(hb_font);
        return false;
    }
    hb_buffer_set_unicode_funcs(buffer, hb_unicode_funcs_get_default());

    hb_buffer_add_utf8(buffer, text, static_cast<int>(byte_len), 0, static_cast<int>(byte_len));

    hb_direction_t direction = lv_text_shape_hb_direction(base_dir);
    if(direction != HB_DIRECTION_INVALID) {
        hb_buffer_set_direction(buffer, direction);
    }
    hb_buffer_guess_segment_properties(buffer);

    hb_shape(hb_font, buffer, NULL, 0);

    unsigned int glyph_count = 0;
    hb_glyph_info_t * infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    hb_glyph_position_t * positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
    if(glyph_count == 0 || infos == NULL || positions == NULL) {
        hb_buffer_destroy(buffer);
        hb_font_destroy(hb_font);
        return false;
    }

    if(sizeof(lv_text_shape_glyph_t) > SIZE_MAX / static_cast<size_t>(glyph_count)) {
        hb_buffer_destroy(buffer);
        hb_font_destroy(hb_font);
        return false;
    }

    lv_text_shape_glyph_t * glyphs = static_cast<lv_text_shape_glyph_t *>(
        lv_malloc(sizeof(lv_text_shape_glyph_t) * glyph_count));
    if(glyphs == NULL) {
        hb_buffer_destroy(buffer);
        hb_font_destroy(hb_font);
        return false;
    }

    int64_t width = 0;
    for(unsigned int i = 0; i < glyph_count; i++) {
        if(infos[i].codepoint == 0) {
            lv_free(glyphs);
            hb_buffer_destroy(buffer);
            hb_font_destroy(hb_font);
            return false;
        }

        glyphs[i].glyph_id = infos[i].codepoint;
        glyphs[i].cluster = infos[i].cluster;
        if(!lv_text_shape_hb_to_px(positions[i].x_advance, &glyphs[i].advance_x) ||
           !lv_text_shape_hb_to_px(positions[i].x_offset, &glyphs[i].offset_x) ||
           !lv_text_shape_hb_to_px(positions[i].y_offset, &glyphs[i].offset_y)) {
            lv_free(glyphs);
            hb_buffer_destroy(buffer);
            hb_font_destroy(hb_font);
            return false;
        }
        width += glyphs[i].advance_x;
        if(width > INT32_MAX || width < INT32_MIN) {
            lv_free(glyphs);
            hb_buffer_destroy(buffer);
            hb_font_destroy(hb_font);
            return false;
        }
    }

    run_out->glyphs = glyphs;
    run_out->glyph_count = glyph_count;
    run_out->width = static_cast<int32_t>(width);

    hb_buffer_destroy(buffer);
    hb_font_destroy(hb_font);
    return true;
}
