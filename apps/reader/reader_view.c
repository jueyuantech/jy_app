/**
 * @file reader_view.c
 * @brief Reader 页面视图、分页排版和阅读输入交互实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_reader
 */
#include "reader.h"
#include "app_def.h"
#include "common/app_framework/app_manager.h"
#include "floatair_dbg.h"
#include "floatair_fs.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "common/widgets/status_bar.h"
#include "common/widgets/roller.h"

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    READER_VIEW_MODE_INIT = 0,
    READER_VIEW_MODE_TEXT,
} reader_view_mode_t;

static reader_view_mode_t s_view_mode = READER_VIEW_MODE_INIT;

static bool reader_config_is_valid_root(cJSON* root) {
    if (!root || !cJSON_IsObject(root)) {
        return false;
    }
    cJSON* fontinfo = cJSON_GetObjectItemCaseSensitive(root, "fontinfo");
    if (!cJSON_IsObject(fontinfo)) {
        return false;
    }
    cJSON* weight = cJSON_GetObjectItemCaseSensitive(fontinfo, "weight");
    cJSON* word_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "wordSpace");
    cJSON* row_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "rowSpace");
    return cJSON_IsNumber(weight) && cJSON_IsNumber(word_space) && cJSON_IsNumber(row_space);
}

static cJSON* reader_config_create_default_root(void) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON* fontinfo = cJSON_AddObjectToObject(root, "fontinfo");
    if (!fontinfo) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(26));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(0));
    return root;
}

bool reader_config_reset_to_default(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_READER, config_path, sizeof(config_path))) {
        return false;
    }
    cJSON* root = reader_config_create_default_root();
    if (!root) {
        return false;
    }
    int ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

bool reader_config_ensure(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_READER, config_path, sizeof(config_path))) {
        return false;
    }
    cJSON* root = load_json(config_path);
    if (root) {
        bool ok = reader_config_is_valid_root(root);
        cJSON_Delete(root);
        if (ok) {
            return true;
        }
    }
    return reader_config_reset_to_default();
}

static lv_obj_t* s_root = NULL;

static roller_t* s_roller = NULL;
static lv_obj_t* s_init_label = NULL;
static char** s_docs = NULL;
static int s_doc_cnt = 0;
static int s_sel = 0;

static lv_obj_t* s_text_label = NULL;
static lv_obj_t* s_text_page_label = NULL;

static char* s_file_buf = NULL;
static uint32_t s_file_len = 0;
static uint32_t s_total_pages = 0;
static uint32_t s_cur_page = 0;
app_font_info_t reader_font_info = {0};
const lv_font_t* reader_font = NULL;
/* 记录运行时最终命中的系统注册字号，避免把字体生命周期留在 app 内。 */
static uint32_t s_reader_font_size_resolved = 0;
static bool reader_bind_fonts_on_load(void);
static void reader_view_set_mode(reader_view_mode_t mode);
static void reader_view_apply_visibility(void);

static bool reader_status_bar_widgets_valid(void) {
    return s_text_page_label != NULL && lv_obj_is_valid(s_text_page_label);
}

static bool reader_status_bar_ensure_widgets(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        s_text_page_label = NULL;
        return false;
    }

    if (!reader_status_bar_widgets_valid()) {
        status_bar_clear_custom_widgets(status_bar);
        s_text_page_label = status_bar_add_text(status_bar, "", STATUS_BAR_WIDGET_ALIGN_CENTER, 80);
    }

    return reader_status_bar_widgets_valid();
}

static void reader_docs_free(void) {
    if (!s_docs) {
        s_doc_cnt = 0;
        s_sel = 0;
        return;
    }

    for (int i = 0; i < s_doc_cnt; ++i) {
        if (s_docs[i]) {
            free(s_docs[i]);
        }
    }
    free(s_docs);
    s_docs = NULL;
    s_doc_cnt = 0;
    s_sel = 0;
}

/**
 * @brief 规范化 Reader 配置字号，非法值回退到系统默认字号。
 * @param[in] configured_size 配置中的字号。
 * @return 返回可继续参与解析的配置字号。
 */
static uint32_t reader_normalize_configured_font_size(uint32_t configured_size) {
    if (!app_fontsize_valid((int32_t)configured_size)) {
        uint32_t fallback_size = get_system_font_size();

        floatair_warn("reader invalid font size %lu, fallback to system size %lu",
                      (unsigned long)configured_size,
                      (unsigned long)fallback_size);
        return fallback_size;
    }

    return configured_size;
}

/**
 * @brief 将 Reader 配置字号解析为 system_res 中可用的运行时字号。
 * @param[in] configured_size 配置中的字号。
 * @return 返回运行时最终使用的系统注册字号。
 */
static uint32_t reader_resolve_font_size(uint32_t configured_size) {
    uint32_t runtime_size = reader_normalize_configured_font_size(configured_size);
    uint32_t resolved_size = 0;

    resolved_size = get_font_size_near(runtime_size);
    if (resolved_size != runtime_size) {
        floatair_info("reader font size resolved from %lu to %lu",
                      (unsigned long)runtime_size,
                      (unsigned long)resolved_size);
    }

    return resolved_size;
}

/**
 * @brief 按当前配置刷新 Reader 运行时字体对象引用。
 * @param[in] configured_size 配置中的目标字号。
 * @return `true` 表示成功拿到系统字体注册表中的字体。
 */
static bool reader_refresh_runtime_font(uint32_t configured_size) {
    uint32_t normalized_size = reader_normalize_configured_font_size(configured_size);
    uint32_t resolved_size = reader_resolve_font_size(normalized_size);
    const lv_font_t* font = get_font_by_size(resolved_size);

    if (font == NULL) {
        font = get_font_by_size_near(normalized_size);
        resolved_size = get_font_size_near(normalized_size);
    }
    if (font == NULL) {
        floatair_err("reader runtime font is NULL");
        return false;
    }

    reader_font = font;
    s_reader_font_size_resolved = resolved_size;
    return true;
}

/**
 * @brief 获取 Reader 当前运行时字体。
 * @return 返回已经在页面初始化阶段绑定好的系统字体；异常时回退系统默认字体。
 */
static const lv_font_t* reader_get_runtime_font(void) {
    if (reader_font == NULL) {
        floatair_err("reader runtime font is not initialized");
        return get_system_font();
    }

    return reader_font;
}

/**
 * @brief 在 Reader 页面加载阶段一次性绑定所需字体。
 * @return `true` 表示页面所需字体均已完成绑定。
 */
static bool reader_bind_fonts_on_load(void) {
    app_font_info_t fallback_info = {0};

    if (reader_font_init_from_config()) {
        return true;
    }

    fallback_info.weight = get_system_font_size();
    fallback_info.wordSpace = get_system_font_word_space();
    fallback_info.rowSpace = get_system_font_row_space();
    reader_font_info = fallback_info;
    return reader_refresh_runtime_font(fallback_info.weight);
}

/**
 * @brief 应用 Reader 文本页样式。
 * @param[in] obj 目标文本对象。
 */
static void reader_apply_text_style(lv_obj_t* obj) {
    if (obj == NULL) {
        return;
    }

    obj_set_text_style(
        obj,
        reader_get_runtime_font(),
        (int32_t)reader_font_info.wordSpace,
        (int32_t)reader_font_info.rowSpace);
}

static void update_page_label(void) {
    if (!s_text_label || !s_text_page_label) {
        return;
    }
    lv_obj_update_layout(s_text_label);
    int32_t vh = lv_obj_get_height(s_text_label);
    int32_t top = lv_obj_get_scroll_top(s_text_label);
    int32_t bottom = lv_obj_get_scroll_bottom(s_text_label);
    int32_t scrollable = top + bottom;
    if (vh <= 0) {
        vh = 1;
    }
    uint32_t total = 1 + (uint32_t)((scrollable + vh - 1) / vh);
    uint32_t cur = 1 + (uint32_t)((top + vh - 1) / vh);
    if (cur > total) {
        cur = total;
    }
    s_total_pages = total;
    s_cur_page = cur ? (cur - 1) : 0;
    char buf[32] = {0};
    snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)cur, (unsigned long)total);
    lv_label_set_text(s_text_page_label, buf);
}

static void free_file(void) {
    if (s_file_buf) {
        free(s_file_buf);
        s_file_buf = NULL;
    }
    s_file_len = 0;
}

static bool reader_has_txt_suffix(const char* path) {
    size_t len = 0;
    const char* suffix = NULL;

    if (path == NULL) {
        return false;
    }

    len = strlen(path);
    if (len < 4) {
        return false;
    }

    suffix = path + len - 4;
    return suffix[0] == '.' &&
           (suffix[1] == 't' || suffix[1] == 'T') &&
           (suffix[2] == 'x' || suffix[2] == 'X') &&
           (suffix[3] == 't' || suffix[3] == 'T');
}

static void read_docs(void) {
    reader_docs_free();

    char docdir[SYSTEM_MAX_PATH_LEN] = {0};
    lv_snprintf(docdir, sizeof(docdir), "/jyt_d/apps/%s", APP_NAME_READER);

    floatair_dir_t* dir = NULL;
    if (floatair_fs_dir_open(docdir, &dir) != FLOATAIR_FS_OK) {
        return;
    }

    char namebuf[SYSTEM_MAX_PATH_LEN] = {0};
    bool is_dir = false;
    for (;;) {
        int r = floatair_fs_dir_read(dir, namebuf, sizeof(namebuf), &is_dir);
        if (r != FLOATAIR_FS_OK) {
            break;
        }
        if (is_dir || namebuf[0] == '/' || namebuf[0] == '\0' || !reader_has_txt_suffix(namebuf)) {
            continue;
        }
        s_doc_cnt++;
    }
    floatair_fs_dir_close(dir);

    if (s_doc_cnt <= 0) {
        s_doc_cnt = 0;
        return;
    }

    s_docs = (char**)malloc(sizeof(char*) * (size_t)s_doc_cnt);
    if (!s_docs) {
        s_doc_cnt = 0;
        return;
    }
    memset(s_docs, 0, sizeof(char*) * (size_t)s_doc_cnt);

    if (floatair_fs_dir_open(docdir, &dir) != FLOATAIR_FS_OK) {
        reader_docs_free();
        return;
    }
    int i = 0;
    for (;;) {
        int r = floatair_fs_dir_read(dir, namebuf, sizeof(namebuf), &is_dir);
        if (r != FLOATAIR_FS_OK) {
            break;
        }
        if (is_dir || namebuf[0] == '/' || namebuf[0] == '\0' || !reader_has_txt_suffix(namebuf)) {
            continue;
        }
        size_t n = strlen(namebuf);
        s_docs[i] = (char*)malloc(n + 1);
        if (s_docs[i]) {
            memcpy(s_docs[i], namebuf, n + 1);
        }
        i++;
        if (i >= s_doc_cnt) {
            break;
        }
    }
    floatair_fs_dir_close(dir);
    if (s_sel < 0 || s_sel >= s_doc_cnt) {
        s_sel = 0;
    }
}

bool reader_font_init_from_config(void) {
    if (!reader_config_ensure()) {
        return false;
    }
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_READER, config_path, sizeof(config_path))) {
        return false;
    }
    app_font_info_t fi = {0};
    if (!system_config_get_font(config_path, &fi)) {
        return false;
    }
    fi.weight = reader_normalize_configured_font_size(fi.weight);
    reader_font_info = fi;
    return reader_refresh_runtime_font(fi.weight);
}

bool reader_font_update_and_save(app_font_info_t* font_info) {
    if (!font_info) {
        return false;
    }
    if (!reader_config_ensure()) {
        return false;
    }
    if (reader_font_info.weight == font_info->weight && reader_font_info.wordSpace == font_info->wordSpace &&
        reader_font_info.rowSpace == font_info->rowSpace && reader_font != NULL) {
        return true;
    }
    reader_text_set_font(font_info);
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (floatair_fs_get_app_config_file(APP_NAME_READER, config_path, sizeof(config_path))) {
        app_font_info_t fi = {0};
        fi = reader_font_info;
        if (!system_config_set_font(config_path, &fi)) {
            return false;
        }
    }
    return true;
}

void reader_text_set_font(app_font_info_t* font_info) {
    if (!font_info) {
        floatair_err("font_info is NULL");
        return;
    }
    reader_font_info.weight = reader_normalize_configured_font_size(font_info->weight);
    reader_font_info.wordSpace = font_info->wordSpace;
    reader_font_info.rowSpace = font_info->rowSpace;
    if (!reader_refresh_runtime_font(reader_font_info.weight)) {
        floatair_assert(false, "reader runtime font is NULL");
        return;
    }
    if (s_text_label) {
        reader_apply_text_style(s_text_label);
        update_page_label();
    }
}

void reader_text_clear(void) {
    reader_view_reset();
}

void reader_text_show_msg(const char* msg) {
    if (s_text_label) {
        lv_label_set_text(s_text_label, msg ? msg : "");
        lv_obj_scroll_to_y(s_text_label, 0, LV_ANIM_OFF);
        update_page_label();
    }
}

bool reader_text_set_file(const char* path) {
    if (!path) {
        return false;
    }
    if (!reader_has_txt_suffix(path)) {
        floatair_warn("reader set file ignored, only .txt is supported: %s", path);
        return false;
    }
    free_file();
    floatair_stat_t st = {0};
    if (floatair_fs_stat(path, &st) != FLOATAIR_FS_OK || st.is_dir || st.size == 0) {
        return false;
    }
    void* fh = floatair_fs_open(path, FLOATAIR_FS_MODE_RD);
    if (!fh) {
        return false;
    }
    s_file_buf = (char*)malloc(st.size + 1);
    if (!s_file_buf) {
        floatair_fs_close(fh);
        return false;
    }
    uint32_t br = 0;
    if (floatair_fs_read(fh, s_file_buf, st.size, &br) != FLOATAIR_FS_OK || br != st.size) {
        floatair_fs_close(fh);
        free_file();
        return false;
    }
    floatair_fs_close(fh);
    s_file_len = st.size;
    s_file_buf[s_file_len] = '\0';
    reader_view_set_mode(READER_VIEW_MODE_TEXT);
    if (s_text_label) {
        lv_label_set_text(s_text_label, s_file_buf);
        lv_obj_scroll_to_y(s_text_label, 0, LV_ANIM_OFF);
        update_page_label();
        reader_text_page_init();
    }
    return true;
}

bool reader_text_set_page(uint32_t page_idx) {
    (void)page_idx;
    return false;
}

uint32_t reader_text_current_page(void) {
    return s_cur_page;
}

bool reader_text_page_up(void) {
    reader_view_set_mode(READER_VIEW_MODE_TEXT);
    if (!s_text_label) {
        return false;
    }
    lv_obj_update_layout(s_text_label);
    int32_t vh = lv_obj_get_height(s_text_label);
    int32_t top = lv_obj_get_scroll_top(s_text_label);
    int32_t step = (vh * 70) / 100;
    if (step <= 0) {
        step = 1;
    }
    int32_t target = top - step;
    if (target < 0) {
        target = 0;
    }
    lv_obj_scroll_to_y(s_text_label, target, LV_ANIM_OFF);
    update_page_label();
    return true;
}

bool reader_text_page_down(void) {
    reader_view_set_mode(READER_VIEW_MODE_TEXT);
    if (!s_text_label) {
        return false;
    }
    lv_obj_update_layout(s_text_label);
    int32_t vh = lv_obj_get_height(s_text_label);
    int32_t top = lv_obj_get_scroll_top(s_text_label);
    int32_t bottom = lv_obj_get_scroll_bottom(s_text_label);
    int32_t step = (vh * 70) / 100;
    if (step <= 0) {
        step = 1;
    }
    int32_t target = top + step;
    if (target > top + bottom) {
        target = top + bottom;
    }
    lv_obj_scroll_to_y(s_text_label, target, LV_ANIM_OFF);
    update_page_label();
    return true;
}

void reader_text_page_init(void) {
    if (s_text_label) {
        lv_obj_scroll_to_y(s_text_label, 0, LV_ANIM_OFF);
        update_page_label();
    }
}

static void exit_event(void) {
    (void)app_router_exit_current_app();
}

static void init_open_selected(void) {
    if (s_doc_cnt <= 0 || !s_docs) {
        return;
    }
    if (s_sel < 0 || s_sel >= s_doc_cnt) {
        s_sel = 0;
    }
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    lv_snprintf(path, sizeof(path), "/jyt_d/apps/%s/%s", APP_NAME_READER, s_docs[s_sel]);
    if (!reader_text_set_file(path)) {
        reader_text_show_msg(app_get_str("READER_LOAD_FILE_FAILED"));
        return;
    }
    reader_view_set_mode(READER_VIEW_MODE_TEXT);
}

static void roller_on_selected(roller_t* roller, uint32_t selected, void* user_data) {
    (void)roller;
    (void)user_data;
    if (s_doc_cnt <= 0) {
        s_sel = 0;
        return;
    }
    if (selected >= (uint32_t)s_doc_cnt) {
        selected = 0;
    }
    s_sel = (int)selected;
}

static void roller_on_activate(roller_t* roller, uint32_t selected, lv_event_code_t code, void* user_data) {
    (void)code;
    roller_on_selected(roller, selected, user_data);
    init_open_selected();
}

static void touch_event_handle(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    bool changed = false;

    if (s_view_mode == READER_VIEW_MODE_INIT) {
        if (code == LV_EVENT_DCLICKED) {
            exit_event();
            system_report_touch_event(code);
            return;
        }
        if (s_roller) {
            (void)roller_key_handler(s_roller, code);
        }
        system_report_touch_event(code);
        return;
    }

    if (code == LV_EVENT_GESTURE_LEFT) {
        changed = reader_text_page_up();
    } else if (code == LV_EVENT_GESTURE_RIGHT) {
        changed = reader_text_page_down();
    } else if (code == LV_EVENT_DCLICKED) {
        exit_event();
        system_report_touch_event(code);
        return;
    }
    if (changed) {
        update_page_label();
    }
    system_report_touch_event(code);
}

static void setup_root_and_fonts(lv_obj_t* root) {
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    floatair_assert(reader_bind_fonts_on_load(), "reader fonts bind failed");
}

static void init_build_ui(lv_obj_t* root) {
    if (!s_init_label) {
        s_init_label = lv_label_create(root);
        obj_set_text_font(s_init_label, get_system_font());
        lv_obj_set_style_text_align(s_init_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_init_label, lv_color_white(), 0);
        lv_obj_remove_flag(s_init_label, LV_OBJ_FLAG_SCROLLABLE);
        lv_label_set_long_mode(s_init_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_init_label, LV_PCT(90));
        lv_obj_align(s_init_label, LV_ALIGN_CENTER, 0, 0);
    }

    if (s_doc_cnt <= 0 || !s_docs) {
        lv_label_set_text(s_init_label, app_get_str("OPEN_IDLE_BOOK"));
        return;
    }

    if (!s_roller) {
        roller_cfg_t cfg = roller_default_cfg();
        cfg.items = (const char**)s_docs;
        cfg.count = (uint32_t)s_doc_cnt;
        cfg.label.font.weight = (int32_t)get_system_font_size();
        cfg.selected_font.weight = (int32_t)get_system_font_size();
        cfg.overflow_mode = ROLLER_OVERFLOW_SCROLL;

        s_roller = roller_create(root, &cfg);
        floatair_assert(s_roller != NULL, "reader roller create failed");
        if (s_roller) {
            lv_obj_t* obj = roller_get_obj(s_roller);
            if (obj) {
                lv_obj_set_width(obj, LV_PCT(80));
                lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
                lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            }
            roller_set_callbacks(s_roller, roller_on_selected, roller_on_activate, NULL);
            roller_set_selected(s_roller, (uint32_t)s_sel, false);
        }
    } else {
        roller_set_items(s_roller, (const char**)s_docs, (uint32_t)s_doc_cnt);
        roller_set_selected(s_roller, (uint32_t)s_sel, false);
    }
}

static void text_build_ui(lv_obj_t* root) {
    floatair_assert(reader_get_runtime_font() != NULL, "font is NULL");

    (void)reader_status_bar_ensure_widgets();

    if (!s_text_label) {
        s_text_label = lv_label_create(root);
        lv_obj_set_size(s_text_label, LV_PCT(100), LV_PCT(100));
        lv_obj_align(s_text_label, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_label_set_long_mode(s_text_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_text_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(s_text_label, lv_color_white(), 0);
        reader_apply_text_style(s_text_label);
        lv_obj_add_flag(s_text_label, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(s_text_label, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_text_label, LV_SCROLLBAR_MODE_ON);
        lv_obj_set_style_bg_opa(s_text_label, LV_OPA_COVER, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(s_text_label, lv_color_white(), LV_PART_SCROLLBAR);
        lv_obj_set_style_width(s_text_label, 4, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(s_text_label, 0, LV_PART_SCROLLBAR);
        lv_obj_set_style_pad_right(s_text_label, 2, LV_PART_SCROLLBAR);
        lv_obj_add_flag(s_text_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_text_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }

    if (s_file_buf && s_file_len > 0) {
        lv_label_set_text(s_text_label, s_file_buf);
    } else {
        lv_label_set_text(s_text_label, "");
    }
    lv_obj_scroll_to_y(s_text_label, 0, LV_ANIM_OFF);
    update_page_label();
    reader_text_page_init();
}

static void reader_view_apply_visibility(void) {
    bool init_mode = (s_view_mode == READER_VIEW_MODE_INIT);
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (s_init_label) {
        if (init_mode && (s_doc_cnt <= 0 || s_docs == NULL)) {
            lv_obj_remove_flag(s_init_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_init_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_roller) {
        lv_obj_t* obj = roller_get_obj(s_roller);
        if (obj) {
            if (init_mode && s_doc_cnt > 0) {
                lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (s_text_label) {
        if (!init_mode) {
            lv_obj_remove_flag(s_text_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_text_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (status_bar && lv_obj_is_valid(status_bar) && s_text_page_label && lv_obj_is_valid(s_text_page_label)) {
        status_bar_set_widget_visible(status_bar, s_text_page_label, !init_mode);
    }
}

static void reader_view_set_mode(reader_view_mode_t mode) {
    if (s_view_mode == mode) {
        return;
    }
    s_view_mode = mode;
    reader_view_apply_visibility();
    if (s_view_mode == READER_VIEW_MODE_TEXT && s_text_label && s_file_buf && s_file_len > 0) {
        lv_label_set_text(s_text_label, s_file_buf);
        update_page_label();
    }
}

static void reader_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    s_root = root;
    setup_root_and_fonts(s_root);

    read_docs();
    init_build_ui(s_root);
    text_build_ui(s_root);
    (void)reader_status_bar_ensure_widgets();
    s_view_mode = READER_VIEW_MODE_INIT;
    reader_view_apply_visibility();
}

static void reader_page_appear(lv_obj_t* root) {
    system_status_bar_set_mode(true);
    (void)reader_status_bar_ensure_widgets();
    if (reader_status_bar_widgets_valid()) {
        lv_obj_remove_flag(s_text_page_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
}

void reader_view_reset(void) {
    free_file();
    if (s_text_label) {
        lv_label_set_text(s_text_label, "");
        lv_obj_scroll_to_y(s_text_label, 0, LV_ANIM_OFF);
    }
    s_total_pages = 0;
    s_cur_page = 0;
    if (s_text_page_label) {
        update_page_label();
    }
    reader_view_set_mode(READER_VIEW_MODE_INIT);
}

static void reader_page_destroy(void) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    free_file();
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }

    s_root = NULL;

    reader_docs_free();
    s_roller = NULL;
    s_init_label = NULL;
    s_text_label = NULL;
    s_text_page_label = NULL;
    s_reader_font_size_resolved = 0;
}

static app_page_t s_reader_page = {
    .name = APP_NAME_READER,
    .on_create = reader_page_create,
    .on_appear = reader_page_appear,
    .on_disappear = NULL,
    .on_destroy = reader_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* reader_page_get(void) {
    return &s_reader_page;
}
