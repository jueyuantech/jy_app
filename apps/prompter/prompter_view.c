/**
 * @file prompter_view.c
 * @brief Prompter 应用页面视图实现，负责文档选择、分页文本展示和翻页交互。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-22
 * @copyright JYTek
 */
#include "prompter.h"

#include "app_def.h"
#include "common/app_framework/app_manager.h"
#include "common/elf_common.h"
#include "floatair_dbg.h"
#include "floatair_fs.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "common/widgets/label.h"
#include "common/widgets/msgbox.h"
#include "common/widgets/paged_text.h"
#include "common/widgets/roller.h"
#include "prompter_entry_overlay_ui.h"
#include "prompter_file_roller_ui.h"
#include "prompter_page_text_preview_ui.h"

#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PROMPTER_VIEW_MODE_INIT = 0, ///< 初始化态，展示手机端下发的文件列表菜单或空态文案。
    PROMPTER_VIEW_MODE_TEXT,     ///< 正文态，展示分页后的提词文本。
} prompter_view_mode_t;

static prompter_view_mode_t s_view_mode = PROMPTER_VIEW_MODE_INIT;
static prompter_entry_overlay_ui_t s_entry_overlay_ui;
static prompter_file_roller_ui_t s_file_roller_ui;
static prompter_page_text_preview_ui_t s_text_preview_ui;

static uint32_t s_menu_id = 0;                         ///< 当前文件列表菜单 ID。
static prompter_menu_item_t* s_menu_items = NULL;      ///< 当前文件列表菜单项数组。
static const char** s_menu_labels = NULL;              ///< 当前文件列表菜单项显示文案数组。
static uint32_t s_menu_count = 0;                      ///< 当前文件列表菜单项数量。
static uint32_t s_menu_selected_index = 0;             ///< 当前文件列表菜单选中索引。

static char* s_file_buf = NULL;
/* 记录当前缓存页在源文件中的起点和长度，用于跳过重复文件读取。 */
static uint32_t s_page_offset = 0;
static uint32_t s_page_len = 0;
static char s_file_path[SYSTEM_MAX_PATH_LEN] = {0};
static uint32_t s_file_size = 0;
static uint32_t s_tick_seconds = 0;                       ///< 当前手机端同步的提词计时秒数。
static prompter_state_t s_prompter_state = PROMPTER_STATE_PAUSE; ///< 当前手机端同步的提词播放状态。

/* Prompter 退出确认弹窗。 */
static msgbox_t* s_prompter_exit_msgbox = NULL;

/* 记录运行时最终命中的系统注册字号，配置字号仍保留在 prompter_font_info 中。 */
static uint32_t s_prompter_font_size_resolved = 0;

/**
 * @brief 释放当前正文文件缓存。
 * @return 无返回值。
 */
static void free_page_buffer(void) {
    if (s_file_buf) {
        free(s_file_buf);
        s_file_buf = NULL;
    }
    s_page_offset = 0;
    s_page_len = 0;
}

/**
 * @brief 清空当前正文文件路径和分页缓存。
 * @return 无返回值。
 */
static void clear_file_state(void) {
    free_page_buffer();
    s_file_path[0] = '\0';
    s_file_size = 0;
}

/**
 * @brief 按源文件偏移刷新 Prompter 进度百分比。
 * @param[in] offset 源文件 UTF-8 字节偏移。
 * @return 无返回值。
 */
static void prompter_update_progress_by_offset(uint32_t offset) {
    uint32_t percent = 0;
    char text[8] = {0};

    if (s_file_size > 0) {
        if (offset > s_file_size) {
            offset = s_file_size;
        }
        percent = (uint32_t)(((uint64_t)offset * 100U) / (uint64_t)s_file_size);
        if (percent > 100U) {
            percent = 100U;
        }
    }

    snprintf(text, sizeof(text), "%lu%%", (unsigned long)percent);
    label_set_text(s_text_preview_ui.progress_label, text);
}

/**
 * @brief 按秒数刷新 Prompter 计时标签。
 * @param[in] tick_seconds 手机端同步的提词计时秒数。
 * @return 无返回值。
 */
static void prompter_update_tick_label(uint32_t tick_seconds) {
    uint32_t hours = tick_seconds / 3600U;
    uint32_t minutes = (tick_seconds / 60U) % 60U;
    uint32_t seconds = tick_seconds % 60U;
    char text[16] = {0};

    snprintf(text,
             sizeof(text),
             "%02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds);
    label_set_text(s_text_preview_ui.timer_label, text);
}

/**
 * @brief 设置 Prompter 提词计时秒数并刷新计时标签。
 * @param[in] tick_seconds 手机端同步的提词计时秒数。
 * @return 无返回值。
 */
void prompter_text_set_tick(uint32_t tick_seconds) {
    s_tick_seconds = tick_seconds;
    if (s_text_preview_ui.timer_label != NULL) {
        prompter_update_tick_label(tick_seconds);
    }
}

/**
 * @brief 按播放状态刷新 Prompter 底部提示文案。
 * @param[in] state 提词播放状态。
 * @return 无返回值。
 */
static void prompter_update_state_hint(prompter_state_t state) {
    const char* text_key = (state == PROMPTER_STATE_RUNNING) ? "PROMPTER_RUNNING_HINT" : "PROMPTER_HINT";

    label_set_text(s_text_preview_ui.hint_label, app_get_str(text_key));
}

/**
 * @brief 设置 Prompter 提词播放状态并刷新底部提示。
 * @param[in] state 手机端同步的提词播放状态。
 * @return 无返回值。
 */
void prompter_text_set_state(prompter_state_t state) {
    if (state != PROMPTER_STATE_RUNNING) {
        state = PROMPTER_STATE_PAUSE;
    }

    s_prompter_state = state;
    if (s_text_preview_ui.hint_label != NULL) {
        prompter_update_state_hint(state);
    }
}

/**
 * @brief 释放当前文件列表菜单缓存。
 * @return 无返回值。
 */
static void prompter_menu_release(void) {
    if (s_menu_items != NULL) {
        free(s_menu_items);
        s_menu_items = NULL;
    }
    if (s_menu_labels != NULL) {
        free(s_menu_labels);
        s_menu_labels = NULL;
    }
    s_menu_id = 0;
    s_menu_count = 0;
    s_menu_selected_index = 0;
}

/**
 * @brief 将当前文件列表菜单同步到滚轮。
 * @return 无返回值。
 */
static void prompter_menu_sync_roller(void) {
    roller_t* roller = s_file_roller_ui.file_roller;

    if (roller == NULL) {
        return;
    }

    roller_set_items(roller, s_menu_labels, s_menu_count);
    if (s_menu_count > 0) {
        if (s_menu_selected_index >= s_menu_count) {
            s_menu_selected_index = 0;
        }
        roller_set_selected(roller, s_menu_selected_index, false);
    }
}

static bool prompter_has_txt_suffix(const char* path) {
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

/**
 * @brief 规范化 Prompter 配置字号，非法值回退到系统默认字号。
 * @param[in] configured_size 配置中的字号。
 * @return 返回可继续参与解析的配置字号。
 */
static uint32_t prompter_normalize_configured_font_size(uint32_t configured_size) {
    if (!app_fontsize_valid((int32_t)configured_size)) {
        uint32_t fallback_size = get_system_font_size();

        floatair_warn("prompter invalid font size %lu, fallback to system size %lu",
                      (unsigned long)configured_size,
                      (unsigned long)fallback_size);
        return fallback_size;
    }

    return configured_size;
}

/**
 * @brief 将 Prompter 配置字号解析为 system_res 中可用的运行时字号。
 * @param[in] configured_size 配置中的字号。
 * @return 返回运行时最终使用的系统注册字号。
 */
static uint32_t prompter_resolve_font_size(uint32_t configured_size) {
    uint32_t runtime_size = prompter_normalize_configured_font_size(configured_size);
    uint32_t resolved_size = 0;

    resolved_size = get_font_size_near(runtime_size);
    if (resolved_size != runtime_size) {
        floatair_info("prompter font size resolved from %lu to %lu",
                      (unsigned long)runtime_size,
                      (unsigned long)resolved_size);
    }

    return resolved_size;
}

/**
 * @brief 按当前配置刷新 Prompter 运行时字体对象引用。
 * @param[in] configured_size 配置中的目标字号。
 * @return `true` 表示成功拿到系统字体注册表中的字体。
 */
static bool prompter_refresh_runtime_font(uint32_t configured_size) {
    uint32_t normalized_size = prompter_normalize_configured_font_size(configured_size);
    uint32_t resolved_size = prompter_resolve_font_size(normalized_size);
    const lv_font_t* font = get_font_by_size(resolved_size);

    if (font == NULL) {
        font = get_font_by_size_near(normalized_size);
        resolved_size = get_font_size_near(normalized_size);
    }
    if (font == NULL) {
        floatair_err("prompter runtime font is NULL");
        return false;
    }

    prompter_font = font;
    s_prompter_font_size_resolved = resolved_size;
    return true;
}

/**
 * @brief 从 Prompter 应用配置文件读取字体配置并刷新运行时字体。
 * @return `true` 表示读取并应用成功，`false` 表示配置不存在或解析失败。
 */
bool prompter_font_init_from_config(void) {
    if (!prompter_config_ensure()) {
        return false;
    }
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_PROMPTER, config_path, sizeof(config_path))) {
        return false;
    }
    app_font_info_t fi = {0};
    if (!system_config_get_font(config_path, &fi)) {
        return false;
    }
    fi.weight = prompter_normalize_configured_font_size(fi.weight);
    prompter_font_info = fi;
    return prompter_refresh_runtime_font(fi.weight);
}

/**
 * @brief 获取 Prompter 正文排版参数。
 * @param layout 输出排版参数。
 * @return `true` 表示获取成功，`false` 表示参数无效。
 */
bool prompter_text_get_layout(prompter_text_layout_t* layout) {
    lv_obj_t* text_obj = NULL;
    int32_t text_inset = 0;
    int32_t text_w = 0;
    int32_t text_h = 0;

    if (layout == NULL) {
        return false;
    }
    if ((prompter_font == NULL || s_prompter_font_size_resolved == 0) &&
        !prompter_font_init_from_config()) {
        prompter_font_info.weight = get_system_font_size();
        prompter_font_info.wordSpace = get_system_font_word_space();
        prompter_font_info.rowSpace = get_system_font_row_space();
        (void)prompter_refresh_runtime_font(prompter_font_info.weight);
    }

    memset(layout, 0, sizeof(*layout));
    text_obj = paged_text_get_obj(s_text_preview_ui.text_view);
    if (text_obj == NULL || !lv_obj_is_valid(text_obj)) {
        return false;
    }
    lv_obj_update_layout(text_obj);
    text_w = (int32_t)lv_obj_get_width(text_obj);
    text_h = (int32_t)lv_obj_get_height(text_obj);
    text_inset = paged_text_get_text_inset(s_text_preview_ui.text_view);
    layout->break_all = false;
    layout->letter_space_px = prompter_font_info.wordSpace;
    layout->line_space_px = prompter_font_info.rowSpace;
    layout->padding_horizontal_px = (uint32_t)(text_inset > 0 ? text_inset : 0);
    layout->padding_vertical_px = (uint32_t)(text_inset > 0 ? text_inset : 0);
    layout->text_size_px = s_prompter_font_size_resolved;
    if (layout->text_size_px == 0) {
        layout->text_size_px = prompter_resolve_font_size(
            prompter_font_info.weight > 0 ? prompter_font_info.weight : get_system_font_size());
    }
    layout->total_width_px = (uint32_t)text_w;
    layout->total_height_px = (uint32_t)text_h;
    return true;
}

/**
 * @brief 在 Prompter 页面加载阶段一次性绑定所需字体。
 * @return `true` 表示页面所需字体均已完成绑定。
 */
static bool prompter_bind_fonts_on_load(void) {
    app_font_info_t fallback_info = {0};

    if (prompter_font_init_from_config()) {
        return true;
    }

    fallback_info.weight = get_system_font_size();
    fallback_info.wordSpace = get_system_font_word_space();
    fallback_info.rowSpace = get_system_font_row_space();
    prompter_font_info = fallback_info;
    return prompter_refresh_runtime_font(fallback_info.weight);
}

/**
 * @brief 按当前视图模式同步初始化态和正文态组件显隐。
 * @return 无返回值。
 */
static void prompter_view_apply_visibility(void) {
    bool init_mode = (s_view_mode == PROMPTER_VIEW_MODE_INIT);
    bool show_empty = init_mode && s_menu_count == 0;
    bool show_menu = init_mode && s_menu_count > 0;

    ui_widget_set_visible(UI_WIDGET(s_entry_overlay_ui.overlay_box), show_empty);
    ui_widget_set_visible(UI_WIDGET(s_entry_overlay_ui.init_label), show_empty);
    ui_widget_set_visible(UI_WIDGET(s_file_roller_ui.file_menu_box), show_menu);
    ui_widget_set_visible(UI_WIDGET(s_text_preview_ui.preview_box), !init_mode);
    if (init_mode) {
        paged_text_hide_highlight_window(s_text_preview_ui.text_view);
    }
}

/**
 * @brief 切换 Prompter 视图模式。
 * @param[in] mode 目标视图模式。
 * @return 无返回值。
 */
static void prompter_view_set_mode(prompter_view_mode_t mode) {
    if (s_view_mode == mode) {
        return;
    }
    s_view_mode = mode;
    prompter_view_apply_visibility();
}

/**
 * @brief 按 Android 端同步的预览状态刷新正文与遮罩。
 * @param[in] view 外部同步预览状态。
 * @return 无返回值。
 */
void prompter_text_apply_external_view(const prompter_external_view_t* view) {
    paged_text_t* text_view = s_text_preview_ui.text_view;
    uint32_t read_len = 0;
    uint32_t br = 0;
    void* fh = NULL;
    char* page_buf = NULL;
    prompter_external_view_t local_view = {0};
    uint32_t start_time_us = 0;
    uint32_t cost_time_us = 0;

    if (s_view_mode != PROMPTER_VIEW_MODE_TEXT || !text_view || !view || s_file_path[0] == '\0') {
        return;
    }
    if (view->offset > s_file_size) {
        return;
    }
    start_time_us = (uint32_t)GetTimeUs();
    prompter_update_progress_by_offset(view->offset);
    read_len = view->length;
    if (read_len > s_file_size - view->offset) {
        read_len = s_file_size - view->offset;
    }
    local_view = *view;
    local_view.offset = 0;
    local_view.length = read_len;
    if (read_len == 0) {
        free_page_buffer();
        paged_text_set_visible_text(text_view, "");
        paged_text_set_highlight_window(text_view,
                                        local_view.top_mask_height,
                                        local_view.bottom_mask_height);
        cost_time_us = (uint32_t)GetTimeUs() - start_time_us;
        floatair_info("prompter apply external view cost %lu us/%lu ms, result=empty, offset=%lu, length=%lu, read=%lu",
                      (unsigned long)cost_time_us,
                      (unsigned long)(cost_time_us / 1000U),
                      (unsigned long)view->offset,
                      (unsigned long)view->length,
                      (unsigned long)read_len);
        return;
    }
    if (s_file_buf != NULL && s_page_offset == view->offset && s_page_len == read_len) {
        paged_text_set_visible_text(text_view, s_file_buf);
        paged_text_set_highlight_window(text_view,
                                        local_view.top_mask_height,
                                        local_view.bottom_mask_height);
        cost_time_us = (uint32_t)GetTimeUs() - start_time_us;
        floatair_info("prompter apply external view cost %lu us/%lu ms, result=cache, offset=%lu, length=%lu, read=%lu",
                      (unsigned long)cost_time_us,
                      (unsigned long)(cost_time_us / 1000U),
                      (unsigned long)view->offset,
                      (unsigned long)view->length,
                      (unsigned long)read_len);
        return;
    }

    page_buf = (char*)malloc(read_len + 1);
    if (page_buf == NULL) {
        return;
    }
    fh = floatair_fs_open(s_file_path, FLOATAIR_FS_MODE_RD);
    if (fh == NULL) {
        free(page_buf);
        return;
    }
    if (floatair_fs_seek(fh, (int32_t)view->offset, SEEK_SET) != FLOATAIR_FS_OK ||
        floatair_fs_read(fh, page_buf, read_len, &br) != FLOATAIR_FS_OK ||
        br != read_len) {
        floatair_fs_close(fh);
        free(page_buf);
        return;
    }
    floatair_fs_close(fh);
    page_buf[read_len] = '\0';

    free_page_buffer();
    s_file_buf = page_buf;
    s_page_offset = view->offset;
    s_page_len = read_len;
    paged_text_set_visible_text(text_view, s_file_buf);
    paged_text_set_highlight_window(text_view,
                                    local_view.top_mask_height,
                                    local_view.bottom_mask_height);

    cost_time_us = (uint32_t)GetTimeUs() - start_time_us;
    floatair_info("prompter apply external view cost %lu us/%lu ms, result=read, offset=%lu, length=%lu, read=%lu",
                  (unsigned long)cost_time_us,
                  (unsigned long)(cost_time_us / 1000U),
                  (unsigned long)view->offset,
                  (unsigned long)view->length,
                  (unsigned long)read_len);
}

/**
 * @brief 记录指定文本文件路径并进入正文态。
 * @param[in] path 文本文件路径。
 * @return `true` 表示记录成功，`false` 表示参数或文件异常。
 */
bool prompter_text_set_file(const char* path) {
    floatair_stat_t st = {0};

    if (!path) {
        floatair_err("prompter set file path is NULL");
        return false;
    }
    if (!prompter_has_txt_suffix(path)) {
        floatair_warn("prompter set file ignored, only .txt is supported: %s", path);
        return false;
    }
    if (floatair_fs_stat(path, &st) != FLOATAIR_FS_OK || st.is_dir || st.size == 0 || st.size > 50 * 1024) {
        floatair_err("prompter set file stat fail or invalid: %s", path);
        return false;
    }
    if (strlen(path) >= sizeof(s_file_path)) {
        floatair_err("prompter file path too long: %s", path);
        return false;
    }

    clear_file_state();
    snprintf(s_file_path, sizeof(s_file_path), "%s", path);
    s_file_size = st.size;
    prompter_update_progress_by_offset(0);
    prompter_text_set_tick(0);
    prompter_text_set_state(PROMPTER_STATE_PAUSE);

    paged_text_set_text(s_text_preview_ui.text_view, "");
    paged_text_hide_highlight_window(s_text_preview_ui.text_view);

    prompter_view_set_mode(PROMPTER_VIEW_MODE_TEXT);
    floatair_info("prompter set file path ok: %s, bytes=%lu", path, (unsigned long)s_file_size);
    return true;
}

/**
 * @brief 重置 Prompter 视图状态。
 * @return 无返回值。
 */
void prompter_view_reset(void) {
    if (msgbox_is_valid(s_prompter_exit_msgbox)) {
        msgbox_set_visible(s_prompter_exit_msgbox, false);
    }
    paged_text_set_text(s_text_preview_ui.text_view, "");
    paged_text_hide_highlight_window(s_text_preview_ui.text_view);
    clear_file_state();
    prompter_update_progress_by_offset(0);
    prompter_text_set_tick(0);
    prompter_text_set_state(PROMPTER_STATE_PAUSE);
    prompter_view_set_mode(PROMPTER_VIEW_MODE_INIT);
}

/**
 * @brief 初始化态按方向切换文档选中项。
 * @param[in] step 选中项步进，通常为 `-1` 或 `1`。
 * @return 无返回值。
 */
static void init_move_selection(int32_t step) {
    roller_t* roller = s_file_roller_ui.file_roller;
    uint32_t sel = 0;
    uint32_t next = 0;

    if (s_menu_count <= 1 || !roller) {
        return;
    }
    sel = roller_get_selected(roller);
    next = (uint32_t)(((int32_t)sel + step + (int32_t)s_menu_count) % (int32_t)s_menu_count);
    s_menu_selected_index = next;
    roller_set_selected(roller, next, true);
}

/**
 * @brief 上报初始化态当前选中的文件列表菜单项。
 * @return 无返回值。
 */
static void init_report_cur_event(void) {
    roller_t* roller = s_file_roller_ui.file_roller;

    if (s_menu_count == 0 || s_menu_items == NULL || !roller) {
        return;
    }
    uint32_t sel = roller_get_selected(roller);
    if (sel >= s_menu_count) {
        return;
    }
    s_menu_selected_index = sel;
    (void)prompter_report_menu_selected(s_menu_id, s_menu_items[sel].id);
}

/**
 * @brief 处理 Prompter 退出确认弹窗结果。
 * @param[in] box 触发确认的消息框。
 * @param[in] key 本次确认结果。
 * @param[in] user_data 用户透传数据。
 * @return 无返回值。
 */
// static void prompter_exit_msgbox_on_result(msgbox_t* box, msgbox_key_t key, void* user_data) {
//     (void)box;
//     (void)user_data;

//     if (key == MSGBOX_KEY_LEAVE) {
//         (void)app_router_exit_current_app();
//     }
// }

/**
 * @brief 处理 Prompter 页面点击、双击和左右手势事件。
 * @param[in] event LVGL 事件对象。
 * @return 无返回值。
 */
static void touch_event_handle(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DCLICKED) {
        // s_prompter_exit_msgbox = msgbox_show(s_prompter_exit_msgbox,
        //                                      app_get_str("PROMPTER_EXIT_TITLE"),
        //                                      MSGBOX_KEY_CANCEL | MSGBOX_KEY_LEAVE);
        // msgbox_set_callback(s_prompter_exit_msgbox, prompter_exit_msgbox_on_result, NULL);
        (void)app_router_exit_current_app();
        return;
    }

    if (s_view_mode == PROMPTER_VIEW_MODE_INIT) {
        switch (code) {
            case LV_EVENT_GESTURE_LEFT:
                init_move_selection(-1);
                break;
            case LV_EVENT_GESTURE_RIGHT:
                init_move_selection(1);
                break;
            case LV_EVENT_CLICKED:
            case LV_EVENT_LONG_PRESSED:
                init_report_cur_event();
                break;
            default:
                break;
        }
    } else {
        switch (code) {
            case LV_EVENT_GESTURE_LEFT:
            case LV_EVENT_GESTURE_RIGHT:
            case LV_EVENT_CLICKED:
            case LV_EVENT_LONG_PRESSED:
                (void)system_report_touch_event(code);
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 为 uic 生成组件补充运行时字体。
 * @return 无返回值。
 */
static void prompter_apply_runtime_font_config(void) {
    label_set_font_info(s_file_roller_ui.hint_label, &prompter_font_info);
    label_set_font_info(s_text_preview_ui.timer_label, &prompter_font_info);
    label_set_font_info(s_text_preview_ui.progress_label, &prompter_font_info);
    label_set_font_info(s_text_preview_ui.hint_label, &prompter_font_info);
    paged_text_set_font_info(s_text_preview_ui.text_view, &prompter_font_info);
}

/**
 * @brief 为 uic 生成组件补充运行时字体、文本和数据状态。
 * @return 无返回值。
 */
static void prompter_apply_runtime_ui_config(void) {
    prompter_apply_runtime_font_config();
    prompter_update_tick_label(s_tick_seconds);
    prompter_update_state_hint(s_prompter_state);
    paged_text_set_text(s_text_preview_ui.text_view, s_file_buf ? s_file_buf : "");
    paged_text_page_init(s_text_preview_ui.text_view);
    paged_text_hide_highlight_window(s_text_preview_ui.text_view);
}

/**
 * @brief Prompter 字体配置变化后刷新运行时字体和页面控件。
 * @return `true` 表示刷新成功。
 */
bool prompter_on_fontconfig_changed(void) {
    if (!prompter_font_init_from_config()) {
        return false;
    }
    prompter_apply_runtime_font_config();
    return true;
}

/**
 * @brief 通过 uic 生成代码创建 Prompter 页面 UI。
 * @param[in] root 页面根对象。
 * @return 无返回值。
 */
static void prompter_build_ui_from_json(lv_obj_t* root) {
    if (s_text_preview_ui.preview_box != NULL) {
        return;
    }

    floatair_assert(prompter_entry_overlay_init_ui(root, &s_entry_overlay_ui),
                    "prompter entry overlay ui create failed");
    floatair_assert(prompter_file_roller_init_ui(root, &s_file_roller_ui),
                    "prompter file roller ui create failed");
    floatair_assert(prompter_page_text_preview_init_ui(root, &s_text_preview_ui),
                    "prompter page text preview ui create failed");

    prompter_apply_runtime_ui_config();
    prompter_menu_sync_roller();
}

/**
 * @brief 设置 Prompter 文件列表菜单。
 * @param menu_id 菜单 ID。
 * @param items 菜单项数组；`count` 为 0 时允许传 `NULL`。
 * @param count 菜单项数量。
 * @param default_item_id 默认选中菜单项 ID；为 0 或不存在时选中第一项。
 * @return `true` 表示设置成功，`false` 表示参数无效或内存不足。
 */
bool prompter_menu_set(uint32_t menu_id,
                       const prompter_menu_item_t* items,
                       uint32_t count,
                       uint32_t default_item_id) {
    prompter_menu_item_t* new_items = NULL;
    const char** new_labels = NULL;
    uint32_t selected_index = 0;

    if (count > 0 && items == NULL) {
        return false;
    }

    if (count > 0) {
        new_items = (prompter_menu_item_t*)calloc((size_t)count, sizeof(prompter_menu_item_t));
        new_labels = (const char**)calloc((size_t)count, sizeof(char*));
        if (new_items == NULL || new_labels == NULL) {
            free(new_items);
            free(new_labels);
            return false;
        }
        memcpy(new_items, items, sizeof(prompter_menu_item_t) * (size_t)count);
        for (uint32_t i = 0; i < count; i++) {
            new_labels[i] = new_items[i].label;
            if (default_item_id != 0 && new_items[i].id == default_item_id) {
                selected_index = i;
            }
        }
    }

    prompter_menu_release();
    s_menu_id = menu_id;
    s_menu_items = new_items;
    s_menu_labels = new_labels;
    s_menu_count = count;
    s_menu_selected_index = selected_index;

    clear_file_state();
    paged_text_set_text(s_text_preview_ui.text_view, "");
    prompter_update_progress_by_offset(0);
    prompter_text_set_tick(0);
    prompter_text_set_state(PROMPTER_STATE_PAUSE);
    paged_text_hide_highlight_window(s_text_preview_ui.text_view);

    prompter_menu_sync_roller();
    prompter_view_set_mode(PROMPTER_VIEW_MODE_INIT);
    prompter_view_apply_visibility();
    return true;
}

/**
 * @brief Prompter 页面加载回调，创建页面组件树。
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参缓存。
 * @return 无返回值。
 */
static void prompter_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    system_status_bar_set_mode(true);
    lv_obj_update_layout(root);
    floatair_assert(prompter_bind_fonts_on_load(), "prompter fonts bind failed");
    floatair_assert(prompter_font != NULL, "font is NULL");

    prompter_build_ui_from_json(root);

    s_view_mode = (s_file_path[0] != '\0') ? PROMPTER_VIEW_MODE_TEXT : PROMPTER_VIEW_MODE_INIT;
    prompter_view_apply_visibility();
}

/**
 * @brief Prompter 页面即将显示回调，注册输入事件。
 * @return 无返回值。
 */
static void prompter_page_appear(lv_obj_t* root) {
    (void)system_request_keyword_spotting_enabled(false);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
}

/**
 * @brief Prompter 页面卸载回调，释放文件缓存和运行时指针。
 * @param[in] base 页面基类对象。
 * @return 无返回值。
 */
static void prompter_page_destroy(void) {
    (void)system_request_keyword_spotting_enabled(system_config_get_keyword_spotting_enabled());

    msgbox_destroy(s_prompter_exit_msgbox);
    s_prompter_exit_msgbox = NULL;

    clear_file_state();
    prompter_menu_release();

    memset(&s_entry_overlay_ui, 0, sizeof(s_entry_overlay_ui));
    memset(&s_file_roller_ui, 0, sizeof(s_file_roller_ui));
    memset(&s_text_preview_ui, 0, sizeof(s_text_preview_ui));

    s_prompter_font_size_resolved = 0;
}

static app_page_t s_prompter_page = {
    .name = APP_NAME_PROMPTER,
    .on_create = prompter_page_create,
    .on_appear = prompter_page_appear,
    .on_disappear = NULL,
    .on_destroy = prompter_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* prompter_page_get(void) {
    return &s_prompter_page;
}
