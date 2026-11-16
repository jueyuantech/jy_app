/**
 * @file bitmap_font.c
 * @brief ROMFS 映射点阵字库读取与缩放实现。
 *
 * 字形描述表和原始点阵直接引用只读文件映射，不复制索引或缓存原始位图。
 * 多字号共享映射和水平插值工作区；解包及缩放直接写入LVGL缓冲。
 * 最后一个字体释放时关闭映射文件；仅用固定小表复用字形定位，不缓存原始点阵。
 * ARM 平台使用 MVE、DSP 或标量缩放，不为单个字形动态分配内存。
 *
 * @author jytek
 * @date 2026-10-16
 */
#include "bitmap_font.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "app_def.h"
#include "floatair_dbg.h"
#include "floatair_fs.h"

#if defined(__ARM_FEATURE_DSP)
#include <arm_acle.h>
#endif
#if defined(__ARM_FEATURE_MVE)
#include <arm_mve.h>
#endif

/* ------------------------------------------------------------------ */
/*  点阵字库二进制格式定义 (与 generate_bitmap_font.py 一致)           */
/* ------------------------------------------------------------------ */

#define BITMAP_FONT_MAGIC "JYBF"    /**< 点阵字库文件标识。 */
#define BITMAP_FONT_HEADER_SIZE 32 /**< 固定文件头字节数。 */
#define BITMAP_FONT_GLYPH_ENTRY_SIZE 24 /**< 固定字形描述记录字节数。 */

/* MSVC 的兼容头会忽略 GNU packed 属性，文件格式需显式使用单字节对齐。 */
#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif

typedef struct __attribute__((packed)) {
    uint8_t magic[4];       /**< "JYBF" */
    uint8_t version;        /**< 版本号, 当前为 1 */
    uint8_t bpp;            /**< 位深 1/2/4/8 */
    uint8_t base_size;      /**< 基准字号 (像素) */
    uint8_t line_height;    /**< 基准字号下行高 (像素) */
    uint8_t base_line;      /**< 基准字号下基线距底部 (像素) */
    uint32_t glyph_count;   /**< 字形数量 */
    uint8_t reserved[19];  /**< 文件格式保留字段。 */
} bitmap_font_header_t;

typedef struct __attribute__((packed)) {
    uint32_t unicode;       /**< Unicode 码点 */
    uint32_t adv_w;         /**< advance width (基准字号, 像素) */
    uint16_t box_w;         /**< 包围盒宽度 */
    uint16_t box_h;         /**< 包围盒高度 */
    int16_t ofs_x;          /**< 包围盒 x 偏移 */
    int16_t ofs_y;          /**< LVGL 约定: bitmap_top - box_h */
    uint32_t bitmap_offset; /**< 位图数据在文件中的偏移 */
    uint32_t bitmap_size;   /**< 位图字节数 */
} bitmap_glyph_entry_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

_Static_assert(sizeof(bitmap_font_header_t) == BITMAP_FONT_HEADER_SIZE,
               "bitmap font header must match the file format");
_Static_assert(offsetof(bitmap_font_header_t, glyph_count) == 9,
               "bitmap font glyph count must start at byte 9");
_Static_assert(sizeof(bitmap_glyph_entry_t) == BITMAP_FONT_GLYPH_ENTRY_SIZE,
               "bitmap glyph entry must match the file format");

/* ------------------------------------------------------------------ */
/*  共享插值工作区配置                                                        */
/* ------------------------------------------------------------------ */

/** 单个字形解包或缩放工作缓冲上限，单位为 A8 像素字节。 */
#define GLYPH_MAX_BYTES (96 * 48)

/** 缩放中间缓冲大小（line 算法的水平 pass 中间结果，sh * dw）。 */
#define SCALE_MID_BUF_SIZE GLYPH_MAX_BYTES
#define SCALE_2BPP_MAX_WIDTH 96 /**< 2bpp快速路径源/目标宽度上限，超过时使用通用算法。 */
#define SCALE_2BPP_ROW_BYTES (SCALE_2BPP_MAX_WIDTH + 1) /**< 单行2位采样值及右边界重复像素。 */
#define GLYPH_METRIC_SLOTS 32 /**< 固定768字节，复用跨测宽、换行及绘制的字号度量。 */
#define GLYPH_LOOKUP_SLOTS 256 /**< 多字号共享的固定定位小表，合计2KiB，无原始位图。 */

/** @brief 只保存ROMFS描述表位置，字号无关；index_plus_one为0表示空槽。 */
typedef struct {
    uint32_t unicode;        /**< 完整码点用于消除哈希冲突。 */
    uint32_t index_plus_one; /**< 表索引加一，UINT32_MAX表示已确认缺字。 */
} bitmap_glyph_lookup_t;


/** @brief 字号相关的最终字形度量，不保存原始点阵或LVGL绘制资源。 */
typedef struct {
    uint32_t unicode;        /**< 完整码点键。 */
    int32_t target_size;     /**< 目标字号键，不跨字号复用度量。 */
    uint32_t index_plus_one; /**< ROMFS表索引加一；0为空，UINT32_MAX为缺字。 */
    uint16_t adv_w;          /**< 缩放后的前进宽度。 */
    uint16_t box_w;          /**< 缩放后的字形宽度。 */
    uint16_t box_h;          /**< 缩放后的字形高度。 */
    int16_t ofs_x;           /**< 缩放后的水平偏移。 */
    int16_t ofs_y;           /**< 缩放后的垂直偏移。 */
} bitmap_glyph_metric_t;

/* ------------------------------------------------------------------ */
/*  共享字库数据（多字号共用，引用计数）                                */
/* ------------------------------------------------------------------ */

/** 多字号共享的只读映射及绘制工作区。 */
typedef struct {
    void *file;                              /**< 保持映射有效的文件句柄。 */
    const uint8_t *mapped;                    /**< ROMFS 文件只读起始地址。 */
    uint32_t mapped_size;                     /**< 映射文件长度，供边界检查。 */
    bitmap_font_header_t header;              /**< 校验后的文件头。 */
    int ref_count;                           /**< 使用当前映射的字体数量。 */
    bitmap_glyph_metric_t metrics[GLYPH_METRIC_SLOTS]; /**< 同一映射各字号共享的固定小型度量表。 */
    bitmap_glyph_lookup_t lookup[GLYPH_LOOKUP_SLOTS]; /**< 固定大小、随映射生命周期清空。 */
    uint8_t scale_mid_buf[SCALE_MID_BUF_SIZE]; /**< 插值算法共享的水平插值工作区。 */
} bitmap_font_data_t;

/* ------------------------------------------------------------------ */
/*  字体描述符                                                          */
/* ------------------------------------------------------------------ */

#define BITMAP_FONT_MAGIC_DSC 0x4246544DU  /* "BTFM" little-endian */

typedef struct bitmap_font_dsc {
    uint32_t magic;
    bitmap_font_data_t *data;        /**< 共享字库数据 */
    int32_t target_size;             /**< 目标字号 */
    bitmap_font_algo_t algo;
    bitmap_font_accel_t accel;
} bitmap_font_dsc_t;

/* ------------------------------------------------------------------ */
/*  工具: 读取打包位图中的像素值                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief 按连续像素序号读取MSB优先打包点阵；奇数宽字形的行不补齐字节。
 * @param src 已校验的ROMFS点阵地址；pixel 为边界内像素序号，bpp 为1/2/4/8。
 * @return 对应像素的A8灰度值。
 */
static inline uint8_t bitmap_get_pixel(const uint8_t *src, size_t pixel, uint8_t bpp)
{
    switch (bpp) {
    case 8: return src[pixel];
    case 4: return ((src[pixel >> 1] >> ((pixel & 1) ? 0 : 4)) & 15) * 17;
    case 2: return ((src[pixel >> 2] >> (6 - ((pixel & 3) * 2))) & 3) * 85;
    case 1: return ((src[pixel >> 3] >> (7 - (pixel & 7))) & 1) ? 255 : 0;
    default: return 0;
    }
}

/**
 * @brief 将打包点阵直接解包到LVGL目标缓冲，保留每行padding。
 * @param src ROMFS源点阵，w/h为有效尺寸，bpp为源位深，dst/stride为A8目标。
 * @return 无返回值；源、目标边界由字体回调检查。
 */
static void bitmap_unpack_to_a8_buf(const uint8_t *src, int w, int h, uint8_t bpp,
                                    uint8_t *dst, size_t stride)
{
    for (int y = 0; y < h; y++) {
        uint8_t *row = dst + (size_t)y * stride;
        if (bpp == 8) {
            memcpy(row, src + (size_t)y * w, (size_t)w);
        } else {
            for (int x = 0; x < w; x++)
                row[x] = bitmap_get_pixel(src, (size_t)y * w + x, bpp);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  缩放算法 —— scalar 纯 C 版本                                        */
/*  mid_buf 为调用方提供的中间缓冲（line 算法使用），大小 >= sh * dw   */
/* ------------------------------------------------------------------ */

static void scale_nearest_scalar(const uint8_t *src, int sw, int sh,
                                 uint8_t *dst, int dw, int dh,
                                 uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    LV_UNUSED(mid_buf);
    for (int dy = 0; dy < dh; dy++) {
        int sy = (dy * sh) / dh;
        if (sy >= sh) sy = sh - 1;
        uint8_t *drow = dst + (size_t)dy * stride;
        for (int dx = 0; dx < dw; dx++) {
            int sx = (dx * sw) / dw;
            if (sx >= sw) sx = sw - 1;
            drow[dx] = bitmap_get_pixel(src, (size_t)sy * sw + sx, bpp);
        }
    }
}

static void scale_line_scalar(const uint8_t *src, int sw, int sh,
                              uint8_t *dst, int dw, int dh,
                              uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    if (mid_buf == NULL) {
        scale_nearest_scalar(src, sw, sh, dst, dw, dh, NULL, stride, bpp);
        return;
    }
    uint8_t *mid = mid_buf;

    /* 水平 pass */
    for (int y = 0; y < sh; y++) {
        uint8_t *mrow = mid + y * dw;
        for (int dx = 0; dx < dw; dx++) {
            int sx_fixed = (dx * sw * 256) / dw;
            int x0 = sx_fixed >> 8;
            int x1 = x0 + 1;
            if (x1 >= sw) x1 = sw - 1;
            int fx = sx_fixed & 0xFF;
            mrow[dx] = (uint8_t)(((int)bitmap_get_pixel(src, (size_t)y * sw + x0, bpp) * (256 - fx) + (int)bitmap_get_pixel(src, (size_t)y * sw + x1, bpp) * fx) >> 8);
        }
    }

    /* 垂直 pass */
    for (int dy = 0; dy < dh; dy++) {
        int sy_fixed = (dy * sh * 256) / dh;
        int y0 = sy_fixed >> 8;
        int y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        int fy = sy_fixed & 0xFF;
        uint8_t *drow = dst + (size_t)dy * stride;
        const uint8_t *mrow0 = mid + y0 * dw;
        const uint8_t *mrow1 = mid + y1 * dw;
        for (int dx = 0; dx < dw; dx++) {
            drow[dx] = (uint8_t)(((int)mrow0[dx] * (256 - fy) + (int)mrow1[dx] * fy) >> 8);
        }
    }
}

static void scale_bilinear_scalar(const uint8_t *src, int sw, int sh,
                                  uint8_t *dst, int dw, int dh,
                                  uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    LV_UNUSED(mid_buf);
    for (int dy = 0; dy < dh; dy++) {
        int sy_fixed = (dy * sh * 256) / dh;
        int y0 = sy_fixed >> 8;
        int y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        int fy = sy_fixed & 0xFF;
        uint8_t *drow = dst + (size_t)dy * stride;
        for (int dx = 0; dx < dw; dx++) {
            int sx_fixed = (dx * sw * 256) / dw;
            int x0 = sx_fixed >> 8;
            int x1 = x0 + 1;
            if (x1 >= sw) x1 = sw - 1;
            int fx = sx_fixed & 0xFF;
            int p00 = bitmap_get_pixel(src, (size_t)y0 * sw + x0, bpp), p10 = bitmap_get_pixel(src, (size_t)y0 * sw + x1, bpp);
            int p01 = bitmap_get_pixel(src, (size_t)y1 * sw + x0, bpp), p11 = bitmap_get_pixel(src, (size_t)y1 * sw + x1, bpp);
            int top = (p00 * (256 - fx) + p10 * fx) >> 8;
            int bot = (p01 * (256 - fx) + p11 * fx) >> 8;
            drow[dx] = (uint8_t)((top * (256 - fy) + bot * fy) >> 8);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  缩放算法 —— ARM DSP 加速版本                                       */
/* ------------------------------------------------------------------ */

#if defined(__ARM_FEATURE_DSP)

static void scale_line_dsp(const uint8_t *src, int sw, int sh,
                           uint8_t *dst, int dw, int dh,
                           uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    if (mid_buf == NULL) {
        scale_line_scalar(src, sw, sh, dst, dw, dh, NULL, stride, bpp);
        return;
    }
    uint8_t *mid = mid_buf;

    /* 水平 pass —— 每次处理 2 个目标像素 */
    for (int y = 0; y < sh; y++) {
        uint8_t *mrow = mid + y * dw;
        int dx = 0;
        for (; dx + 1 < dw; dx += 2) {
            int sx0 = (dx * sw * 256) / dw;
            int sx1 = ((dx + 1) * sw * 256) / dw;
            int ix0 = sx0 >> 8, ix1 = sx1 >> 8;
            int fx0 = sx0 & 0xFF, fx1 = sx1 & 0xFF;
            int nx0 = ix0 + 1 < sw ? ix0 + 1 : sw - 1;
            int nx1 = ix1 + 1 < sw ? ix1 + 1 : sw - 1;
            int16x2_t pair0 = (int16x2_t)(((uint16_t)bitmap_get_pixel(src, (size_t)y * sw + nx0, bpp) << 16) | (uint16_t)bitmap_get_pixel(src, (size_t)y * sw + ix0, bpp));
            int16x2_t w0 = (int16x2_t)(((uint16_t)fx0 << 16) | (uint16_t)(256 - fx0));
            int32_t acc0 = __smlad(pair0, w0, 0) >> 8;
            int16x2_t pair1 = (int16x2_t)(((uint16_t)bitmap_get_pixel(src, (size_t)y * sw + nx1, bpp) << 16) | (uint16_t)bitmap_get_pixel(src, (size_t)y * sw + ix1, bpp));
            int16x2_t w1 = (int16x2_t)(((uint16_t)fx1 << 16) | (uint16_t)(256 - fx1));
            int32_t acc1 = __smlad(pair1, w1, 0) >> 8;
            mrow[dx] = (uint8_t)acc0;
            mrow[dx + 1] = (uint8_t)acc1;
        }
        for (; dx < dw; dx++) {
            int sx = (dx * sw * 256) / dw;
            int x0 = sx >> 8;
            int x1 = x0 + 1;
            if (x1 >= sw) x1 = sw - 1;
            int fx = sx & 0xFF;
            mrow[dx] = (uint8_t)(((int)bitmap_get_pixel(src, (size_t)y * sw + x0, bpp) * (256 - fx) + (int)bitmap_get_pixel(src, (size_t)y * sw + x1, bpp) * fx) >> 8);
        }
    }

    /* 垂直 pass —— scalar */
    for (int dy = 0; dy < dh; dy++) {
        int sy_fixed = (dy * sh * 256) / dh;
        int y0 = sy_fixed >> 8;
        int y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        int fy = sy_fixed & 0xFF;
        uint8_t *drow = dst + (size_t)dy * stride;
        const uint8_t *mrow0 = mid + y0 * dw;
        const uint8_t *mrow1 = mid + y1 * dw;
        for (int dx = 0; dx < dw; dx++) {
            drow[dx] = (uint8_t)(((int)mrow0[dx] * (256 - fy) + (int)mrow1[dx] * fy) >> 8);
        }
    }
}

static void scale_bilinear_dsp(const uint8_t *src, int sw, int sh,
                               uint8_t *dst, int dw, int dh,
                               uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    LV_UNUSED(mid_buf);
    for (int dy = 0; dy < dh; dy++) {
        int sy_fixed = (dy * sh * 256) / dh;
        int y0 = sy_fixed >> 8;
        int y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        int fy = sy_fixed & 0xFF;
        uint8_t *drow = dst + (size_t)dy * stride;
        for (int dx = 0; dx < dw; dx++) {
            int sx_fixed = (dx * sw * 256) / dw;
            int x0 = sx_fixed >> 8;
            int x1 = x0 + 1;
            if (x1 >= sw) x1 = sw - 1;
            int fx = sx_fixed & 0xFF;
            int16x2_t pair0 = (int16x2_t)(((uint16_t)bitmap_get_pixel(src, (size_t)y0 * sw + x1, bpp) << 16) | (uint16_t)bitmap_get_pixel(src, (size_t)y0 * sw + x0, bpp));
            int16x2_t w = (int16x2_t)(((uint16_t)fx << 16) | (uint16_t)(256 - fx));
            int top = __smlad(pair0, w, 0) >> 8;
            int16x2_t pair1 = (int16x2_t)(((uint16_t)bitmap_get_pixel(src, (size_t)y1 * sw + x1, bpp) << 16) | (uint16_t)bitmap_get_pixel(src, (size_t)y1 * sw + x0, bpp));
            int bot = __smlad(pair1, w, 0) >> 8;
            drow[dx] = (uint8_t)((top * (256 - fy) + bot * fy) >> 8);
        }
    }
}

#endif /* __ARM_FEATURE_DSP */

/* ------------------------------------------------------------------ */
/*  缩放算法 —— ARM MVE (Helium) 向量加速版本                          */
/* ------------------------------------------------------------------ */

#if defined(__ARM_FEATURE_MVE)

static void scale_nearest_mve(const uint8_t *src, int sw, int sh,
                              uint8_t *dst, int dw, int dh,
                              uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    LV_UNUSED(mid_buf);
    for (int dy = 0; dy < dh; dy++) {
        int sy = (dy * sh) / dh;
        if (sy >= sh) sy = sh - 1;
        uint8_t *drow = dst + (size_t)dy * stride;
        int dx = 0;
        for (; dx + 16 <= dw; dx += 16) {
            uint8_t tmp[16];
            for (int i = 0; i < 16; i++) {
                int sx = ((dx + i) * sw) / dw;
                if (sx >= sw) sx = sw - 1;
                tmp[i] = bitmap_get_pixel(src, (size_t)sy * sw + sx, bpp);
            }
            vst1q_u8(drow + dx, vld1q_u8(tmp));
        }
        for (; dx < dw; dx++) {
            int sx = (dx * sw) / dw;
            if (sx >= sw) sx = sw - 1;
            drow[dx] = bitmap_get_pixel(src, (size_t)sy * sw + sx, bpp);
        }
    }
}

static void scale_line_mve(const uint8_t *src, int sw, int sh,
                           uint8_t *dst, int dw, int dh,
                           uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    if (mid_buf == NULL) {
        scale_line_scalar(src, sw, sh, dst, dw, dh, NULL, stride, bpp);
        return;
    }
    uint8_t *mid = mid_buf;

    for (int y = 0; y < sh; y++) {
        uint8_t *mrow = mid + y * dw;
        int dx = 0;
        for (; dx + 16 <= dw; dx += 16) {
            uint8_t tmp[16];
            for (int i = 0; i < 16; i++) {
                int sx = ((dx + i) * sw * 256) / dw;
                int x0 = sx >> 8, x1 = x0 + 1;
                if (x1 >= sw) x1 = sw - 1;
                int fx = sx & 0xFF;
                tmp[i] = (uint8_t)(((int)bitmap_get_pixel(src, (size_t)y * sw + x0, bpp) * (256 - fx) + (int)bitmap_get_pixel(src, (size_t)y * sw + x1, bpp) * fx) >> 8);
            }
            vst1q_u8(mrow + dx, vld1q_u8(tmp));
        }
        for (; dx < dw; dx++) {
            int sx = (dx * sw * 256) / dw;
            int x0 = sx >> 8, x1 = x0 + 1;
            if (x1 >= sw) x1 = sw - 1;
            int fx = sx & 0xFF;
            mrow[dx] = (uint8_t)(((int)bitmap_get_pixel(src, (size_t)y * sw + x0, bpp) * (256 - fx) + (int)bitmap_get_pixel(src, (size_t)y * sw + x1, bpp) * fx) >> 8);
        }
    }

    for (int dy = 0; dy < dh; dy++) {
        int sy_fixed = (dy * sh * 256) / dh;
        int y0 = sy_fixed >> 8, y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        int fy = sy_fixed & 0xFF;
        uint8_t *drow = dst + (size_t)dy * stride;
        const uint8_t *mrow0 = mid + y0 * dw;
        const uint8_t *mrow1 = mid + y1 * dw;
        if (fy == 0) {
            /* 256 无法存入 8 位向量权重，整数行直接复制。 */
            memcpy(drow, mrow0, (size_t)dw);
            continue;
        }
        int dx = 0;
        for (; dx + 16 <= dw; dx += 16) {
            uint8x16_t v0 = vld1q_u8(mrow0 + dx);
            uint8x16_t v1 = vld1q_u8(mrow1 + dx);
            uint16x8_t lo0 = vmullbq_int_u8(v0, vdupq_n_u8(256 - fy));
            uint16x8_t lo1 = vmullbq_int_u8(v1, vdupq_n_u8(fy));
            uint16x8_t hi0 = vmulltq_int_u8(v0, vdupq_n_u8(256 - fy));
            uint16x8_t hi1 = vmulltq_int_u8(v1, vdupq_n_u8(fy));
            uint16x8_t lo = vshrq_n_u16(vaddq_u16(lo0, lo1), 8);
            uint16x8_t hi = vshrq_n_u16(vaddq_u16(hi0, hi1), 8);
            uint8x16_t res = vmovnbq_u16(vdupq_n_u8(0), lo);
            res = vmovntq_u16(res, hi);
            vst1q_u8(drow + dx, res);
        }
        for (; dx < dw; dx++) {
            drow[dx] = (uint8_t)(((int)mrow0[dx] * (256 - fy) + (int)mrow1[dx] * fy) >> 8);
        }
    }
}

static void scale_bilinear_mve(const uint8_t *src, int sw, int sh,
                               uint8_t *dst, int dw, int dh,
                               uint8_t *mid_buf, size_t stride, uint8_t bpp)
{
    LV_UNUSED(mid_buf);
    for (int dy = 0; dy < dh; dy++) {
        int sy_fixed = (dy * sh * 256) / dh;
        int y0 = sy_fixed >> 8, y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        int fy = sy_fixed & 0xFF;
        uint8_t *drow = dst + (size_t)dy * stride;
        int dx = 0;
        for (; dx + 16 <= dw; dx += 16) {
            uint8_t tmp0[16], tmp1[16];
            for (int i = 0; i < 16; i++) {
                int sx = ((dx + i) * sw * 256) / dw;
                int x0 = sx >> 8, x1 = x0 + 1;
                if (x1 >= sw) x1 = sw - 1;
                int fx = sx & 0xFF;
                tmp0[i] = (uint8_t)(((int)bitmap_get_pixel(src, (size_t)y0 * sw + x0, bpp) * (256 - fx) + (int)bitmap_get_pixel(src, (size_t)y0 * sw + x1, bpp) * fx) >> 8);
                tmp1[i] = (uint8_t)(((int)bitmap_get_pixel(src, (size_t)y1 * sw + x0, bpp) * (256 - fx) + (int)bitmap_get_pixel(src, (size_t)y1 * sw + x1, bpp) * fx) >> 8);
            }
            uint8x16_t vtop = vld1q_u8(tmp0);
            if (fy == 0) {
                vst1q_u8(drow + dx, vtop);
                continue;
            }
            uint8x16_t vbot = vld1q_u8(tmp1);
            uint16x8_t lo0 = vmullbq_int_u8(vtop, vdupq_n_u8(256 - fy));
            uint16x8_t lo1 = vmullbq_int_u8(vbot, vdupq_n_u8(fy));
            uint16x8_t hi0 = vmulltq_int_u8(vtop, vdupq_n_u8(256 - fy));
            uint16x8_t hi1 = vmulltq_int_u8(vbot, vdupq_n_u8(fy));
            uint16x8_t lo = vshrq_n_u16(vaddq_u16(lo0, lo1), 8);
            uint16x8_t hi = vshrq_n_u16(vaddq_u16(hi0, hi1), 8);
            uint8x16_t res = vmovnbq_u16(vdupq_n_u8(0), lo);
            res = vmovntq_u16(res, hi);
            vst1q_u8(drow + dx, res);
        }
        for (; dx < dw; dx++) {
            int sx = (dx * sw * 256) / dw;
            int x0 = sx >> 8, x1 = x0 + 1;
            if (x1 >= sw) x1 = sw - 1;
            int fx = sx & 0xFF;
            int p00 = bitmap_get_pixel(src, (size_t)y0 * sw + x0, bpp), p10 = bitmap_get_pixel(src, (size_t)y0 * sw + x1, bpp);
            int p01 = bitmap_get_pixel(src, (size_t)y1 * sw + x0, bpp), p11 = bitmap_get_pixel(src, (size_t)y1 * sw + x1, bpp);
            int top = (p00 * (256 - fx) + p10 * fx) >> 8;
            int bot = (p01 * (256 - fx) + p11 * fx) >> 8;
            drow[dx] = (uint8_t)((top * (256 - fy) + bot * fy) >> 8);
        }
    }
}

#endif /* __ARM_FEATURE_MVE */

/**
 * @brief 连续读取当前源行压缩字节并批量解包，再从共享工作区做水平插值。
 * @param src 源点阵，sw/sy为源宽和行号；x0/fx为预计算横坐标与8位小数。
 * @param row/dw 目标A8行及有效宽度；samples保存sw+1个0..3采样值，accel选择MVE或标量。
 * @return 无返回值；调用方保证sw、dw宽度上限及源、工作区边界。
 */
static inline __attribute__((always_inline)) void scale_2bpp_horizontal(const uint8_t *src, int sw, int sy,
                                  const uint16_t *x0, const uint16_t *fx,
                                  uint8_t *row, int dw, uint8_t *samples, bitmap_font_accel_t accel)
{
    size_t pixel = (size_t)sy * sw;
    int sx = 0;
    /* 连续位流的行首可能落在字节中间，该字节只读一次。 */
    if (pixel & 3) {
        uint8_t bits = src[pixel >> 2];
        do {
            samples[sx++] = (bits >> (6 - ((pixel & 3) * 2))) & 3;
            pixel++;
        } while (sx < sw && (pixel & 3));
    }
#if defined(__ARM_FEATURE_MVE)
    if (accel == BITMAP_FONT_ACCEL_MVE) {
        uint16x8_t offsets = vidupq_n_u16(0, 4);
        while (sx + 4 <= sw) {
            unsigned bytes = (unsigned)(sw - sx) / 4;
            if (bytes > 8) bytes = 8;
            mve_pred16_t active = vctp16q(bytes);
            /* 一次连续加载最多8字节，展开32像素；屏蔽尾部读取及写入。 */
            uint16x8_t bits = vldrbq_z_u16(src + (pixel >> 2), active);
            vstrbq_scatter_offset_p_u16(samples + sx, offsets,
                vshrq_n_u16(bits, 6), active);
            vstrbq_scatter_offset_p_u16(samples + sx + 1, offsets,
                vandq_u16(vshrq_n_u16(bits, 4), vdupq_n_u16(3)), active);
            vstrbq_scatter_offset_p_u16(samples + sx + 2, offsets,
                vandq_u16(vshrq_n_u16(bits, 2), vdupq_n_u16(3)), active);
            vstrbq_scatter_offset_p_u16(samples + sx + 3, offsets,
                vandq_u16(bits, vdupq_n_u16(3)), active);
            sx += (int)bytes * 4;
            pixel += bytes * 4;
        }
    }
#else
    LV_UNUSED(accel);
#endif
    for (; sx + 4 <= sw; sx += 4, pixel += 4) {
        uint8_t bits = src[pixel >> 2];
        samples[sx] = bits >> 6;
        samples[sx + 1] = (bits >> 4) & 3;
        samples[sx + 2] = (bits >> 2) & 3;
        samples[sx + 3] = bits & 3;
    }
    if (sx < sw) {
        uint8_t bits = src[pixel >> 2];
        do {
            samples[sx++] = (bits >> (6 - ((pixel & 3) * 2))) & 3;
            pixel++;
        } while (sx < sw);
    }
    samples[sw] = samples[sw - 1];

    int dx = 0;
#if defined(__ARM_FEATURE_MVE)
    if (accel == BITMAP_FONT_ACCEL_MVE) {
        for (; dx + 8 <= dw; dx += 8) {
            uint16x8_t left = vld1q_u16(x0 + dx);
            /* gather只访问当前行工作区，不再反复读ROMFS的压缩字节。 */
            uint16x8_t a = vldrbq_gather_offset_u16(samples, left);
            uint16x8_t b = vldrbq_gather_offset_u16(samples, vaddq_n_u16(left, 1));
            uint16x8_t weight = vld1q_u16(fx + dx);
            uint16x8_t sum = vaddq_u16(vmulq_u16(a, vsubq_u16(vdupq_n_u16(256), weight)),
                                      vmulq_u16(b, weight));
            /* 0..3先插值再乘85，与先展开A8完全等价，最大值65280。 */
            vstrbq_u16(row + dx, vshrq_n_u16(vmulq_n_u16(sum, 85), 8));
        }
    }
#endif
    for (; dx < dw; dx++) {
        int a = samples[x0[dx]], b = samples[x0[dx] + 1];
        row[dx] = (uint8_t)(((a * (256 - fx[dx]) + b * fx[dx]) * 85) >> 8);
    }
}

/**
 * @brief 2bpp双线性直采：横坐标只算一次，复用相邻源行的水平插值结果。
 * @param src/sw/sh 源ROMFS点阵及尺寸；dst/dw/dh/stride为LVGL A8缓冲布局。
 * @param scratch 共享工作区，前部保存sh*dw插值结果，尾部复用当前源行；accel选择MVE或标量。
 * @return 无返回值；调用方保证源/目标宽度上限，sh*dw与工作区尾部单行数据不重叠。
 */
static void scale_bilinear_2bpp(const uint8_t *src, int sw, int sh,
                                uint8_t *dst, int dw, int dh, uint8_t *scratch,
                                size_t stride, bitmap_font_accel_t accel)
{
    uint16_t x0[SCALE_2BPP_MAX_WIDTH];
    uint16_t fx[SCALE_2BPP_MAX_WIDTH];
    uint8_t *samples = scratch + SCALE_MID_BUF_SIZE - SCALE_2BPP_ROW_BYTES;
    for (int dx = 0; dx < dw; dx++) {
        int sx = (dx * sw * 256) / dw;
        x0[dx] = (uint16_t)(sx >> 8);
        fx[dx] = (uint16_t)(sx & 255);
    }
    /* 源行坐标单调递增；只生成实际参与插值的行，每个源行最多一次。 */
    int last_y = -1;
    for (int dy = 0; dy < dh; dy++) {
        int sy = (dy * sh * 256) / dh;
        int y0 = sy >> 8, y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        if (y0 > last_y) {
            scale_2bpp_horizontal(src, sw, y0, x0, fx, scratch + y0 * dw, dw, samples, accel);
            last_y = y0;
        }
        if ((sy & 255) && y1 > last_y) {
            scale_2bpp_horizontal(src, sw, y1, x0, fx, scratch + y1 * dw, dw, samples, accel);
            last_y = y1;
        }
    }
    for (int dy = 0; dy < dh; dy++) {
        int sy = (dy * sh * 256) / dh;
        int y0 = sy >> 8, y1 = y0 + 1;
        if (y1 >= sh) y1 = sh - 1;
        int fy = sy & 255;
        const uint8_t *top = scratch + y0 * dw;
        const uint8_t *bot = scratch + y1 * dw;
        uint8_t *drow = dst + (size_t)dy * stride;
        if (fy == 0 || y0 == y1) {
            memcpy(drow, top, (size_t)dw);
            continue;
        }
        int dx = 0;
#if defined(__ARM_FEATURE_MVE)
        if (accel == BITMAP_FONT_ACCEL_MVE) {
            for (; dx + 8 <= dw; dx += 8) {
                uint16x8_t a = vmulq_n_u16(vldrbq_u16(top + dx), 256 - fy);
                uint16x8_t b = vmulq_n_u16(vldrbq_u16(bot + dx), fy);
                vstrbq_u16(drow + dx, vshrq_n_u16(vaddq_u16(a, b), 8));
            }
        }
#endif
        for (; dx < dw; dx++)
            drow[dx] = (uint8_t)((top[dx] * (256 - fy) + bot[dx] * fy) >> 8);
    }
}

/* ------------------------------------------------------------------ */
/*  算法分发表                                                          */
/* ------------------------------------------------------------------ */

typedef void (*scale_fn_t)(const uint8_t *src, int sw, int sh,
                           uint8_t *dst, int dw, int dh,
                           uint8_t *mid_buf, size_t stride, uint8_t bpp);

static scale_fn_t scale_dispatch(bitmap_font_algo_t algo, bitmap_font_accel_t accel)
{
    LV_UNUSED(accel);
    switch (algo) {
    case BITMAP_FONT_ALGO_NEAREST:
#if defined(__ARM_FEATURE_MVE)
        if (accel == BITMAP_FONT_ACCEL_MVE) return scale_nearest_mve;
#endif
        return scale_nearest_scalar;

    case BITMAP_FONT_ALGO_LINE:
#if defined(__ARM_FEATURE_MVE)
        if (accel == BITMAP_FONT_ACCEL_MVE) return scale_line_mve;
#endif
#if defined(__ARM_FEATURE_DSP)
        if (accel == BITMAP_FONT_ACCEL_DSP) return scale_line_dsp;
#endif
        return scale_line_scalar;

    case BITMAP_FONT_ALGO_BILINEAR:
#if defined(__ARM_FEATURE_MVE)
        if (accel == BITMAP_FONT_ACCEL_MVE) return scale_bilinear_mve;
#endif
#if defined(__ARM_FEATURE_DSP)
        if (accel == BITMAP_FONT_ACCEL_DSP) return scale_bilinear_dsp;
#endif
        return scale_bilinear_scalar;

    default:
        return scale_bilinear_scalar;
    }
}

/**
 * @brief 复用固定小表中的字形定位；未命中时在ROMFS描述表二分查找。
 * @param[in] fd 已校验且保持有效的字库映射。
 * @param[in] unicode Unicode 码点。
 * @param[out] out_entry 找到的字形描述，使用复制避免未对齐访问。
 * @param[out] out_index 映射描述表中的索引，供位图回调直接定位。
 * @return 找到返回 true，否则返回 false。
 */
static bool bitmap_font_find_glyph(bitmap_font_data_t *fd, uint32_t unicode,
                                   bitmap_glyph_entry_t *out_entry, uint32_t *out_index)
{
    /* 跨测宽、换行、绘制及字号复用定位；不复制ROMFS描述表或原始点阵。 */
    uint32_t slot = ((unicode * 2654435761U) >> 24);
    bitmap_glyph_lookup_t *cached = &fd->lookup[slot];
    if (cached->index_plus_one && cached->unicode == unicode) {
        if (cached->index_plus_one == UINT32_MAX) return false;
        *out_index = cached->index_plus_one - 1;
        memcpy(out_entry, fd->mapped + BITMAP_FONT_HEADER_SIZE +
               (size_t)*out_index * BITMAP_FONT_GLYPH_ENTRY_SIZE, sizeof(*out_entry));
        return true;
    }
    uint32_t lo = 0;
    uint32_t hi = fd->header.glyph_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *entry = fd->mapped + BITMAP_FONT_HEADER_SIZE +
                               (size_t)mid * BITMAP_FONT_GLYPH_ENTRY_SIZE;
        uint32_t codepoint;
        memcpy(&codepoint, entry, sizeof(codepoint));
        if (codepoint < unicode) {
            lo = mid + 1;
        } else if (codepoint > unicode) {
            hi = mid;
        } else {
            memcpy(out_entry, entry, sizeof(*out_entry));
            *out_index = mid;
            cached->unicode = unicode;
            cached->index_plus_one = mid + 1;
            return true;
        }
    }
    cached->unicode = unicode;
    cached->index_plus_one = UINT32_MAX;
    return false;
}

/* ------------------------------------------------------------------ */
/*  LVGL 字体回调                                                       */
/* ------------------------------------------------------------------ */

static bool bitmap_font_get_glyph_dsc_cb(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                         uint32_t unicode_letter, uint32_t letter_next)
{
    LV_UNUSED(letter_next);
    bitmap_font_dsc_t *dsc = (bitmap_font_dsc_t *)font->dsc;
    bitmap_font_data_t *fd = dsc->data;

    bitmap_glyph_entry_t ge;
    uint32_t glyph_index = 0;
    bitmap_glyph_metric_t *metric = &fd->metrics[(unicode_letter * 2654435761U) >> 27];
    bool cached = metric->index_plus_one && metric->unicode == unicode_letter &&
                  metric->target_size == dsc->target_size;
    bool found = cached ? metric->index_plus_one != UINT32_MAX :
                 bitmap_font_find_glyph(fd, unicode_letter, &ge, &glyph_index);
    if (!cached) {
        metric->unicode = unicode_letter;
        metric->target_size = dsc->target_size;
        metric->index_plus_one = found ? glyph_index + 1 : UINT32_MAX;
    }
    if (!found) {
        dsc_out->adv_w = 0;
        dsc_out->box_h = 0;
        dsc_out->box_w = 0;
        dsc_out->ofs_x = 0;
        dsc_out->ofs_y = 0;
        dsc_out->format = LV_FONT_GLYPH_FORMAT_NONE;
        return false;
    }
    if (!cached) {
        int32_t base = fd->header.base_size;
        int32_t target = dsc->target_size;
        metric->adv_w = (uint16_t)((ge.adv_w * target + base / 2) / base);
        metric->box_w = (uint16_t)((ge.box_w * target + base / 2) / base);
        metric->box_h = (uint16_t)((ge.box_h * target + base / 2) / base);
        metric->ofs_x = (int16_t)((ge.ofs_x * target + base / 2) / base);
        metric->ofs_y = (int16_t)((ge.ofs_y * target + base / 2) / base);
    }
    dsc_out->resolved_font = font;
    dsc_out->adv_w = metric->adv_w;
    dsc_out->box_w = metric->box_w;
    dsc_out->box_h = metric->box_h;
    dsc_out->ofs_x = metric->ofs_x;
    dsc_out->ofs_y = metric->ofs_y;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->is_placeholder = 0;
    dsc_out->gid.index = metric->index_plus_one - 1;
    return true;
}

static const void *bitmap_font_get_glyph_bitmap_cb(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf)
{
    if (draw_buf == NULL) return NULL;
    const lv_font_t *font = g_dsc->resolved_font;
    bitmap_font_dsc_t *dsc = (bitmap_font_dsc_t *)font->dsc;
    bitmap_font_data_t *fd = dsc->data;

    /* 描述回调已完成二分查找，直接按表索引读取，不缓存原始点阵。 */
    if (g_dsc->gid.index >= fd->header.glyph_count) return NULL;
    bitmap_glyph_entry_t ge;
    memcpy(&ge, fd->mapped + BITMAP_FONT_HEADER_SIZE +
           (size_t)g_dsc->gid.index * BITMAP_FONT_GLYPH_ENTRY_SIZE, sizeof(ge));
    if (ge.box_w == 0 || ge.box_h == 0) return NULL;

    int src_w = ge.box_w;
    int src_h = ge.box_h;
    size_t pixels = (size_t)src_w * src_h;
    size_t packed_size = (pixels * fd->header.bpp + 7) / 8;
    size_t table_end = BITMAP_FONT_HEADER_SIZE +
                       (size_t)fd->header.glyph_count * BITMAP_FONT_GLYPH_ENTRY_SIZE;
    if (pixels > GLYPH_MAX_BYTES || ge.bitmap_size != packed_size ||
        ge.bitmap_offset < table_end || ge.bitmap_offset > fd->mapped_size ||
        ge.bitmap_size > fd->mapped_size - ge.bitmap_offset) return NULL;
    const uint8_t *src = fd->mapped + ge.bitmap_offset;

    int dst_w = g_dsc->box_w;
    int dst_h = g_dsc->box_h;
    size_t stride = draw_buf->header.stride;
    if (dst_w <= 0 || dst_h <= 0 || draw_buf->data == NULL ||
        stride < (size_t)dst_w || draw_buf->header.w < (uint32_t)dst_w ||
        draw_buf->header.h < (uint32_t)dst_h ||
        draw_buf->header.cf != LV_COLOR_FORMAT_A8 ||
        stride > draw_buf->data_size / (size_t)dst_h) return NULL;
    uint8_t *dst_data = draw_buf->data;

    if (dst_w == src_w && dst_h == src_h) {
        bitmap_unpack_to_a8_buf(src, src_w, src_h, fd->header.bpp, dst_data, stride);
    } else {
        if ((size_t)dst_w * dst_h > GLYPH_MAX_BYTES ||
            (dsc->algo == BITMAP_FONT_ALGO_LINE &&
             (size_t)src_h * dst_w > SCALE_MID_BUF_SIZE)) return NULL;
        if (fd->header.bpp == 2 && dsc->algo == BITMAP_FONT_ALGO_BILINEAR &&
            src_w <= SCALE_2BPP_MAX_WIDTH && dst_w <= SCALE_2BPP_MAX_WIDTH &&
            (size_t)src_h * dst_w <= SCALE_MID_BUF_SIZE - SCALE_2BPP_ROW_BYTES) {
            scale_bilinear_2bpp(src, src_w, src_h, dst_data, dst_w, dst_h,
                                fd->scale_mid_buf, stride, dsc->accel);
        } else {
            scale_fn_t scale_fn = scale_dispatch(dsc->algo, dsc->accel);
            scale_fn(src, src_w, src_h, dst_data, dst_w, dst_h,
                     fd->scale_mid_buf, stride, fd->header.bpp);
        }

        /* 保持1bpp缩小的原有阈值行为；2/4/8bpp保留灰度及目标行padding。 */
        if (fd->header.bpp == 1 && (dst_w < src_w || dst_h < src_h)) {
            for (int y = 0; y < dst_h; y++) {
                uint8_t *row = dst_data + (size_t)y * stride;
                for (int x = 0; x < dst_w; x++)
                    row[x] = row[x] > 128 ? 255 : 0;
            }
        }
    }
    return draw_buf;
}

/* ------------------------------------------------------------------ */
/*  共享数据管理                                                        */
/* ------------------------------------------------------------------ */

static bitmap_font_data_t g_shared_data_storage;
static bitmap_font_data_t *g_shared_data = NULL;

static bitmap_font_data_t *bitmap_font_data_acquire(const char *path)
{
    if (g_shared_data != NULL) {
        g_shared_data->ref_count++;
        return g_shared_data;
    }

    /* 工作区静态分配，映射不占用整库大小的 RAM。 */
    bitmap_font_data_t *fd = &g_shared_data_storage;
    memset(fd, 0, sizeof(*fd));
    fd->file = floatair_fs_open(path, FLOATAIR_FS_MODE_RD);
    if (!fd->file) {
        floatair_err("bitmap font open failed: %s", path);
        return NULL;
    }

    const void *mapped = NULL;
    if (floatair_fs_map_readonly(fd->file, &mapped, &fd->mapped_size) != FLOATAIR_FS_OK ||
        fd->mapped_size < BITMAP_FONT_HEADER_SIZE) {
        floatair_err("bitmap font mapping failed: %s", path);
        floatair_fs_close(fd->file);
        fd->file = NULL;
        return NULL;
    }
    fd->mapped = mapped;
    memcpy(&fd->header, fd->mapped, sizeof(fd->header));
    if (memcmp(fd->header.magic, BITMAP_FONT_MAGIC, 4) != 0 ||
        fd->header.version != 1 || fd->header.base_size == 0 ||
        fd->header.glyph_count == 0 ||
        (fd->header.bpp != 1 && fd->header.bpp != 2 &&
         fd->header.bpp != 4 && fd->header.bpp != 8) ||
        fd->header.glyph_count >
            (fd->mapped_size - BITMAP_FONT_HEADER_SIZE) / BITMAP_FONT_GLYPH_ENTRY_SIZE) {
        floatair_err("bitmap font invalid mapped header: %s", path);
        floatair_fs_close(fd->file);
        fd->file = NULL;
        fd->mapped = NULL;
        return NULL;
    }

    fd->ref_count = 1;
    g_shared_data = fd;

    floatair_info("bitmap font mapped: base=%d bpp=%d glyphs=%lu bytes=%lu workspace=%lu",
                  fd->header.base_size, fd->header.bpp,
                  (unsigned long)fd->header.glyph_count,
                  (unsigned long)fd->mapped_size, (unsigned long)sizeof(*fd));
    return fd;
}

static void bitmap_font_data_release(bitmap_font_data_t *fd)
{
    if (fd == NULL) return;
    fd->ref_count--;
    if (fd->ref_count > 0) return;

    if (fd->file) floatair_fs_close(fd->file);
    fd->file = NULL;
    fd->mapped = NULL;
    fd->mapped_size = 0;
    if (g_shared_data == fd) g_shared_data = NULL;
    /* fd 是静态存储，不 free */
}

/* ------------------------------------------------------------------ */
/*  对外接口                                                            */
/* ------------------------------------------------------------------ */

lv_font_t *bitmap_font_create_file(const char *path, int32_t font_size)
{
    if (!path || font_size <= 0) return NULL;

    bitmap_font_data_t *fd = bitmap_font_data_acquire(path);
    if (!fd) return NULL;

    bitmap_font_dsc_t *dsc = (bitmap_font_dsc_t *)malloc(sizeof(bitmap_font_dsc_t));
    if (!dsc) {
        bitmap_font_data_release(fd);
        return NULL;
    }
    memset(dsc, 0, sizeof(*dsc));
    dsc->magic = BITMAP_FONT_MAGIC_DSC;
    dsc->data = fd;
    dsc->target_size = font_size;
    /* 1bpp 字体用 LINE 算法（水平、垂直两次线性插值）；
       高 bpp 字体保留 BILINEAR 以获得平滑缩放。 */
    dsc->algo = (fd->header.bpp == 1) ? BITMAP_FONT_ALGO_LINE : BITMAP_FONT_ALGO_BILINEAR;
#if defined(__ARM_FEATURE_MVE)
    dsc->accel = BITMAP_FONT_ACCEL_MVE;
#elif defined(__ARM_FEATURE_DSP)
    dsc->accel = BITMAP_FONT_ACCEL_DSP;
#else
    dsc->accel = BITMAP_FONT_ACCEL_SCALAR;
#endif

    lv_font_t *font = (lv_font_t *)malloc(sizeof(lv_font_t));
    if (!font) {
        free(dsc);
        bitmap_font_data_release(fd);
        return NULL;
    }
    memset(font, 0, sizeof(*font));

    int32_t base = fd->header.base_size;
    int32_t target = font_size;
    font->line_height = (int32_t)((fd->header.line_height * target + base / 2) / base);
    font->base_line   = (int32_t)((fd->header.base_line   * target + base / 2) / base);
    font->get_glyph_dsc = bitmap_font_get_glyph_dsc_cb;
    font->get_glyph_bitmap = bitmap_font_get_glyph_bitmap_cb;
    font->dsc = dsc;
    font->kerning = LV_FONT_KERNING_NONE;
    font->subpx = LV_FONT_SUBPX_NONE;
    font->underline_position = -2;
    font->underline_thickness = 1;

    floatair_info("bitmap font create ok: size=%d base=%d line_h=%d ref=%d",
                  (int)font_size, fd->header.base_size, (int)font->line_height, fd->ref_count);
    return font;
}

void bitmap_font_destroy(lv_font_t *font)
{
    if (!font) return;
    bitmap_font_dsc_t *dsc = (bitmap_font_dsc_t *)font->dsc;
    if (dsc && dsc->magic == BITMAP_FONT_MAGIC_DSC) {
        bitmap_font_data_release(dsc->data);
        free(dsc);
    }
    free(font);
}

bool bitmap_font_is_font(const lv_font_t *font)
{
    if (!font || !font->dsc) return false;
    bitmap_font_dsc_t *dsc = (bitmap_font_dsc_t *)font->dsc;
    return dsc->magic == BITMAP_FONT_MAGIC_DSC;
}

bool bitmap_font_set_algo(lv_font_t *font, bitmap_font_algo_t algo)
{
    if (!bitmap_font_is_font(font) || algo >= BITMAP_FONT_ALGO_COUNT) return false;
    ((bitmap_font_dsc_t *)font->dsc)->algo = algo;
    return true;
}

bool bitmap_font_set_accel(lv_font_t *font, bitmap_font_accel_t accel)
{
    if (!bitmap_font_is_font(font) || accel >= BITMAP_FONT_ACCEL_COUNT) return false;
    if (accel == BITMAP_FONT_ACCEL_DSP) {
#if !defined(__ARM_FEATURE_DSP)
        accel = BITMAP_FONT_ACCEL_SCALAR;
#endif
    } else if (accel == BITMAP_FONT_ACCEL_MVE) {
#if !defined(__ARM_FEATURE_MVE)
        accel = BITMAP_FONT_ACCEL_SCALAR;
#endif
    }
    ((bitmap_font_dsc_t *)font->dsc)->accel = accel;
    return true;
}

bitmap_font_algo_t bitmap_font_get_algo(const lv_font_t *font)
{
    if (!bitmap_font_is_font(font)) return BITMAP_FONT_ALGO_BILINEAR;
    return ((bitmap_font_dsc_t *)font->dsc)->algo;
}

bitmap_font_accel_t bitmap_font_get_accel(const lv_font_t *font)
{
    if (!bitmap_font_is_font(font)) return BITMAP_FONT_ACCEL_SCALAR;
    return ((bitmap_font_dsc_t *)font->dsc)->accel;
}

uint8_t bitmap_font_get_base_size(const lv_font_t *font)
{
    if (!bitmap_font_is_font(font)) return 0;
    return ((bitmap_font_dsc_t *)font->dsc)->data->header.base_size;
}
