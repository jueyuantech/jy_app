/**
 * @file speech.c
 * @brief Speech 应用生命周期、STT 服务初始化和页面入口实现。
 */
#include "speech.h"

#include "app_def.h"
#include "common/app_framework/app_nav.h"
#include "common/app_framework/app_router.h"
#include "floatair_fs.h"
#include "speech_view.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_config_json.h"

#include <string.h>

extern void rpmsgttf_cache_bitmap_enable(bool enable);

#define SPEECH_FACTORYRESET_HANDLER_NAME "speech" ///< Speech 模块恢复出厂处理器名称。
#define SPEECH_DEFAULT_FONT_WEIGHT 32              ///< Speech 配置缺失时使用的默认字号。
#define SPEECH_DEFAULT_EXIT_DOUBLE_CLICK true      ///< Speech 配置缺失时默认启用双击退出确认。

static const speech_app_entry_t s_speech_entries[] = {
    {
        .app_name = APP_NAME_TRANSCRIBE,
        .msg_id = APP_MSG_ID_TRANSCRIBE,
        .view_mode = SPEECH_VIEW_MODE_TRANSCRIBE,
    },
    {
        .app_name = APP_NAME_TRANSLATE,
        .msg_id = APP_MSG_ID_TRANSLATE,
        .view_mode = SPEECH_VIEW_MODE_TRANSLATE,
    }
};

static app_message_t s_speech_msgs[sizeof(s_speech_entries) / sizeof(s_speech_entries[0])];
static app_t s_speech_apps[sizeof(s_speech_entries) / sizeof(s_speech_entries[0])];
static bool s_speech_msg_registered = false;    ///< Speech 消息是否已注册。
static bool s_speech_exit_double_click_confirm_enabled = SPEECH_DEFAULT_EXIT_DOUBLE_CLICK; ///< 双击退出是否需要确认。

static cJSON* speech_config_create_default_root(bool exit_double_click) {
    cJSON* root = cJSON_CreateObject();
    cJSON* fontinfo = NULL;

    if (root == NULL) {
        return NULL;
    }
    fontinfo = cJSON_AddObjectToObject(root, "fontinfo");
    if (fontinfo == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(SPEECH_DEFAULT_FONT_WEIGHT));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(root, "exitdoubleclick", cJSON_CreateBool(exit_double_click));
    return root;
}

static bool speech_config_is_valid_root(cJSON* root) {
    cJSON* fontinfo = NULL;
    cJSON* weight = NULL;
    cJSON* word_space = NULL;
    cJSON* row_space = NULL;
    cJSON* exit_double_click = NULL;

    if (root == NULL || !cJSON_IsObject(root)) {
        return false;
    }
    fontinfo = cJSON_GetObjectItemCaseSensitive(root, "fontinfo");
    if (!cJSON_IsObject(fontinfo)) {
        return false;
    }
    weight = cJSON_GetObjectItemCaseSensitive(fontinfo, "weight");
    word_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "wordSpace");
    row_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "rowSpace");
    exit_double_click = cJSON_GetObjectItemCaseSensitive(root, "exitdoubleclick");
    return cJSON_IsNumber(weight) &&
           cJSON_IsNumber(word_space) &&
           cJSON_IsNumber(row_space) &&
           cJSON_IsBool(exit_double_click);
}

static bool speech_config_write_default(const char* config_path, bool exit_double_click) {
    cJSON* root = speech_config_create_default_root(exit_double_click);
    int ret = 0;

    if (root == NULL) {
        return false;
    }
    ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

static bool speech_config_ensure(const char* config_path) {
    cJSON* root = load_json(config_path);

    if (root != NULL) {
        bool ok = speech_config_is_valid_root(root);
        cJSON_Delete(root);
        if (ok) {
            return true;
        }
    }
    return speech_config_write_default(config_path, SPEECH_DEFAULT_EXIT_DOUBLE_CLICK);
}

static const speech_app_entry_t* speech_current_entry(void) {
    return speech_entry_from_app_name(app_manager_current_name());
}

const speech_app_entry_t* speech_entry_from_app_name(const char* app_name) {
    if (app_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_speech_entries) / sizeof(s_speech_entries[0]); ++i) {
        if (strcmp(app_name, s_speech_entries[i].app_name) == 0) {
            return &s_speech_entries[i];
        }
    }
    return NULL;
}

const speech_app_entry_t* speech_entry_from_msg_id(uint32_t msg_id) {
    for (size_t i = 0; i < sizeof(s_speech_entries) / sizeof(s_speech_entries[0]); ++i) {
        if (s_speech_entries[i].msg_id == msg_id) {
            return &s_speech_entries[i];
        }
    }
    return NULL;
}

bool speech_exit_double_click_confirm_enabled(void) {
    return s_speech_exit_double_click_confirm_enabled;
}

static bool speech_factoryreset_handler(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    cJSON* root = NULL;
    cJSON* exit_double_click = NULL;
    bool enabled = SPEECH_DEFAULT_EXIT_DOUBLE_CLICK;

    if (!floatair_fs_get_app_config_file(SPEECH_CONFIG_APP_NAME, config_path, sizeof(config_path))) {
        return false;
    }
    root = load_json(config_path);
    if (root != NULL) {
        exit_double_click = cJSON_GetObjectItemCaseSensitive(root, "exitdoubleclick");
        if (cJSON_IsBool(exit_double_click)) {
            enabled = cJSON_IsTrue(exit_double_click);
        }
        cJSON_Delete(root);
    }
    return speech_config_write_default(config_path, enabled);
}

static bool speech_msg_register_once(void) {
    if (s_speech_msg_registered) {
        return true;
    }

    for (size_t i = 0; i < sizeof(s_speech_msgs) / sizeof(s_speech_msgs[0]); ++i) {
        if (app_msg_register(&s_speech_msgs[i]) != 0) {
            while (i > 0) {
                --i;
                (void)app_msg_delete(s_speech_msgs[i].id);
            }
            return false;
        }
    }

    s_speech_msg_registered = true;
    return true;
}

static void speech_msg_unregister_if_needed(void) {
    if (!s_speech_msg_registered) {
        return;
    }

    for (size_t i = 0; i < sizeof(s_speech_msgs) / sizeof(s_speech_msgs[0]); ++i) {
        int ret = app_msg_delete(s_speech_msgs[i].id);
        floatair_assert(ret == 0, "app_msg_delete failed");
    }
    s_speech_msg_registered = false;
}

static bool speech_service_init(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    cJSON* root = NULL;
    cJSON* exit_double_click = NULL;

    if (!floatair_fs_get_app_config_file(SPEECH_CONFIG_APP_NAME, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return false;
    }
    if (!speech_config_ensure(config_path)) {
        return false;
    }
    root = load_json(config_path);
    if (root == NULL) {
        return false;
    }
    exit_double_click = cJSON_GetObjectItemCaseSensitive(root, "exitdoubleclick");
    s_speech_exit_double_click_confirm_enabled = cJSON_IsTrue(exit_double_click);
    cJSON_Delete(root);
    stt_service_init(config_path);
    return true;
}

static void speech_app_on_start(void) {
    const speech_app_entry_t* entry = speech_current_entry();

    if (entry == NULL) {
        floatair_assert(false, "speech current entry NULL");
        return;
    }

    rpmsgttf_cache_bitmap_enable(true);

    if (!speech_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!speech_service_init()) {
        floatair_assert(false, "speech service init failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)speech_page_get(), NULL, 0)) {
        floatair_assert(false, "speech page replace failed");
    }
}

static void speech_app_on_stop(void) {
    speech_msg_unregister_if_needed();
    speech_stt_clear();
    stt_service_deinit();
    rpmsgttf_cache_bitmap_enable(false);
}

bool speech_app_register(void) {
    for (size_t i = 0; i < sizeof(s_speech_entries) / sizeof(s_speech_entries[0]); ++i) {
        s_speech_msgs[i].id = s_speech_entries[i].msg_id;
        s_speech_msgs[i].name = (char*)s_speech_entries[i].app_name;
        s_speech_msgs[i].cb = speech_route_cmd;

        s_speech_apps[i].name = s_speech_entries[i].app_name;
        s_speech_apps[i].on_start = speech_app_on_start;
        s_speech_apps[i].on_resume = NULL;
        s_speech_apps[i].on_pause = NULL;
        s_speech_apps[i].on_stop = speech_app_on_stop;
        s_speech_apps[i].on_back = NULL;
    }

    (void)system_factoryreset_register(SPEECH_FACTORYRESET_HANDLER_NAME, speech_factoryreset_handler);

    for (size_t i = 0; i < sizeof(s_speech_apps) / sizeof(s_speech_apps[0]); ++i) {
        if (!app_manager_register(&s_speech_apps[i])) {
            return false;
        }
    }
    return true;
}
