#include "msgbox.h"

#include <string.h>

#include "app_def.h"
#include "floatair_lcd.h"
#include "button.h"
#include "common/app_framework/app_layers.h"
#include "container.h"
#include "label.h"
#include "system/system_res.h"

/**
 * @brief 消息框实例，保存弹窗本体、标题、左右按钮和当前选中状态。
 */
struct msgbox_t {
    container_t* dlg;     ///< 最外层弹窗容器。
    label_t* title;       ///< 顶部提示文本组件。
    container_t* actions;///< 底部按钮行布局容器。
    button_t* btn_left;   ///< 左侧按钮组件。
    button_t* btn_right;  ///< 右侧按钮组件。
    msgbox_key_t sel;     ///< 当前高亮的按钮。
    msgbox_cfg_t cfg;     ///< 当前生效的配置缓存。
    msgbox_event_cb_t cb; ///< 确认结果回调。
    void* user_data;      ///< 回调透传数据。
};

static msgbox_t* s_active_msgbox = NULL; ///< 当前活动消息框。

static msgbox_key_t msgbox_key_handler(msgbox_t* box, lv_event_code_t code);

/**
 * @brief 获取弹层应挂载的父对象。
 *
 * 优先挂到全局弹窗层，使消息框不再依赖当前页面栈；
 * 弹窗层不可用时回退到当前活动屏幕。
 *
 * @return 返回弹层父对象；都不可用时返回 `NULL`。
 */
static lv_obj_t* msgbox_get_parent(void)
{
    lv_obj_t* popup = app_layers_get_popup();
    if (popup != NULL && lv_obj_is_valid(popup)) {
        return popup;
    }

    return lv_screen_active();
}

/**
 * @brief 判断消息框句柄及内部对象是否有效。
 *
 * @param box 目标消息框组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
bool msgbox_is_valid(const msgbox_t* box)
{
    return box && box->dlg && container_is_valid(box->dlg);
}

/**
 * @brief 获取默认消息框配置。
 *
 * @return 返回填充好默认值的消息框配置结构体。
 */
msgbox_cfg_t msgbox_default_cfg(void)
{
    msgbox_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.dlg = container_default_cfg();
    cfg.dlg.w = 330;
    cfg.dlg.h = LV_SIZE_CONTENT;
    cfg.dlg.opa = LV_OPA_COVER;
    cfg.dlg.radius = 18;
    cfg.dlg.border_width = 2;
    cfg.title = label_default_cfg();
    cfg.actions = container_default_cfg();
    cfg.button = button_default_cfg();
    cfg.title_width = 0;
    cfg.top_pad = 24;
    cfg.bottom_pad = 20;
    cfg.title_button_pad = 18;
    cfg.button_x_pad = 24;
    cfg.message = "";
    cfg.left.key = MSGBOX_KEY_CANCEL;
    cfg.right.key = MSGBOX_KEY_CONFIRM;
    cfg.left.text = app_get_str("MSGBOX_CANCEL");
    cfg.right.text = app_get_str("MSGBOX_CONFIRM");
    return cfg;
}

/**
 * @brief 获取预定义语义键对应的默认按钮文案。
 *
 * @param key 按钮语义键。
 * @return 返回对应默认文案；未定义语义返回 `NULL`。
 */
static const char* msgbox_key_default_text(msgbox_key_t key)
{
    switch (key) {
    case MSGBOX_KEY_CANCEL:
        return app_get_str("MSGBOX_CANCEL");
    case MSGBOX_KEY_SAVE:
        return app_get_str("MSGBOX_SAVE");
    case MSGBOX_KEY_OK:
        return app_get_str("MSGBOX_OK");
    case MSGBOX_KEY_CONFIRM:
        return app_get_str("MSGBOX_CONFIRM");
    case MSGBOX_KEY_YES:
        return app_get_str("MSGBOX_YES");
    case MSGBOX_KEY_NO:
        return app_get_str("MSGBOX_NO");
    case MSGBOX_KEY_LEAVE:
        return app_get_str("MSGBOX_LEAVE");
    default:
        return NULL;
    }
}

/**
 * @brief 判断按键是否属于预定义语义键。
 *
 * @param key 待判断的按键值。
 * @return `true` 表示属于已支持的语义键，`false` 表示不是。
 */
static bool msgbox_is_semantic_key(msgbox_key_t key)
{
    switch (key) {
    case MSGBOX_KEY_CANCEL:
    case MSGBOX_KEY_SAVE:
    case MSGBOX_KEY_OK:
    case MSGBOX_KEY_CONFIRM:
    case MSGBOX_KEY_YES:
    case MSGBOX_KEY_NO:
    case MSGBOX_KEY_LEAVE:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 判断按钮配置是否需要显示。
 *
 * @param action 按钮配置。
 * @return `true` 表示按钮应显示，`false` 表示隐藏。
 */
static bool msgbox_action_visible(const msgbox_action_t* action)
{
    return action && (action->key != MSGBOX_KEY_NONE || (action->text && action->text[0]));
}

/**
 * @brief 从按键位掩码中提取左右按钮要使用的语义键。
 *
 * 当 `keys` 中包含多个预定义语义时，按固定顺序提取前两个；
 * `pick_first == true` 返回第一个，`false` 返回第二个。
 *
 * @param keys 按钮语义位掩码。
 * @param pick_first 是否提取第一个命中的语义键。
 * @return 返回提取到的语义键；没有匹配时返回 `MSGBOX_KEY_NONE`。
 */
static msgbox_key_t msgbox_pick_semantic_key(msgbox_key_t keys, bool pick_first)
{
    static const msgbox_key_t semantic_keys[] = {
        MSGBOX_KEY_CANCEL,
        MSGBOX_KEY_SAVE,
        MSGBOX_KEY_OK,
        MSGBOX_KEY_CONFIRM,
        MSGBOX_KEY_YES,
        MSGBOX_KEY_NO,
        MSGBOX_KEY_LEAVE,
    };
    msgbox_key_t found[2] = {MSGBOX_KEY_NONE, MSGBOX_KEY_NONE};
    uint32_t count = 0;
    uint32_t i = 0;

    for (i = 0; i < sizeof(semantic_keys) / sizeof(semantic_keys[0]); ++i) {
        if ((keys & semantic_keys[i]) == semantic_keys[i]) {
            found[count++] = semantic_keys[i];
            if (count >= 2) {
                break;
            }
        }
    }

    if (count == 0) {
        return MSGBOX_KEY_NONE;
    }

    if (count == 1 && pick_first) {
        return found[0];
    }

    if (count == 1) {
        return MSGBOX_KEY_NONE;
    }

    return pick_first ? found[0] : found[1];
}

/**
 * @brief 根据当前选中侧解析消息框关闭时的最终返回值。
 *
 * 若当前侧配置了预定义语义键，则返回语义键；
 * 否则回退为 `MSGBOX_KEY_LEFT` 或 `MSGBOX_KEY_RIGHT`。
 *
 * @param box 目标消息框组件句柄。
 * @param side 当前选中的按钮侧别。
 * @return 返回最终确认结果。
 */
static msgbox_key_t msgbox_resolve_result(const msgbox_t* box, msgbox_key_t side)
{
    msgbox_key_t key = MSGBOX_KEY_NONE;

    if (!box) {
        return MSGBOX_KEY_NONE;
    }

    if (side == MSGBOX_KEY_LEFT) {
        key = box->cfg.left.key;
        return msgbox_is_semantic_key(key) ? key : MSGBOX_KEY_LEFT;
    }

    if (side == MSGBOX_KEY_RIGHT) {
        key = box->cfg.right.key;
        return msgbox_is_semantic_key(key) ? key : MSGBOX_KEY_RIGHT;
    }

    return MSGBOX_KEY_NONE;
}

/**
 * @brief 根据当前选中项刷新左右按钮的高亮状态。
 *
 * @param box 目标消息框组件句柄。
 * @return 无返回值。
 */
static void msgbox_apply_btn_state(msgbox_t* box)
{
    lv_opa_t sel_opa = LV_OPA_COVER;
    lv_opa_t unsel_opa = (lv_opa_t)(LV_OPA_COVER * 70 / 100);

    if (!box) {
        return;
    }

    if (box->btn_left) {
        button_set_opacity(box->btn_left, box->sel == MSGBOX_KEY_LEFT ? sel_opa : unsel_opa);
    }

    if (box->btn_right) {
        button_set_opacity(box->btn_right, box->sel == MSGBOX_KEY_RIGHT ? sel_opa : unsel_opa);
    }
}

/**
 * @brief 刷新弹窗的布局容器和尺寸。
 *
 * @param box 目标消息框组件句柄。
 * @return 无返回值。
 */
static void msgbox_sync_layout(msgbox_t* box)
{
    const msgbox_cfg_t* cfg = NULL;
    lv_obj_t* dlg_obj = NULL;
    lv_obj_t* title_obj = NULL;
    lv_obj_t* left_obj = NULL;
    lv_obj_t* right_obj = NULL;
    bool left_visible = false;
    bool right_visible = false;
    lv_coord_t actions_w = 0;
    lv_coord_t title_w = 0;

    if (!msgbox_is_valid(box)) {
        return;
    }
    cfg = &box->cfg;
    dlg_obj = container_get_obj(box->dlg);
    title_obj = box->title ? label_get_obj(box->title) : NULL;
    left_obj = box->btn_left ? button_get_obj(box->btn_left) : NULL;
    right_obj = box->btn_right ? button_get_obj(box->btn_right) : NULL;
    left_visible = msgbox_action_visible(&cfg->left);
    right_visible = msgbox_action_visible(&cfg->right);
    if (!dlg_obj || !box->actions || !title_obj || !left_obj || !right_obj) {
        return;
    }

    ui_widget_set_size(UI_WIDGET(box->dlg), (lv_coord_t)cfg->dlg.w, LV_SIZE_CONTENT);
    container_set_opacity(box->dlg, cfg->dlg.opa);
    container_set_border_width(box->dlg, (lv_coord_t)cfg->dlg.border_width);
    container_set_radius(box->dlg, (lv_coord_t)cfg->dlg.radius);
    lv_obj_set_style_pad_top(dlg_obj, (lv_coord_t)cfg->top_pad, 0);
    lv_obj_set_style_pad_bottom(dlg_obj, (lv_coord_t)cfg->bottom_pad, 0);
    lv_obj_set_style_pad_left(dlg_obj, (lv_coord_t)cfg->dlg.pad_hor, 0);
    lv_obj_set_style_pad_right(dlg_obj, (lv_coord_t)cfg->dlg.pad_hor, 0);
    lv_obj_align(dlg_obj, LV_ALIGN_CENTER, 0, 0);

    title_w = (lv_coord_t)cfg->title_width;
    if (title_w <= 0 || title_w == LV_SIZE_CONTENT) {
        title_w = (lv_coord_t)cfg->dlg.w - 2 * (lv_coord_t)cfg->button_x_pad;
        if (title_w < 0) {
            title_w = 0;
        }
    }
    ui_widget_set_size(UI_WIDGET(box->title), title_w, LV_SIZE_CONTENT);

    actions_w = (lv_coord_t)cfg->button.w;
    if (left_visible && right_visible) {
        actions_w = (lv_coord_t)cfg->dlg.w - 2 * (lv_coord_t)cfg->button_x_pad;
        if (actions_w < 2 * (lv_coord_t)cfg->button.w) {
            actions_w = 2 * (lv_coord_t)cfg->button.w;
        }
    }
    ui_widget_set_size(UI_WIDGET(box->actions), actions_w, LV_SIZE_CONTENT);

    ui_widget_set_size(UI_WIDGET(box->btn_left), (lv_coord_t)cfg->button.w, (lv_coord_t)cfg->button.h);
    ui_widget_set_size(UI_WIDGET(box->btn_right), (lv_coord_t)cfg->button.w, (lv_coord_t)cfg->button.h);
}

/**
 * @brief 弹窗删除回调，负责释放消息框对象自身。
 *
 * @param e LVGL 删除事件。
 * @return 无返回值。
 */
static void msgbox_on_delete(lv_event_t* e)
{
    msgbox_t* box = (msgbox_t*)lv_event_get_user_data(e);

    if (!box) {
        return;
    }

    if (s_active_msgbox == box) {
        s_active_msgbox = NULL;
    }

    box->dlg = NULL;
    box->title = NULL;
    box->actions = NULL;
    box->btn_left = NULL;
    box->btn_right = NULL;
    lv_free(box);
}

/**
 * @brief 按按钮配置刷新指定按钮的显示文案。
 *
 * @param box 目标消息框组件句柄。
 * @param button 目标按钮组件句柄。
 * @param action 按钮配置；传 `NULL` 时回退为默认文案。
 * @param fallback_text 当语义和文案都不可用时使用的兜底文案。
 * @return 无返回值。
 */
static void msgbox_apply_action(msgbox_t* box,
                                button_t* button,
                                const msgbox_action_t* action,
                                const char* fallback_text)
{
    const char* resolved = NULL;
    msgbox_key_t key = MSGBOX_KEY_NONE;

    if (!msgbox_is_valid(box) || !button) {
        return;
    }

    if (action) {
        key = action->key;
        resolved = (action->text && action->text[0]) ? action->text : msgbox_key_default_text(key);
    }

    button_set_text(button, resolved ? resolved : fallback_text);
}

/**
 * @brief 批量应用消息框配置。
 *
 * @param box 目标消息框组件句柄。
 * @param cfg 消息框配置；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
static void msgbox_apply_cfg(msgbox_t* box, const msgbox_cfg_t* cfg)
{
    msgbox_cfg_t default_cfg;

    if (!msgbox_is_valid(box)) {
        return;
    }

    default_cfg = msgbox_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }
    box->cfg = *cfg;

    container_set_layout_vbox_spaced(box->dlg, cfg->title_button_pad);
    container_set_align(box->dlg,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    container_set_layout_hbox(box->actions);
    container_set_align(box->actions,
                        msgbox_action_visible(&cfg->left) && msgbox_action_visible(&cfg->right)
                            ? CONTAINER_ALIGN_SPACE_BETWEEN
                            : CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container_get_obj(box->actions), 0, 0);

    label_apply_cfg(box->title, &cfg->title);
    button_apply_cfg(box->btn_left, &cfg->button);
    button_apply_cfg(box->btn_right, &cfg->button);
    label_set_align(box->title, LABEL_ALIGN_CENTER);
    label_set_overflow(box->title, LABEL_OVERFLOW_WRAP);
    msgbox_apply_action(box, box->btn_left, &cfg->left, "Left");
    msgbox_apply_action(box, box->btn_right, &cfg->right, "Right");
    ui_widget_set_visible(UI_WIDGET(box->btn_left), msgbox_action_visible(&cfg->left));
    ui_widget_set_visible(UI_WIDGET(box->btn_right), msgbox_action_visible(&cfg->right));
    msgbox_set_text(box, cfg->message);
}

/**
 * @brief 创建消息框组件。
 *
 * @param cfg 消息框配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回消息框组件句柄，失败返回 `NULL`。
 */
static msgbox_t* msgbox_create(const msgbox_cfg_t* cfg)
{
    msgbox_cfg_t default_cfg;
    msgbox_t* box = NULL;
    lv_obj_t* parent = NULL;
    default_cfg = msgbox_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    box = (msgbox_t*)lv_malloc(sizeof(msgbox_t));
    if (!box) {
        return NULL;
    }
    memset(box, 0, sizeof(*box));
    box->sel = MSGBOX_KEY_RIGHT;
    box->cfg = *cfg;

    parent = msgbox_get_parent();
    if (!parent) {
        lv_free(box);
        return NULL;
    }

    box->dlg = container_create(parent, NULL);
    if (!box->dlg) {
        lv_free(box);
        return NULL;
    }
    lv_obj_add_event_cb(container_get_obj(box->dlg), msgbox_on_delete, LV_EVENT_DELETE, box);

    box->title = label_create(container_get_obj(box->dlg), NULL);

    box->actions = container_create(container_get_obj(box->dlg), NULL);

    box->btn_left = button_create(container_get_obj(box->actions), NULL);
    box->btn_right = button_create(container_get_obj(box->actions), NULL);

    if (!box->title || !box->actions || !box->btn_left || !box->btn_right) {
        msgbox_destroy(box);
        return NULL;
    }

    msgbox_apply_cfg(box, cfg);
    ui_widget_set_visible(UI_WIDGET(box->dlg), false);
    return box;
}

/**
 * @brief 在当前活动屏幕上创建消息框组件。
 *
 * @param box 目标消息框组件句柄。
 * @return 无返回值。
 */
void msgbox_destroy(msgbox_t* box)
{
    if (!box) {
        return;
    }

    if (msgbox_is_valid(box)) {
        ui_widget_destroy(UI_WIDGET(box->dlg));
        return;
    }

    lv_free(box);
}

/**
 * @brief 设置消息框确认结果回调。
 * @param[in,out] box 目标消息框组件句柄。
 * @param[in] cb 确认结果回调；传 `NULL` 表示清空。
 * @param[in] user_data 回调透传数据。
 * @return 无返回值。
 */
void msgbox_set_callback(msgbox_t* box, msgbox_event_cb_t cb, void* user_data)
{
    if (!msgbox_is_valid(box)) {
        return;
    }

    box->cb = cb;
    box->user_data = user_data;
}

/**
 * @brief 设置消息框提示文本。
 *
 * @param box 调用者当前持有的消息框对象；传 `NULL` 时会新建。
 * @param message 新的提示文本；为空时内部显示单个空格占位。
 * @return 无返回值。
 */
void msgbox_set_text(msgbox_t* box, const char* message)
{
    if (!msgbox_is_valid(box) || !box->title || !lv_obj_is_valid(label_get_obj(box->title))) {
        return;
    }

    label_set_text(box->title, (message && message[0]) ? message : " ");
    msgbox_sync_layout(box);
}

/**
 * @brief 设置消息框显隐状态。
 *
 * @param box 目标消息框组件句柄。
 * @param show `true` 表示显示，`false` 表示隐藏。
 * @return 无返回值。
 */
void msgbox_set_visible(msgbox_t* box, bool show)
{
    if (!msgbox_is_valid(box)) {
        return;
    }

    if (show) {
        box->sel = msgbox_action_visible(&box->cfg.right) ? MSGBOX_KEY_RIGHT : MSGBOX_KEY_LEFT;
        msgbox_sync_layout(box);
        msgbox_apply_btn_state(box);
        ui_widget_move_foreground(UI_WIDGET(box->dlg));
        ui_widget_set_visible(UI_WIDGET(box->dlg), true);
        s_active_msgbox = box;
    } else {
        ui_widget_set_visible(UI_WIDGET(box->dlg), false);
        if (s_active_msgbox == box) {
            s_active_msgbox = NULL;
        }
    }
}

/**
 * @brief 使用消息文本和按钮语义快速显示消息框。
 *
 * 内部会将位掩码形式的按钮语义转换为完整配置，再复用
 * `msgbox_show_with_cfg()` 完成显示。
 *
 * @param box 调用者当前持有的消息框对象；传 `NULL` 时会新建。
 * @param msg 提示文本；传 `NULL` 时内部显示单个空格占位。
 * @param keys 按钮语义位掩码。
 * @return 返回可继续持有的消息框对象，失败返回 `NULL`。
 */
msgbox_t* msgbox_show(msgbox_t* box, const char* msg, msgbox_key_t keys)
{
    msgbox_cfg_t cfg = msgbox_default_cfg();
    msgbox_key_t left_key = MSGBOX_KEY_NONE;
    msgbox_key_t right_key = MSGBOX_KEY_NONE;

    left_key = msgbox_pick_semantic_key(keys, true);
    right_key = msgbox_pick_semantic_key(keys, false);

    cfg.message = msg;
    if (left_key != MSGBOX_KEY_NONE && right_key == MSGBOX_KEY_NONE) {
        cfg.left.key = MSGBOX_KEY_NONE;
        cfg.right.key = left_key;
    } else {
        cfg.left.key = left_key != MSGBOX_KEY_NONE ? left_key : MSGBOX_KEY_LEFT;
        cfg.right.key = right_key != MSGBOX_KEY_NONE ? right_key : MSGBOX_KEY_RIGHT;
    }

    if (cfg.left.key == MSGBOX_KEY_NONE) {
        cfg.left.text = NULL;
    } else if (!msgbox_is_semantic_key(cfg.left.key)) {
        cfg.left.text = "Left";
    } else {
        cfg.left.text = NULL;
    }

    if (cfg.right.key == MSGBOX_KEY_NONE) {
        cfg.right.text = NULL;
    } else if (!msgbox_is_semantic_key(cfg.right.key)) {
        cfg.right.text = "Right";
    } else {
        cfg.right.text = NULL;
    }

    return msgbox_show_with_cfg(box, &cfg);
}

/**
 * @brief 创建或复用一个消息框，并立即显示。
 *
 * @param box 调用者当前持有的消息框对象；传 `NULL` 时会新建。
 * @param cfg 消息框配置。
 * @return 返回可继续持有的消息框对象，失败返回 `NULL`。
 */
msgbox_t* msgbox_show_with_cfg(msgbox_t* box, const msgbox_cfg_t* cfg)
{
    msgbox_cfg_t default_cfg;
    lv_obj_t* parent = msgbox_get_parent();

    default_cfg = msgbox_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    if (msgbox_is_valid(box) && parent &&
        ui_widget_get_parent(UI_WIDGET(box->dlg)) != parent) {
        msgbox_destroy(box);
        box = NULL;
    }

    if (!msgbox_is_valid(box)) {
        box = msgbox_create(cfg);
        if (!box) {
            return NULL;
        }
    }

    msgbox_apply_cfg(box, cfg);
    msgbox_set_visible(box, true);
    return box;
}

/**
 * @brief 判断消息框当前是否隐藏。
 *
 * @param box 目标消息框组件句柄。
 * @return `true` 表示隐藏或对象无效，`false` 表示当前可见。
 */
bool msgbox_is_hidden(const msgbox_t* box)
{
    if (!msgbox_is_valid(box)) {
        return true;
    }

    return ui_widget_is_hidden(UI_WIDGET(box->dlg));
}

/**
 * @brief 处理当前活动消息框输入事件。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示事件已消费，`false` 表示未消费。
 */
bool msgbox_handle_active_event(lv_event_code_t code)
{
    msgbox_t* box = s_active_msgbox;
    msgbox_key_t result = MSGBOX_KEY_NONE;

    if (!msgbox_is_valid(box) || msgbox_is_hidden(box)) {
        s_active_msgbox = NULL;
        return false;
    }

    switch (code) {
    case LV_EVENT_DCLICKED:
        msgbox_set_visible(box, false);
        return true;
    case LV_EVENT_GESTURE_RIGHT:
    case LV_EVENT_GESTURE_LEFT:
    case LV_EVENT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
        result = msgbox_key_handler(box, code);
        if (result != MSGBOX_KEY_NONE && box->cb) {
            box->cb(box, result, box->user_data);
        }
        return true;
    default:
        return false;
    }
}

/**
 * @brief 处理消息框按键或手势输入。
 *
 * @param box 目标消息框组件句柄。
 * @param code LVGL 事件码。
 * @return 返回最终确认的按钮；仅切换高亮时返回 `MSGBOX_KEY_NONE`。
 */
static msgbox_key_t msgbox_key_handler(msgbox_t* box, lv_event_code_t code)
{
    if (!msgbox_is_valid(box) || msgbox_is_hidden(box)) {
        return MSGBOX_KEY_NONE;
    }

    switch (code) {
    case LV_EVENT_GESTURE_RIGHT:
        box->sel = msgbox_action_visible(&box->cfg.right) ? MSGBOX_KEY_RIGHT : MSGBOX_KEY_LEFT;
        msgbox_apply_btn_state(box);
        break;
    case LV_EVENT_GESTURE_LEFT:
        box->sel = msgbox_action_visible(&box->cfg.left) ? MSGBOX_KEY_LEFT : MSGBOX_KEY_RIGHT;
        msgbox_apply_btn_state(box);
        break;
    case LV_EVENT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
        msgbox_set_visible(box, false);
        return msgbox_resolve_result(box, box->sel);
    default:
        break;
    }

    return MSGBOX_KEY_NONE;
}
