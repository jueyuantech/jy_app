/**
 * @file notify_list_view.c
 * @brief Notify list 弹窗视图实现。
 */
#include "notify_list.h"

#include "notify_list_card_ui.h"
#include "notify_list_popup_ui.h"
#include "app_def.h"
#include "common/app_framework/app_layers.h"
#include "common/widgets/container.h"
#include "common/widgets/img.h"
#include "common/widgets/label.h"
#include "common/widgets/ui_widget.h"
#include "lvgl.h"
#include "system/system.h"
#include "system/system_notification.h"

#include <string.h>

static notify_list_popup_ui_t s_ui;
static system_notification_entry_t s_notify_items[SYSTEM_NOTIFICATION_QUEUE_MAX] = {0};
static size_t s_notify_item_count = 0;

/**
 * @brief 获取通知列表项标题。
 * @param[in] item 通知列表项。
 * @return 返回可展示标题文本。
 */
static const char* notify_list_item_title(const system_notification_entry_t* item) {
    if (!item) {
        return "";
    }
    if (item->has_title && item->title[0] != '\0') {
        return item->title;
    }
    return app_get_str("NOTIFY_LIST_UNTITLED");
}

/**
 * @brief 根据通知列表项添加一张通知卡片。
 * @param[in] item 通知列表项。
 * @return 无返回值。
 */
static void notify_list_add_card(const system_notification_entry_t* item) {
    notify_list_card_ui_t card_ui;
    lv_obj_t* image_obj = NULL;

    if (item == NULL || s_ui.list == NULL ||
        !notify_list_card_init_ui(container_get_obj(s_ui.list), &card_ui)) {
        return;
    }

    if (card_ui.image != NULL) {
        image_obj = ui_widget_get_obj(UI_WIDGET(card_ui.image));
    }
    if (image_obj != NULL) {
        if (!item->has_image && !item->has_image_dsc && !item->has_image_path) {
            lv_obj_add_flag(image_obj, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
        } else {
            lv_obj_remove_flag(image_obj, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
            if (item->has_image_dsc) {
                img_set_src(card_ui.image, item->image_dsc);
            } else if (item->has_image_path) {
                img_set_src(card_ui.image, item->image_path);
            } else if (!img_set_l8_data(card_ui.image,
                                        item->image,
                                        item->image_size,
                                        SYSTEM_NOTIFICATION_IMAGE_WIDTH,
                                        SYSTEM_NOTIFICATION_IMAGE_HEIGHT)) {
                lv_obj_add_flag(image_obj, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
            }
        }
    }

    if (card_ui.title != NULL) {
        label_set_text(card_ui.title, notify_list_item_title(item));
    }
}

/**
 * @brief 重新加载通知列表弹窗内容。
 * @return 无返回值。
 */
void notify_list_view_reload(void) {
    lv_obj_t* list_obj = NULL;

    if (!s_ui.list) {
        return;
    }

    list_obj = container_get_obj(s_ui.list);
    s_notify_item_count = system_notification_copy(s_notify_items, SYSTEM_NOTIFICATION_QUEUE_MAX);
    lv_obj_clean(list_obj);
    if (s_notify_item_count == 0) {
        system_notification_entry_t empty_item = {0};
        strncpy(empty_item.title, app_get_str("NOTIFY_LIST_EMPTY"), sizeof(empty_item.title) - 1);
        empty_item.has_title = true;
        notify_list_add_card(&empty_item);
    } else {
        for (size_t i = 0; i < s_notify_item_count; i++) {
            notify_list_add_card(&s_notify_items[i]);
        }
    }

    lv_obj_update_layout(list_obj);
    lv_obj_scroll_to_y(list_obj, lv_obj_get_scroll_bottom(list_obj), LV_ANIM_OFF);
}

/**
 * @brief 处理通知列表弹窗删除事件。
 * @param[in] e LVGL 删除事件对象。
 * @return 无返回值。
 */
static void notify_list_delete_event_handle(lv_event_t* e) {
    (void)e;
    memset(&s_ui, 0, sizeof(s_ui));
    s_notify_item_count = 0;
    memset(s_notify_items, 0, sizeof(s_notify_items));
}

/**
 * @brief 获取通知列表弹窗父对象。
 * @return 返回可挂载父对象；失败时返回当前屏幕。
 */
static lv_obj_t* notify_list_get_popup_parent(void) {
    lv_obj_t* parent = app_layers_get_popup();

    if (parent != NULL && lv_obj_is_valid(parent)) {
        return parent;
    }

    return lv_screen_active();
}

/**
 * @brief 查询通知列表弹窗是否正在显示。
 * @return `true` 表示正在显示，`false` 表示未显示。
 */
bool notify_list_is_open(void) {
    lv_obj_t* root = container_get_obj(s_ui.popup_root);

    return root != NULL && lv_obj_is_valid(root);
}

/**
 * @brief 将输入事件转发给通知列表弹窗。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示事件已消费，`false` 表示未消费。
 */
bool notify_list_handle_event(lv_event_code_t code) {
    if (!notify_list_is_open()) {
        return false;
    }

    switch (code) {
        case LV_EVENT_CLICKED:
        case LV_EVENT_LONG_PRESSED:
            return true;
        case LV_EVENT_DCLICKED:
            system_notification_clear();
            (void)notify_list_close();
            return true;
        case LV_EVENT_GESTURE_LEFT:
            if (s_ui.list) {
                container_scroll_up(s_ui.list, 3.0f / 4.0f);
            }
            return true;
        case LV_EVENT_GESTURE_RIGHT:
            if (s_ui.list) {
                container_scroll_down(s_ui.list, 3.0f / 4.0f);
            }
            return true;
        default:
            return false;
    }
}

/**
 * @brief 打开通知列表弹窗。
 * @return `true` 表示打开成功，`false` 表示打开失败。
 */
bool notify_list_open(void) {
    lv_obj_t* parent = notify_list_get_popup_parent();
    lv_obj_t* popup_root = NULL;

    if (notify_list_is_open()) {
        lv_obj_move_foreground(container_get_obj(s_ui.popup_root));
        notify_list_view_reload();
        return true;
    }

    if (parent == NULL) {
        floatair_err("notify_list popup parent NULL");
        return false;
    }

    floatair_assert(notify_list_popup_init_ui(parent, &s_ui), "notify_list popup init failed");
    popup_root = container_get_obj(s_ui.popup_root);
    floatair_assert(popup_root != NULL, "notify_list popup root NULL");
    lv_obj_add_event_cb(popup_root, notify_list_delete_event_handle, LV_EVENT_DELETE, NULL);

    lv_obj_move_foreground(popup_root);
    notify_list_view_reload();
    return true;
}

/**
 * @brief 关闭通知列表弹窗。
 * @return `true` 表示关闭成功，`false` 表示关闭失败。
 */
bool notify_list_close(void) {
    lv_obj_t* popup_root = container_get_obj(s_ui.popup_root);

    if (!notify_list_is_open()) {
        notify_list_delete_event_handle(NULL);
        return true;
    }

    lv_obj_add_flag(popup_root, LV_OBJ_FLAG_HIDDEN);
    memset(&s_ui, 0, sizeof(s_ui));
    s_notify_item_count = 0;
    memset(s_notify_items, 0, sizeof(s_notify_items));
    lv_obj_delete_async(popup_root);
    return true;
}
