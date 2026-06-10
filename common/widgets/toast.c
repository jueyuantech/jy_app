/**
 * @file toast.c
 * @brief Toast 组件实现，负责单实例提示的显示、更新与自动关闭。
 */
#include "toast.h"

#include "floatair_lcd.h"
#include "common/app_framework/app_layers.h"
#include "system/system_timer.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Toast 组件内部数据结构。
 */
struct toast_t {
    container_t* box;     ///< Toast 外层容器。
    label_t* label;       ///< Toast 文本组件。
    uint32_t timer_id;    ///< 自动关闭定时器 ID；0 表示无定时器。
    uint32_t id;          ///< 当前 Toast 业务标识。
    uint32_t duration_ms; ///< 当前自动关闭时间。
    uint8_t level;        ///< 当前 Toast 显示等级；数值越小优先级越高。
};

static toast_t* s_active_toast = NULL;

static void toast_timer_cb(void* user_data);
static void toast_on_delete(lv_event_t* e);

/**
 * @brief 获取弹层应挂载的父对象。
 *
 * 优先挂到全局弹窗层，使 Toast 不再依赖当前页面栈；
 * 弹窗层不可用时回退到当前活动屏幕。
 *
 * @return 返回弹层父对象；都不可用时返回 `NULL`。
 */
static lv_obj_t* toast_get_parent(void) {
    lv_obj_t* popup = app_layers_get_popup();
    if (popup != NULL && lv_obj_is_valid(popup)) {
        return popup;
    }

    return lv_screen_active();
}

/**
 * @brief 判断 Toast 句柄及底层对象是否有效。
 *
 * @param toast 目标 Toast 句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static bool toast_is_valid(const toast_t* toast) {
    return toast && toast->box && container_is_valid(toast->box);
}

/**
 * @brief 判断当前活动 Toast 是否仍挂在期望的父层上。
 *
 * 仅当父层未变化时才复用现有 Toast；若弹层上下文已经切换，
 * 则回退到重建流程，避免 Toast 停留在旧父对象下。
 *
 * @param toast 目标 Toast 句柄。
 * @return `true` 表示可复用，`false` 表示应重建。
 */
static bool toast_parent_matches(const toast_t* toast) {
    lv_obj_t* parent = NULL;
    lv_obj_t* box_obj = NULL;

    if (!toast_is_valid(toast)) {
        return false;
    }

    parent = toast_get_parent();
    box_obj = container_get_obj(toast->box);
    if (!parent || !box_obj || !lv_obj_is_valid(box_obj)) {
        return false;
    }

    return lv_obj_get_parent(box_obj) == parent;
}

/**
 * @brief 获取默认 Toast 配置。
 *
 * @return 返回填充好默认值的 Toast 配置结构体。
 */
toast_cfg_t toast_default_cfg(void) {
    toast_cfg_t cfg;

    cfg.box = container_default_cfg();
    cfg.box.w = LV_SIZE_CONTENT;
    cfg.box.h = LV_SIZE_CONTENT;
    cfg.box.opa = LV_OPA_COVER;
    cfg.box.radius = 8;
    cfg.box.pad_hor = 12;
    cfg.box.pad_ver = 10;
    cfg.box.border_width = 1;
    cfg.label = label_default_cfg();
    cfg.label.overflow = LABEL_OVERFLOW_WRAP;
    cfg.id = TOAST_ID_DEFAULT;
    cfg.duration_ms = 3000;
    cfg.level = 1;

    return cfg;
}

/**
 * @brief 刷新 Toast 的容器与文本布局。
 *
 * @param toast 目标 Toast 句柄。
 * @return 无返回值。
 */
static void toast_sync_layout(toast_t* toast, const char* text, const toast_cfg_t* cfg) {
    container_cfg_t box_cfg;
    label_cfg_t label_cfg;

    if (!toast_is_valid(toast) || !toast->label || !cfg || !text) {
        return;
    }

    box_cfg = cfg->box;
    box_cfg.w = LV_SIZE_CONTENT;
    box_cfg.h = LV_SIZE_CONTENT;
    label_cfg = cfg->label;
    label_cfg.text = text;

    container_apply_cfg(toast->box, &box_cfg);
    container_set_layout_vbox(toast->box);
    container_set_align(toast->box,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);

    label_apply_cfg(toast->label, &label_cfg);
    lv_obj_align(container_get_obj(toast->box), LV_ALIGN_CENTER, 0, 0);
}

/**
 * @brief 停止 Toast 的自动关闭定时器。
 *
 * @param toast 目标 Toast 句柄。
 * @return 无返回值。
 */
static void toast_stop_timer(toast_t* toast) {
    if (!toast || toast->timer_id == 0) {
        return;
    }

    system_timer_autodestroy_cancel(toast->timer_id);
    toast->timer_id = 0;
}

/**
 * @brief 重新按当前配置启动 Toast 自动关闭定时器。
 *
 * @param toast 目标 Toast 句柄。
 * @return 无返回值。
 */
static void toast_start_timer(toast_t* toast) {
    uint32_t timer_id = 0;

    if (!toast_is_valid(toast)) {
        return;
    }

    toast_stop_timer(toast);

    if (toast->duration_ms > 0) {
        if (system_timer_autodestroy_start(toast->duration_ms, toast_timer_cb, toast, &timer_id)) {
            toast->timer_id = timer_id;
        }
    }
}

/**
 * @brief 批量应用 Toast 配置与文本。
 *
 * @param toast 目标 Toast 句柄。
 * @param text Toast 文本；传 `NULL` 时返回 `NULL`。
 * @param cfg Toast 配置；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
static void toast_apply_cfg(toast_t* toast, const char* text, const toast_cfg_t* cfg) {
    toast_cfg_t default_cfg;

    if (!toast_is_valid(toast) || !toast->label || !text) {
        return;
    }

    default_cfg = toast_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    toast->id = cfg->id;
    toast->duration_ms = cfg->duration_ms;
    toast->level = cfg->level;
    toast_sync_layout(toast, text, cfg);
}

/**
 * @brief 创建一个 Toast 实例。
 *
 * @param parent 父对象；传 `NULL` 时退化为当前活动屏幕。
 * @param text 初始文本。
 * @param cfg Toast 配置；传 `NULL` 时使用默认配置。
 * @return 成功返回 Toast 句柄，失败返回 `NULL`。
 */
static toast_t* toast_create(const char* text, const toast_cfg_t* cfg) {
    toast_t* toast = NULL;
    lv_obj_t* parent = toast_get_parent();

    if (!text || !parent) {
        return NULL;
    }

    toast = (toast_t*)malloc(sizeof(*toast));
    if (!toast) {
        return NULL;
    }
    memset(toast, 0, sizeof(*toast));

    toast->box = container_create(parent, NULL);
    if (!toast->box) {
        free(toast);
        floatair_err("toast create: container create failed");
        return NULL;
    }

    lv_obj_add_event_cb(container_get_obj(toast->box), toast_on_delete, LV_EVENT_DELETE, toast);

    toast->label = label_create(container_get_obj(toast->box), NULL);
    if (!toast->label) {
        ui_widget_destroy(UI_WIDGET(toast->box));
        free(toast);
        floatair_err("toast create: label create failed");
        return NULL;
    }

    toast_apply_cfg(toast, text, cfg);
    return toast;
}

/**
 * @brief Toast 自动关闭定时器回调。
 *
 * @param user_data Toast 句柄。
 * @return 无返回值。
 */
static void toast_timer_cb(void* user_data) {
    toast_t* toast = (toast_t*)user_data;

    if (toast != s_active_toast || !toast_is_valid(toast)) {
        floatair_dbg("toast timer cb: toast not active or invalid");
        return;
    }

    toast->timer_id = 0;
    ui_widget_destroy(UI_WIDGET(toast->box));
    floatair_dbg("toast timer cb: toast %p closed", toast);
}

/**
 * @brief Toast 删除事件回调。
 *
 * 容器被删除时负责回收定时器并释放 Toast 句柄。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void toast_on_delete(lv_event_t* e) {
    toast_t* toast = (toast_t*)lv_event_get_user_data(e);

    if (!toast) {
        floatair_dbg("toast on delete: toast is NULL");
        return;
    }

    toast_stop_timer(toast);

    toast->box = NULL;
    toast->label = NULL;

    if (s_active_toast == toast) {
        s_active_toast = NULL;
    }

    free(toast);
    toast = NULL;
    floatair_dbg("toast on delete: toast %p freed", toast);
}

/**
 * @brief 创建并显示一个 Toast。
 *
 * @param text Toast 文本。
 * @return 成功返回 Toast 句柄，失败返回 `NULL`。
 */
toast_t* toast_show(const char* text) {
    if (!text) {
        return NULL;
    }

    return toast_show_with_cfg(text, NULL);
}

/**
 * @brief 按指定等级创建并显示一个 Toast。
 *
 * @param text Toast 文本；传 `NULL` 时返回 `NULL`。
 * @param level Toast 显示等级；数值越小优先级越高。
 * @return 成功返回 Toast 句柄，失败或被高等级 Toast 拦截时返回 `NULL`。
 */
toast_t* toast_show_with_level(const char* text, uint8_t level) {
    toast_cfg_t cfg;

    if (!text) {
        return NULL;
    }

    cfg = toast_default_cfg();
    cfg.level = level;
    return toast_show_with_cfg(text, &cfg);
}

/**
 * @brief 使用完整配置创建并显示一个 Toast。
 *
 * @param text Toast 文本；传 `NULL` 时返回 `NULL`。
 * @param cfg Toast 配置；传 `NULL` 时使用默认配置。
 * @return 成功返回 Toast 句柄，失败或被高等级 Toast 拦截时返回 `NULL`。
 */
toast_t* toast_show_with_cfg(const char* text, const toast_cfg_t* cfg) {
    toast_t* toast = NULL;
    toast_cfg_t default_cfg;

    if (!text) {
        return NULL;
    }

    default_cfg = toast_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    if (!toast_is_valid(s_active_toast)) {
        s_active_toast = NULL;
    }
    if (s_active_toast) {
        if (cfg->level > s_active_toast->level) {
            return NULL;
        }

        if (toast_parent_matches(s_active_toast)) {
            toast_apply_cfg(s_active_toast, text, cfg);
            ui_widget_move_foreground(UI_WIDGET(s_active_toast->box));
            toast_start_timer(s_active_toast);
            return s_active_toast;
        }
    }

    toast = toast_create(text, cfg);
    if (!toast) {
        return NULL;
    }

    if (s_active_toast) {
        toast_t* prev = s_active_toast;
        ui_widget_destroy(UI_WIDGET(prev->box));
    }

    ui_widget_move_foreground(UI_WIDGET(toast->box));
    toast_start_timer(toast);

    s_active_toast = toast;
    return toast;
}

/**
 * @brief 按业务标识关闭当前活动 Toast。
 *
 * @param id 需要关闭的 Toast 业务标识。
 * @return 无返回值。
 */
void toast_dismiss(uint32_t id) {
    toast_t* toast = s_active_toast;

    if (!toast) {
        return;
    }

    if (toast->id != id) {
        return;
    }

    if (!toast_is_valid(toast)) {
        s_active_toast = NULL;
        return;
    }

    ui_widget_destroy(UI_WIDGET(toast->box));
}

/**
 * @brief 关闭当前活动 Toast。
 * @return 无返回值。
 */
void toast_dismiss_active(void) {
    toast_t* toast = s_active_toast;

    if (!toast_is_valid(toast)) {
        s_active_toast = NULL;
        return;
    }

    ui_widget_destroy(UI_WIDGET(toast->box));
}
