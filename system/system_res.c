/**
 * @file system_res.c
 * @brief 系统资源模块实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system_res.h"

#include "system/system.h"
#include "system/system_config_json.h"
#include "app_def.h"
#include "floatair_fs.h"
#include "lvgl/src/draw/lv_image_decoder.h"
#include "lvgl.h"

#if defined(CONFIG_RPMSG_TTF_CLIENT)
#include "rpmsgttf_client.h"
#else
#include "lvgl/src/libs/tiny_ttf/lv_tiny_ttf.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/**
 * @brief 系统字体注册项，记录注册字号和对应字体对象。
 */
typedef struct {
    uint32_t size;
    lv_font_t* font;
} system_font_entry_t;

static app_font_info_t s_system_font_info = {0};
static const lv_font_t* s_system_font = NULL;
static i18n_handle_t* s_system_i18n = NULL;
static const char s_empty_text[] = "";

/**
 * @brief 系统字体的 Tiny TTF 缓存条目上限。
 *
 * 全局注册表主要承担常规界面文本；
 * 这里保守控制每个字号的缓存规模，降低多字号并存时的总体占用。
 */
#define SYSTEM_FONT_TINY_TTF_CACHE_CNT 1024

static system_font_entry_t s_font_registry[] = {
    {10, NULL},
    {12, NULL},
    {14, NULL},
    {15, NULL},
    {18, NULL},
    {20, NULL},
    {23, NULL},
    {24, NULL},
    {26, NULL},
    {30, NULL},
    {35, NULL},
    {36, NULL},
    {38, NULL},
    {50, NULL},
    {55, NULL},
    {60, NULL},
};

/**
 * @brief 返回系统字体注册表中的条目数量。
 */
static size_t system_font_registry_count(void) {
    return sizeof(s_font_registry) / sizeof(s_font_registry[0]);
}

/**
 * @brief 在系统字体注册表中按精确字号查找条目。
 */
static system_font_entry_t* system_font_find_entry(uint32_t size) {
    size_t i = 0;

    for (i = 0; i < system_font_registry_count(); ++i) {
        if (s_font_registry[i].size == size) {
            return &s_font_registry[i];
        }
    }

    return NULL;
}

/**
 * @brief 在系统字体注册表中查找与目标字号最近的条目。
 */
static system_font_entry_t* system_font_find_nearest_entry(uint32_t size) {
    size_t i = 0;
    system_font_entry_t* best = &s_font_registry[0];
    uint32_t best_diff = (best->size > size) ? (best->size - size) : (size - best->size);

    for (i = 1; i < system_font_registry_count(); ++i) {
        system_font_entry_t* cur = &s_font_registry[i];
        uint32_t diff = (cur->size > size) ? (cur->size - size) : (size - cur->size);

        if (diff < best_diff || (diff == best_diff && cur->size > best->size)) {
            best = cur;
            best_diff = diff;
        }
    }

    return best;
}

/**
 * @brief 销毁系统字体注册表中当前已创建的全部字体对象。
 */
static void system_font_destroy_registry(void) {
    size_t i = 0;

    for (i = 0; i < system_font_registry_count(); ++i) {
        app_font_destroy(s_font_registry[i].font);
        s_font_registry[i].font = NULL;
    }
}

/**
 * @brief 检查字号是否位于应用允许的配置范围内。
 */
bool app_fontsize_valid(int32_t font_size) {
    return font_size >= APP_FONT_SIZE_MIN && font_size <= APP_FONT_SIZE_MAX;
}

/**
 * @brief 检查行间距是否位于应用允许的配置范围内。
 */
bool app_font_rowspace_valid(int32_t row_space) {
    return row_space >= APP_FONT_ROWSPACE_MIN && row_space <= APP_FONT_ROWSPACE_MAX;
}

/**
 * @brief 检查字间距是否位于应用允许的配置范围内。
 */
bool app_font_wordspace_valid(int32_t word_space) {
    return word_space >= APP_FONT_WORDSPACE_MIN && word_space <= APP_FONT_WORDSPACE_MAX;
}

/**
 * @brief 创建一个应用层 Tiny TTF 字体对象。
 */
lv_font_t* app_font_create(int32_t font_size, size_t cache_cnt) {
    lv_font_t* font = NULL;

#if defined(CONFIG_RPMSG_TTF_CLIENT)
    font = rpmsgttf_create_font(floatair_fs_get_system_font_file(),
                                font_size,
                                LV_FONT_KERNING_NORMAL,
                                cache_cnt);
#else
    font = lv_tiny_ttf_create_file_ex(floatair_fs_get_system_font_file(),
                                      font_size,
                                      LV_FONT_KERNING_NORMAL,
                                      cache_cnt);
#endif
    if (font == NULL) {
        floatair_err("font create failed size[%" PRId32 "]", font_size);
        return NULL;
    }

    floatair_info("font (%p) create size[%" PRId32 "] cache=%" PRIu32,
                  font,
                  font_size,
                  (uint32_t)cache_cnt);
    return font;
}

/**
 * @brief 销毁一个应用层字体对象。
 */
void app_font_destroy(lv_font_t* font) {
    if (font != NULL) {
#if defined(CONFIG_RPMSG_TTF_CLIENT)
        rpmsgttf_destroy_font(font);
#else
        lv_tiny_ttf_destroy(font);
#endif
    }
    floatair_info("font (%p) destroy", font);
}

/**
 * @brief 根据图片文件名拼接系统图片完整路径并检查目标文件是否存在。
 */
bool app_images_path(const char *img, char *path, size_t size) {
    char temppath[SYSTEM_MAX_PATH_LEN] = {0};
    size_t img_len = 0;
    size_t base_len = 0;
    size_t full_len = 0;

    if (img == NULL || path == NULL || size == 0) {
        floatair_err("img or path is NULL");
        return false;
    }

    path[0] = '\0';

    img_len = strlen(img);
    base_len = strlen(floatair_fs_get_system_images_path());
    full_len = img_len + base_len + 1;
    if (full_len > size || full_len > sizeof(temppath)) {
        floatair_err("img name is too long[%s]", img);
        return false;
    }

    memset(temppath, 0, sizeof(temppath));
    snprintf(temppath, sizeof(temppath), "%s%s", floatair_fs_get_system_images_path(), img);
    if (!floatair_fs_is_exist(temppath)) {
        floatair_err("img path not found: %s", temppath);
        return false;
    }

    snprintf(path, size, "%s", temppath);
    floatair_info("img path: %s", path);
    return true;
}

/**
 * @brief 检查图片路径是否存在且可用。
 */
bool app_image_path_valid(const char *path) {
    if (path == NULL) {
        floatair_err("path is NULL");
        return false;
    }
    if (strlen(path) <= 0) {
        floatair_err("path is too short");
        return false;
    }
    return floatair_fs_is_exist(path);
}

static bool app_image_jpeg_is_progressive(const char* posix_path) {
    char lvpath[SYSTEM_MAX_PATH_LEN] = {0};
    void* h = NULL;
    unsigned char buf[4096];
    uint32_t n = 0;

    if (posix_path == NULL) {
        return false;
    }

    lv_snprintf(lvpath, sizeof(lvpath), "%s", posix_path);
    h = floatair_fs_open(lvpath, FLOATAIR_FS_MODE_RD);
    if (h == NULL) {
        return false;
    }

    if (floatair_fs_read(h, buf, sizeof(buf), &n) != FLOATAIR_FS_OK) {
        floatair_fs_close(h);
        return false;
    }
    floatair_fs_close(h);

    if (n <= 1) {
        return false;
    }

    for (uint32_t i = 0; i < n - 1; ++i) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xC2) {
            return true;
        }
    }

    return false;
}

bool app_image_decode_supported(const char* path) {
    lv_image_header_t header;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if (app_image_jpeg_is_progressive(path)) {
        floatair_err("progressive jpeg not supported: %s", path);
        return false;
    }

    if (lv_image_decoder_get_info(path, &header) == LV_RESULT_OK) {
        floatair_info("img info ok: %s %dx%d cf=%d",
                      path,
                      (int)header.w,
                      (int)header.h,
                      (int)header.cf);
        return true;
    }

    floatair_err("img info failed: %s", path);
    return false;
}

/**
 * @brief 查询当前系统语言下的国际化字符串。
 */
const char *app_get_str(const char *key) {
    const char* curlang = NULL;

    if (key == NULL || key[0] == '\0') {
        floatair_err("i18n key is empty");
        return s_empty_text;
    }

    curlang = system_config_get_curlang();
    if (curlang == NULL || curlang[0] == '\0') {
        return s_empty_text;
    }

    if (s_system_i18n == NULL) {
        s_system_i18n = i18n_open(floatair_fs_get_system_i18n_path(), curlang);
        if (s_system_i18n == NULL) {
            floatair_err("i18n_open failed");
            return s_empty_text;
        }
    }

    {
        const char *res = i18n_query(s_system_i18n, key);
        if (res == NULL) {
            floatair_err("i18n query failed for key: %s", key);
            return s_empty_text;
        }
        floatair_info("i18n key: %s, value: %s", key, res);
        return res;
    }
}

/**
 * @brief 按当前系统语言重新加载国际化资源。
 */
bool system_i18n_reload(void) {
    const char* curlang = system_config_get_curlang();

    if (s_system_i18n != NULL) {
        i18n_close(s_system_i18n);
        s_system_i18n = NULL;
    }

    if (curlang == NULL || curlang[0] == '\0') {
        return false;
    }

    s_system_i18n = i18n_open(floatair_fs_get_system_i18n_path(), curlang);
    if (s_system_i18n == NULL) {
        floatair_err("i18n_reload failed");
        return false;
    }

    return true;
}

/**
 * @brief 检查指定字号是否已注册到系统字体注册表中。
 */
bool system_font_size_supported(uint32_t size) {
    return system_font_find_entry(size) != NULL;
}

/**
 * @brief 初始化系统字体注册表并加载系统默认字体。
 */
bool system_font_init(void) {
    size_t i = 0;
    uint32_t resolved_size = 0;
    bool font_cfg_ok = false;
    system_font_entry_t* resolved_entry = NULL;

    font_cfg_ok = system_config_get_font(system_config_path(), &s_system_font_info);
    if (!font_cfg_ok) {
        floatair_err("font config missing");
        s_system_font = NULL;
        system_font_destroy_registry();
        return false;
    }

    s_system_font = NULL;

    for (i = 0; i < system_font_registry_count(); ++i) {
        if (s_font_registry[i].font == NULL) {
            s_font_registry[i].font = app_font_create((int32_t)s_font_registry[i].size,
                                                      SYSTEM_FONT_TINY_TTF_CACHE_CNT);
            if (s_font_registry[i].font == NULL) {
                floatair_err("font create failed at size %" PRIu32, s_font_registry[i].size);
                system_font_destroy_registry();
                return false;
            }
        }
    }

    resolved_size = s_system_font_info.weight;
    if (!system_font_size_supported(resolved_size)) {
        resolved_size = get_font_size_near(resolved_size);
        floatair_info(
            "system font size resolved from %" PRIu32 " to %" PRIu32,
            s_system_font_info.weight,
            resolved_size);
    }

    resolved_entry = system_font_find_entry(resolved_size);
    if (resolved_entry == NULL || resolved_entry->font == NULL) {
        floatair_err("system font load failed at size %" PRIu32, resolved_size);
        return false;
    }

    s_system_font = resolved_entry->font;
    s_system_font_info.weight = resolved_size;
    return true;
}

/**
 * @brief 释放系统字体注册表中持有的全部字体对象。
 */
bool system_font_deinit(void) {
    s_system_font = NULL;
    system_font_destroy_registry();
    return true;
}

/**
 * @brief 按精确字号获取系统注册字体。
 */
const lv_font_t* get_font_by_size(uint32_t size) {
    system_font_entry_t* entry = system_font_find_entry(size);

    if (entry == NULL) {
        return NULL;
    }

    return entry->font;
}

/**
 * @brief 按最近字号获取系统注册字体。
 */
const lv_font_t* get_font_by_size_near(uint32_t size) {
    system_font_entry_t* entry = system_font_find_nearest_entry(size);
    return entry->font;
}

/**
 * @brief 按最近字号获取系统最终采用的注册字号。
 */
uint32_t get_font_size_near(uint32_t size) {
    return system_font_find_nearest_entry(size)->size;
}

/**
 * @brief 获取当前系统默认字体对象。
 */
const lv_font_t* get_system_font(void) {
    floatair_assert(s_system_font != NULL, "font is NULL");
    return s_system_font;
}

/**
 * @brief 获取当前系统默认字体的实际字号。
 */
uint32_t get_system_font_size(void) {
    floatair_assert(s_system_font != NULL, "font is NULL");
    return s_system_font_info.weight;
}

/**
 * @brief 获取指定字体的单行高度。
 */
uint32_t get_font_height(const lv_font_t* font) {
    if (font == NULL) {
        floatair_err("font is NULL");
        return 0;
    }

    return (uint32_t)lv_font_get_line_height(font);
}

/**
 * @brief 获取指定字体在给定字间距下的单行文本宽度。
 */
int32_t get_font_text_width(const lv_font_t* font, const char* text, int32_t letter_space) {
    if (font == NULL || text == NULL) {
        floatair_err("font or text is NULL");
        return 0;
    }

    return lv_text_get_width(text, (uint32_t)strlen(text), font, letter_space);
}

/**
 * @brief 获取指定字体在给定排版参数下的文本尺寸。
 */
bool get_font_text_size(
    const lv_font_t* font,
    lv_point_t* out,
    const char* text,
    int32_t letter_space,
    int32_t line_space,
    int32_t max_width) {
    if (font == NULL || out == NULL || text == NULL) {
        floatair_err("font, out or text is NULL");
        return false;
    }

    lv_text_get_size(out, text, font, letter_space, line_space, max_width, LV_TEXT_FLAG_NONE);
    return true;
}

/**
 * @brief 为 LVGL 文本对象设置字体。
 */
void obj_set_text_font(lv_obj_t* obj, const lv_font_t* font) {
    if (obj == NULL || font == NULL) {
        floatair_err("obj or font is NULL");
        return;
    }

    lv_obj_set_style_text_font(obj, font, 0);
}

/**
 * @brief 为 LVGL 文本对象同时设置字体、字间距和行间距。
 */
void obj_set_text_style(lv_obj_t* obj, const lv_font_t* font, int32_t letter_space, int32_t line_space) {
    if (obj == NULL || font == NULL) {
        floatair_err("obj or font is NULL");
        return;
    }

    obj_set_text_font(obj, font);
    lv_obj_set_style_text_letter_space(obj, letter_space, 0);
    lv_obj_set_style_text_line_space(obj, line_space, 0);
}

/**
 * @brief 获取当前系统默认字体的单行高度。
 */
uint32_t get_system_font_height(void) {
    floatair_assert(s_system_font != NULL, "font is NULL");
    return get_font_height(s_system_font);
}

/**
 * @brief 获取当前系统默认字体的行间距配置。
 */
uint32_t get_system_font_row_space(void) {
    floatair_assert(s_system_font != NULL, "font is NULL");
    return s_system_font_info.rowSpace;
}

/**
 * @brief 获取当前系统默认字体的字间距配置。
 */
uint32_t get_system_font_word_space(void) {
    floatair_assert(s_system_font != NULL, "font is NULL");
    return s_system_font_info.wordSpace;
}
