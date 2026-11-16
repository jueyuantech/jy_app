/**
 * @file speech_runtime.c
 * @brief Speech 应用生命周期、STT 服务初始化和页面入口实现。
 */
#include "common/speech/speech_runtime.h"

#include "app_def.h"
#include "common/app_framework/app_nav.h"
#include "common/app_framework/app_router.h"
#include "floatair_fs.h"
#include "common/speech/speech_view.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_config_json.h"

#include <string.h>

extern void rpmsgttf_cache_bitmap_enable(bool enable);

#define SPEECH_DEFAULT_EXIT_DOUBLE_CLICK true      ///< Speech 配置缺失时默认启用双击退出确认。
#define SPEECH_DEFAULT_REPORT_DOUBLE_CLICK false   ///< Speech 配置缺失时默认由眼镜端处理双击退出。
#define SPEECH_APP_PROFILE_MAX 8                   ///< Speech 可注册逻辑入口上限。

static const speech_app_profile_t* s_speech_profiles[SPEECH_APP_PROFILE_MAX];
static app_message_t s_speech_msgs[SPEECH_APP_PROFILE_MAX];
static app_t s_speech_apps[SPEECH_APP_PROFILE_MAX];
static size_t s_speech_profile_count = 0;
static bool s_speech_msg_registered = false;    ///< Speech 消息是否已注册。
static bool s_speech_exit_double_click_confirm_enabled = SPEECH_DEFAULT_EXIT_DOUBLE_CLICK; ///< 双击退出是否需要确认。
static bool s_speech_report_double_click_enabled = SPEECH_DEFAULT_REPORT_DOUBLE_CLICK; ///< 双击是否仅上报手机。
static bool s_speech_bordered_stt_enabled = false; ///< 当前 Speech 应用是否使用左右带框气泡。

static const speech_app_profile_t* speech_current_profile(void) {
    return speech_profile_from_app_name(app_manager_current_name());
}

const speech_app_profile_t* speech_profile_from_app_name(const char* app_name) {
    if (app_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < s_speech_profile_count; ++i) {
        if (strcmp(app_name, s_speech_profiles[i]->app_name) == 0) {
            return s_speech_profiles[i];
        }
    }
    return NULL;
}

const speech_app_profile_t* speech_profile_from_msg_id(uint32_t msg_id) {
    for (size_t i = 0; i < s_speech_profile_count; ++i) {
        if (s_speech_profiles[i]->msg_id == msg_id) {
            return s_speech_profiles[i];
        }
    }
    return NULL;
}

bool speech_exit_double_click_confirm_enabled(void) {
    return s_speech_exit_double_click_confirm_enabled;
}

bool speech_report_double_click_enabled(void) {
    return s_speech_report_double_click_enabled;
}

bool speech_bordered_stt_enabled(void) {
    return s_speech_bordered_stt_enabled;
}

void speech_set_bordered_stt_enabled(bool enabled) {
    if (s_speech_bordered_stt_enabled == enabled) {
        return;
    }
    s_speech_bordered_stt_enabled = enabled;
    speech_on_bordered_stt_changed();
}

static bool speech_msg_register_once(void) {
    if (s_speech_msg_registered) {
        return true;
    }

    for (size_t i = 0; i < s_speech_profile_count; ++i) {
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

    for (size_t i = 0; i < s_speech_profile_count; ++i) {
        int ret = app_msg_delete(s_speech_msgs[i].id);
        floatair_assert(ret == 0, "app_msg_delete failed");
    }
    s_speech_msg_registered = false;
}

static bool speech_service_init(const speech_app_profile_t* profile) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    cJSON* root = NULL;
    cJSON* exit_double_click = NULL;
    cJSON* report_double_click = NULL;
    cJSON* bordered_stt = NULL;

    if (profile == NULL ||
        !floatair_fs_get_app_config_file(profile->config_name,
                                         config_path,
                                         sizeof(config_path))) {
        floatair_err("get app config file failed");
        return false;
    }
    root = system_config_load_json(config_path);
    if (root == NULL) {
        return false;
    }
    exit_double_click = cJSON_GetObjectItemCaseSensitive(root, "exitdoubleclick");
    report_double_click = cJSON_GetObjectItemCaseSensitive(root, "reportdoubleclick");
    bordered_stt = cJSON_GetObjectItemCaseSensitive(root, "borderedstt");
    s_speech_exit_double_click_confirm_enabled = cJSON_IsTrue(exit_double_click);
    s_speech_report_double_click_enabled = cJSON_IsTrue(report_double_click);
    s_speech_bordered_stt_enabled = cJSON_IsTrue(bordered_stt);
    cJSON_Delete(root);
    stt_service_init(config_path);
    return true;
}

static void speech_app_on_start(void) {
    const speech_app_profile_t* profile = speech_current_profile();

    if (profile == NULL) {
        floatair_assert(false, "speech current profile NULL");
        return;
    }

    system_runtime_state_set_kws_intercept(
        SYSTEM_KWS_INTERCEPT_REASON_SPEECH_ACTIVE,
        false);
    rpmsgttf_cache_bitmap_enable(true);

    if (!speech_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!speech_service_init(profile)) {
        floatair_assert(false, "speech service init failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)speech_page_get(), NULL, 0)) {
        floatair_assert(false, "speech page replace failed");
    }
}

static void speech_app_on_stop(void) {
    system_runtime_state_set_kws_intercept(
        SYSTEM_KWS_INTERCEPT_REASON_SPEECH_ACTIVE,
        false);
    speech_msg_unregister_if_needed();
    speech_stt_clear();
    stt_service_deinit();
    s_speech_bordered_stt_enabled = false;
    rpmsgttf_cache_bitmap_enable(false);
}

bool speech_runtime_register(const void* profile_context) {
    const speech_app_profile_t* profile = (const speech_app_profile_t*)profile_context;
    size_t index = s_speech_profile_count;

    if (profile == NULL || profile->app_name == NULL || profile->app_name[0] == '\0' ||
        profile->config_name == NULL || profile->config_name[0] == '\0' ||
        profile->msg_id == 0 || profile->exit_message_key == NULL ||
        index >= SPEECH_APP_PROFILE_MAX ||
        speech_profile_from_app_name(profile->app_name) != NULL ||
        speech_profile_from_msg_id(profile->msg_id) != NULL) {
        return false;
    }
    for (size_t i = 0; i < s_speech_profile_count; ++i) {
        if (strcmp(profile->config_name, s_speech_profiles[i]->config_name) == 0) {
            return false;
        }
    }

    s_speech_profiles[index] = profile;
    s_speech_msgs[index].id = profile->msg_id;
    s_speech_msgs[index].name = (char*)profile->app_name;
    s_speech_msgs[index].cb = speech_route_cmd;

    s_speech_apps[index].name = profile->app_name;
    s_speech_apps[index].on_start = speech_app_on_start;
    s_speech_apps[index].on_resume = NULL;
    s_speech_apps[index].on_pause = NULL;
    s_speech_apps[index].on_stop = speech_app_on_stop;
    s_speech_apps[index].on_back = NULL;

    if (!app_manager_register(&s_speech_apps[index])) {
        s_speech_profiles[index] = NULL;
        s_speech_msgs[index] = (app_message_t){0};
        s_speech_apps[index] = (app_t){0};
        return false;
    }
    s_speech_profile_count++;
    return true;
}
