/**
 * @file lv_bidi.c
 * @brief 基于 SheenBidi 的 LVGL 双向文本适配实现。
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_bidi_private.h"
#include "lv_text_private.h"
#include "lv_types.h"
#include "../stdlib/lv_mem.h"
#include "../stdlib/lv_string.h"

#if LV_USE_BIDI

#include <SheenBidi/SheenBidi.h>

/**********************
 *  STATIC VARIABLES
 **********************/
static const char * custom_neutrals = NULL;

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * 获取下一段文本的字节长度。
 *
 * @param txt 待处理文本
 * @return 当前段落的字节长度
 */
static uint32_t lv_bidi_get_next_paragraph(const char * txt)
{
    uint32_t i = 0;

    if(txt == NULL || txt[0] == '\0') return 0;

    lv_text_encoded_next(txt, &i);

    while(txt[i] != '\0' && txt[i] != '\n' && txt[i] != '\r') {
        lv_text_encoded_next(txt, &i);
    }

    return i;
}

/**
 * 获取 UTF-8 文本的字节长度。
 *
 * @param txt 待统计文本
 * @return 文本字节长度
 */
static uint32_t lv_bidi_strlen(const char * txt)
{
    uint32_t len = 0;

    if(txt == NULL) return 0;

    while(txt[len] != '\0') len++;

    return len;
}

/**
 * 把 LVGL 基础方向转换为 SheenBidi 段落基础 level。
 *
 * @param base_dir LVGL 基础方向
 * @return SheenBidi 段落基础 level
 */
static SBLevel lv_bidi_to_sheen_base_level(lv_base_dir_t base_dir)
{
    if(base_dir == LV_BASE_DIR_RTL) return 1;
    if(base_dir == LV_BASE_DIR_LTR) return 0;
    return SBLevelDefaultLTR;
}

/**
 * 把 SheenBidi level 转换为 LVGL 基础方向。
 *
 * @param level SheenBidi embedding level
 * @return LVGL 基础方向
 */
static lv_base_dir_t lv_bidi_from_sheen_level(SBLevel level)
{
    return (level & 1) ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR;
}

/**
 * 统计指定字节位置之前的 UTF-8 字符数。
 *
 * @param text 待统计文本
 * @param byte_limit 截止字节位置
 * @return 截止位置之前的字符数量
 */
static uint32_t lv_bidi_count_chars_before(const char * text, uint32_t byte_limit)
{
    uint32_t i = 0;
    uint32_t count = 0;

    if(text == NULL) return 0;

    while(text[i] != '\0' && i < byte_limit) {
        uint32_t before = i;
        lv_text_encoded_next(text, &i);
        if(i <= before) break;
        if(i <= byte_limit) count++;
    }

    return count;
}

/**
 * 获取 UTF-8 文本在最大字节长度内的字符数量。
 *
 * @param txt 待统计文本
 * @param max_len 最大字节长度
 * @return 字符数量
 */
static uint32_t get_txt_len(const char * txt, uint32_t max_len)
{
    return lv_bidi_count_chars_before(txt, max_len);
}

/**
 * 填充 LVGL 逻辑/视觉位置映射。
 *
 * @param out 位置映射输出
 * @param len 映射长度
 * @param index 起始逻辑位置
 * @param is_rtl 该片段是否为 RTL
 */
static void lv_bidi_fill_pos_conv(uint16_t * out, uint16_t len, uint16_t index, bool is_rtl)
{
    uint16_t i;

    if(out == NULL) return;

    for(i = 0; i < len; i++) {
        uint16_t pos = (uint16_t)((index + i) & 0x7FFF);
        out[i] = is_rtl ? (uint16_t)(pos | 0x8000) : pos;
    }
}

/**
 * 在 SheenBidi 失败时填充原序文本和位置映射。
 *
 * @param str_in 输入文本
 * @param str_out 输出文本
 * @param len 字节长度
 * @param pos_conv_out 位置映射输出
 * @param pos_conv_len 位置映射长度
 */
static void lv_bidi_copy_identity(const char * str_in, char * str_out, uint32_t len,
                                  uint16_t * pos_conv_out, uint16_t pos_conv_len)
{
    if(str_out) {
        lv_memcpy(str_out, str_in, len);
        str_out[len] = '\0';
    }

    lv_bidi_fill_pos_conv(pos_conv_out, pos_conv_len, 0, false);
}

/**
 * 按视觉顺序写入一个 SheenBidi run。
 *
 * @param str_in 原始逻辑文本
 * @param str_out 视觉顺序输出
 * @param run SheenBidi 视觉 run
 * @param reverse_chars 是否按字符反向写入该 run
 * @param wr 输出字节写入位置
 * @param visual_pos 输出视觉字符位置
 * @param pos_conv_out 位置映射输出
 * @param pos_conv_len 位置映射长度
 * @return 写入后的字节位置
 */
static uint32_t lv_bidi_write_visual_run(const char * str_in, char * str_out, const SBRun * run,
                                         bool reverse_chars, uint32_t wr, uint32_t * visual_pos,
                                         uint16_t * pos_conv_out, uint16_t pos_conv_len)
{
    uint32_t start = (uint32_t)run->offset;
    uint32_t end = (uint32_t)(run->offset + run->length);

    if(!reverse_chars) {
        uint32_t rd = start;

        while(rd < end && str_in[rd] != '\0') {
            uint32_t before = rd;
            uint32_t logical_pos;

            lv_text_encoded_next(str_in, &rd);
            if(rd <= before || rd > end) break;

            if(str_out) {
                lv_memcpy(&str_out[wr], &str_in[before], rd - before);
            }

            logical_pos = lv_bidi_count_chars_before(str_in, before);
            if(pos_conv_out && *visual_pos < pos_conv_len) {
                pos_conv_out[*visual_pos] = (uint16_t)(logical_pos & 0x7FFF);
            }

            wr += rd - before;
            (*visual_pos)++;
        }
    }
    else {
        uint32_t rd = end;

        while(rd > start) {
            uint32_t before = rd;
            uint32_t logical_pos;

            uint32_t letter = lv_text_encoded_prev(str_in, &before);
            if(before >= rd || before < start) break;

            if(str_out) {
                uint32_t mirror = SBCodepointGetMirror(letter);
                uint32_t encoded = lv_text_unicode_to_encoded(mirror);
                /* 镜像映射保持 UTF-8 长度，沿用调用方 len + 1 的缓冲约定。 */
                if(mirror && lv_text_encoded_size((const char *)&encoded) == rd - before) {
                    lv_memcpy(&str_out[wr], &encoded, rd - before);
                }
                else lv_memcpy(&str_out[wr], &str_in[before], rd - before);
            }

            logical_pos = lv_bidi_count_chars_before(str_in, before);
            if(pos_conv_out && *visual_pos < pos_conv_len) {
                pos_conv_out[*visual_pos] = (uint16_t)((logical_pos & 0x7FFF) | 0x8000);
            }

            wr += rd - before;
            rd = before;
            (*visual_pos)++;
        }
    }

    return wr;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_bidi_process(const char * str_in, char * str_out, lv_base_dir_t base_dir)
{
    uint32_t par_start = 0;
    uint32_t par_len;

    if(str_in == NULL || str_out == NULL) return;

    while(str_in[par_start] == '\n' || str_in[par_start] == '\r') {
        str_out[par_start] = str_in[par_start];
        par_start ++;
    }

    while(str_in[par_start] != '\0') {
        par_len = lv_bidi_get_next_paragraph(&str_in[par_start]);
        lv_bidi_process_paragraph(&str_in[par_start], &str_out[par_start], par_len, base_dir, NULL, 0);
        par_start += par_len;

        while(str_in[par_start] == '\n' || str_in[par_start] == '\r') {
            str_out[par_start] = str_in[par_start];
            par_start ++;
        }
    }

    str_out[par_start] = '\0';
}

lv_base_dir_t lv_bidi_detect_base_dir(const char * txt)
{
    uint32_t len = lv_bidi_strlen(txt);
    SBCodepointSequence codepoint_sequence;
    SBAlgorithmRef algorithm;
    SBParagraphRef paragraph;
    lv_base_dir_t dir = LV_BASE_DIR_LTR;

    if(len == 0) {
        if(LV_BIDI_BASE_DIR_DEF == LV_BASE_DIR_RTL) return LV_BASE_DIR_RTL;
        return LV_BASE_DIR_LTR;
    }

    codepoint_sequence.stringEncoding = SBStringEncodingUTF8;
    codepoint_sequence.stringBuffer = txt;
    codepoint_sequence.stringLength = len;

    algorithm = SBAlgorithmCreate(&codepoint_sequence);
    if(algorithm == NULL) return dir;

    paragraph = SBAlgorithmCreateParagraph(algorithm, 0, len, SBLevelDefaultLTR);
    if(paragraph) {
        dir = lv_bidi_from_sheen_level(SBParagraphGetBaseLevel(paragraph));
        SBParagraphRelease(paragraph);
    }

    SBAlgorithmRelease(algorithm);

    return dir;
}

uint16_t lv_bidi_get_logical_pos(const char * str_in, char ** bidi_txt, uint32_t len, lv_base_dir_t base_dir,
                                 uint32_t visual_pos, bool * is_rtl)
{
    uint32_t pos_conv_len = get_txt_len(str_in, len);
    char * buf = lv_malloc(len + 1);
    uint16_t * pos_conv_buf;
    uint16_t res;

    if(buf == NULL) return (uint16_t) -1;

    pos_conv_buf = lv_malloc(pos_conv_len * sizeof(uint16_t));
    if(pos_conv_buf == NULL) {
        lv_free(buf);
        return (uint16_t) -1;
    }

    if(bidi_txt) *bidi_txt = buf;

    lv_bidi_process_paragraph(str_in, bidi_txt ? *bidi_txt : buf, len, base_dir, pos_conv_buf, pos_conv_len);

    if(visual_pos >= pos_conv_len) {
        if(bidi_txt == NULL) lv_free(buf);
        lv_free(pos_conv_buf);
        return (uint16_t) -1;
    }

    if(is_rtl) *is_rtl = (pos_conv_buf[visual_pos] & 0x8000) != 0;
    res = (uint16_t)(pos_conv_buf[visual_pos] & 0x7FFF);

    if(bidi_txt == NULL) lv_free(buf);
    lv_free(pos_conv_buf);
    return res;
}

uint16_t lv_bidi_get_visual_pos(const char * str_in, char ** bidi_txt, uint16_t len, lv_base_dir_t base_dir,
                                uint32_t logical_pos, bool * is_rtl)
{
    uint32_t pos_conv_len = get_txt_len(str_in, len);
    char * buf = lv_malloc(len + 1);
    uint16_t * pos_conv_buf;
    uint16_t i;

    if(buf == NULL) return (uint16_t) -1;

    pos_conv_buf = lv_malloc(pos_conv_len * sizeof(uint16_t));
    if(pos_conv_buf == NULL) {
        lv_free(buf);
        return (uint16_t) -1;
    }

    if(bidi_txt) *bidi_txt = buf;

    lv_bidi_process_paragraph(str_in, bidi_txt ? *bidi_txt : buf, len, base_dir, pos_conv_buf, pos_conv_len);

    for(i = 0; i < pos_conv_len; i++) {
        if((pos_conv_buf[i] & 0x7FFF) == logical_pos) {
            if(is_rtl) *is_rtl = (pos_conv_buf[i] & 0x8000) != 0;
            lv_free(pos_conv_buf);

            if(bidi_txt == NULL) lv_free(buf);
            return i;
        }
    }

    lv_free(pos_conv_buf);
    if(bidi_txt == NULL) lv_free(buf);
    return (uint16_t) -1;
}

void lv_bidi_process_paragraph(const char * str_in, char * str_out, uint32_t len, lv_base_dir_t base_dir,
                               uint16_t * pos_conv_out, uint16_t pos_conv_len)
{
    SBCodepointSequence codepoint_sequence;
    SBAlgorithmRef algorithm;
    SBParagraphRef paragraph = NULL;
    SBLineRef line = NULL;
    const SBRun * runs;
    SBUInteger run_count;
    SBLevel base_level;
    uint32_t wr = 0;
    uint32_t visual_pos = 0;
    SBUInteger i;

    if(str_in == NULL) return;

    if(str_out) str_out[len] = '\0';

    if(len == 0) {
        lv_bidi_copy_identity(str_in, str_out, len, pos_conv_out, pos_conv_len);
        return;
    }

    codepoint_sequence.stringEncoding = SBStringEncodingUTF8;
    codepoint_sequence.stringBuffer = str_in;
    codepoint_sequence.stringLength = len;

    algorithm = SBAlgorithmCreate(&codepoint_sequence);
    if(algorithm == NULL) {
        lv_bidi_copy_identity(str_in, str_out, len, pos_conv_out, pos_conv_len);
        return;
    }

    base_level = lv_bidi_to_sheen_base_level(base_dir);
    paragraph = SBAlgorithmCreateParagraph(algorithm, 0, len, base_level);
    if(paragraph == NULL) {
        SBAlgorithmRelease(algorithm);
        lv_bidi_copy_identity(str_in, str_out, len, pos_conv_out, pos_conv_len);
        return;
    }

    line = SBParagraphCreateLine(paragraph, 0, SBParagraphGetLength(paragraph));
    if(line == NULL) {
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
        lv_bidi_copy_identity(str_in, str_out, len, pos_conv_out, pos_conv_len);
        return;
    }

    runs = SBLineGetRunsPtr(line);
    run_count = SBLineGetRunCount(line);

    for(i = 0; i < run_count; i++) {
        bool reverse_chars = (runs[i].level & 1) != 0;
        wr = lv_bidi_write_visual_run(str_in, str_out, &runs[i], reverse_chars, wr, &visual_pos,
                                      pos_conv_out, pos_conv_len);
    }

    if(str_out) str_out[wr] = '\0';

    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);
}

bool lv_bidi_get_visual_runs(const char * str_in, uint32_t len, lv_base_dir_t base_dir,
                             lv_bidi_visual_runs_t * runs_out)
{
    SBCodepointSequence codepoint_sequence;
    SBAlgorithmRef algorithm;
    SBParagraphRef paragraph = NULL;
    SBLineRef line = NULL;
    const SBRun * runs;
    SBUInteger run_count;
    SBUInteger i;

    if(runs_out == NULL) return false;

    lv_memzero(runs_out, sizeof(*runs_out));

    if(str_in == NULL || len == 0) return false;

    codepoint_sequence.stringEncoding = SBStringEncodingUTF8;
    codepoint_sequence.stringBuffer = str_in;
    codepoint_sequence.stringLength = len;

    algorithm = SBAlgorithmCreate(&codepoint_sequence);
    if(algorithm == NULL) return false;

    paragraph = SBAlgorithmCreateParagraph(algorithm, 0, len, lv_bidi_to_sheen_base_level(base_dir));
    if(paragraph == NULL) {
        SBAlgorithmRelease(algorithm);
        return false;
    }

    line = SBParagraphCreateLine(paragraph, 0, SBParagraphGetLength(paragraph));
    if(line == NULL) {
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
        return false;
    }

    runs = SBLineGetRunsPtr(line);
    run_count = SBLineGetRunCount(line);
    if(runs == NULL || run_count == 0) {
        SBLineRelease(line);
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
        return false;
    }

    runs_out->runs = lv_malloc(sizeof(lv_bidi_visual_run_t) * run_count);
    if(runs_out->runs == NULL) {
        SBLineRelease(line);
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
        return false;
    }

    runs_out->run_count = (uint32_t)run_count;
    for(i = 0; i < run_count; i++) {
        runs_out->runs[i].byte_start = (uint32_t)runs[i].offset;
        runs_out->runs[i].byte_len = (uint32_t)runs[i].length;
        runs_out->runs[i].base_dir = lv_bidi_from_sheen_level(runs[i].level);
    }

    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);
    return true;
}

void lv_bidi_visual_runs_destroy(lv_bidi_visual_runs_t * runs)
{
    if(runs == NULL) return;

    if(runs->runs) {
        lv_free(runs->runs);
    }

    lv_memzero(runs, sizeof(*runs));
}

void lv_bidi_calculate_align(lv_text_align_t * align, lv_base_dir_t * base_dir, const char * txt)
{
    if(*base_dir == LV_BASE_DIR_AUTO) *base_dir = lv_bidi_detect_base_dir(txt);

    if(*align == LV_TEXT_ALIGN_AUTO) {
        if(*base_dir == LV_BASE_DIR_RTL) *align = LV_TEXT_ALIGN_RIGHT;
        else *align = LV_TEXT_ALIGN_LEFT;
    }
}

void lv_bidi_set_custom_neutrals_static(const char * neutrals)
{
    custom_neutrals = neutrals;
    LV_UNUSED(custom_neutrals);
}

#endif /*LV_USE_BIDI*/
