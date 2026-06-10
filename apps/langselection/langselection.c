/**
 * @file langselection.c
 * @brief 语言选择 App 页面、语言文件切换和完成态上报实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_langselection
 */
#include "langselection.h"

#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_nav.h"
#include "common/app_framework/app_router.h"
#include "app_def.h"
#include "floatair_dbg.h"
#include "floatair_fs.h"
#include "i18n.h"
#include "message.h"
#include "common/widgets/roller.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "system/system_def.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define LANG_SELECTION_USE_STATIC_LANG_LIST 1        ///< 语言来源开关：1 使用 s_lang_list 固定顺序，0 遍历 i18n 目录。
#define LANG_SELECTION_ROLLER_WIDTH LV_PCT(80)      ///< 语言滚轮宽度。
#define LANG_SELECTION_ROLLER_ROW_HEIGHT 56         ///< 语言滚轮单行高度。
#define LANG_SELECTION_ROLLER_ROW_GAP 18            ///< 语言滚轮行间距。
#define LANG_SELECTION_ROLLER_RADIUS 10             ///< 语言滚轮选中框圆角。
#define LANG_SELECTION_NOTICE_BOTTOM 8              ///< 底部操作提示文案下边距。
#define LANG_SELECTION_JSON_SUFFIX ".json"          ///< 语言资源文件后缀。

static const char* s_lang_list[] = {
    "en-US.json",
    "zh-CN.json",
    "ko.json",
    "ja.json",
    "de.json",
    "fr-FR.json",
    "es-ES.json",
    "pt-BR.json",
    "it.json",
};

static lv_obj_t* s_lang_cont = NULL;
static roller_t* s_lang_roller = NULL;
static lv_obj_t* s_setting_notice = NULL;
static const lv_font_t* s_font_s = NULL;
static char** s_lang_codes = NULL;
static int s_current_selected = 0;
static int s_lang_count = 0;
static bool s_langselection_msg_registered = false;

static bool langselection_has_json_suffix(const char* name) {
    size_t name_len = 0;
    size_t suffix_len = strlen(LANG_SELECTION_JSON_SUFFIX);

    if (name == NULL) {
        return false;
    }

    name_len = strlen(name);
    return name_len > suffix_len &&
           strcmp(name + name_len - suffix_len, LANG_SELECTION_JSON_SUFFIX) == 0;
}

static char* langselection_dup_lang_code(const char* filename) {
    size_t name_len = 0;
    size_t code_len = 0;
    char* code = NULL;

    if (!langselection_has_json_suffix(filename)) {
        return NULL;
    }

    name_len = strlen(filename);
    code_len = name_len - strlen(LANG_SELECTION_JSON_SUFFIX);
    code = (char*)malloc(code_len + 1);
    if (code == NULL) {
        return NULL;
    }
    memcpy(code, filename, code_len);
    code[code_len] = '\0';
    return code;
}

static void langselection_clear_lang_codes(void) {
    if (s_lang_codes != NULL) {
        for (int i = 0; i < s_lang_count; i++) {
            free(s_lang_codes[i]);
            s_lang_codes[i] = NULL;
        }
        free(s_lang_codes);
        s_lang_codes = NULL;
    }
    s_lang_count = 0;
    s_current_selected = 0;
}

static bool langselection_load_lang_codes(void) {
#if LANG_SELECTION_USE_STATIC_LANG_LIST
    int lang_count = (int)(sizeof(s_lang_list) / sizeof(s_lang_list[0]));

    langselection_clear_lang_codes();
    s_lang_codes = (char**)malloc(sizeof(char*) * lang_count);
    if (s_lang_codes == NULL) {
        floatair_err("malloc static lang codes failed");
        return false;
    }
    memset(s_lang_codes, 0, sizeof(char*) * lang_count);

    for (int i = 0; i < lang_count; i++) {
        s_lang_codes[i] = langselection_dup_lang_code(s_lang_list[i]);
        if (s_lang_codes[i] == NULL) {
            langselection_clear_lang_codes();
            floatair_err("dup static lang code failed: %s", s_lang_list[i]);
            return false;
        }
        s_lang_count++;
    }

    floatair_info("loaded static lang count %d", s_lang_count);
    return s_lang_count > 0;
#else
    const char* i18n_dir = floatair_fs_get_system_i18n_path();
    floatair_dir_t* dir = NULL;
    char namebuf[SYSTEM_MAX_PATH_LEN] = {0};
    bool is_dir = false;
    int cap = 0;

    langselection_clear_lang_codes();
    if (i18n_dir == NULL || i18n_dir[0] == '\0') {
        floatair_err("i18n dir invalid");
        return false;
    }
    if (floatair_fs_dir_open(i18n_dir, &dir) != FLOATAIR_FS_OK) {
        floatair_err("open i18n dir failed: %s", i18n_dir);
        return false;
    }

    for (;;) {
        int ret = floatair_fs_dir_read(dir, namebuf, sizeof(namebuf), &is_dir);
        if (ret != FLOATAIR_FS_OK) {
            break;
        }
        if (namebuf[0] == '\0') {
            break;
        }
        if (is_dir || namebuf[0] == '/' ||
            strcmp(namebuf, ".") == 0 ||
            strcmp(namebuf, "..") == 0 ||
            !langselection_has_json_suffix(namebuf)) {
            continue;
        }

        if (s_lang_count == cap) {
            int next_cap = (cap == 0) ? 8 : cap * 2;
            char** next = (char**)realloc(s_lang_codes, sizeof(char*) * next_cap);
            if (next == NULL) {
                floatair_fs_dir_close(dir);
                langselection_clear_lang_codes();
                floatair_err("realloc lang codes failed");
                return false;
            }
            s_lang_codes = next;
            cap = next_cap;
        }

        s_lang_codes[s_lang_count] = langselection_dup_lang_code(namebuf);
        if (s_lang_codes[s_lang_count] == NULL) {
            floatair_fs_dir_close(dir);
            langselection_clear_lang_codes();
            floatair_err("dup lang code failed: %s", namebuf);
            return false;
        }
        s_lang_count++;
    }

    floatair_fs_dir_close(dir);
    floatair_info("loaded lang count %d", s_lang_count);
    return s_lang_count > 0;
#endif
}

static int find_lang_index_by_curlang(const char* curlang) {
    if (!curlang || curlang[0] == '\0') {
        return 0;
    }
    char want[64] = {0};
    snprintf(want, sizeof(want), "%s", curlang);
    for (int i = 0; i < s_lang_count; i++) {
        if (strcmp(s_lang_codes[i], want) == 0) {
            return i;
        }
    }
    return 0;
}

static void update_lang_notice(void) {
    if (!s_setting_notice || s_lang_count <= 0) {
        return;
    }
    const char* i18n_dir = floatair_fs_get_system_i18n_path();
    char* str = i18n_get_single_string(i18n_dir, s_lang_codes[s_current_selected], "SETTING_NOTICE");
    if (str) {
        lv_label_set_text(s_setting_notice, str);
        free(str);
    }
}

static void on_lang_confirmed(void) {
    if (s_lang_count <= 0) {
        return;
    }
    const char* langcode = s_lang_codes[s_current_selected];
    if (!langcode || langcode[0] == '\0') {
        floatair_err("langcode invalid");
        return;
    }
    if (!system_config_set_langselection_finish(langcode)) {
        floatair_err("set curlang failed");
        return;
    }
    (void)app_router_call_home();
}

static void on_roller_selected_changed(roller_t* roller, uint32_t selected, void* user_data) {
    (void)roller;
    (void)user_data;
    s_current_selected = (int)selected;
    update_lang_notice();
}

static void on_roller_activated(roller_t* roller, uint32_t selected, lv_event_code_t code, void* user_data) {
    (void)roller;
    (void)code;
    (void)user_data;
    s_current_selected = (int)selected;
    on_lang_confirmed();
}

static void touch_event_handle(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    bool* consumed = (bool*)lv_event_get_param(event);

    if (consumed != NULL) {
        *consumed = true;
    }
    if (code == LV_EVENT_CLICKED) {
        return;
    }
    if (s_lang_roller != NULL && roller_key_handler(s_lang_roller, code)) {
        return;
    }
}

static void langselection_page_create(lv_obj_t* root, const app_page_data_t* data) {
    (void)data;
    const char** lang_items = NULL;
    char** lang_item_allocs = NULL;
    const char* i18n_dir = floatair_fs_get_system_i18n_path();
    roller_cfg_t roller_cfg = {0};
    lv_obj_t* roller_obj = NULL;
    floatair_assert(root != NULL, "root NULL");

    floatair_assert(langselection_load_lang_codes(), "load lang codes failed");
    lang_items = (const char**)malloc(sizeof(char*) * s_lang_count);
    floatair_assert(lang_items != NULL, "malloc lang items failed");
    lang_item_allocs = (char**)malloc(sizeof(char*) * s_lang_count);
    floatair_assert(lang_item_allocs != NULL, "malloc lang item allocs failed");
    memset(lang_items, 0, sizeof(char*) * s_lang_count);
    memset(lang_item_allocs, 0, sizeof(char*) * s_lang_count);

    s_current_selected = find_lang_index_by_curlang(system_config_get_curlang());
    for (int i = 0; i < s_lang_count; i++) {
        lang_item_allocs[i] = i18n_get_single_string(i18n_dir, s_lang_codes[i], "LANG_NAME");
        lang_items[i] = (lang_item_allocs[i] != NULL && lang_item_allocs[i][0] != '\0')
                            ? lang_item_allocs[i]
                            : s_lang_codes[i];
    }

    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_font_s = get_font_by_size_near(20);
    floatair_assert(s_font_s != NULL, "font s missing");

    s_lang_cont = lv_obj_create(root);
    lv_obj_remove_style_all(s_lang_cont);
    lv_obj_set_size(s_lang_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_lang_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_lang_cont, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_lang_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_lang_cont, LV_ALIGN_CENTER, 0, 0);

    roller_cfg = roller_default_cfg();
    roller_cfg.items = lang_items;
    roller_cfg.count = (uint32_t)s_lang_count;
    roller_cfg.label.font.weight = 20;
    roller_cfg.selected_font.weight = 36;
    roller_cfg.overflow_mode = ROLLER_OVERFLOW_EXPAND_HEIGHT;
    roller_cfg.row_height = LANG_SELECTION_ROLLER_ROW_HEIGHT;
    roller_cfg.row_gap = LANG_SELECTION_ROLLER_ROW_GAP;
    roller_cfg.selected_pad_ver = 4;
    roller_cfg.radius = LANG_SELECTION_ROLLER_RADIUS;
    s_lang_roller = roller_create(s_lang_cont, &roller_cfg);
    for (int i = 0; i < s_lang_count; i++) {
        if (lang_item_allocs[i] != NULL) {
            free(lang_item_allocs[i]);
            lang_item_allocs[i] = NULL;
        }
    }
    free(lang_item_allocs);
    free(lang_items);
    floatair_assert(s_lang_roller != NULL, "lang roller create failed");
    roller_obj = roller_get_obj(s_lang_roller);
    floatair_assert(roller_obj != NULL, "lang roller obj missing");
    lv_obj_set_width(roller_obj, LANG_SELECTION_ROLLER_WIDTH);
    lv_obj_align(roller_obj, LV_ALIGN_CENTER, 0, 0);
    roller_set_callbacks(s_lang_roller,
                         on_roller_selected_changed,
                         on_roller_activated,
                         NULL);
    roller_set_selected(s_lang_roller, (uint32_t)s_current_selected, false);

    s_setting_notice = lv_label_create(s_lang_cont);
    lv_obj_set_size(s_setting_notice, LV_PCT(90), (lv_coord_t)(get_font_height(s_font_s) * 2 + 6));
    lv_obj_align(s_setting_notice,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 -LANG_SELECTION_NOTICE_BOTTOM);
    lv_obj_set_style_text_color(s_setting_notice, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_setting_notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(s_setting_notice, lv_color_black(), 0);
    obj_set_text_font(s_setting_notice, s_font_s);
    lv_label_set_long_mode(s_setting_notice, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_setting_notice, app_get_str("SETTING_NOTICE"));

    update_lang_notice();
}

static void langselection_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "root NULL");
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_DCLICKED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_LEFT, NULL);
    lv_obj_add_event_cb(root, touch_event_handle, LV_EVENT_GESTURE_RIGHT, NULL);
}

static void langselection_page_destroy(void) {
    if (s_lang_cont && lv_obj_is_valid(s_lang_cont)) {
        lv_obj_delete(s_lang_cont);
        s_lang_cont = NULL;
    }
    s_lang_roller = NULL;
    s_setting_notice = NULL;
    langselection_clear_lang_codes();

    s_font_s = NULL;
}

static bool langselection_msg_cb(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    if (!msg) {
        return false;
    }
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool langselection_host_system_control_allowed(const msg_pack_t* msg) {
    static const char* const allow_cmds[] = {
        "getView",
        "sendTouchEvent",
        "sendHeartbeat",
        "sendKeepAlive",
        "sendHandshake",
        "setTimeConfig"
    };

    if (msg == NULL || msg->id != APP_MSG_ID_SYSTEM) {
        return false;
    }

    for (size_t i = 0; i < sizeof(allow_cmds) / sizeof(allow_cmds[0]); i++) {
        if (strcmp(msg->cmd, allow_cmds[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool langselection_app_on_host_message(msg_pack_t* msg) {
    if (system_config_get_langselection_finish()) {
        return false;
    }
    if (langselection_host_system_control_allowed(msg)) {
        return false;
    }

    if (msg != NULL) {
        floatair_warn("langselection block host mpack, id=%u biz=%s cmd=%s",
                      (unsigned)msg->id,
                      msg->biz,
                      msg->cmd);
        (void)app_mpack_send_ack(msg, ErrNotReady);
    }
    return true;
}

static bool langselection_system_event_allowed(uint16_t event_type) {
    switch (event_type) {
        case SET_IMU_SINGLE_TAP:
        case SET_IMU_DOUBLE_TAP:
        case SET_BAT_SOC_CHANGED:
        case SET_BAT_VOLT_CHANGED:
        case SET_CHARGER_STATE_CHANGED:
        case SET_SLIDE_FORWARD:
        case SET_SLIDE_BACKWORD:
        case SET_FORCE_SINGLE_CLICK:
        case SET_FORCE_DOUBLE_CLICK:
        case SET_FORCE_TRI_CLICK:
        case SET_FORCE_LONG_PRESSED:
        case SET_REPORT_DEVICE_STATE:
        case SET_TWS_LINK_BROKEN:
        case SET_JYP_HOST_DISCONNECTED:
        case SET_JYP_HOST_CONNECTED:
        case SET_JYT_TIMER_TRIGGER:
        case SET_JYT_BT_VISIBLE_CHANGED:
            return true;
        default:
            return false;
    }
}

static bool langselection_app_on_system_event(JYT_ELF_MQ_MSG* msg) {
    uint16_t event_type = 0;

    if (system_config_get_langselection_finish()) {
        return false;
    }
    if (msg == NULL) {
        return true;
    }

    event_type = msg->Header.event_type;
    if (langselection_system_event_allowed(event_type)) {
        return false;
    }

    floatair_info("langselection consume system event %u", (unsigned)event_type);
    return true;
}

static bool langselection_app_on_emerg_message(const char* msg, size_t msg_size) {
    (void)msg;
    (void)msg_size;

    if (system_config_get_langselection_finish()) {
        return false;
    }

    floatair_info("langselection consume emerg message");
    return true;
}

static app_message_t langselection_msg = {
    .id   = APP_MSG_ID_LANGSELECTION,
    .name = APP_NAME_LANGSELECTION,
    .cb   = langselection_msg_cb,
};

static bool langselection_msg_register_once(void) {
    int ret = 0;

    if (s_langselection_msg_registered) {
        return true;
    }

    ret = app_msg_register(&langselection_msg);
    if (ret != 0) {
        return false;
    }
    s_langselection_msg_registered = true;
    return true;
}

static void langselection_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_langselection_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_LANGSELECTION);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_langselection_msg_registered = false;
}

static app_page_t s_langselection_page = {
    .name = APP_NAME_LANGSELECTION,
    .on_create = langselection_page_create,
    .on_appear = langselection_page_appear,
    .on_disappear = NULL,
    .on_destroy = langselection_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

const app_page_t* langselection_page_get(void) {
    return &s_langselection_page;
}

static void langselection_app_on_start(void) {
    system_status_bar_set_mode(false);
    if (!langselection_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)langselection_page_get(), NULL, 0)) {
        floatair_assert(false, "langselection page replace failed");
    }
}

static void langselection_app_on_stop(void) {
    langselection_msg_unregister_if_needed();
    langselection_page_destroy();
}

static app_t s_langselection_app = {
    .name = APP_NAME_LANGSELECTION,
    .use_top_layer = true,
    .on_start = langselection_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = langselection_app_on_stop,
    .on_back = NULL,
    .on_host_message = langselection_app_on_host_message,
    .on_system_event = langselection_app_on_system_event,
    .on_emerg_message = langselection_app_on_emerg_message,
};

bool langselection_app_register(void) {
    return app_manager_register(&s_langselection_app);
}
