/**
 * @file notify.c
 * @brief 系统通知浮层实现。
 */
#include "notify.h"

#include "notify_popup_ui.h"
#include "common/app_framework/app_layers.h"
#include "common/widgets/container.h"
#include "common/widgets/img.h"
#include "common/widgets/label.h"
#include "common/widgets/ui_widget.h"
#include "system/system_timer.h"

#define NOTIFY_IMAGE_WIDTH 32
#define NOTIFY_IMAGE_HEIGHT 32
#define NOTIFY_IMAGE_BUF_SIZE (NOTIFY_IMAGE_WIDTH * NOTIFY_IMAGE_HEIGHT)
#define NOTIFY_DEFAULT_DURATION_MS 5000

/**
 * @brief Notify 组件内部数据结构。
 */
struct notify_t {
    notify_popup_ui_t ui;       ///< Notify UI 句柄集合。
    const char* title;          ///< 当前标题文本。
    const void* image_src;      ///< 当前图标图片源。
    size_t image_src_size;      ///< 当前图标图片源长度。
    uint32_t duration_ms;       ///< 自动关闭时长；0 表示不自动关闭。
    uint32_t timer_id;          ///< 自动关闭定时器 ID；0 表示无定时器。
    notify_mode_t mode;         ///< 当前 Notify 显示模式。
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

    if (!notify->ui.root || !container_is_valid(notify->ui.root)) {
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
    cfg.duration_ms = NOTIFY_DEFAULT_DURATION_MS;

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

    if (notify->ui.image && ui_widget_is_valid(UI_WIDGET(notify->ui.image))) {
        img_set_src(notify->ui.image, NULL);
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
    if (!notify || !notify->ui.image) {
        return false;
    }

    if (!image_src) {
        notify_release_image(notify);
        ui_widget_set_visible(UI_WIDGET(notify->ui.image), false);
        return true;
    }

    if (image_src_size >= NOTIFY_IMAGE_BUF_SIZE) {
        if (!img_set_l8_data(notify->ui.image,
                             image_src,
                             image_src_size,
                             NOTIFY_IMAGE_WIDTH,
                             NOTIFY_IMAGE_HEIGHT)) {
            notify_release_image(notify);
            return false;
        }
    } else {
        img_set_src(notify->ui.image, image_src);
    }

    ui_widget_set_visible(UI_WIDGET(notify->ui.image), true);
    return true;
}

/**
 * @brief 按当前模式创建 Notify UI。
 * @param[in,out] notify 目标 Notify 句柄。
 * @param[in] parent 父对象。
 * @return `true` 表示创建成功，`false` 表示创建失败。
 */
static bool notify_init_ui(notify_t* notify, lv_obj_t* parent) {
    notify_popup_mode_t ui_mode = notify->mode == NOTIFY_MODE_CALL
                                      ? NOTIFY_POPUP_MODE_CALL
                                      : NOTIFY_POPUP_MODE_MESSAGE;

    if (!notify_popup_init_ui(parent, &notify->ui, ui_mode)) {
        return false;
    }
    return notify->ui.root != NULL &&
           notify->ui.header != NULL &&
           notify->ui.image != NULL &&
           notify->ui.header_label != NULL &&
           notify->ui.body_label != NULL;
}

/**
 * @brief 刷新 Notify 文本、图片和宽度约束。
 * @param[in,out] notify 目标 Notify 句柄。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
static bool notify_apply_content(notify_t* notify) {
    lv_obj_t* root_obj = NULL;

    if (!notify ||
        !notify->ui.root ||
        !notify->ui.header ||
        !notify->ui.header_label ||
        !notify->ui.body_label) {
        return false;
    }

    root_obj = container_get_obj(notify->ui.root);
    if (root_obj == NULL) {
        return false;
    }

    label_set_text(notify->ui.header_label, notify->title ? notify->title : "");
    if (!notify_set_image_src(notify, notify->image_src, notify->image_src_size)) {
        return false;
    }

    lv_obj_update_layout(root_obj);
    container_limit_child_width_to_remaining_row_space(notify->ui.header,
                                                       label_get_obj(notify->ui.header_label));
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
    resolved_cfg.mode = resolved_cfg.mode == NOTIFY_MODE_CALL ? NOTIFY_MODE_CALL : NOTIFY_MODE_MESSAGE;

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

    notify->title = resolved_cfg.title;
    notify->image_src = resolved_cfg.image_src;
    notify->image_src_size = resolved_cfg.image_src_size;
    notify->duration_ms = resolved_cfg.duration_ms;
    notify->mode = resolved_cfg.mode;

    if (!notify_init_ui(notify, parent)) {
        lv_free(notify);
        return NULL;
    }
    lv_obj_add_event_cb(container_get_obj(notify->ui.root), notify_on_delete, LV_EVENT_DELETE, notify);

    if (!notify_apply_content(notify)) {
        ui_widget_destroy(UI_WIDGET(notify->ui.root));
        return NULL;
    }

    ui_widget_move_foreground(UI_WIDGET(notify->ui.root));

    if (notify->mode == NOTIFY_MODE_MESSAGE && notify->duration_ms > 0) {
        uint32_t timer_id = 0;
        if (!system_timer_autodestroy_start(notify->duration_ms, notify_timer_cb, notify, &timer_id)) {
            ui_widget_destroy(UI_WIDGET(notify->ui.root));
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

bool notify_handle_active_event(lv_event_code_t code) {
    notify_t* active_notify = s_active_notify;

    if (!notify_is_active_valid(active_notify)) {
        return false;
    }
    if (!notify_is_interceptable_event(code)) {
        return false;
    }

    if (active_notify->on_event) {
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

void notify_dismiss(void) {
    notify_t* notify = s_active_notify;

    if (!notify || !notify_is_active_valid(notify)) {
        return;
    }

    notify_stop_timer(notify);
    s_active_notify = NULL;
    ui_widget_destroy(UI_WIDGET(notify->ui.root));
}
