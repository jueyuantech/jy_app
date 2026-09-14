/**
 * @file speech_view.c
 * @brief 转写和翻译共享的语音文本页面视图实现。
 */
#include "common/speech/speech_view.h"

#include "home/home.h"
#include "common/speech/speech_runtime.h"
#include "common/stt/stt_view_common.h"
#include "speech_start_ui.h"
#include "speech_stt_row_ui.h"
#include "speech_progress_overlay_ui.h"

#include "app_def.h"
#include "common/app_framework/app_router.h"
#include "common/widgets/container.h"
#include "common/widgets/img.h"
#include "common/widgets/label.h"
#include "common/widgets/progress_indicator.h"
#include "common/widgets/roller.h"
#include "common/widgets/status_bar.h"
#include "common/widgets/msgbox.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_def.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"
#include "floatair_fs.h"
#include "ui_res.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief 语音文本页面当前展示模式。
 */
typedef enum {
    SPEECH_PAGE_MODE_NONE = 0,        ///< 页面内容全部隐藏。
    SPEECH_PAGE_MODE_DESCRIPTION = 1, ///< 展示功能说明态。
    SPEECH_PAGE_MODE_STT = 2,         ///< 展示实时语音文本态。
} speech_page_mode_t;

/**
 * @brief Speech 页面顶部提示展示模式。
 */
typedef enum {
    SPEECH_TOP_HINT_NONE = 0,     ///< 不展示顶部提示。
    SPEECH_TOP_HINT_LANGUAGE = 1, ///< 展示语言提示。
    SPEECH_TOP_HINT_TEXT = 2,     ///< 展示手机下发的顶部文本。
} speech_top_hint_mode_t;

#define SPEECH_TEXT_SIDE_PADDING 12
#define SPEECH_BORDERED_ROW_GAP 8
#define SPEECH_BORDERED_MAX_WIDTH 448
#define SPEECH_BORDERED_ICON_GAP 10
#define SPEECH_HEADER_SIDE_PADDING 24
#define SPEECH_HEADER_TOP_PADDING 20
#define SPEECH_HEADER_BOTTOM_PADDING 10
#define SPEECH_SCROLL_TOP_MARGIN 2
#define SPEECH_LANG_HINT_PADDING LVGL_UI_MARGIN_10
#define SPEECH_LANG_HINT_MAX_SIDE_PADDING 4
#define SPEECH_ACTION_HINT_GAP LVGL_UI_MARGIN_10 ///< 正文与底部操作提示之间的间距。
#define SPEECH_PAGE_NAME "speech" ///< Speech 共享页面名称。
#define SPEECH_EXIT_DIALOG_ACTION_KEY "TRANSCRIBE_EXIT_DIALOG_ACTION"
#define SPEECH_STATE_PAUSED 0
#define SPEECH_STATE_STARTED 1
#define SPEECH_FUNCTION_MENU_ITEM_HEIGHT 64
#define SPEECH_FUNCTION_MENU_ITEM_GAP 8
#define SPEECH_FUNCTION_MENU_SIDE_PADDING 20
#define SPEECH_FUNCTION_MENU_SELECTED_BORDER 2
#define SPEECH_FUNCTION_MENU_NORMAL_OPA LV_OPA_60

/**
 * @brief 单条语音文本行视图缓存。
 */
typedef struct {
    speech_stt_row_ui_t ui;   ///< uic 生成的行控件句柄。
    container_t* row;         ///< 行根容器。
    img_t* left_icon;         ///< 左侧 STT 气泡图标。
    label_t* first_label;     ///< 第一段文本标签。
    label_t* second_label;    ///< 第二段文本标签，转写模式隐藏。
    label_t* bubble_label;    ///< 带边框 STT 模式的正文标签。
    img_t* right_icon;        ///< 右侧 STT 气泡图标。
    char* bubble_text;        ///< 双语气泡复用的拼接缓冲区。
    size_t bubble_text_capacity; ///< 气泡拼接缓冲区容量。
} speech_stt_row_t;

/**
 * @brief 语音文本页面运行时上下文。
 */
typedef struct {
    const speech_app_profile_t* profile;               ///< 当前逻辑 App 入口配置。
    speech_page_mode_t page_mode;                      ///< 当前展示模式。
    speech_top_hint_mode_t top_hint_mode;              ///< 当前互斥的顶部提示模式。
    uint8_t speech_state;                              ///< 手机下发的当前语音启停状态。
    bool speech_state_received;                        ///< 当前页面会话是否已收到语音状态。
    speech_start_ui_t start_ui;                        ///< 起始页 UI 句柄。
    speech_progress_overlay_ui_t overlay_ui;    ///< 进度提示遮罩 UI 句柄。
    label_t* init_hint;                                ///< 中间起始文本。
    label_t* action_hint;                              ///< 底部点击开始/暂停提示。
    lv_obj_t* audio_source;                            ///< 底栏音频来源图标。
    lv_obj_t* mic_direction;                           ///< 底栏麦克风方向图标。
    lv_obj_t* waveicon;                                ///< 底栏波形图标。
    lv_obj_t* root;                                    ///< 页面根对象。
    container_t* content;                              ///< 内容容器。
    label_t* header_text;                              ///< 手机协议驱动的顶部提示或结果文本。
    label_t* transcribe_lang;                          ///< 转写语言提示。
    container_t* translate_lang;                       ///< 翻译语言提示容器。
    container_t* translate_lang_row;                   ///< 翻译语言提示行。
    label_t* translate_lang_source;                    ///< 翻译源语言标签。
    label_t* translate_lang_target;                    ///< 翻译目标语言标签。
    img_t* translate_lang_icon;                        ///< 翻译语言切换图标。
    container_t* scroll;                               ///< STT 滚动容器。
    container_t* scroll_spacer;                        ///< STT 顶部弹性占位。
    speech_stt_row_t rows[STT_INFO_MAX_MSG_NUM];       ///< STT 行缓存。
    container_t* function_menu;                        ///< 手机协议驱动的功能选择遮罩。
    roller_t* function_menu_roller;                    ///< 复用三槽位的通用滚轮。
    bool function_menu_visible;                        ///< 功能选择遮罩显隐状态。
} speech_view_context_t;

static speech_view_context_t s_speech_context = {
    .page_mode = SPEECH_PAGE_MODE_NONE,
    .top_hint_mode = SPEECH_TOP_HINT_NONE,
    .speech_state = SPEECH_STATE_PAUSED,
};
static stt_view_deferred_update_t s_speech_stt_deferred_update;

static void speech_stt_update_now(void);

static void speech_stt_cancel_pending_update(void);

static void speech_function_menu_apply_roller_cfg(speech_view_context_t* ctx);

static void speech_stt_deferred_update_cb(void* user_data) {
    speech_view_context_t* ctx = (speech_view_context_t*)user_data;
    system_progress_hint_param_t hint_param = {
        .visible = false,
        .text = "",
        .bg_opa = 0,
    };

    if (ctx == NULL || ctx->root == NULL || !lv_obj_is_valid(ctx->root)) {
        return;
    }

    (void)system_ui_send_progress_hint(&hint_param);
    speech_stt_update_now();
}

static void speech_stt_cancel_pending_update(void) {
    stt_view_cancel_deferred_update(&s_speech_stt_deferred_update);
}

static const speech_app_profile_t* speech_config(const speech_view_context_t* ctx) {
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->profile;
}

static speech_view_context_t* speech_context_by_app_name(const char* app_name) {
    const speech_app_profile_t* profile = speech_profile_from_app_name(app_name);

    if (profile == NULL) {
        return NULL;
    }
    s_speech_context.profile = profile;
    return &s_speech_context;
}

static bool speech_status_bar_widgets_valid(speech_view_context_t* ctx) {
    return ctx != NULL &&
           ctx->audio_source != NULL &&
           lv_obj_is_valid(ctx->audio_source) &&
           ctx->waveicon != NULL &&
           lv_obj_is_valid(ctx->waveicon) &&
           ctx->mic_direction != NULL &&
           lv_obj_is_valid(ctx->mic_direction);
}

static bool speech_status_bar_ensure_widgets(speech_view_context_t* ctx) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (ctx == NULL) {
        return false;
    }
    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        ctx->audio_source = NULL;
        ctx->waveicon = NULL;
        ctx->mic_direction = NULL;
        return false;
    }
    if (!speech_status_bar_widgets_valid(ctx)) {
        status_bar_clear_custom_widgets(status_bar);
        ctx->audio_source = status_bar_add_image(status_bar, UI_RES_IMAGE_SOUND_PHONE, STATUS_BAR_WIDGET_ALIGN_LEFT);
        ctx->waveicon = status_bar_add_image(status_bar, UI_RES_IMAGE_SOUND_WAVE, STATUS_BAR_WIDGET_ALIGN_RIGHT);
        ctx->mic_direction = status_bar_add_image(status_bar, UI_RES_IMAGE_MICPHONE, STATUS_BAR_WIDGET_ALIGN_RIGHT);
    }

    return speech_status_bar_widgets_valid(ctx);
}

static bool speech_is_current_home(void) {
    const char* current_app = app_router_get_app();
    const char* home_viewname = app_router_get_home_viewname();

    return current_app != NULL && home_viewname != NULL && strcmp(current_app, home_viewname) == 0;
}

/**
 * @brief 获取当前 Speech 模式使用的语言提示根对象。
 * @param[in] ctx Speech 页面上下文。
 * @return 返回语言提示根对象；尚未创建时返回 `NULL`。
 */
static lv_obj_t* speech_get_lang_hint_obj(const speech_view_context_t* ctx) {
    if (ctx == NULL) {
        return NULL;
    }
    if (stt_config.language_hint == 1) {
        return container_get_obj(ctx->translate_lang);
    }
    return label_get_obj(ctx->transcribe_lang);
}

/**
 * @brief 判断当前 Speech 模式是否应展示语言提示。
 * @param[in] ctx Speech 页面上下文。
 * @return `true` 表示语言提示字段完整并应显示。
 */
static bool speech_lang_hint_should_show(const speech_view_context_t* ctx) {
    if (ctx == NULL || ctx->top_hint_mode != SPEECH_TOP_HINT_LANGUAGE ||
        stt_config.language_source[0] == '\0') {
        return false;
    }
    if (stt_config.language_hint == 0) {
        return true;
    }
    return stt_config.language_hint == 1 && stt_config.language_target[0] != '\0';
}

/**
 * @brief 应用顶部文本的不透明遮罩样式。
 * @param[in] header 顶部文本标签。
 * @return 无返回值。
 */
static void speech_apply_header_theme(label_t* header) {
    lv_obj_t* header_obj = NULL;

    if (header == NULL) {
        return;
    }

    stt_view_apply_text_theme(header, LABEL_ALIGN_LEFT, LABEL_OVERFLOW_WRAP);
    label_set_opacity(header, LV_OPA_COVER);
    header_obj = label_get_obj(header);
    if (header_obj != NULL) {
        lv_obj_set_width(header_obj, LV_PCT(100));
        lv_obj_set_style_bg_color(header_obj, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(header_obj, LV_OPA_COVER, LV_PART_MAIN);
    }
}

/**
 * @brief 按语言提示显隐同步正文顶部留白。
 * @param[in] ctx Speech 页面上下文。
 * @return 无返回值。
 */
static void speech_sync_content_padding(speech_view_context_t* ctx) {
    int32_t top_padding = SPEECH_SCROLL_TOP_MARGIN;

    if (ctx == NULL || ctx->content == NULL) {
        return;
    }
    if (speech_lang_hint_should_show(ctx)) {
        top_padding += STATUS_BAR_IMG_H;
    }
    container_set_padding_box(ctx->content,
                              0,
                              0,
                              top_padding,
                              get_system_font_height() + SPEECH_ACTION_HINT_GAP);
}

static void speech_sync_lang_hint_layout(speech_view_context_t* ctx) {
    lv_obj_t* lang_obj = NULL;
    lv_coord_t max_width;

    if (ctx == NULL) {
        return;
    }

    if (ctx->transcribe_lang != NULL) {
        lang_obj = label_get_obj(ctx->transcribe_lang);
        if (lang_obj != NULL) {
            max_width = (lv_coord_t)config_lcd.ui_width - SPEECH_LANG_HINT_MAX_SIDE_PADDING * 2;
            if (max_width < 0) {
                max_width = 0;
            }
            lv_obj_set_size(lang_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_max_width(lang_obj, max_width, LV_PART_MAIN);
            lv_obj_set_style_min_height(lang_obj, STATUS_BAR_IMG_H, LV_PART_MAIN);
            lv_obj_align(lang_obj, LV_ALIGN_TOP_MID, 0, SPEECH_SCROLL_TOP_MARGIN);
        }
    }

    if (ctx->translate_lang != NULL) {
        lang_obj = container_get_obj(ctx->translate_lang);
        if (lang_obj != NULL) {
            lv_obj_set_size(lang_obj, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_min_height(lang_obj, STATUS_BAR_IMG_H, 0);
            lv_obj_align(lang_obj, LV_ALIGN_TOP_MID, 0, SPEECH_SCROLL_TOP_MARGIN);
            container_set_padding(ctx->translate_lang, 0, 3);
        }
        if (ctx->translate_lang_row != NULL) {
            lv_obj_set_size(container_get_obj(ctx->translate_lang_row), LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        }
    }
}

static void speech_set_progress_overlay_visible(speech_view_context_t* ctx,
                                                bool visible,
                                                const char* text,
                                                uint8_t bg_opa) {
    lv_obj_t* mask_obj = NULL;
    lv_obj_t* top_hint_obj = NULL;

    if (ctx == NULL ||
        ctx->overlay_ui.mask == NULL ||
        ctx->overlay_ui.indicator == NULL) {
        return;
    }

    progress_indicator_set_text(ctx->overlay_ui.indicator, text != NULL ? text : "");
    progress_indicator_set_icon_visible(ctx->overlay_ui.indicator, true);
    container_set_opacity(ctx->overlay_ui.mask, bg_opa);
    ui_widget_set_visible(UI_WIDGET(ctx->overlay_ui.mask), visible);
    if (visible) {
        mask_obj = container_get_obj(ctx->overlay_ui.mask);
        if (mask_obj != NULL) {
            lv_obj_move_foreground(mask_obj);
        }
        if (ctx->top_hint_mode == SPEECH_TOP_HINT_TEXT && ctx->header_text != NULL) {
            top_hint_obj = label_get_obj(ctx->header_text);
        } else if (ctx->top_hint_mode == SPEECH_TOP_HINT_LANGUAGE) {
            top_hint_obj = speech_get_lang_hint_obj(ctx);
        }
        if (top_hint_obj != NULL && !lv_obj_has_flag(top_hint_obj, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_move_foreground(top_hint_obj);
        }
    }
}

static bool speech_function_menu_find_selected(const speech_function_menu_t* menu,
                                               size_t* selected_index) {
    bool selected_found = false;

    if (menu == NULL || selected_index == NULL || menu->items == NULL ||
        menu->item_count == 0 || menu->item_count > UINT32_MAX) {
        return false;
    }

    for (size_t i = 0; i < menu->item_count; ++i) {
        if (menu->items[i].label[0] == '\0') {
            return false;
        }
        for (size_t j = i + 1; j < menu->item_count; ++j) {
            if (menu->items[i].id == menu->items[j].id) {
                return false;
            }
        }
        if (menu->items[i].id == menu->selected_item_id) {
            *selected_index = i;
            selected_found = true;
        }
    }
    return selected_found;
}

static void speech_function_menu_restore_status(speech_view_context_t* ctx) {
    if (ctx == NULL || ctx->page_mode == SPEECH_PAGE_MODE_NONE) {
        return;
    }

    stt_view_update_audio_source(ctx->audio_source);
    stt_view_update_mic_direction(ctx->mic_direction);
    stt_view_update_waveicon(ctx->waveicon, ctx->page_mode == SPEECH_PAGE_MODE_STT);
}

static void speech_function_menu_apply_roller_cfg(speech_view_context_t* ctx) {
    roller_cfg_t cfg = roller_default_cfg();

    if (ctx == NULL || !roller_is_valid(ctx->function_menu_roller)) {
        return;
    }

    cfg.label.font.weight = get_system_font_size();
    cfg.label.font.wordSpace = get_system_font_word_space();
    cfg.label.font.rowSpace = get_system_font_row_space();
    cfg.selected_font = cfg.label.font;
    cfg.overflow_mode = ROLLER_OVERFLOW_EXPAND_HEIGHT;
    cfg.row_height = SPEECH_FUNCTION_MENU_ITEM_HEIGHT;
    cfg.row_gap = SPEECH_FUNCTION_MENU_ITEM_GAP;
    cfg.selected_pad_ver = 0;
    cfg.border_width = SPEECH_FUNCTION_MENU_SELECTED_BORDER;
    cfg.opa_normal = SPEECH_FUNCTION_MENU_NORMAL_OPA;
    cfg.opa_selected = LV_OPA_COVER;
    cfg.show_hint = true;
    roller_apply_cfg(ctx->function_menu_roller, &cfg);
}

bool speech_function_menu_show(const speech_function_menu_t* menu) {
    speech_view_context_t* ctx = &s_speech_context;
    size_t selected_index = 0;
    lv_obj_t* menu_obj = NULL;
    const char** labels = NULL;

    if (ctx == NULL || ctx->function_menu == NULL ||
        !roller_is_valid(ctx->function_menu_roller) ||
        !speech_function_menu_find_selected(menu, &selected_index)) {
        return false;
    }

    labels = (const char**)calloc(menu->item_count, sizeof(*labels));
    if (labels == NULL) {
        return false;
    }
    for (size_t i = 0; i < menu->item_count; ++i) {
        labels[i] = menu->items[i].label;
    }
    roller_set_items(ctx->function_menu_roller, labels, (uint32_t)menu->item_count);
    free(labels);
    if (roller_get_count(ctx->function_menu_roller) != (uint32_t)menu->item_count) {
        return false;
    }
    roller_set_selected(ctx->function_menu_roller, (uint32_t)selected_index, false);
    ctx->function_menu_visible = true;

    ui_widget_set_visible(UI_WIDGET(ctx->overlay_ui.mask), false);
    if (ctx->audio_source != NULL && lv_obj_is_valid(ctx->audio_source)) {
        lv_obj_add_flag(ctx->audio_source, LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->mic_direction != NULL && lv_obj_is_valid(ctx->mic_direction)) {
        lv_obj_add_flag(ctx->mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
    stt_view_update_waveicon(ctx->waveicon, false);

    ui_widget_set_visible(UI_WIDGET(ctx->function_menu), true);
    menu_obj = container_get_obj(ctx->function_menu);
    if (menu_obj != NULL) {
        lv_obj_move_foreground(menu_obj);
    }
    return true;
}

void speech_function_menu_hide(void) {
    speech_view_context_t* ctx = &s_speech_context;

    if (ctx == NULL || !ctx->function_menu_visible) {
        return;
    }

    ctx->function_menu_visible = false;
    if (ctx->function_menu != NULL) {
        ui_widget_set_visible(UI_WIDGET(ctx->function_menu), false);
    }
    speech_function_menu_restore_status(ctx);
}

bool speech_function_menu_is_visible(void) {
    return s_speech_context.function_menu_visible;
}

/**
 * @brief 处理 Speech 退出确认操作。
 * @param[in] choice 用户选择。
 * @param[in] user_data 未使用。
 * @return 无返回值。
 */
static void speech_exit_msgbox_on_choice(msgbox_choice_t choice, void* user_data) {
    (void)user_data;

    if (choice == MSGBOX_CHOICE_CONFIRM && !speech_is_current_home()) {
        (void)app_router_exit_current_app();
    }
}

/**
 * @brief 显示 Speech 本地退出确认框。
 * @param[in] ctx Speech 页面上下文。
 * @return 无返回值。
 */
static void speech_show_exit_msgbox(speech_view_context_t* ctx) {
    const speech_app_profile_t* view_cfg = speech_config(ctx);

    if (ctx == NULL || view_cfg == NULL) {
        return;
    }

    (void)msgbox_show_local(app_get_str(view_cfg->exit_message_key),
                            app_get_str("MSGBOX_CANCEL"),
                            app_get_str(SPEECH_EXIT_DIALOG_ACTION_KEY),
                            speech_exit_msgbox_on_choice,
                            NULL);
}

static void speech_mode_go_none(speech_view_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    ctx->page_mode = SPEECH_PAGE_MODE_NONE;
    ui_widget_set_visible(UI_WIDGET(ctx->init_hint), false);
    ui_widget_set_visible(UI_WIDGET(ctx->overlay_ui.mask), false);
    ui_widget_set_visible(UI_WIDGET(ctx->action_hint), false);
    if (ctx->audio_source != NULL && lv_obj_is_valid(ctx->audio_source)) {
        lv_obj_add_flag(ctx->audio_source, LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->mic_direction != NULL && lv_obj_is_valid(ctx->mic_direction)) {
        lv_obj_add_flag(ctx->mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
    ui_widget_set_visible(UI_WIDGET(ctx->content), false);
    ui_widget_set_visible(UI_WIDGET(ctx->transcribe_lang), false);
    ui_widget_set_visible(UI_WIDGET(ctx->translate_lang), false);
    ui_widget_set_visible(UI_WIDGET(ctx->scroll), false);
    stt_view_update_waveicon(ctx->waveicon, false);
}

static void speech_mode_show_description(speech_view_context_t* ctx, const char* text) {
    if (ctx == NULL) {
        return;
    }

    ctx->page_mode = SPEECH_PAGE_MODE_DESCRIPTION;
    ui_widget_set_visible(UI_WIDGET(ctx->init_hint), true);
    ui_widget_set_visible(UI_WIDGET(ctx->overlay_ui.mask), false);
    if (ctx->init_hint != NULL) {
        label_set_text(ctx->init_hint, text != NULL ? text : "");
    }
    stt_view_update_audio_source(ctx->audio_source);
    stt_view_update_mic_direction(ctx->mic_direction);
    ui_widget_set_visible(UI_WIDGET(ctx->content), false);
    speech_update_lang_hint();
    ui_widget_set_visible(UI_WIDGET(ctx->scroll), false);
    stt_view_update_waveicon(ctx->waveicon, false);
}

static void speech_mode_go_stt(speech_view_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    ctx->page_mode = SPEECH_PAGE_MODE_STT;
    ui_widget_set_visible(UI_WIDGET(ctx->init_hint), false);
    ui_widget_set_visible(UI_WIDGET(ctx->overlay_ui.mask), false);
    stt_view_update_audio_source(ctx->audio_source);
    stt_view_update_mic_direction(ctx->mic_direction);
    ui_widget_set_visible(UI_WIDGET(ctx->content), true);
    speech_update_lang_hint();
    ui_widget_set_visible(UI_WIDGET(ctx->scroll), true);
    if (ctx->content != NULL) {
        lv_obj_update_layout(container_get_obj(ctx->content));
    }
    stt_view_update_waveicon(ctx->waveicon, true);
}

static void speech_stt_init_row(speech_stt_row_t* row, lv_obj_t* parent) {
    if (row == NULL || parent == NULL) {
        return;
    }
    if (row->row != NULL) {
        return;
    }

    floatair_assert(speech_stt_row_init_ui(parent, &row->ui), "speech stt row ui create failed");
    row->row = row->ui.row;
    floatair_assert(row->row != NULL, "speech stt row NULL");
    row->left_icon = row->ui.left_icon;
    row->first_label = row->ui.first_label;
    row->second_label = row->ui.second_label;
    row->bubble_label = row->ui.bubble_label;
    row->right_icon = row->ui.right_icon;
    floatair_assert(row->left_icon != NULL, "speech stt left_icon NULL");
    floatair_assert(row->first_label != NULL, "speech stt first_label NULL");
    floatair_assert(row->second_label != NULL, "speech stt second_label NULL");
    floatair_assert(row->bubble_label != NULL, "speech stt bubble_label NULL");
    floatair_assert(row->right_icon != NULL, "speech stt right_icon NULL");
    ui_widget_set_visible(UI_WIDGET(row->row), false);
}

static void speech_stt_reset_row_labels(speech_view_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (ctx->rows[i].left_icon != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->rows[i].left_icon), false);
        }
        if (ctx->rows[i].first_label != NULL) {
            label_set_text(ctx->rows[i].first_label, "");
            ui_widget_set_visible(UI_WIDGET(ctx->rows[i].first_label), false);
        }
        if (ctx->rows[i].second_label != NULL) {
            label_set_text(ctx->rows[i].second_label, "");
            ui_widget_set_visible(UI_WIDGET(ctx->rows[i].second_label), false);
        }
        if (ctx->rows[i].bubble_label != NULL) {
            label_set_text(ctx->rows[i].bubble_label, "");
            ui_widget_set_visible(UI_WIDGET(ctx->rows[i].bubble_label), false);
        }
        if (ctx->rows[i].right_icon != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->rows[i].right_icon), false);
        }
    }
}

static void speech_stt_update_bordered_text(speech_stt_row_t* row,
                                            const char* first,
                                            const char* second) {
    size_t first_len = first != NULL ? strlen(first) : 0;
    size_t second_len = second != NULL ? strlen(second) : 0;
    size_t required_capacity = 0;
    size_t new_capacity = 0;
    char* resized_text = NULL;

    if (row == NULL || row->bubble_label == NULL) {
        return;
    }
    if (first_len == 0 || second_len == 0) {
        stt_view_update_incremental_text(row->bubble_label, first_len > 0 ? first : second);
        return;
    }
    if (first_len > SIZE_MAX - second_len - 2) {
        stt_view_update_incremental_text(row->bubble_label, first);
        return;
    }

    required_capacity = first_len + second_len + 2;
    if (row->bubble_text_capacity < required_capacity) {
        new_capacity = row->bubble_text_capacity > 0 ? row->bubble_text_capacity : 128;
        while (new_capacity < required_capacity) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = required_capacity;
                break;
            }
            new_capacity *= 2;
        }
        resized_text = realloc(row->bubble_text, new_capacity);
        if (resized_text == NULL) {
            floatair_warn("resize speech bubble text failed: %zu", new_capacity);
            stt_view_update_incremental_text(row->bubble_label, first);
            return;
        }
        row->bubble_text = resized_text;
        row->bubble_text_capacity = new_capacity;
    }

    memcpy(row->bubble_text, first, first_len);
    row->bubble_text[first_len] = '\n';
    memcpy(row->bubble_text + first_len + 1, second, second_len);
    row->bubble_text[first_len + second_len + 1] = '\0';
    stt_view_update_incremental_text(row->bubble_label, row->bubble_text);
}

static void speech_stt_update_row(speech_stt_row_t* row,
                                  const char* first,
                                  lv_base_dir_t first_dir,
                                  const char* second,
                                  lv_base_dir_t second_dir,
                                  uint32_t area,
                                  bool is_bottom_most,
                                  const speech_app_profile_t* profile) {
    lv_obj_t* row_obj = NULL;
    bool has_first = (first != NULL && first[0] != '\0');
    bool has_second = (second != NULL && second[0] != '\0');
    bool bordered = profile != NULL && profile->bordered_stt;
    const void* icon_src = NULL;
    lv_coord_t bubble_max_width = SPEECH_BORDERED_MAX_WIDTH;
    lv_base_dir_t bordered_dir = has_first ? first_dir : second_dir;

    if (row == NULL || row->row == NULL || (!has_first && !has_second)) {
        if (row != NULL && row->row != NULL) {
            ui_widget_set_visible(UI_WIDGET(row->row), false);
        }
        return;
    }

    row_obj = container_get_obj(row->row);
    if (row_obj == NULL) {
        ui_widget_set_visible(UI_WIDGET(row->row), false);
        return;
    }

    ui_widget_set_size(UI_WIDGET(row->row), LV_PCT(100), LV_SIZE_CONTENT);
    ui_widget_set_visible(UI_WIDGET(row->row), true);
    if (bordered) {
        if (profile->stt_icons) {
            icon_src = area == 1 ? UI_RES_IMAGE_SPEECH_USER : UI_RES_IMAGE_SPEECH_AI;
        }
        container_set_layout_hbox(row->row);
        container_set_spacing(row->row, icon_src != NULL ? SPEECH_BORDERED_ICON_GAP : 0);
        container_set_align(row->row,
                            area == 1 ? CONTAINER_ALIGN_END : CONTAINER_ALIGN_START,
                            CONTAINER_ALIGN_CENTER,
                            CONTAINER_ALIGN_CENTER);
        if (area != 1 && icon_src != NULL) {
            img_set_src(row->left_icon, icon_src);
            ui_widget_set_visible(UI_WIDGET(row->left_icon), true);
        } else {
            ui_widget_set_visible(UI_WIDGET(row->left_icon), false);
        }
        label_set_text(row->first_label, "");
        label_set_text(row->second_label, "");
        ui_widget_set_visible(UI_WIDGET(row->first_label), false);
        ui_widget_set_visible(UI_WIDGET(row->second_label), false);
        stt_view_apply_stt_label_text_theme(row->bubble_label, bordered_dir);
        ui_widget_set_size(UI_WIDGET(row->bubble_label), LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(label_get_obj(row->bubble_label),
                                   bubble_max_width,
                                   LV_PART_MAIN);
        speech_stt_update_bordered_text(row, first, second);
        ui_widget_set_visible(UI_WIDGET(row->bubble_label), true);
        stt_view_apply_stt_label_theme(row->bubble_label,
                                       bordered_dir,
                                       is_bottom_most);
        if (area == 1 && icon_src != NULL) {
            img_set_src(row->right_icon, icon_src);
            ui_widget_set_visible(UI_WIDGET(row->right_icon), true);
        } else {
            ui_widget_set_visible(UI_WIDGET(row->right_icon), false);
        }
        lv_obj_update_layout(row_obj);
        return;
    }
    container_set_layout_vbox(row->row);
    container_set_spacing(row->row, has_first && has_second ? get_system_font_row_space() : 0);
    container_set_align(row->row,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START);
    ui_widget_set_visible(UI_WIDGET(row->left_icon), false);
    ui_widget_set_visible(UI_WIDGET(row->bubble_label), false);
    ui_widget_set_visible(UI_WIDGET(row->right_icon), false);

    if (has_first) {
        stt_view_apply_stt_label_text_theme(row->first_label, first_dir);
        stt_view_update_incremental_text(row->first_label, first);
        ui_widget_set_size(UI_WIDGET(row->first_label), LV_PCT(100), LV_SIZE_CONTENT);
        ui_widget_set_visible(UI_WIDGET(row->first_label), true);
        stt_view_apply_stt_label_theme(row->first_label, first_dir, is_bottom_most);
    } else {
        label_set_text(row->first_label, "");
        ui_widget_set_visible(UI_WIDGET(row->first_label), false);
    }

    if (has_second) {
        stt_view_apply_stt_label_text_theme(row->second_label, second_dir);
        stt_view_update_incremental_text(row->second_label, second);
        ui_widget_set_size(UI_WIDGET(row->second_label), LV_PCT(100), LV_SIZE_CONTENT);
        ui_widget_set_visible(UI_WIDGET(row->second_label), true);
        stt_view_apply_stt_label_theme(row->second_label, second_dir, is_bottom_most);
    } else {
        label_set_text(row->second_label, "");
        ui_widget_set_visible(UI_WIDGET(row->second_label), false);
    }

    lv_obj_update_layout(row_obj);
}

static void speech_stt_init_rows(speech_view_context_t* ctx) {
    lv_obj_t* scroll_obj = NULL;

    if (ctx == NULL || ctx->scroll == NULL) {
        return;
    }

    scroll_obj = container_get_obj(ctx->scroll);
    if (scroll_obj == NULL) {
        return;
    }

    if (ctx->scroll_spacer == NULL) {
        ctx->scroll_spacer = stt_view_create_plain_container(scroll_obj, LV_PCT(100), 0);
        floatair_assert(ctx->scroll_spacer != NULL, "speech_scroll_spacer NULL");
        container_set_child_grow(container_get_obj(ctx->scroll_spacer), 1);
    }

    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        speech_stt_init_row(&ctx->rows[i], scroll_obj);
    }
}

static void speech_stt_hide_all_rows(speech_view_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (ctx->rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->rows[i].row), false);
        }
    }
}

static lv_base_dir_t speech_text_base_dir(bool is_source) {
    uint8_t direction = is_source ? stt_config.sourceTextDirection : stt_config.targetTextDirection;

    return direction == TEXT_DIRECTION_RTL ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR;
}

static void speech_stt_update_now(void) {
    speech_view_context_t* ctx = &s_speech_context;
    const speech_app_profile_t* cfg = speech_config(ctx);
    size_t visible_row_count = 0;
    const char* center_text = NULL;
    int latest_normal_index = -1;
    int start_index = 0;

    if (ctx == NULL || cfg == NULL) {
        return;
    }

    speech_update_lang_hint();

    if (ctx->scroll == NULL) {
        floatair_info("speech_scroll == NULL");
        return;
    }

    if (container_get_obj(ctx->scroll) == NULL) {
        floatair_info("speech_scroll obj == NULL");
        return;
    }

    if (stt_size() == 0) {
        floatair_info("stt_size() == 0");
        speech_stt_hide_all_rows(ctx);
        ui_widget_set_visible(UI_WIDGET(ctx->scroll), false);
        if (ctx->page_mode != SPEECH_PAGE_MODE_NONE) {
            stt_view_update_audio_source(ctx->audio_source);
            stt_view_update_mic_direction(ctx->mic_direction);
        }
        return;
    }

    for (int index = 0; index < stt_size(); index++) {
        const char* text = stt_buffer_get_transcribe_by_index(index);
        uint32_t area = stt_buffer_get_area_by_index(index);

        if (text == NULL || text[0] == '\0') {
            continue;
        }
        if (area == STT_INFO_AREA_CENTER && center_text == NULL) {
            center_text = text;
            continue;
        }
        if (area != STT_INFO_AREA_CENTER) {
            latest_normal_index = index;
            break;
        }
    }

    if (latest_normal_index < 0 && center_text != NULL) {
        speech_stt_hide_all_rows(ctx);
        ui_widget_set_visible(UI_WIDGET(ctx->scroll), false);
        speech_mode_show_description(ctx, center_text);
        return;
    }

    if (ctx->page_mode != SPEECH_PAGE_MODE_STT) {
        speech_mode_go_stt(ctx);
    } else {
        stt_view_update_audio_source(ctx->audio_source);
        stt_view_update_mic_direction(ctx->mic_direction);
    }

    speech_stt_init_rows(ctx);
    start_index = (stt_config.textMode != TEXTMODE_HISTORY) ? latest_normal_index : (stt_size() - 1);

    for (int index = start_index;
         index != -1 && visible_row_count < STT_INFO_MAX_MSG_NUM;
         index--) {
        const char* transcribe_text = stt_buffer_get_transcribe_by_index(index);
        const char* translate_text = stt_buffer_get_translate_by_index(index);
        const char* first = transcribe_text;
        const char* second = NULL;
        lv_base_dir_t first_dir = speech_text_base_dir(true);
        lv_base_dir_t second_dir = speech_text_base_dir(false);
        uint32_t area = stt_buffer_get_area_by_index(index);
        bool is_bottom_most = false;

        if (area == STT_INFO_AREA_CENTER) {
            continue;
        }

        if (stt_config.transMode == TRANSMODE_SHOW_ONLY_TRANS) {
            if (translate_text != NULL && translate_text[0] != '\0') {
                first = translate_text;
                first_dir = speech_text_base_dir(false);
            }
        } else if (stt_config.transMode == TRANSMODE_SHOW_ONLY_ORI) {
            if (transcribe_text == NULL || transcribe_text[0] == '\0') {
                first = translate_text;
                first_dir = speech_text_base_dir(false);
            }
        } else {
            first = transcribe_text;
            second = translate_text;
            first_dir = speech_text_base_dir(true);
            second_dir = speech_text_base_dir(false);
        }

        if ((first == NULL || first[0] == '\0') &&
            (second == NULL || second[0] == '\0')) {
            continue;
        }

        is_bottom_most = (stt_config.textMode != TEXTMODE_HISTORY) || (index == latest_normal_index);
        speech_stt_update_row(&ctx->rows[visible_row_count],
                              first,
                              first_dir,
                              second,
                              second_dir,
                              area,
                              is_bottom_most,
                              cfg);
        ++visible_row_count;
        if (stt_config.textMode != TEXTMODE_HISTORY) {
            break;
        }
    }

    for (size_t i = visible_row_count; i < STT_INFO_MAX_MSG_NUM; ++i) {
        if (ctx->rows[i].row != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->rows[i].row), false);
        }
    }

    container_scroll_to_bottom(ctx->scroll, LV_ANIM_OFF);
}

void speech_stt_update(void) {
    speech_stt_cancel_pending_update();
    speech_stt_update_now();
}

void speech_stt_request_update(void) {
    stt_view_request_deferred_update(&s_speech_stt_deferred_update,
                                     "speech",
                                     speech_stt_deferred_update_cb,
                                     &s_speech_context);
}

void speech_stt_clear(void) {
    speech_view_context_t* ctx = &s_speech_context;

    speech_stt_cancel_pending_update();
    stt_buffer_init();
    (void)speech_set_header_text(NULL, false);
    if (ctx != NULL && ctx->scroll != NULL) {
        speech_stt_hide_all_rows(ctx);
        ui_widget_set_visible(UI_WIDGET(ctx->scroll), false);
    }
}

static void speech_touch_event_handle(lv_event_t* event) {
    speech_view_context_t* ctx = (speech_view_context_t*)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    bool can_scroll = false;

    if (ctx == NULL) {
        return;
    }

    if (ctx->function_menu_visible) {
        switch (code) {
        case LV_EVENT_GESTURE_LEFT:
        case LV_EVENT_GESTURE_RIGHT:
        case LV_EVENT_CLICKED:
            (void)system_report_touch_event(code);
            return;
        default:
            break;
        }
    }

    can_scroll = (ctx->page_mode == SPEECH_PAGE_MODE_STT &&
                  ctx->scroll != NULL &&
                  stt_size() > 0 &&
                  stt_buffer_get_is_final_by_index(0));

    switch (code) {
    case LV_EVENT_GESTURE_LEFT:
        if (ctx->page_mode == SPEECH_PAGE_MODE_STT) {
            if (ctx->scroll == NULL) {
                floatair_info("speech_scroll == NULL");
                break;
            }
            if (can_scroll) {
                container_scroll_up(ctx->scroll, 1.0f / 2.0f, LV_ANIM_ON);
            }
        }
        break;
    case LV_EVENT_GESTURE_RIGHT:
        if (ctx->page_mode == SPEECH_PAGE_MODE_STT) {
            if (ctx->scroll == NULL) {
                floatair_info("speech_scroll == NULL");
                break;
            }
            if (can_scroll) {
                container_scroll_down(ctx->scroll, 1.0f / 2.0f, LV_ANIM_ON);
            }
        }
        break;
    case LV_EVENT_CLICKED:
        system_report_touch_event(code);
        break;
    case LV_EVENT_LONG_PRESSED:
        system_report_touch_event(code);
        break;
    case LV_EVENT_DCLICKED:
        if (speech_report_double_click_enabled()) {
            (void)system_report_touch_event(code);
            break;
        }
        if (!speech_is_current_home()) {
            if (speech_exit_double_click_confirm_enabled() &&
                ctx->speech_state == SPEECH_STATE_STARTED) {
                speech_show_exit_msgbox(ctx);
            } else {
                (void)app_router_exit_current_app();
            }
        }
        break;
    default:
        break;
    }
}

void speech_on_fontconfig_changed(void) {
    speech_view_context_t* ctx = &s_speech_context;
    const speech_app_profile_t* cfg = speech_config(ctx);
    lv_obj_t* action_obj = NULL;
    lv_obj_t* lang_obj = NULL;

    if (ctx == NULL || cfg == NULL) {
        return;
    }

    stt_style_init();

    stt_view_apply_text_theme(ctx->init_hint, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    stt_view_apply_status_hint_text_theme(ctx->overlay_ui.indicator);
    stt_view_apply_text_theme(ctx->action_hint, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    speech_apply_header_theme(ctx->header_text);
    speech_function_menu_apply_roller_cfg(ctx);
    speech_sync_content_padding(ctx);
    action_obj = label_get_obj(ctx->action_hint);
    if (action_obj != NULL) {
        lv_obj_set_size(action_obj, LV_PCT(100), get_system_font_height());
        lv_obj_align(action_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    speech_sync_lang_hint_layout(ctx);
    if (ctx->transcribe_lang != NULL) {
        stt_view_apply_lang_hint_text_theme(ctx->transcribe_lang,
                                            SPEECH_LANG_HINT_PADDING,
                                            speech_text_base_dir(true));
    }
    if (ctx->translate_lang_source != NULL) {
        stt_view_apply_lang_hint_text_theme(ctx->translate_lang_source,
                                            SPEECH_LANG_HINT_PADDING,
                                            speech_text_base_dir(true));
    }
    if (ctx->translate_lang_target != NULL) {
        stt_view_apply_lang_hint_text_theme(ctx->translate_lang_target,
                                            SPEECH_LANG_HINT_PADDING,
                                            speech_text_base_dir(false));
    }
    if (ctx->content != NULL) {
        lv_obj_move_foreground(container_get_obj(ctx->content));
    }
    if (ctx->top_hint_mode == SPEECH_TOP_HINT_TEXT && ctx->header_text != NULL) {
        lang_obj = label_get_obj(ctx->header_text);
    } else if (ctx->top_hint_mode == SPEECH_TOP_HINT_LANGUAGE) {
        lang_obj = speech_get_lang_hint_obj(ctx);
    }
    if (lang_obj != NULL) {
        lv_obj_move_foreground(lang_obj);
    }
    if (action_obj != NULL) {
        lv_obj_move_foreground(action_obj);
    }
    if (ctx->function_menu_visible && ctx->function_menu != NULL) {
        lv_obj_move_foreground(container_get_obj(ctx->function_menu));
    }
    speech_update_lang_hint();
    speech_stt_reset_row_labels(ctx);
    if (ctx->page_mode == SPEECH_PAGE_MODE_STT) {
        speech_stt_update();
    }
}

static void speech_update_transcribe_lang_hint(speech_view_context_t* ctx) {
    lv_base_dir_t base_dir = LV_BASE_DIR_LTR;

    if (ctx->transcribe_lang == NULL) {
        floatair_err("speech transcribe_lang is NULL");
        return;
    }

    if (stt_config.language_hint == 0 && stt_config.language_source[0] != '\0') {
        base_dir = speech_text_base_dir(true);
        label_set_text(ctx->transcribe_lang, stt_config.language_source);
        stt_view_apply_lang_hint_theme(ctx->transcribe_lang, SPEECH_LANG_HINT_PADDING, base_dir);
        ui_widget_set_visible(UI_WIDGET(ctx->transcribe_lang), true);
        lv_obj_move_foreground(label_get_obj(ctx->transcribe_lang));
        speech_sync_content_padding(ctx);
        if (ctx->content != NULL) {
            lv_obj_update_layout(container_get_obj(ctx->content));
        }
        return;
    }

    label_set_text(ctx->transcribe_lang, "");
    ui_widget_set_visible(UI_WIDGET(ctx->transcribe_lang), false);
    speech_sync_content_padding(ctx);
    if (ctx->content != NULL) {
        lv_obj_update_layout(container_get_obj(ctx->content));
    }
}

static void speech_update_translate_lang_hint(speech_view_context_t* ctx) {
    lv_obj_t* lang_obj = NULL;
    lv_obj_t* lang_row_obj = NULL;
    bool dual_language = false;

    if (ctx->translate_lang == NULL) {
        floatair_err("speech translate_lang is NULL");
        return;
    }

    lang_obj = container_get_obj(ctx->translate_lang);
    if (lang_obj == NULL) {
        floatair_err("speech translate_lang obj is NULL");
        return;
    }

    dual_language = stt_config.language_hint == 1 &&
                    stt_config.language_target[0] != '\0';
    if (stt_config.language_source[0] != '\0' &&
        (stt_config.language_hint == 0 || dual_language)) {
        if (ctx->translate_lang_row == NULL) {
            ctx->translate_lang_row = stt_view_create_plain_container(lang_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            floatair_assert(ctx->translate_lang_row != NULL, "speech_translate_lang_row NULL");
            container_set_layout_hbox_spaced(ctx->translate_lang_row, SPEECH_LANG_HINT_PADDING);
            container_set_align(ctx->translate_lang_row,
                                CONTAINER_ALIGN_CENTER,
                                CONTAINER_ALIGN_CENTER,
                                CONTAINER_ALIGN_CENTER);
        }

        lang_row_obj = container_get_obj(ctx->translate_lang_row);
        if (ctx->translate_lang_source == NULL) {
            ctx->translate_lang_source = stt_view_create_text_label(lang_row_obj,
                                                                    LV_SIZE_CONTENT,
                                                                    LV_SIZE_CONTENT,
                                                                    "",
                                                                    LABEL_ALIGN_CENTER,
                                                                    LABEL_OVERFLOW_SCROLL_CIRCULAR);
            floatair_assert(ctx->translate_lang_source != NULL, "speech_translate_lang_source NULL");
            stt_view_apply_lang_hint_text_theme(ctx->translate_lang_source,
                                                SPEECH_LANG_HINT_PADDING,
                                                speech_text_base_dir(true));
        }
        if (dual_language && ctx->translate_lang_icon == NULL) {
            ctx->translate_lang_icon = img_create(lang_row_obj, NULL);
            floatair_assert(ctx->translate_lang_icon != NULL, "speech_translate_lang_icon NULL");
            img_set_src(ctx->translate_lang_icon, UI_RES_IMAGE_SWITCH);
        }
        if (dual_language && ctx->translate_lang_target == NULL) {
            ctx->translate_lang_target = stt_view_create_text_label(lang_row_obj,
                                                                    LV_SIZE_CONTENT,
                                                                    LV_SIZE_CONTENT,
                                                                    "",
                                                                    LABEL_ALIGN_CENTER,
                                                                    LABEL_OVERFLOW_SCROLL_CIRCULAR);
            floatair_assert(ctx->translate_lang_target != NULL, "speech_translate_lang_target NULL");
            stt_view_apply_lang_hint_text_theme(ctx->translate_lang_target,
                                                SPEECH_LANG_HINT_PADDING,
                                                speech_text_base_dir(false));
        }

        label_set_text(ctx->translate_lang_source, stt_config.language_source);
        stt_view_apply_lang_hint_theme(ctx->translate_lang_source,
                                       SPEECH_LANG_HINT_PADDING,
                                       speech_text_base_dir(true));
        ui_widget_set_visible(UI_WIDGET(ctx->translate_lang_source), true);

        if (ctx->translate_lang_icon != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->translate_lang_icon), dual_language);
        }
        if (ctx->translate_lang_target != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->translate_lang_target), dual_language);
        }
        if (dual_language) {
            lv_obj_move_to_index(ui_widget_get_obj(UI_WIDGET(ctx->translate_lang_icon)), 1);
            label_set_text(ctx->translate_lang_target, stt_config.language_target);
            stt_view_apply_lang_hint_theme(ctx->translate_lang_target,
                                           SPEECH_LANG_HINT_PADDING,
                                           speech_text_base_dir(false));
            ui_widget_set_size(UI_WIDGET(ctx->translate_lang_target),
                               LV_SIZE_CONTENT,
                               LV_SIZE_CONTENT);
        }

        ui_widget_set_visible(UI_WIDGET(ctx->translate_lang), true);
        lv_obj_move_foreground(lang_obj);
        ui_widget_set_size(UI_WIDGET(ctx->translate_lang_source), LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_update_layout(lang_row_obj);
        speech_sync_content_padding(ctx);
        if (ctx->content != NULL) {
            lv_obj_update_layout(container_get_obj(ctx->content));
        }
        return;
    }

    ui_widget_set_visible(UI_WIDGET(ctx->translate_lang), false);
    speech_sync_content_padding(ctx);
    if (ctx->content != NULL) {
        lv_obj_update_layout(container_get_obj(ctx->content));
    }
}

void speech_update_lang_hint(void) {
    speech_view_context_t* ctx = &s_speech_context;

    if (ctx == NULL) {
        return;
    }

    if (ctx->top_hint_mode != SPEECH_TOP_HINT_LANGUAGE) {
        if (ctx->transcribe_lang != NULL) {
            label_set_text(ctx->transcribe_lang, "");
            ui_widget_set_visible(UI_WIDGET(ctx->transcribe_lang), false);
        }
        if (ctx->translate_lang != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->translate_lang), false);
        }
        speech_sync_content_padding(ctx);
        if (ctx->content != NULL) {
            lv_obj_update_layout(container_get_obj(ctx->content));
        }
        return;
    }

    if (stt_config.language_hint == 1) {
        if (ctx->transcribe_lang != NULL) {
            label_set_text(ctx->transcribe_lang, "");
            ui_widget_set_visible(UI_WIDGET(ctx->transcribe_lang), false);
        }
        speech_update_translate_lang_hint(ctx);
    } else {
        if (ctx->translate_lang != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->translate_lang), false);
        }
        speech_update_transcribe_lang_hint(ctx);
    }
}

void speech_activate_language_hint(void) {
    speech_view_context_t* ctx = &s_speech_context;

    if (ctx == NULL) {
        return;
    }

    ctx->top_hint_mode = SPEECH_TOP_HINT_LANGUAGE;
    if (ctx->header_text != NULL) {
        label_set_text(ctx->header_text, "");
        ui_widget_set_visible(UI_WIDGET(ctx->header_text), false);
    }
}

bool speech_set_state(uint8_t state) {
    speech_view_context_t* ctx = &s_speech_context;
    const speech_app_profile_t* profile = speech_config(ctx);
    bool is_translate = profile != NULL && profile->config_name != NULL &&
                        strcmp(profile->config_name, "translate") == 0;
    const char* text_key = NULL;
    bool kws_intercepted = false;

    switch (state) {
    case SPEECH_STATE_PAUSED:
        text_key = is_translate ? "TRANSLATE_TAP_START" : "TRANSCRIBE_TAP_START";
        kws_intercepted = false;
        break;
    case SPEECH_STATE_STARTED:
        text_key = is_translate ? "TRANSLATE_TAP_PAUSE" : "TRANSCRIBE_TAP_PAUSE";
        kws_intercepted = true;
        break;
    default:
        floatair_err("invalid speech state: %u", (unsigned int)state);
        return false;
    }

    ctx->speech_state = state;
    ctx->speech_state_received = true;

    system_runtime_state_set_kws_intercept(
        SYSTEM_KWS_INTERCEPT_REASON_SPEECH_ACTIVE,
        kws_intercepted);

    if (ctx != NULL && ctx->action_hint != NULL) {
        label_set_text(ctx->action_hint, app_get_str(text_key));
        ui_widget_set_visible(UI_WIDGET(ctx->action_hint), true);
    }
    return true;
}

bool speech_set_header_text(const char* text, bool visible) {
    speech_view_context_t* ctx = &s_speech_context;
    lv_obj_t* content_obj = NULL;
    lv_obj_t* header_obj = NULL;

    if (ctx == NULL || ctx->header_text == NULL ||
        (visible && (text == NULL || text[0] == '\0'))) {
        return false;
    }

    if (visible) {
        ctx->top_hint_mode = SPEECH_TOP_HINT_TEXT;
        if (ctx->transcribe_lang != NULL) {
            label_set_text(ctx->transcribe_lang, "");
            ui_widget_set_visible(UI_WIDGET(ctx->transcribe_lang), false);
        }
        if (ctx->translate_lang != NULL) {
            ui_widget_set_visible(UI_WIDGET(ctx->translate_lang), false);
        }
        label_set_text(ctx->header_text, text);
        ui_widget_set_visible(UI_WIDGET(ctx->header_text), true);
        speech_apply_header_theme(ctx->header_text);
        header_obj = label_get_obj(ctx->header_text);
        if (header_obj != NULL) {
            lv_obj_update_layout(header_obj);
            lv_obj_move_foreground(header_obj);
        }
    } else {
        label_set_text(ctx->header_text, "");
        ui_widget_set_visible(UI_WIDGET(ctx->header_text), false);
        if (ctx->top_hint_mode == SPEECH_TOP_HINT_TEXT) {
            ctx->top_hint_mode = SPEECH_TOP_HINT_NONE;
        }
    }
    speech_sync_content_padding(ctx);
    if (ctx->content != NULL) {
        content_obj = container_get_obj(ctx->content);
        if (content_obj != NULL) {
            lv_obj_update_layout(content_obj);
        }
    }
    return true;
}

static void speech_progress_hint_event_handle(lv_event_t* event) {
    speech_view_context_t* ctx = (speech_view_context_t*)lv_event_get_user_data(event);
    const system_progress_hint_param_t* param =
        (const system_progress_hint_param_t*)lv_event_get_param(event);

    if (ctx == NULL || param == NULL) {
        return;
    }

    if (param->visible) {
        speech_function_menu_hide();
    }
    speech_set_progress_overlay_visible(ctx, param->visible, param->text, param->bg_opa);
}

static void speech_view_sync_layout(speech_view_context_t* ctx) {
    lv_obj_t* init_hint_obj = NULL;
    lv_obj_t* mask_obj = NULL;

    if (ctx == NULL || ctx->root == NULL || !lv_obj_is_valid(ctx->root)) {
        return;
    }

    if (ctx->init_hint != NULL) {
        init_hint_obj = label_get_obj(ctx->init_hint);
        if (init_hint_obj != NULL) {
            lv_obj_set_size(init_hint_obj, (lv_coord_t)config_lcd.ui_width, LV_SIZE_CONTENT);
            lv_obj_align(init_hint_obj, LV_ALIGN_CENTER, 0, 0);
        }
    }
    if (ctx->action_hint != NULL) {
        lv_obj_set_size(label_get_obj(ctx->action_hint), (lv_coord_t)config_lcd.ui_width, get_system_font_height());
        lv_obj_align(label_get_obj(ctx->action_hint), LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (ctx->scroll != NULL) {
        lv_obj_set_width(container_get_obj(ctx->scroll), (lv_coord_t)config_lcd.ui_width);
    }
    if (ctx->header_text != NULL) {
        lv_obj_t* header_obj = label_get_obj(ctx->header_text);
        lv_obj_set_size(header_obj, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_align(header_obj, LV_ALIGN_TOP_LEFT, 0, SPEECH_SCROLL_TOP_MARGIN);
    }
    if (ctx->overlay_ui.mask != NULL) {
        mask_obj = container_get_obj(ctx->overlay_ui.mask);
        if (mask_obj != NULL) {
            lv_obj_set_size(mask_obj, LV_PCT(100), LV_PCT(100));
        }
    }
    if (ctx->function_menu != NULL) {
        lv_obj_t* menu_obj = container_get_obj(ctx->function_menu);
        if (menu_obj != NULL) {
            lv_obj_set_size(menu_obj, LV_PCT(100), LV_PCT(100));
        }
    }
    if (roller_is_valid(ctx->function_menu_roller)) {
        lv_coord_t roller_width =
            (lv_coord_t)config_lcd.ui_width - SPEECH_FUNCTION_MENU_SIDE_PADDING * 2;
        lv_obj_t* roller_obj = roller_get_obj(ctx->function_menu_roller);
        lv_obj_set_width(roller_obj, roller_width > 0 ? roller_width : 0);
        lv_obj_align(roller_obj, LV_ALIGN_CENTER, 0, 0);
    }
    speech_sync_content_padding(ctx);
    speech_sync_lang_hint_layout(ctx);
}

static void speech_create_lang_hint(speech_view_context_t* ctx, lv_obj_t* parent) {
    lv_obj_t* lang_obj = NULL;

    if (ctx == NULL || parent == NULL) {
        return;
    }

    ctx->transcribe_lang = stt_view_create_text_label(parent,
                                                      LV_SIZE_CONTENT,
                                                      LV_SIZE_CONTENT,
                                                      "",
                                                      LABEL_ALIGN_CENTER,
                                                      LABEL_OVERFLOW_SCROLL_CIRCULAR);
    floatair_assert(ctx->transcribe_lang != NULL, "speech transcribe_lang NULL");
    stt_view_apply_lang_hint_text_theme(ctx->transcribe_lang,
                                        SPEECH_LANG_HINT_PADDING,
                                        speech_text_base_dir(true));
    ui_widget_set_visible(UI_WIDGET(ctx->transcribe_lang), false);

    ctx->translate_lang = stt_view_create_plain_container(parent, LV_PCT(100), LV_SIZE_CONTENT);
    floatair_assert(ctx->translate_lang != NULL, "speech translate_lang NULL");
    container_set_layout_hbox(ctx->translate_lang);
    container_set_align(ctx->translate_lang,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    container_set_padding(ctx->translate_lang, 0, 3);
    lang_obj = container_get_obj(ctx->translate_lang);
    lv_obj_set_style_min_height(lang_obj, STATUS_BAR_IMG_H, 0);
    ui_widget_set_visible(UI_WIDGET(ctx->translate_lang), false);
    speech_sync_lang_hint_layout(ctx);
}

static void speech_function_menu_create(speech_view_context_t* ctx, lv_obj_t* parent) {
    const char* initial_items[] = {""};
    roller_cfg_t roller_cfg = roller_default_cfg();
    lv_obj_t* menu_obj = NULL;
    lv_obj_t* roller_obj = NULL;
    lv_coord_t item_width;

    if (ctx == NULL || parent == NULL) {
        return;
    }

    ctx->function_menu = stt_view_create_plain_container(parent, LV_PCT(100), LV_PCT(100));
    floatair_assert(ctx->function_menu != NULL, "speech function menu NULL");
    menu_obj = container_get_obj(ctx->function_menu);
    container_set_padding(ctx->function_menu, 0, 0);
    container_set_opacity(ctx->function_menu, LV_OPA_COVER);
    lv_obj_set_style_bg_color(menu_obj, lv_color_black(), LV_PART_MAIN);

    item_width = (lv_coord_t)config_lcd.ui_width - SPEECH_FUNCTION_MENU_SIDE_PADDING * 2;
    if (item_width < 0) {
        item_width = 0;
    }
    roller_cfg.items = initial_items;
    roller_cfg.count = 1;
    ctx->function_menu_roller = roller_create(menu_obj, &roller_cfg);
    floatair_assert(ctx->function_menu_roller != NULL,
                    "speech function menu roller NULL");
    speech_function_menu_apply_roller_cfg(ctx);
    roller_obj = roller_get_obj(ctx->function_menu_roller);
    lv_obj_set_width(roller_obj, item_width);
    lv_obj_align(roller_obj, LV_ALIGN_CENTER, 0, 0);
    ui_widget_set_visible(UI_WIDGET(ctx->function_menu), false);
}

static void speech_page_create(lv_obj_t* root, const app_page_data_t* data) {
    speech_view_context_t* ctx = speech_context_by_app_name(app_manager_current_name());
    lv_obj_t* status_bar = NULL;
    lv_obj_t* content_obj = NULL;
    lv_obj_t* lang_obj = NULL;
    lv_obj_t* scroll_obj = NULL;
    app_router_entry_t entry_mode = app_router_get_entry_mode();

    (void)data;
    floatair_info("entry");
    floatair_assert(root != NULL, "root NULL");
    floatair_assert(ctx != NULL, "speech context NULL");
    ctx->root = root;
    ctx->top_hint_mode = SPEECH_TOP_HINT_NONE;

    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }

    stt_style_init();

    floatair_assert(speech_start_init_ui(root, &ctx->start_ui), "speech start ui create failed");
    floatair_assert(speech_progress_overlay_init_ui(root, &ctx->overlay_ui),
                    "speech progress overlay ui create failed");
    lv_obj_add_event_cb(root,
                        speech_progress_hint_event_handle,
                        system_ui_get_progress_hint_event(),
                        ctx);

    ctx->init_hint = ctx->start_ui.start_text;
    floatair_assert(ctx->init_hint != NULL, "speech init_hint NULL");
    stt_view_apply_text_theme(ctx->init_hint, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    stt_view_apply_status_hint_text_theme(ctx->overlay_ui.indicator);

    ctx->content = stt_view_create_plain_container(root, LV_PCT(100), LV_PCT(100));
    floatair_assert(ctx->content != NULL, "speech content NULL");
    container_set_layout_vbox(ctx->content);
    container_set_align(ctx->content,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_START);
    speech_sync_content_padding(ctx);
    content_obj = container_get_obj(ctx->content);

    speech_create_lang_hint(ctx, root);

    ctx->header_text = stt_view_create_text_label(root,
                                                   LV_PCT(100),
                                                   LV_SIZE_CONTENT,
                                                   "",
                                                   LABEL_ALIGN_LEFT,
                                                   LABEL_OVERFLOW_WRAP);
    floatair_assert(ctx->header_text != NULL, "speech header_text NULL");
    speech_apply_header_theme(ctx->header_text);
    lv_obj_set_style_pad_left(label_get_obj(ctx->header_text), SPEECH_HEADER_SIDE_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_right(label_get_obj(ctx->header_text), SPEECH_HEADER_SIDE_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label_get_obj(ctx->header_text), SPEECH_HEADER_TOP_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label_get_obj(ctx->header_text), SPEECH_HEADER_BOTTOM_PADDING, LV_PART_MAIN);
    lv_obj_align(label_get_obj(ctx->header_text), LV_ALIGN_TOP_LEFT, 0, SPEECH_SCROLL_TOP_MARGIN);
    ui_widget_set_visible(UI_WIDGET(ctx->header_text), false);

    ctx->action_hint = stt_view_create_text_label(root,
                                                  LV_PCT(100),
                                                  get_system_font_height(),
                                                  "",
                                                  LABEL_ALIGN_CENTER,
                                                  LABEL_OVERFLOW_WRAP);
    floatair_assert(ctx->action_hint != NULL, "speech action_hint NULL");
    stt_view_apply_text_theme(ctx->action_hint, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_WRAP);
    lv_obj_set_size(label_get_obj(ctx->action_hint), LV_PCT(100), get_system_font_height());
    lv_obj_align(label_get_obj(ctx->action_hint), LV_ALIGN_BOTTOM_MID, 0, 0);
    ui_widget_set_visible(UI_WIDGET(ctx->action_hint), false);

    speech_function_menu_create(ctx, root);

    floatair_assert(speech_status_bar_ensure_widgets(ctx), "speech status bar widgets are NULL");

    ctx->scroll = stt_view_create_plain_container(content_obj, LV_PCT(100), 0);
    floatair_assert(ctx->scroll != NULL, "speech scroll NULL");
    scroll_obj = container_get_obj(ctx->scroll);
    container_set_child_grow(scroll_obj, 1);
    container_set_layout_vbox_spaced(ctx->scroll,
                                     ctx->profile != NULL && ctx->profile->bordered_stt
                                         ? SPEECH_BORDERED_ROW_GAP
                                         : get_system_font_row_space());
    container_set_align(ctx->scroll,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_START);
    container_set_padding_box(ctx->scroll,
                              SPEECH_TEXT_SIDE_PADDING,
                              SPEECH_TEXT_SIDE_PADDING,
                              0,
                              0);
    container_set_scrollable(ctx->scroll, true);
    container_set_scroll_dir(ctx->scroll, LV_DIR_VER);
    container_set_scrollbar_mode(ctx->scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_move_foreground(content_obj);
    lang_obj = speech_get_lang_hint_obj(ctx);
    if (lang_obj != NULL) {
        lv_obj_move_foreground(lang_obj);
    }
    lv_obj_move_foreground(label_get_obj(ctx->action_hint));
    speech_stt_init_rows(ctx);

    if (entry_mode == APP_ROUTER_ENTRY_LOCAL || entry_mode == APP_ROUTER_ENTRY_REMOTE) {
        speech_mode_show_description(ctx, "");
    } else {
        speech_mode_go_none(ctx);
    }
    if (ctx->speech_state_received) {
        (void)speech_set_state(ctx->speech_state);
    }

    floatair_info("Done");
}

static void speech_page_appear(lv_obj_t* root) {
    speech_view_context_t* ctx = speech_context_by_app_name(app_manager_current_name());

    floatair_assert(root != NULL, "root NULL");
    floatair_assert(ctx != NULL, "speech context NULL");
    system_status_bar_set_mode(true);
    floatair_assert(speech_status_bar_ensure_widgets(ctx), "speech status bar widgets are NULL");
    speech_view_sync_layout(ctx);
    lv_obj_add_event_cb(root, speech_touch_event_handle, LV_EVENT_GESTURE_LEFT, ctx);
    lv_obj_add_event_cb(root, speech_touch_event_handle, LV_EVENT_GESTURE_RIGHT, ctx);
    lv_obj_add_event_cb(root, speech_touch_event_handle, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(root, speech_touch_event_handle, LV_EVENT_DCLICKED, ctx);
    lv_obj_add_event_cb(root, speech_touch_event_handle, LV_EVENT_LONG_PRESSED, ctx);
    speech_stt_update();
}

static void speech_page_destroy(void) {
    speech_view_context_t* ctx = &s_speech_context;
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (ctx == NULL) {
        return;
    }

    speech_stt_cancel_pending_update();
    for (size_t i = 0; i < STT_INFO_MAX_MSG_NUM; ++i) {
        free(ctx->rows[i].bubble_text);
        ctx->rows[i].bubble_text = NULL;
        ctx->rows[i].bubble_text_capacity = 0;
    }
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_clear_custom_widgets(status_bar);
    }

    ctx->root = NULL;
    ctx->init_hint = NULL;
    ctx->action_hint = NULL;
    msgbox_dismiss_local();
    ctx->audio_source = NULL;
    ctx->mic_direction = NULL;
    ctx->waveicon = NULL;
    ctx->content = NULL;
    ctx->header_text = NULL;
    ctx->transcribe_lang = NULL;
    ctx->translate_lang = NULL;
    ctx->translate_lang_row = NULL;
    ctx->translate_lang_source = NULL;
    ctx->translate_lang_target = NULL;
    ctx->translate_lang_icon = NULL;
    ctx->scroll = NULL;
    ctx->scroll_spacer = NULL;
    ctx->function_menu = NULL;
    ctx->function_menu_roller = NULL;
    ctx->function_menu_visible = false;
    memset(&ctx->start_ui, 0, sizeof(ctx->start_ui));
    memset(&ctx->overlay_ui, 0, sizeof(ctx->overlay_ui));
    memset(ctx->rows, 0, sizeof(ctx->rows));
    ctx->profile = NULL;
    ctx->page_mode = SPEECH_PAGE_MODE_NONE;
    ctx->top_hint_mode = SPEECH_TOP_HINT_NONE;
    ctx->speech_state = SPEECH_STATE_PAUSED;
    ctx->speech_state_received = false;
}

static app_page_t s_speech_page = {
    .name = SPEECH_PAGE_NAME,
    .on_create = speech_page_create,
    .on_appear = speech_page_appear,
    .on_disappear = NULL,
    .on_destroy = speech_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* speech_page_get(void) {
    return &s_speech_page;
}
