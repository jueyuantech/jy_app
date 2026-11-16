/**
* @file lv_tiny_ttf.c
*
*/

/*********************
 *      INCLUDES
 *********************/
#include "../../lvgl.h"

#if LV_USE_TINY_TTF != 0
#include "../../core/lv_global.h"
#include <limits.h>

#define font_draw_buf_handlers &(LV_GLOBAL_DEFAULT()->font_draw_buf_handlers)

/*********************
 *      DEFINES
 *********************/

#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_HEAP_FACTOR_SIZE_32 50
#define STBTT_HEAP_FACTOR_SIZE_128 20
#define STBTT_HEAP_FACTOR_SIZE_DEFAULT 10
#define STBTT_malloc(x, u) ((void)(u), lv_malloc(x))
#define STBTT_free(x, u) ((void)(u), lv_free(x))
#define TINY_TTF_DESC_MAGIC 0x54544654u

#if LV_TINY_TTF_FILE_SUPPORT != 0
/* for stream support */
#define STBTT_STREAM_TYPE ttf_cb_stream_t *
#define STBTT_STREAM_SEEK(s, x) ttf_cb_stream_seek(s, x);
#define STBTT_STREAM_READ(s, x, y) ttf_cb_stream_read(s, x, y);
/* 将字体流定位和读取收敛为一次加锁操作，避免并发字形读取串位。 */
#define STBTT_STREAM_SEEK_AND_READ(s, o, x, y) ttf_cb_stream_seek_and_read(s, o, x, y);

/* a hydra stream that can be in memory or from a file*/
typedef struct ttf_cb_stream {
    lv_fs_file_t * file;
    const void * data;
    lv_mutex_t mutex; /* 字体流定位与读取的原子访问锁。 */
    size_t size;
    size_t position;
} ttf_cb_stream_t;

static void ttf_cb_stream_read(ttf_cb_stream_t * stream, void * data, size_t to_read);
static void ttf_cb_stream_seek(ttf_cb_stream_t * stream, size_t position);
static void ttf_cb_stream_seek_and_read(ttf_cb_stream_t * stream, size_t position, void * data, size_t to_read);
#endif

#include "stb_rect_pack.h"
#include "stb_truetype_htcw.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct ttf_font_desc {
    uint32_t magic; /**< TinyTTF 私有描述符标识，用于防止非 TinyTTF 字体误入 */
    lv_fs_file_t file;
#if LV_TINY_TTF_FILE_SUPPORT != 0
    ttf_cb_stream_t stream;
#else
    const uint8_t * stream;
    size_t stream_size;
#endif
    stbtt_fontinfo info;
    float scale;
    int32_t font_size; /**< 当前字体 em 映射像素大小，用于 HarfBuzz 26.6 scale */
    int ascent;
    int descent;

    lv_font_kerning_t kerning;

    int cache_size;
    lv_cache_t * glyph_cache;
    lv_cache_t * draw_data_cache;
} ttf_font_desc_t;

typedef struct _tiny_ttf_glyph_cache_data_t {
    uint32_t unicode;
    int adv_w;
    lv_font_glyph_dsc_t glyph_dsc;
} tiny_ttf_glyph_cache_data_t;

typedef struct lv_tiny_ttf_cache_data_t {
    uint32_t glyph_index;
    uint32_t size;
    lv_draw_buf_t * draw_buf;
} tiny_ttf_cache_data_t;
/**********************
 *  STATIC PROTOTYPES
 **********************/
static bool ttf_get_glyph_dsc_cb(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter,
                                 uint32_t unicode_letter_next);
static const void * ttf_get_glyph_bitmap_cb(lv_font_glyph_dsc_t * g_dsc, lv_draw_buf_t * draw_buf);
static void ttf_release_glyph_cb(const lv_font_t * font, lv_font_glyph_dsc_t * g_dsc);
static lv_font_t * lv_tiny_ttf_create(const char * path, const void * data, size_t data_size,
                                      int32_t font_size, lv_font_kerning_t kerning,
                                      size_t cache_size);

static bool tiny_ttf_glyph_cache_create_cb(tiny_ttf_glyph_cache_data_t * node, void * user_data);
static void tiny_ttf_glyph_cache_free_cb(tiny_ttf_glyph_cache_data_t * node, void * user_data);
static lv_cache_compare_res_t tiny_ttf_glyph_cache_compare_cb(const tiny_ttf_glyph_cache_data_t * lhs,
                                                              const tiny_ttf_glyph_cache_data_t * rhs);
static bool tiny_ttf_fill_glyph_dsc_by_gid(ttf_font_desc_t * dsc, lv_font_glyph_dsc_t * dsc_out, uint32_t glyph_id,
                                           int32_t advance_override, int * adv_w_out);

static bool tiny_ttf_draw_data_cache_create_cb(tiny_ttf_cache_data_t * node, void * user_data);
static void tiny_ttf_draw_data_cache_free_cb(tiny_ttf_cache_data_t * node, void * user_data);
static lv_cache_compare_res_t tiny_ttf_draw_data_cache_compare_cb(const tiny_ttf_cache_data_t * lhs,
                                                                  const tiny_ttf_cache_data_t * rhs);

static void lv_tiny_ttf_cache_create(ttf_font_desc_t * dsc);
static bool tiny_ttf_stream_read_at(ttf_font_desc_t * dsc, uint32_t offset, void * data, uint32_t size);
static uint16_t tiny_ttf_read_u16_be(const uint8_t * data);
static uint32_t tiny_ttf_read_u32_be(const uint8_t * data);
static void tiny_ttf_close_file_if_open(ttf_font_desc_t * dsc);
static ttf_font_desc_t * tiny_ttf_get_desc(const lv_font_t * font);
/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_tiny_ttf_set_size(lv_font_t * font, int32_t font_size)
{
    if(font_size <= 0) {
        LV_LOG_ERROR("invalid font size: %"LV_PRIx32, font_size);
        return;
    }
    ttf_font_desc_t * dsc = (ttf_font_desc_t *)font->dsc;
    dsc->font_size = font_size;
    dsc->scale = stbtt_ScaleForMappingEmToPixels(&dsc->info, font_size);
    int line_gap = 0;
    stbtt_GetFontVMetrics(&dsc->info, &dsc->ascent, &dsc->descent, &line_gap);
    font->line_height = (int32_t)(dsc->scale * (dsc->ascent - dsc->descent + line_gap));
    font->base_line = (int32_t)(dsc->scale * (line_gap - dsc->descent));

    /* size change means cache needs to be invalidated. */

    if(dsc->glyph_cache) {
        lv_cache_destroy(dsc->glyph_cache, NULL);
        dsc->glyph_cache = NULL;
    }

    if(dsc->draw_data_cache) {
        lv_cache_destroy(dsc->draw_data_cache, NULL);
        dsc->draw_data_cache = NULL;
    }

    lv_tiny_ttf_cache_create(dsc);
}

void lv_tiny_ttf_destroy(lv_font_t * font)
{
    LV_ASSERT_NULL(font);

    if(font->dsc != NULL) {
        ttf_font_desc_t * ttf = (ttf_font_desc_t *)font->dsc;
#if LV_TINY_TTF_FILE_SUPPORT != 0
        lv_mutex_lock(&ttf->stream.mutex);
        if(ttf->stream.file != NULL) {
            lv_fs_close(&ttf->file);
        }
        lv_mutex_unlock(&ttf->stream.mutex);
        lv_mutex_delete(&ttf->stream.mutex);
#endif
        lv_cache_destroy(ttf->glyph_cache, NULL);
        lv_cache_destroy(ttf->draw_data_cache, NULL);
        ttf->magic = 0;
        lv_free(ttf);
        font->dsc = NULL;
    }

    lv_free(font);
}

bool lv_tiny_ttf_is_font(const lv_font_t * font)
{
    return tiny_ttf_get_desc(font) != NULL;
}

bool lv_tiny_ttf_get_glyph_dsc_by_gid(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t glyph_id,
                                      int32_t advance_override)
{
    if(dsc_out == NULL) {
        return false;
    }

    ttf_font_desc_t * dsc = tiny_ttf_get_desc(font);
    if(dsc == NULL) {
        return false;
    }

    if(!tiny_ttf_fill_glyph_dsc_by_gid(dsc, dsc_out, glyph_id, advance_override, NULL)) {
        return false;
    }

    dsc_out->resolved_font = font;
    dsc_out->entry = NULL;
    return true;
}

bool lv_tiny_ttf_get_table(const lv_font_t * font, uint32_t tag, uint8_t ** data_out, uint32_t * size_out)
{
    if(data_out == NULL || size_out == NULL) {
        return false;
    }

    *data_out = NULL;
    *size_out = 0;

    ttf_font_desc_t * dsc = tiny_ttf_get_desc(font);
    if(dsc == NULL) {
        return false;
    }

    if(dsc->info.fontstart < 0) {
        return false;
    }

    uint32_t font_start = (uint32_t)dsc->info.fontstart;
    uint8_t sfnt_header[12];
    if(font_start > UINT32_MAX - sizeof(sfnt_header) ||
       !tiny_ttf_stream_read_at(dsc, font_start, sfnt_header, sizeof(sfnt_header))) {
        return false;
    }

    uint16_t num_tables = tiny_ttf_read_u16_be(&sfnt_header[4]);
    for(uint16_t i = 0; i < num_tables; i++) {
        uint8_t table_record[16];
        uint32_t record_offset_from_start = 12 + ((uint32_t)i * 16);
        if(font_start > UINT32_MAX - record_offset_from_start) {
            return false;
        }

        uint32_t record_offset = font_start + record_offset_from_start;
        if(!tiny_ttf_stream_read_at(dsc, record_offset, table_record, sizeof(table_record))) {
            return false;
        }

        if(tiny_ttf_read_u32_be(&table_record[0]) != tag) {
            continue;
        }

        uint32_t table_offset = tiny_ttf_read_u32_be(&table_record[8]);
        uint32_t table_size = tiny_ttf_read_u32_be(&table_record[12]);
        if(table_size == 0) {
            return false;
        }

        uint8_t * table_data = lv_malloc(table_size);
        if(table_data == NULL) {
            LV_LOG_ERROR("tiny_ttf: out of memory");
            return false;
        }

        if(!tiny_ttf_stream_read_at(dsc, table_offset, table_data, table_size)) {
            lv_free(table_data);
            return false;
        }

        *data_out = table_data;
        *size_out = table_size;
        return true;
    }

    return false;
}

bool lv_tiny_ttf_get_hb_scale(const lv_font_t * font, int32_t * scale_out)
{
    if(scale_out == NULL) {
        return false;
    }

    ttf_font_desc_t * dsc = tiny_ttf_get_desc(font);
    if(dsc == NULL) {
        return false;
    }

    if(dsc->font_size <= 0 || dsc->font_size > (INT_MAX / 64)) {
        return false;
    }

    *scale_out = dsc->font_size * 64;
    return true;
}

void lv_tiny_ttf_free_table(uint8_t * data)
{
    lv_free(data);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#if LV_TINY_TTF_FILE_SUPPORT != 0
static void ttf_cb_stream_read(ttf_cb_stream_t * stream, void * data, size_t to_read)
{
    if(stream->file != NULL) {
        uint32_t br;
        lv_fs_read(stream->file, data, to_read, &br);
    }
    else {
        if(to_read + stream->position >= stream->size) {
            to_read = stream->size - stream->position;
        }
        memcpy(data, ((const unsigned char *)stream->data + stream->position), to_read);
        stream->position += to_read;
    }
}
static void ttf_cb_stream_seek(ttf_cb_stream_t * stream, size_t position)
{
    if(stream->file != NULL) {
        lv_fs_seek(stream->file, position, LV_FS_SEEK_SET);
    }
    else {
        if(position > stream->size) {
            stream->position = stream->size;
        }
        else {
            stream->position = position;
        }
    }
}

static void ttf_cb_stream_seek_and_read(ttf_cb_stream_t * stream, size_t position, void * data, size_t to_read)
{
    lv_mutex_lock(&stream->mutex);
    ttf_cb_stream_seek(stream, position);
    ttf_cb_stream_read(stream, data, to_read);
    lv_mutex_unlock(&stream->mutex);
}
#endif

static bool tiny_ttf_stream_read_at(ttf_font_desc_t * dsc, uint32_t offset, void * data, uint32_t size)
{
    if(size == 0) {
        return true;
    }

    if(dsc == NULL || data == NULL) {
        return false;
    }

#if LV_TINY_TTF_FILE_SUPPORT != 0
    ttf_cb_stream_t * stream = &dsc->stream;
    if(stream->file != NULL) {
        uint32_t current_pos = 0;
        uint32_t br = 0;

        if(lv_fs_tell(stream->file, &current_pos) != LV_FS_RES_OK) {
            return false;
        }

        if(lv_fs_seek(stream->file, offset, LV_FS_SEEK_SET) != LV_FS_RES_OK) {
            lv_fs_seek(stream->file, current_pos, LV_FS_SEEK_SET);
            return false;
        }

        lv_fs_res_t read_res = lv_fs_read(stream->file, data, size, &br);
        lv_fs_res_t restore_res = lv_fs_seek(stream->file, current_pos, LV_FS_SEEK_SET);
        return read_res == LV_FS_RES_OK && restore_res == LV_FS_RES_OK && br == size;
    }

    if(stream->data == NULL || offset > stream->size || size > stream->size - offset) {
        return false;
    }

    memcpy(data, (const uint8_t *)stream->data + offset, size);
    return true;
#else
    if(dsc->stream == NULL || offset > dsc->stream_size || size > dsc->stream_size - offset) {
        return false;
    }

    memcpy(data, dsc->stream + offset, size);
    return true;
#endif
}

static uint16_t tiny_ttf_read_u16_be(const uint8_t * data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static uint32_t tiny_ttf_read_u32_be(const uint8_t * data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

static void tiny_ttf_close_file_if_open(ttf_font_desc_t * dsc)
{
#if LV_TINY_TTF_FILE_SUPPORT != 0
    if(dsc != NULL && dsc->stream.file != NULL) {
        lv_fs_close(&dsc->file);
        dsc->stream.file = NULL;
    }
#else
    LV_UNUSED(dsc);
#endif
}

static ttf_font_desc_t * tiny_ttf_get_desc(const lv_font_t * font)
{
    if(font == NULL || font->dsc == NULL) {
        return NULL;
    }

    ttf_font_desc_t * dsc = (ttf_font_desc_t *)font->dsc;
    if(dsc->magic != TINY_TTF_DESC_MAGIC) {
        return NULL;
    }

    return dsc;
}

static bool ttf_get_glyph_dsc_cb(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter,
                                 uint32_t unicode_letter_next)
{
    if(unicode_letter < 0x20 ||
       unicode_letter == 0xf8ff || /*LV_SYMBOL_DUMMY*/
       unicode_letter == 0x200c) { /*ZERO WIDTH NON-JOINER*/
        dsc_out->box_w = 0;
        dsc_out->adv_w = 0;
        dsc_out->box_h = 0; /*height of the bitmap in [px]*/
        dsc_out->ofs_x = 0; /*X offset of the bitmap in [pf]*/
        dsc_out->ofs_y = 0; /*Y offset of the bitmap in [pf]*/
        dsc_out->format = LV_FONT_GLYPH_FORMAT_NONE;
        dsc_out->is_placeholder = false;
        return true;
    }

    ttf_font_desc_t * dsc = (ttf_font_desc_t *)font->dsc;

    tiny_ttf_glyph_cache_data_t search_key = {
        .unicode = unicode_letter,
    };

    int adv_w;
    lv_cache_entry_t * entry = lv_cache_acquire_or_create(dsc->glyph_cache, &search_key, (void *)dsc);

    if(entry == NULL) {
        LV_LOG_ERROR("cache_size: %u", dsc->cache_size);
        if(!dsc->cache_size) {  /* no cache, do everything directly */
            uint32_t g1 = stbtt_FindGlyphIndex(&dsc->info, (int)unicode_letter);
            tiny_ttf_glyph_cache_create_cb(&search_key, dsc);
            *dsc_out = search_key.glyph_dsc;
            adv_w = search_key.adv_w;

            /*Kerning correction*/
            if(font->kerning == LV_FONT_KERNING_NORMAL &&
               unicode_letter_next != 0) {
                int g2 = stbtt_FindGlyphIndex(&dsc->info, (int)unicode_letter_next); /* not using cache, only do glyph id lookup */
                if(g2) {
                    int k = stbtt_GetGlyphKernAdvance(&dsc->info, g1, g2);
                    dsc_out->adv_w = (uint16_t)floor((((float)adv_w + (float)k) * dsc->scale) +
                                                     0.5f); /*Horizontal space required by the glyph in [px]*/
                }
            }

            dsc_out->entry = NULL;
            return true;
        }
        LV_LOG_ERROR("cache not allocated");
        return false;
    }

    tiny_ttf_glyph_cache_data_t * data = lv_cache_entry_get_data(entry);
    *dsc_out = data->glyph_dsc;
    adv_w = data->adv_w;
    lv_cache_release(dsc->glyph_cache, entry, NULL);

    /*Kerning correction*/
    if(font->kerning == LV_FONT_KERNING_NORMAL &&
       unicode_letter_next != 0) { /* check if we need to do any kerning calculations */
        uint32_t g1 = dsc_out->gid.index;

        int g2 = 0;
        search_key.unicode = unicode_letter_next; /* reuse search key */
        lv_cache_entry_t * entry_next = lv_cache_acquire_or_create(dsc->glyph_cache, &search_key, (void *)dsc);

        if(entry_next == NULL)
            g2 = stbtt_FindGlyphIndex(&dsc->info, (int)unicode_letter_next);
        else {
            tiny_ttf_glyph_cache_data_t * data_next = lv_cache_entry_get_data(entry_next);
            g2 = data_next->glyph_dsc.gid.index;
            lv_cache_release(dsc->glyph_cache, entry_next, NULL);
        }

        if(g2) {
            int k = stbtt_GetGlyphKernAdvance(&dsc->info, g1, g2);
            dsc_out->adv_w = (uint16_t)floor((((float)adv_w + (float)k) * dsc->scale) +
                                             0.5f); /*Horizontal space required by the glyph in [px]*/
        }
    }

    dsc_out->entry = NULL;
    return true; /*true: glyph found; false: glyph was not found*/
}

static const void * ttf_get_glyph_bitmap_cb(lv_font_glyph_dsc_t * g_dsc, lv_draw_buf_t * draw_buf)
{
    LV_UNUSED(draw_buf);
    uint32_t glyph_index = g_dsc->gid.index;
    const lv_font_t * font = g_dsc->resolved_font;
    ttf_font_desc_t * dsc = (ttf_font_desc_t *)font->dsc;
    tiny_ttf_cache_data_t search_key = {
        .glyph_index = glyph_index,
        .size = font->line_height,
    };

    lv_cache_entry_t * entry = lv_cache_acquire_or_create(dsc->draw_data_cache, &search_key, (void *)font->dsc);
    if(entry == NULL) {
        if(!dsc->cache_size) {  /* no cache, do everything directly */
            if(tiny_ttf_draw_data_cache_create_cb(&search_key, (void *)font->dsc)) {
                /* use the cache entry to store the buffer if no cache specified */
                g_dsc->entry = (lv_cache_entry_t *)search_key.draw_buf;
                return g_dsc->entry;
            }
            else {
                return NULL;
            }
        }
        LV_LOG_ERROR("cache not allocated");
        return NULL;
    }

    g_dsc->entry = entry;
    tiny_ttf_cache_data_t * cached_data = lv_cache_entry_get_data(entry);
    return cached_data->draw_buf;
}

static void ttf_release_glyph_cb(const lv_font_t * font, lv_font_glyph_dsc_t * g_dsc)
{
    LV_ASSERT_NULL(font);

    ttf_font_desc_t * dsc = (ttf_font_desc_t *)font->dsc;
    if(!dsc->cache_size) {  /* no cache, do everything directly */
        lv_draw_buf_destroy_user(font_draw_buf_handlers, (lv_draw_buf_t *)g_dsc->entry);
    }
    else {
        if(g_dsc->entry == NULL) {
            return;
        }
        lv_cache_release(dsc->draw_data_cache, g_dsc->entry, NULL);
    }
    g_dsc->entry = NULL;
}

static void lv_tiny_ttf_cache_create(ttf_font_desc_t * dsc)
{
    /*Init cache*/
    LV_LOG_ERROR("cache_size: %" PRIu32, (uint32_t)dsc->cache_size);
    dsc->glyph_cache = lv_cache_create(&lv_cache_class_lru_rb_count, sizeof(tiny_ttf_glyph_cache_data_t), dsc->cache_size,
    (lv_cache_ops_t) {
        .compare_cb = (lv_cache_compare_cb_t)tiny_ttf_glyph_cache_compare_cb,
        .create_cb = (lv_cache_create_cb_t)tiny_ttf_glyph_cache_create_cb,
        .free_cb = (lv_cache_free_cb_t)tiny_ttf_glyph_cache_free_cb
    });
    lv_cache_set_name(dsc->glyph_cache, "TINY_TTF_GLYPH");

    dsc->draw_data_cache = lv_cache_create(&lv_cache_class_lru_rb_count, sizeof(tiny_ttf_cache_data_t), dsc->cache_size,
    (lv_cache_ops_t) {
        .compare_cb = (lv_cache_compare_cb_t)tiny_ttf_draw_data_cache_compare_cb,
        .create_cb = (lv_cache_create_cb_t)tiny_ttf_draw_data_cache_create_cb,
        .free_cb = (lv_cache_free_cb_t)tiny_ttf_draw_data_cache_free_cb,
    });
    lv_cache_set_name(dsc->draw_data_cache, "TINY_TTF_DRAW_DATA");
}

static lv_font_t * lv_tiny_ttf_create(const char * path, const void * data, size_t data_size, int32_t font_size,
                                      lv_font_kerning_t kerning, size_t cache_size)
{
    LV_LOG_ERROR("cache_size: %u font_size: %ld", cache_size, font_size);
    if((path == NULL && data == NULL) || 0 >= font_size) {
        LV_LOG_ERROR("tiny_ttf: invalid argument");
        return NULL;
    }
    ttf_font_desc_t * dsc = lv_malloc_zeroed(sizeof(ttf_font_desc_t));
    if(dsc == NULL) {
        LV_LOG_ERROR("tiny_ttf: out of memory");
        return NULL;
    }
    dsc->magic = TINY_TTF_DESC_MAGIC;
#if LV_TINY_TTF_FILE_SUPPORT != 0
    lv_mutex_init(&dsc->stream.mutex);
    if(path != NULL) {
        if(LV_FS_RES_OK != lv_fs_open(&dsc->file, path, LV_FS_MODE_RD)) {
            lv_mutex_delete(&dsc->stream.mutex);
            lv_free(dsc);
            LV_LOG_ERROR("tiny_ttf: unable to open %s\n", path);
            return NULL;
        }
        dsc->stream.file = &dsc->file;
    }
    else {
        dsc->stream.data = (const uint8_t *)data;
        dsc->stream.size = data_size;
    }
    if(0 == stbtt_InitFont(&dsc->info, &dsc->stream, stbtt_GetFontOffsetForIndex(&dsc->stream, 0))) {
        tiny_ttf_close_file_if_open(dsc);
        lv_mutex_delete(&dsc->stream.mutex);
        lv_free(dsc);
        LV_LOG_ERROR("tiny_ttf: init failed");
        return NULL;
    }

#else
    dsc->stream = (const uint8_t *)data;
    dsc->stream_size = data_size;
    if(0 == stbtt_InitFont(&dsc->info, dsc->stream, stbtt_GetFontOffsetForIndex(dsc->stream, 0))) {
        lv_free(dsc);
        LV_LOG_ERROR("tiny_ttf: init failed");
        return NULL;
    }
#endif

    dsc->cache_size = cache_size;

    lv_font_t * out_font = lv_malloc_zeroed(sizeof(lv_font_t));
    if(out_font == NULL) {
        tiny_ttf_close_file_if_open(dsc);
        lv_free(dsc);
        LV_LOG_ERROR("tiny_ttf: out of memory");
        return NULL;
    }

    /* check if font  has kerning tables to use, else disable kerning automatically. */
    if(stbtt_KernTableCheck(&dsc->info) == 0) {
        kerning = LV_FONT_KERNING_NONE; /* disable kerning if font has no tables. */
    }

    dsc->kerning = kerning;
    out_font->kerning = kerning;

    out_font->get_glyph_dsc = ttf_get_glyph_dsc_cb;
    out_font->get_glyph_bitmap = ttf_get_glyph_bitmap_cb;
    out_font->release_glyph = ttf_release_glyph_cb;
    out_font->dsc = dsc;
    lv_tiny_ttf_set_size(out_font, font_size);
    return out_font;
}
#if LV_TINY_TTF_FILE_SUPPORT != 0
lv_font_t * lv_tiny_ttf_create_file_ex(const char * path, int32_t font_size, lv_font_kerning_t kerning,
                                       size_t cache_size)
{
    return lv_tiny_ttf_create(path, NULL, 0, font_size, kerning, cache_size);
}
lv_font_t * lv_tiny_ttf_create_file(const char * path, int32_t font_size)
{
    return lv_tiny_ttf_create(path, NULL, 0, font_size, LV_FONT_KERNING_NORMAL, LV_TINY_TTF_CACHE_GLYPH_CNT);
}
#endif

lv_font_t * lv_tiny_ttf_create_data_ex(const void * data, size_t data_size, int32_t font_size,
                                       lv_font_kerning_t kerning, size_t cache_size)
{
    return lv_tiny_ttf_create(NULL, data, data_size, font_size, kerning, cache_size);
}
lv_font_t * lv_tiny_ttf_create_data(const void * data, size_t data_size, int32_t font_size)
{
    return lv_tiny_ttf_create(NULL, data, data_size, font_size, LV_FONT_KERNING_NORMAL, LV_TINY_TTF_CACHE_GLYPH_CNT);
}

/*-----------------
 * Cache Callbacks
 *----------------*/

static bool tiny_ttf_fill_glyph_dsc_by_gid(ttf_font_desc_t * dsc, lv_font_glyph_dsc_t * dsc_out, uint32_t glyph_id,
                                           int32_t advance_override, int * adv_w_out)
{
    if(dsc == NULL || dsc_out == NULL || glyph_id == 0 || glyph_id > (uint32_t)INT_MAX ||
       glyph_id >= (uint32_t)dsc->info.numGlyphs) {
        return false;
    }

    int g1 = (int)glyph_id;
    int x1, y1, x2, y2;
    stbtt_GetGlyphBitmapBox(&dsc->info, g1, dsc->scale, dsc->scale, &x1, &y1, &x2, &y2);

    int advw;
    stbtt_GetGlyphHMetrics(&dsc->info, g1, &advw, NULL);

    if(advance_override >= 0) {
        dsc_out->adv_w = advance_override > UINT16_MAX ? UINT16_MAX : (uint16_t)advance_override;
    }
    else if(dsc->kerning != LV_FONT_KERNING_NORMAL) { /* calculate default advance */
        int k = stbtt_GetGlyphKernAdvance(&dsc->info, g1, 0);
        dsc_out->adv_w = (uint16_t)floor((((float)advw + (float)k) * dsc->scale) +
                                         0.5f); /*Horizontal space required by the glyph in [px]*/
    }
    else {
        dsc_out->adv_w = (uint16_t)floor(((float)advw * dsc->scale) +
                                         0.5f); /*Horizontal space required by the glyph in [px]*/
    }

    if(adv_w_out != NULL) {
        *adv_w_out = advw;
    }

    dsc_out->box_w = (x2 - x1 + 1);         /*width of the bitmap in [px]*/
    dsc_out->box_h = (y2 - y1 + 1);         /*height of the bitmap in [px]*/
    dsc_out->ofs_x = x1;                    /*X offset of the bitmap in [pf]*/
    dsc_out->ofs_y = -y2;                   /*Y offset of the bitmap measured from the as line*/
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->is_placeholder = false;
    dsc_out->gid.index = glyph_id;

    return true;
}

static bool tiny_ttf_glyph_cache_create_cb(tiny_ttf_glyph_cache_data_t * node, void * user_data)
{
    ttf_font_desc_t * dsc = (ttf_font_desc_t *)user_data;
    lv_font_glyph_dsc_t * dsc_out = &node->glyph_dsc;

    uint32_t unicode_letter = node->unicode;

    int g1 = stbtt_FindGlyphIndex(&dsc->info, (int)unicode_letter);
    return tiny_ttf_fill_glyph_dsc_by_gid(dsc, dsc_out, (uint32_t)g1, -1, &node->adv_w);
}

static void tiny_ttf_glyph_cache_free_cb(tiny_ttf_glyph_cache_data_t * node, void * user_data)
{
    LV_UNUSED(node);
    LV_UNUSED(user_data);
}

static lv_cache_compare_res_t tiny_ttf_glyph_cache_compare_cb(const tiny_ttf_glyph_cache_data_t * lhs,
                                                              const tiny_ttf_glyph_cache_data_t * rhs)
{
    if(lhs->unicode != rhs->unicode) {
        return lhs->unicode > rhs->unicode ? 1 : -1;
    }

    return 0;
}

static bool tiny_ttf_draw_data_cache_create_cb(tiny_ttf_cache_data_t * node, void * user_data)
{
    int g1 = (int)node->glyph_index;
    if(g1 == 0) {
        /* Glyph not found */
        return false;
    }

    ttf_font_desc_t * dsc = (ttf_font_desc_t *)user_data;

    const stbtt_fontinfo * info = (const stbtt_fontinfo *)&dsc->info;
    int x1, y1, x2, y2;
    stbtt_GetGlyphBitmapBox(info, g1, dsc->scale, dsc->scale, &x1, &y1, &x2, &y2);
    int w, h;
    w = x2 - x1 + 1;
    h = y2 - y1 + 1;

    lv_draw_buf_t * draw_buf = lv_draw_buf_create_ex(font_draw_buf_handlers, w, h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    if(NULL == draw_buf) {
        LV_LOG_ERROR("tiny_ttf: out of memory");
        return false;
    }

    lv_draw_buf_clear(draw_buf, NULL);

    uint32_t stride = draw_buf->header.stride;
    stbtt_MakeGlyphBitmap(info, draw_buf->data, w, h, stride, dsc->scale, dsc->scale, g1);

    node->draw_buf = draw_buf;
    return true;
}

static void tiny_ttf_draw_data_cache_free_cb(tiny_ttf_cache_data_t * node, void * user_data)
{
    LV_UNUSED(user_data);

    lv_draw_buf_destroy(node->draw_buf);
}

static lv_cache_compare_res_t tiny_ttf_draw_data_cache_compare_cb(const tiny_ttf_cache_data_t * lhs,
                                                                  const tiny_ttf_cache_data_t * rhs)
{
    if(lhs->glyph_index != rhs->glyph_index) {
        return lhs->glyph_index > rhs->glyph_index ? 1 : -1;
    }

    if(lhs->size != rhs->size) {
        return lhs->size > rhs->size ? 1 : -1;
    }

    return 0;
}

#endif
