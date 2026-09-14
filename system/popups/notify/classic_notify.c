/**
 * @file classic_notify.c
 * @brief 经典系统通知浮层实现。
 */
#include "notify.h"

#include "app_def.h"
#include "common/app_framework/app_layers.h"
#include "common/widgets/container.h"
#include "common/widgets/img.h"
#include "common/widgets/label.h"
#include "common/widgets/ui_widget.h"
#include "system/system_timer.h"
#include <stdio.h>

#define NOTIFY_IMAGE_WIDTH 32
#define NOTIFY_IMAGE_HEIGHT 32
#define NOTIFY_IMAGE_BUF_SIZE (NOTIFY_IMAGE_WIDTH * NOTIFY_IMAGE_HEIGHT)
#define NOTIFY_DEFAULT_DURATION_MS 5000

/**
 * @brief Notify 组件内部数据结构。
 */
struct notify_t {
    container_t* root;          ///< 覆盖 popup 层的 Notify 根容器。
    container_t* header;        ///< 顶部通知内容容器。
    img_t* image;               ///< 通知图标。
    label_t* header_label;      ///< 通知标题标签。
    label_t* body_label;        ///< 底部操作提示标签。
    char title[MSG_STR_MAX_LEN]; ///< 当前标题文本缓存。
    const void* image_src;      ///< 当前图标图片源。
    size_t image_src_size;      ///< 当前图标图片源长度。
    uint32_t duration_ms;       ///< 自动关闭时长；0 表示不自动关闭。
    uint32_t timer_id;          ///< 自动关闭定时器 ID；0 表示无定时器。
    notify_mode_t mode;         ///< 当前 Notify 显示模式。
    notify_call_state_t call_state; ///< 当前电话通知阶段。
    bool passthrough_input;     ///< 是否将触摸输入继续透传给当前页面。
    notify_event_cb_t on_event; ///< Notify 输入事件统一回调。
    void* callback_user_data;   ///< 回调透传数据。
};

static notify_t* s_active_notify = NULL;

/**
 * @brief 获取 Notify 应挂载的父对象。
 * @return 返回 Notify 父对象；都不可用时返回 `NULL`。
 */
static lv_obj_t* notify_get_parent(void) {
    lv_obj_t* popup = app_layers_get_popup();
    if (popup != NULL && lv_obj_is_valid(popup)) {
        return popup;
    }

    return lv_screen_active();
}

/**
 * @brief 判断事件是否应被 Notify 拦截。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示应拦截，`false` 表示不处理。
 */
static bool notify_is_interceptable_event(lv_event_code_t code) {
    return code == LV_EVENT_CLICKED ||
           code == LV_EVENT_DCLICKED ||
           code == LV_EVENT_LONG_PRESSED ||
           code == LV_EVENT_GESTURE_LEFT ||
           code == LV_EVENT_GESTURE_RIGHT;
}

/**
 * @brief 判断 Notify 句柄是否仍是当前活动对象。
 * @param[in] notify 目标 Notify 句柄。
 * @return `true` 表示当前仍可安全访问，`false` 表示已经失效或不是活动对象。
 */
static bool notify_is_active_valid(notify_t* notify) {
    if (!notify || notify != s_active_notify) {
        return false;
    }

    if (!notify->root || !container_is_valid(notify->root)) {
        s_active_notify = NULL;
        return false;
    }

    return true;
}

notify_cfg_t notify_default_cfg(void) {
    notify_cfg_t cfg;

    cfg.title = NULL;
    cfg.image_src = NULL;
    cfg.image_src_size = 0;
    cfg.mode = NOTIFY_MODE_MESSAGE;
    cfg.call_state = NOTIFY_CALL_STATE_RINGING;
    cfg.duration_ms = NOTIFY_DEFAULT_DURATION_MS;
    cfg.passthrough_input = false;

    return cfg;
}

/**
 * @brief 停止 Notify 的自动关闭定时器。
 * @param[in,out] notify 目标 Notify 句柄。
 * @return 无返回值。
 */
static void notify_stop_timer(notify_t* notify) {
    if (!notify || notify->timer_id == 0) {
        return;
    }

    system_timer_autodestroy_cancel(notify->timer_id);
    notify->timer_id = 0;
}

/**
 * @brief 释放 Notify 当前图片资源引用。
 * @param[in,out] notify 目标 Notify 句柄。
 * @return 无返回值。
 */
static void notify_release_image(notify_t* notify) {
    if (!notify) {
        return;
    }

    if (notify->image && ui_widget_is_valid(UI_WIDGET(notify->image))) {
        img_set_src(notify->image, NULL);
    }
}

/**
 * @brief 更新 Notify 头像/图标数据源。
 * @param[in,out] notify 目标 Notify 句柄。
 * @param[in] image_src 图片源；可传 32x32 L8 原始像素数据、路径或 LVGL 图片描述符。
 * @param[in] image_src_size `image_src` 为 L8 原始像素数据时对应长度；其他图片源传 `0`。
 * @return `true` 表示设置成功，`false` 表示设置失败。
 */
static bool notify_set_image_src(notify_t* notify, const void* image_src, size_t image_src_size) {
    if (!notify || !notify->image) {
        return false;
    }

    if (!image_src) {
        notify_release_image(notify);
        ui_widget_set_visible(UI_WIDGET(notify->image), false);
        return true;
    }

    if (image_src_size >= NOTIFY_IMAGE_BUF_SIZE) {
        if (!img_set_l8_data(notify->image,
                             image_src,
                             image_src_size,
                             NOTIFY_IMAGE_WIDTH,
                             NOTIFY_IMAGE_HEIGHT)) {
            notify_release_image(notify);
            return false;
        }
    } else {
        img_set_src(notify->image, image_src);
    }

    ui_widget_set_visible(UI_WIDGET(notify->image), true);
    return true;
}

/**
 * @brief 按当前模式创建 Notify UI。
 * @param[in,out] notify 目标 Notify 句柄。
 * @param[in] parent 父对象。
 * @return `true` 表示创建成功，`false` 表示创建失败。
 */
static bool notify_init_ui(notify_t* notify, lv_obj_t* parent) {
    container_cfg_t root_cfg = container_default_cfg();
    container_cfg_t header_slot_cfg = container_default_cfg();
    container_cfg_t header_cfg = container_default_cfg();
    container_cfg_t body_slot_cfg = container_default_cfg();
    img_cfg_t image_cfg = img_default_cfg();
    label_cfg_t header_label_cfg = label_default_cfg();
    label_cfg_t body_label_cfg = label_default_cfg();
    container_t* header_slot = NULL;
    container_t* body_slot = NULL;
    lv_obj_t* root_obj = NULL;
    lv_obj_t* header_slot_obj = NULL;
    lv_obj_t* header_obj = NULL;
    lv_obj_t* body_slot_obj = NULL;
    bool call_mode = false;
    bool success_mode = false;

    if (notify == NULL || parent == NULL) {
        return false;
    }
    call_mode = notify->mode == NOTIFY_MODE_CALL;
    success_mode = notify->mode == NOTIFY_MODE_SUCCESS;

    root_cfg.x = 0;
    root_cfg.y = 0;
    root_cfg.w = LV_PCT(100);
    root_cfg.h = LV_PCT(100);
    root_cfg.opa = 0;
    notify->root = container_create(parent, &root_cfg);
    if (notify->root == NULL) {
        return false;
    }
    root_obj = container_get_obj(notify->root);
    if (root_obj == NULL) {
        return false;
    }
    container_set_padding_box(notify->root, 16, 16, 16, 32);
    container_set_layout_vbox_spaced(notify->root, call_mode ? 8 : 0);
    container_set_align(notify->root,
                        call_mode ? CONTAINER_ALIGN_START : CONTAINER_ALIGN_SPACE_BETWEEN,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_START);

    header_slot_cfg.w = LV_PCT(100);
    header_slot_cfg.h = LV_SIZE_CONTENT;
    header_slot_cfg.opa = 0;
    header_slot = container_create(root_obj, &header_slot_cfg);
    if (header_slot == NULL) {
        return false;
    }
    header_slot_obj = container_get_obj(header_slot);
    if (header_slot_obj == NULL) {
        return false;
    }
    container_set_layout_hbox_spaced(header_slot, 0);
    container_set_align(header_slot,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_START);

    header_cfg.w = LV_SIZE_CONTENT;
    header_cfg.h = LV_SIZE_CONTENT;
    header_cfg.radius = 12;
    header_cfg.border_width = 1;
    header_cfg.pad_hor = 12;
    header_cfg.pad_ver = 8;
    header_cfg.opa = LV_OPA_COVER;
    header_cfg.max_w = LV_PCT(100);
    notify->header = container_create(header_slot_obj, &header_cfg);
    if (notify->header == NULL) {
        return false;
    }
    header_obj = container_get_obj(notify->header);
    if (header_obj == NULL) {
        return false;
    }
    container_set_layout_hbox_spaced(notify->header, 12);
    container_set_align(notify->header,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_START);

    if (!success_mode) {
        image_cfg.w = NOTIFY_IMAGE_WIDTH;
        image_cfg.h = NOTIFY_IMAGE_HEIGHT;
        notify->image = img_create(header_obj, &image_cfg);
        if (notify->image == NULL) {
            return false;
        }
    }

    header_label_cfg.w = LV_SIZE_CONTENT;
    header_label_cfg.h = LV_SIZE_CONTENT;
    header_label_cfg.align = success_mode ? LABEL_ALIGN_CENTER : LABEL_ALIGN_LEFT;
    header_label_cfg.overflow = LABEL_OVERFLOW_WRAP;
    header_label_cfg.max_lines = 3;
    notify->header_label = label_create(header_obj, &header_label_cfg);
    if (notify->header_label == NULL) {
        return false;
    }

    body_slot_cfg.w = LV_PCT(100);
    body_slot_cfg.h = LV_SIZE_CONTENT;
    body_slot_cfg.opa = 0;
    body_slot = container_create(root_obj, &body_slot_cfg);
    if (body_slot == NULL) {
        return false;
    }
    body_slot_obj = container_get_obj(body_slot);
    if (body_slot_obj == NULL) {
        return false;
    }
    container_set_layout_hbox_spaced(body_slot, 0);
    container_set_align(body_slot,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_START);

    body_label_cfg.w = LV_SIZE_CONTENT;
    body_label_cfg.h = LV_SIZE_CONTENT;
    body_label_cfg.radius = 12;
    body_label_cfg.border_width = 0;
    body_label_cfg.pad_hor = 12;
    body_label_cfg.pad_ver = 8;
    body_label_cfg.text = call_mode ? app_get_str("NOTIFY_CALL_HINT") : "";
    body_label_cfg.align = LABEL_ALIGN_LEFT;
    body_label_cfg.overflow = LABEL_OVERFLOW_WRAP;
    body_label_cfg.max_lines = 3;
    notify->body_label = label_create(body_slot_obj, &body_label_cfg);
    if (notify->body_label == NULL) {
        return false;
    }
    ui_widget_set_visible(UI_WIDGET(notify->body_label),
                          call_mode && notify->call_state == NOTIFY_CALL_STATE_RINGING);
    return true;
}

/**
 * @brief 刷新 Notify 文本、图片和宽度约束。
 * @param[in,out] notify 目标 Notify 句柄。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
static bool notify_apply_content(notify_t* notify) {
    lv_obj_t* root_obj = NULL;
    char success_title[MSG_STR_MAX_LEN + sizeof(NOTIFY_SUCCESS_PREFIX)] = {0};

    if (!notify ||
        !notify->root ||
        !notify->header ||
        !notify->header_label ||
        !notify->body_label) {
        return false;
    }

    root_obj = container_get_obj(notify->root);
    if (root_obj == NULL) {
        return false;
    }

    if (notify->mode == NOTIFY_MODE_SUCCESS) {
        snprintf(success_title,
                 sizeof(success_title),
                 "%s%s",
                 NOTIFY_SUCCESS_PREFIX,
                 notify->title);
        label_set_text(notify->header_label, success_title);
    } else {
        label_set_text(notify->header_label, notify->title);
        if (!notify_set_image_src(notify, notify->image_src, notify->image_src_size)) {
            return false;
        }
    }

    lv_obj_update_layout(root_obj);
    container_limit_child_width_to_remaining_row_space(notify->header,
                                                       label_get_obj(notify->header_label));
    return true;
}

/**
 * @brief Notify 删除事件回调。
 * @param[in] e LVGL 事件对象。
 * @return 无返回值。
 */
static void notify_on_delete(lv_event_t* e) {
    notify_t* notify = (notify_t*)lv_event_get_user_data(e);

    if (!notify) {
        return;
    }

    notify_stop_timer(notify);
    notify_release_image(notify);

    if (s_active_notify == notify) {
        s_active_notify = NULL;
    }

    lv_free(notify);
}

/**
 * @brief Notify 自动关闭定时器回调。
 * @param[in] user_data Notify 句柄。
 * @return 无返回值。
 */
static void notify_timer_cb(void* user_data) {
    notify_t* notify = (notify_t*)user_data;

    if (notify != s_active_notify) {
        return;
    }

    notify->timer_id = 0;
    notify_dismiss();
}

notify_t* notify_show_with_cfg(const notify_cfg_t* cfg) {
    notify_t* notify = NULL;
    lv_obj_t* parent = notify_get_parent();
    notify_cfg_t resolved_cfg;

    if (!parent) {
        return NULL;
    }

    resolved_cfg = notify_default_cfg();
    if (cfg) {
        resolved_cfg = *cfg;
    }
    if (resolved_cfg.mode != NOTIFY_MODE_CALL &&
        resolved_cfg.mode != NOTIFY_MODE_SUCCESS) {
        resolved_cfg.mode = NOTIFY_MODE_MESSAGE;
    }
    if (resolved_cfg.mode == NOTIFY_MODE_SUCCESS) {
        resolved_cfg.image_src = NULL;
        resolved_cfg.image_src_size = 0;
    }

    if ((!resolved_cfg.title || resolved_cfg.title[0] == '\0') && resolved_cfg.image_src == NULL) {
        return NULL;
    }

    if (s_active_notify && !notify_is_active_valid(s_active_notify)) {
        s_active_notify = NULL;
    }
    if (s_active_notify) {
        notify_dismiss();
    }

    notify = (notify_t*)lv_malloc(sizeof(*notify));
    if (!notify) {
        return NULL;
    }
    lv_memzero(notify, sizeof(*notify));

    if (resolved_cfg.title != NULL) {
        strncpy(notify->title, resolved_cfg.title, sizeof(notify->title) - 1u);
    }
    notify->image_src = resolved_cfg.image_src;
    notify->image_src_size = resolved_cfg.image_src_size;
    notify->duration_ms = resolved_cfg.duration_ms;
    notify->mode = resolved_cfg.mode;
    notify->call_state = resolved_cfg.call_state;
    if (notify->call_state != NOTIFY_CALL_STATE_OUTGOING &&
        notify->call_state != NOTIFY_CALL_STATE_CONNECTED) {
        notify->call_state = NOTIFY_CALL_STATE_RINGING;
    }
    notify->passthrough_input = resolved_cfg.passthrough_input;

    if (!notify_init_ui(notify, parent)) {
        if (notify->root != NULL && container_is_valid(notify->root)) {
            ui_widget_destroy(UI_WIDGET(notify->root));
        }
        lv_free(notify);
        return NULL;
    }
    lv_obj_add_event_cb(container_get_obj(notify->root), notify_on_delete, LV_EVENT_DELETE, notify);

    if (!notify_apply_content(notify)) {
        ui_widget_destroy(UI_WIDGET(notify->root));
        return NULL;
    }

    ui_widget_move_foreground(UI_WIDGET(notify->root));

    if (notify->mode != NOTIFY_MODE_CALL && notify->duration_ms > 0) {
        uint32_t timer_id = 0;
        if (!system_timer_autodestroy_start(notify->duration_ms, notify_timer_cb, notify, &timer_id)) {
            ui_widget_destroy(UI_WIDGET(notify->root));
            return NULL;
        }
        notify->timer_id = timer_id;
    }

    s_active_notify = notify;
    return notify;
}

void notify_set_callbacks(notify_t* notify, notify_event_cb_t on_event, void* user_data) {
    if (!notify_is_active_valid(notify)) {
        return;
    }

    notify->on_event = on_event;
    notify->callback_user_data = user_data;
}

void notify_set_body_hint_visible(notify_t* notify, bool visible) {
    if (!notify_is_active_valid(notify)) {
        return;
    }
    ui_widget_set_visible(UI_WIDGET(notify->body_label), visible);
}

bool notify_handle_active_event(lv_event_code_t code) {
    notify_t* active_notify = s_active_notify;

    if (!notify_is_active_valid(active_notify)) {
        return false;
    }
    if (active_notify->passthrough_input) {
        return false;
    }
    if (!notify_is_interceptable_event(code)) {
        return false;
    }

    /* 经典样式沿用原交互：吞掉滑动，只有点击类事件触发业务动作。 */
    if (active_notify->on_event &&
        code != LV_EVENT_GESTURE_LEFT &&
        code != LV_EVENT_GESTURE_RIGHT) {
        active_notify->on_event(active_notify, code, active_notify->callback_user_data);
    }

    return true;
}

bool notify_get_active_mode(notify_mode_t* mode_out) {
    notify_t* active_notify = s_active_notify;

    if (!notify_is_active_valid(active_notify)) {
        return false;
    }

    if (mode_out) {
        *mode_out = active_notify->mode;
    }

    return true;
}

bool notify_update_call_state(notify_call_state_t state) {
    notify_t* notify = s_active_notify;

    if (!notify_is_active_valid(notify) || notify->mode != NOTIFY_MODE_CALL) {
        return false;
    }
    if (state == NOTIFY_CALL_STATE_CONNECTED) {
        notify_dismiss();
        return false;
    }

    notify->call_state = state == NOTIFY_CALL_STATE_OUTGOING
                             ? NOTIFY_CALL_STATE_OUTGOING
                             : NOTIFY_CALL_STATE_RINGING;
    ui_widget_set_visible(UI_WIDGET(notify->body_label),
                          notify->call_state == NOTIFY_CALL_STATE_RINGING);
    return true;
}

bool notify_update_call_text(const char* text) {
    notify_t* notify = s_active_notify;

    if (!notify_is_active_valid(notify) || notify->mode != NOTIFY_MODE_CALL ||
        text == NULL || text[0] == '\0') {
        return false;
    }
    strncpy(notify->title, text, sizeof(notify->title) - 1u);
    notify->title[sizeof(notify->title) - 1u] = '\0';
    label_set_text(notify->header_label, notify->title);
    return true;
}

void notify_dismiss(void) {
    notify_t* notify = s_active_notify;

    if (!notify || !notify_is_active_valid(notify)) {
        return;
    }

    notify_stop_timer(notify);
    s_active_notify = NULL;
    ui_widget_destroy(UI_WIDGET(notify->root));
}
