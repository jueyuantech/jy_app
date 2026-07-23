#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl/lvgl.h>

#include "common/app_framework/app_layers.h"
#include "floatair_dbg.h"
#include "message.h"
#include "system/system_res.h"

typedef enum {
    SYSTEM_DRAW_ITEM_TEXT = 1,
    SYSTEM_DRAW_ITEM_IMAGE = 2,
    SYSTEM_DRAW_ITEM_PROGRESS = 3,
} system_draw_item_type_t;

typedef struct {
    uint32_t id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    bool isfloat;
} system_draw_item_common_t;

typedef struct {
    system_draw_item_common_t common;
    uint8_t align;
    uint8_t font_size;
    bool isbolder;
    char text[MSG_STR_MAX_LEN];
} system_draw_text_t;

typedef struct {
    system_draw_item_common_t common;
    char path[MSG_STR_MAX_LEN];
} system_draw_image_t;

typedef struct {
    system_draw_item_common_t common;
    uint32_t progress;
} system_draw_progress_t;

typedef struct {
    uint32_t id;
    uint32_t progress;
} system_draw_progress_update_t;

typedef struct {
    uint32_t id;
} system_draw_clear_t;

static bool system_draw_get_flag(mpack_node_t node, const char* key, bool default_value, bool* out) {
    if (key == NULL || out == NULL) {
        return false;
    }

    mpack_node_t flag_node = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(flag_node) || mpack_node_is_nil(flag_node)) {
        *out = default_value;
        return true;
    }

    if (mpack_node_type(flag_node) == mpack_type_bool) {
        *out = mpack_node_bool(flag_node);
        return true;
    }

    if (mpack_node_type(flag_node) == mpack_type_uint) {
        uint32_t v = mpack_node_u32(flag_node);
        if (v > 1) {
            return false;
        }
        *out = (v != 0);
        return true;
    }

    if (mpack_node_type(flag_node) == mpack_type_int) {
        int32_t v = mpack_node_i32(flag_node);
        if (v < 0 || v > 1) {
            return false;
        }
        *out = (v != 0);
        return true;
    }

    return false;
}

static bool system_draw_parse_common(mpack_node_t node, system_draw_item_common_t* out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!app_msg_get_u32(node, false, "id", &out->id)) {
        return false;
    }
    if (!app_msg_get_32(node, false, "x", &out->x)) {
        return false;
    }
    if (!app_msg_get_32(node, false, "y", &out->y)) {
        return false;
    }
    if (!app_msg_get_32(node, false, "w", &out->w)) {
        return false;
    }
    if (!app_msg_get_32(node, false, "h", &out->h)) {
        return false;
    }
    if (!system_draw_get_flag(node, "isfloat", false, &out->isfloat)) {
        return false;
    }
    return true;
}

static bool system_draw_parse_text(mpack_node_t node, system_draw_text_t* out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!system_draw_parse_common(node, &out->common)) {
        return false;
    }

    uint32_t align = 0;
    if (!app_msg_get_u32(node, true, "align", &align)) {
        return false;
    }
    if (align > UINT8_MAX) {
        return false;
    }
    if (align > LV_TEXT_ALIGN_RIGHT) {
        return false;
    }
    out->align = (uint8_t)align;

    uint32_t font_size = 0;
    if (!app_msg_get_u32(node, true, "font_size", &font_size)) {
        return false;
    }
    if (font_size > UINT8_MAX) {
        return false;
    }
    if (font_size != 0 && !app_fontsize_valid((int32_t)font_size)) {
        return false;
    }
    out->font_size = (uint8_t)font_size;

    if (!system_draw_get_flag(node, "isbolder", false, &out->isbolder)) {
        return false;
    }

    if (app_msg_get_str(node, "text", out->text, sizeof(out->text)) == 0) {
        return false;
    }

    return true;
}

static bool system_draw_parse_image(mpack_node_t node, system_draw_image_t* out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!system_draw_parse_common(node, &out->common)) {
        return false;
    }
    if (app_msg_get_str(node, "path", out->path, sizeof(out->path)) == 0) {
        return false;
    }
    if (!app_image_path_valid(out->path)) {
        return false;
    }
    return true;
}

static bool system_draw_parse_progress(mpack_node_t node, system_draw_progress_t* out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!system_draw_parse_common(node, &out->common)) {
        return false;
    }
    if (!app_msg_get_u32(node, false, "progress", &out->progress)) {
        return false;
    }
    return true;
}

static bool system_draw_parse_progress_update(mpack_node_t node, system_draw_progress_update_t* out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!app_msg_get_u32(node, false, "id", &out->id)) {
        return false;
    }
    if (!app_msg_get_u32(node, false, "progress", &out->progress)) {
        return false;
    }
    return true;
}

static bool system_draw_parse_clear(mpack_node_t node, system_draw_clear_t* out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!app_msg_get_u32(node, false, "id", &out->id)) {
        return false;
    }
    return true;
}

typedef struct {
    uint32_t id;
    system_draw_item_type_t type;
    bool isfloat;
    lv_obj_t* obj;
} system_draw_slot_t;

static system_draw_slot_t s_draw_slots[64] = {0};

static lv_obj_t* system_draw_get_parent(bool isfloat) {
    lv_obj_t* parent = NULL;

    if (isfloat) {
        parent = app_layers_get_app_float();
    } else {
        parent = app_layers_get_top();
    }
    if (parent == NULL || !lv_obj_is_valid(parent)) {
        parent = lv_screen_active();
    }
    return parent;
}

static system_draw_slot_t* system_draw_find_slot(uint32_t id) {
    if (id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_draw_slots) / sizeof(s_draw_slots[0]); i++) {
        if (s_draw_slots[i].id == id) {
            if (s_draw_slots[i].obj != NULL && !lv_obj_is_valid(s_draw_slots[i].obj)) {
                s_draw_slots[i] = (system_draw_slot_t){0};
                return NULL;
            }
            return &s_draw_slots[i];
        }
    }
    return NULL;
}

static system_draw_slot_t* system_draw_get_or_alloc_slot(uint32_t id) {
    system_draw_slot_t* slot = system_draw_find_slot(id);
    if (slot != NULL) {
        return slot;
    }
    if (id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_draw_slots) / sizeof(s_draw_slots[0]); i++) {
        if (s_draw_slots[i].id == 0) {
            s_draw_slots[i].id = id;
            return &s_draw_slots[i];
        }
    }
    return NULL;
}

static void system_draw_clear_slot(system_draw_slot_t* slot) {
    if (slot == NULL) {
        return;
    }
    if (slot->obj != NULL && lv_obj_is_valid(slot->obj)) {
        lv_obj_delete(slot->obj);
    }
    *slot = (system_draw_slot_t){0};
}

static lv_text_align_t system_draw_to_lv_text_align(uint8_t align) {
    switch (align) {
        case 0:
        case 1:
        case 2:
        case 3:
            return (lv_text_align_t)align;
        default:
            return LV_TEXT_ALIGN_CENTER;
    }
}

static void system_draw_apply_common_layout(lv_obj_t* obj, const system_draw_item_common_t* common) {
    if (obj == NULL || common == NULL) {
        return;
    }
    lv_obj_set_pos(obj, (lv_coord_t)common->x, (lv_coord_t)common->y);
    lv_obj_set_size(obj, (lv_coord_t)common->w, (lv_coord_t)common->h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t* system_draw_get_text_label(lv_obj_t* container) {
    if (container == NULL || !lv_obj_is_valid(container)) {
        return NULL;
    }
    return lv_obj_get_child(container, 0);
}

static bool system_draw_upsert_text(const system_draw_text_t* req) {
    system_draw_slot_t* slot = system_draw_get_or_alloc_slot(req->common.id);
    if (slot == NULL) {
        return false;
    }

    if (slot->type != SYSTEM_DRAW_ITEM_TEXT || slot->isfloat != req->common.isfloat) {
        system_draw_clear_slot(slot);
        slot = system_draw_get_or_alloc_slot(req->common.id);
        if (slot == NULL) {
            return false;
        }
        slot->type = SYSTEM_DRAW_ITEM_TEXT;
        slot->isfloat = req->common.isfloat;

        lv_obj_t* parent = system_draw_get_parent(req->common.isfloat);
        slot->obj = lv_obj_create(parent);
        if (slot->obj == NULL) {
            system_draw_clear_slot(slot);
            return false;
        }
        lv_obj_remove_style_all(slot->obj);
        lv_obj_set_style_bg_opa(slot->obj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_all(slot->obj, 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(slot->obj, lv_color_white(), LV_PART_MAIN);
        lv_obj_clear_flag(slot->obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* label = lv_label_create(slot->obj);
        if (label == NULL) {
            system_draw_clear_slot(slot);
            return false;
        }
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    }

    const lv_font_t* font = NULL;
    if (req->font_size != 0) {
        font = get_font_by_size_near(req->font_size);
    }
    if (font == NULL) {
        font = get_system_font();
    }

    system_draw_apply_common_layout(slot->obj, &req->common);
    lv_obj_set_style_border_width(slot->obj, req->isbolder ? 1 : 0, LV_PART_MAIN);
    lv_obj_set_style_radius(slot->obj, req->isbolder ? 10 : 0, LV_PART_MAIN);

    lv_obj_t* label = system_draw_get_text_label(slot->obj);
    if (label == NULL || !lv_obj_is_valid(label)) {
        return false;
    }
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, system_draw_to_lv_text_align(req->align), 0);
    lv_label_set_text(label, req->text);
    lv_obj_set_width(label, LV_MAX((lv_coord_t)req->common.w - 12, 1));
    lv_obj_update_layout(label);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(slot->obj);
    return true;
}

static bool system_draw_upsert_image(const system_draw_image_t* req) {
    system_draw_slot_t* slot = system_draw_get_or_alloc_slot(req->common.id);
    if (slot == NULL) {
        return false;
    }

    if (slot->type != SYSTEM_DRAW_ITEM_IMAGE || slot->isfloat != req->common.isfloat) {
        system_draw_clear_slot(slot);
        slot = system_draw_get_or_alloc_slot(req->common.id);
        if (slot == NULL) {
            return false;
        }
        slot->type = SYSTEM_DRAW_ITEM_IMAGE;
        slot->isfloat = req->common.isfloat;

        lv_obj_t* parent = system_draw_get_parent(req->common.isfloat);
        slot->obj = lv_image_create(parent);
        if (slot->obj == NULL) {
            system_draw_clear_slot(slot);
            return false;
        }
        lv_obj_remove_style_all(slot->obj);
    }

    lv_image_set_src(slot->obj, req->path);
    system_draw_apply_common_layout(slot->obj, &req->common);
    lv_obj_move_foreground(slot->obj);
    return true;
}

static bool system_draw_upsert_progress(const system_draw_progress_t* req) {
    system_draw_slot_t* slot = system_draw_get_or_alloc_slot(req->common.id);
    if (slot == NULL) {
        return false;
    }

    if (slot->type != SYSTEM_DRAW_ITEM_PROGRESS || slot->isfloat != req->common.isfloat) {
        system_draw_clear_slot(slot);
        slot = system_draw_get_or_alloc_slot(req->common.id);
        if (slot == NULL) {
            return false;
        }
        slot->type = SYSTEM_DRAW_ITEM_PROGRESS;
        slot->isfloat = req->common.isfloat;

        lv_obj_t* parent = system_draw_get_parent(req->common.isfloat);
        slot->obj = lv_bar_create(parent);
        if (slot->obj == NULL) {
            system_draw_clear_slot(slot);
            return false;
        }
        lv_bar_set_range(slot->obj, 0, 100);
        lv_obj_set_style_bg_opa(slot->obj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(slot->obj, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(slot->obj, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_radius(slot->obj, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(slot->obj, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slot->obj, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slot->obj, lv_color_white(), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slot->obj, 4, LV_PART_INDICATOR);
    }

    uint32_t value = req->progress;
    if (value > 100) {
        value = 100;
    }
    lv_bar_set_value(slot->obj, (int32_t)value, LV_ANIM_OFF);
    system_draw_apply_common_layout(slot->obj, &req->common);
    lv_obj_move_foreground(slot->obj);
    return true;
}

static bool system_draw_update_progress(const system_draw_progress_update_t* req) {
    system_draw_slot_t* slot = system_draw_find_slot(req->id);
    if (slot == NULL || slot->type != SYSTEM_DRAW_ITEM_PROGRESS || slot->obj == NULL ||
        !lv_obj_is_valid(slot->obj)) {
        return false;
    }

    uint32_t value = req->progress;
    if (value > 100) {
        value = 100;
    }
    lv_bar_set_value(slot->obj, (int32_t)value, LV_ANIM_OFF);
    lv_obj_move_foreground(slot->obj);
    return true;
}

static bool system_draw_drawtext(mpack_node_t node, msg_pack_t* msg) {
    system_draw_text_t req;
    if (!system_draw_parse_text(node, &req)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!system_draw_upsert_text(&req)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_draw_drawimage(mpack_node_t node, msg_pack_t* msg) {
    system_draw_image_t req;
    if (!system_draw_parse_image(node, &req)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!system_draw_upsert_image(&req)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_draw_drawprogressbar(mpack_node_t node, msg_pack_t* msg) {
    system_draw_progress_t req;
    if (!system_draw_parse_progress(node, &req)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!system_draw_upsert_progress(&req)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_draw_updateprogress(mpack_node_t node, msg_pack_t* msg) {
    system_draw_progress_update_t req;
    if (!system_draw_parse_progress_update(node, &req)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!system_draw_update_progress(&req)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_draw_clear(mpack_node_t node, msg_pack_t* msg) {
    system_draw_clear_t req;
    if (!system_draw_parse_clear(node, &req)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    system_draw_slot_t* slot = system_draw_find_slot(req.id);
    if (slot != NULL) {
        system_draw_clear_slot(slot);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_draw_clear_all(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    for (size_t i = 0; i < sizeof(s_draw_slots) / sizeof(s_draw_slots[0]); i++) {
        if (s_draw_slots[i].id != 0) {
            system_draw_clear_slot(&s_draw_slots[i]);
        }
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

app_cmd_func_t system_draw_cmd_funcs[] = {
    {"drawText", system_draw_drawtext},
    {"drawImage", system_draw_drawimage},
    {"drawProgress", system_draw_drawprogressbar},
    {"updateProgress", system_draw_updateprogress},
    {"clearDrawId", system_draw_clear},
    {"clearDrawAll", system_draw_clear_all},
};
const size_t system_draw_cmd_funcs_count =
    sizeof(system_draw_cmd_funcs) / sizeof(system_draw_cmd_funcs[0]);
