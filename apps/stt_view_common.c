/**
 * @file stt_view_common.c
 * @brief STT 文本视图公共辅助实现
 */
#include "stt_view_common.h"

#include "common/widgets/status_bar.h"
#include "system/stt_common.h"
#include "system/system_def.h"
#include "system/system_res.h"
#include "floatair_fs.h"

/**
 * @brief 获取合法 UTF-8 后缀起始位置。
 *
 * @param text 原始文本。
 * @param max_len 允许的最大字节数。
 * @return 返回后缀在 `text` 中的起始下标。
 */
static size_t stt_view_get_utf8_suffix_start(const char* text, size_t max_len) {
    size_t text_len = 0;
    size_t start = 0;

    if (text == NULL) {
        return 0;
    }

    text_len = strlen(text);
    if (text_len <= max_len) {
        return 0;
    }

    start = text_len - max_len;
    while (start < text_len && ((((const unsigned char*)text)[start] & 0xC0U) == 0x80U)) {
        ++start;
    }

    return start;
}

/**
 * @brief 按原文本或截断后的文本刷新 label。
 *
 * @param label 目标文本组件。
 * @param text 最新完整文本。
 * @return 无返回值。
 */
static void stt_view_update_incremental_text_internal(label_t* label, const char* text) {
    lv_obj_t* label_obj = NULL;

    if (label == NULL || text == NULL) {
        return;
    }

    label_obj = label_get_obj(label);
    if (label_obj != NULL) {
        lv_label_set_text(label_obj, text);
    } else {
        label_set_text(label, text);
    }
    if (label_obj != NULL) {
        container_notify_child_layout_changed(label_obj);
    }
}

label_t* stt_view_create_text_label(lv_obj_t* parent,
                                    lv_coord_t width,
                                    lv_coord_t height,
                                    const char* text,
                                    label_align_t align,
                                    label_overflow_t overflow) {
    label_cfg_t cfg = label_default_cfg();

    cfg.w = width;
    cfg.h = height;
    cfg.text = text;
    cfg.align = align;
    cfg.overflow = overflow;

    return label_create(parent, &cfg);
}

container_t* stt_view_create_plain_container(lv_obj_t* parent,
                                             lv_coord_t width,
                                             lv_coord_t height) {
    container_cfg_t cfg = container_default_cfg();

    cfg.w = width;
    cfg.h = height;

    return container_create(parent, &cfg);
}

img_t* stt_view_create_center_image(lv_obj_t* parent) {
    img_cfg_t cfg = img_default_cfg();
    img_t* image = NULL;

    cfg.w = LVGL_UI_ICONW_80;
    cfg.h = LVGL_UI_ICONH_80;
    image = img_create(parent, &cfg);
    if (image == NULL) {
        return NULL;
    }

    lv_obj_align(ui_widget_get_obj(UI_WIDGET(image)), LV_ALIGN_CENTER, 0, 0);
    return image;
}

void stt_view_apply_text_theme(label_t* label,
                               label_align_t align,
                               label_overflow_t overflow) {
    app_font_info_t font_info;
    lv_obj_t* obj = NULL;
    const lv_font_t* stt_font = NULL;

    if (label == NULL) {
        return;
    }

    font_info.weight = stt_get_font_size();
    font_info.wordSpace = get_system_font_word_space();
    font_info.rowSpace = get_system_font_row_space();
    label_set_font_info(label, &font_info);
    obj = label_get_obj(label);
    stt_font = stt_get_font();
    if (obj != NULL && stt_font != NULL) {
        obj_set_text_font(obj, stt_font);
    }
    label_set_align(label, align);
    label_set_overflow(label, overflow);
    label_set_opacity(label, LV_OPA_COVER);
}

void stt_view_apply_lang_hint_text_theme(label_t* label, int32_t pad_hor, lv_base_dir_t base_dir) {
    lv_obj_t* obj = NULL;

    if (label == NULL) {
        return;
    }

    obj = label_get_obj(label);
    stt_view_apply_text_theme(label, LABEL_ALIGN_CENTER, LABEL_OVERFLOW_SCROLL_CIRCULAR);
    label_set_padding(label, pad_hor, 0);
    if (obj != NULL) {
        lv_obj_add_style(obj, &stt_stylebolder, LV_PART_MAIN);
        lv_obj_set_style_base_dir(obj, base_dir, 0);
    }
}

void stt_view_apply_lang_hint_theme(label_t* label, int32_t pad_hor, lv_base_dir_t base_dir) {
    lv_obj_t* obj = NULL;
    LV_UNUSED(base_dir);

    if (label == NULL) {
        return;
    }

    obj = label_get_obj(label);
    label_set_padding(label, pad_hor, 0);
    if (obj != NULL) {
        lv_obj_add_style(obj, &stt_stylebolder, LV_PART_MAIN);
    }
}

void stt_view_apply_stt_label_text_theme(label_t* label, lv_base_dir_t base_dir) {
    lv_obj_t* obj = NULL;

    if (label == NULL) {
        return;
    }

    obj = label_get_obj(label);
    stt_view_apply_text_theme(label,
                              base_dir == LV_BASE_DIR_RTL ? LABEL_ALIGN_RIGHT : LABEL_ALIGN_LEFT,
                              LABEL_OVERFLOW_WRAP);
    if (obj != NULL) {
        lv_obj_set_style_base_dir(obj, base_dir, 0);
    }
}

void stt_view_apply_stt_label_theme(label_t* label, lv_base_dir_t base_dir, bool is_bottom_most) {
    lv_obj_t* obj = NULL;
    LV_UNUSED(base_dir);

    if (label == NULL) {
        return;
    }

    obj = label_get_obj(label);
    if (obj != NULL) {
        lv_obj_add_style(obj, is_bottom_most ? &stt_stylecur : &stt_stylehis, LV_PART_MAIN);
    }
}

void stt_view_update_incremental_text(label_t* label, const char* text) {
    stt_view_update_incremental_text_internal(label, text);
}

void stt_view_update_incremental_text_max_128(label_t* label, const char* text) {
    const char* effective_text = text;
    char limited_text[STT_VIEW_TEXT_MAX_LEN + 1] = {0};
    size_t text_len = 0;
    size_t suffix_start = 0;
    size_t copy_len = 0;

    if (label == NULL || text == NULL) {
        return;
    }

    text_len = strlen(text);
    if (text_len > STT_VIEW_TEXT_MAX_LEN) {
        suffix_start = stt_view_get_utf8_suffix_start(text, STT_VIEW_TEXT_MAX_LEN);
        copy_len = text_len - suffix_start;
        if (copy_len > STT_VIEW_TEXT_MAX_LEN) {
            copy_len = STT_VIEW_TEXT_MAX_LEN;
        }
        memcpy(limited_text, text + suffix_start, copy_len);
        limited_text[copy_len] = '\0';
        effective_text = limited_text;
    }

    stt_view_update_incremental_text_internal(label, effective_text);
}

void stt_view_update_waveicon(lv_obj_t* waveicon, bool show) {
    lv_obj_t* status_bar = system_get_status_bar(STATUS_BAR_POS_BOTTOM);

    if (status_bar == NULL || !lv_obj_is_valid(status_bar) ||
        waveicon == NULL || !lv_obj_is_valid(waveicon)) {
        floatair_err("status_bar or waveicon is invalid");
        return;
    }

    status_bar_set_widget_visible(status_bar, waveicon, show);
}

void stt_view_update_mic_direction(lv_obj_t* mic_direction) {
    if (mic_direction == NULL || !lv_obj_is_valid(mic_direction)) {
        floatair_err("mic_direction is invalid");
        return;
    }

    if (OMNIDIRECTIONAL == stt_config.micDirectional) {
        lv_image_set_src(mic_direction, FLOATAIR_SYS_IMG("mic360.jpg"));
        lv_obj_remove_flag(mic_direction, LV_OBJ_FLAG_HIDDEN);
    } else if (DIRECTIONAL == stt_config.micDirectional) {
        lv_image_set_src(mic_direction, FLOATAIR_SYS_IMG("micphone.jpg"));
        lv_obj_remove_flag(mic_direction, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mic_direction, LV_OBJ_FLAG_HIDDEN);
    }
}

void stt_view_update_audio_source(lv_obj_t* audio_source) {
    if (audio_source == NULL || !lv_obj_is_valid(audio_source)) {
        floatair_err("audio_source is invalid");
        return;
    }

    if (AUDIOSOURCE_PHONE == stt_config.audioSourceIndicator) {
        lv_image_set_src(audio_source, FLOATAIR_SYS_IMG("sound_phone.jpg"));
        lv_obj_remove_flag(audio_source, LV_OBJ_FLAG_HIDDEN);
    } else if (AUDIOSOURCE_GLASSES == stt_config.audioSourceIndicator) {
        lv_image_set_src(audio_source, FLOATAIR_SYS_IMG("sound_glass.jpg"));
        lv_obj_remove_flag(audio_source, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(audio_source, LV_OBJ_FLAG_HIDDEN);
    }
}
