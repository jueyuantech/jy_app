/**
 * @file assistant.c
 * @brief Assistant 弹窗模块生命周期实现。
 */
#include "assistant.h"

#include "app_def.h"
#include "common/app_framework/app_router.h"
#include "floatair_fs.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_config_json.h"

#include <string.h>

static bool s_assistant_service_ready = false;          ///< assistant STT 服务是否已初始化。
static bool s_assistant_service_suspended = false;      ///< 是否已挂起底层 STT 服务状态。
static stt_service_snapshot_t s_assistant_stt_snapshot; ///< assistant 打开前的底层 STT 状态快照。
static bool s_assistant_factoryreset_registered = false;

static bool assistant_config_reset_to_default(void);
static bool assistant_config_ensure_by_path(const char* config_path);

static bool assistant_config_is_valid_root(cJSON* root) {
    if (!root || !cJSON_IsObject(root)) {
        return false;
    }
    cJSON* fontinfo = cJSON_GetObjectItemCaseSensitive(root, "fontinfo");
    if (!cJSON_IsObject(fontinfo)) {
        return false;
    }
    cJSON* weight = cJSON_GetObjectItemCaseSensitive(fontinfo, "weight");
    cJSON* word_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "wordSpace");
    cJSON* row_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "rowSpace");
    return cJSON_IsNumber(weight) && cJSON_IsNumber(word_space) && cJSON_IsNumber(row_space);
}

static cJSON* assistant_config_create_default_root(void) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON* fontinfo = cJSON_AddObjectToObject(root, "fontinfo");
    if (!fontinfo) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(32));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(0));
    return root;
}

static bool assistant_config_write_default(const char* config_path) {
    cJSON* root = assistant_config_create_default_root();
    if (!root) {
        return false;
    }
    int ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

static bool assistant_config_reset_to_default(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_ASSISTANT, config_path, sizeof(config_path))) {
        return false;
    }
    return assistant_config_write_default(config_path);
}

static bool assistant_config_ensure_by_path(const char* config_path) {
    cJSON* root = load_json(config_path);
    if (root) {
        bool ok = assistant_config_is_valid_root(root);
        cJSON_Delete(root);
        if (ok) {
            return true;
        }
    }
    return assistant_config_write_default(config_path);
}
static const char* const s_assistant_open_ignore_apps[] = {
    APP_NAME_TRANSCRIBE, ///< 转写页收到 assistant 打开请求时不触发 assistant。
    APP_NAME_TRANSLATE,  ///< 翻译页收到 assistant 打开请求时不触发 assistant。
    APP_NAME_PROMPTER,   ///< 提词页收到 assistant 打开请求时不触发 assistant。
    APP_NAME_AI,         ///< AI 页收到 assistant 打开请求时不触发 assistant。
};

/**
 * @brief 判断当前应用是否命中指定应用列表。
 * @param[in] current_app 当前路由应用名称。
 * @param[in] apps 应用列表。
 * @param[in] app_count 应用数量。
 * @return `true` 表示命中，`false` 表示未命中。
 */
static bool assistant_app_name_in_list(const char* current_app, const char* const apps[], size_t app_count) {
    if (current_app == NULL || apps == NULL) {
        return false;
    }

    for (size_t i = 0; i < app_count; i++) {
        if (apps[i] != NULL && strcmp(current_app, apps[i]) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief 判断当前应用是否需要忽略 assistant 打开请求。
 * @return `true` 表示需要忽略，`false` 表示允许打开。
 */
static bool assistant_should_ignore_open_request(void) {
    return assistant_app_name_in_list(app_router_get_app(),
                                      s_assistant_open_ignore_apps,
                                      sizeof(s_assistant_open_ignore_apps) /
                                          sizeof(s_assistant_open_ignore_apps[0]));
}

/**
 * @brief 释放 assistant 占用的 STT 服务资源。
 * @return 无返回值。
 */
static void assistant_release_service(void) {
    if (!s_assistant_service_ready) {
        return;
    }

    if (s_assistant_service_suspended) {
        stt_service_resume(&s_assistant_stt_snapshot);
        s_assistant_service_suspended = false;
    } else {
        stt_service_deinit();
    }
    s_assistant_service_ready = false;
}

/**
 * @brief 确保 assistant 依赖的 STT 服务已初始化。
 * @return `true` 表示服务可用，`false` 表示初始化失败。
 */
static bool assistant_ensure_service(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};

    if (s_assistant_service_ready) {
        return true;
    }

    if (!floatair_fs_get_app_config_file(APP_NAME_ASSISTANT, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return false;
    }

    if (!s_assistant_factoryreset_registered) {
        s_assistant_factoryreset_registered =
            system_factoryreset_register(APP_NAME_ASSISTANT, assistant_config_reset_to_default);
    }

    if (!assistant_config_ensure_by_path(config_path)) {
        return false;
    }

    if (!stt_service_suspend(&s_assistant_stt_snapshot)) {
        floatair_err("suspend stt service failed");
        return false;
    }
    s_assistant_service_suspended = true;
    stt_service_init(config_path);
    stt_config.textMode = TEXTMODE_HISTORY;
    stt_config.transMode = TRANSMODE_SHOW_DUAL;
    s_assistant_service_ready = true;
    return true;
}

/**
 * @brief 打开 assistant 弹窗并初始化依赖资源。
 * @return `true` 表示打开成功，`false` 表示打开失败。
 */
bool assistant_open(void) {
    const char* current_app = app_router_get_app();

    if (assistant_should_ignore_open_request()) {
        floatair_info("ignore assistant open request in current_app=%s", current_app);
        return true;
    }

    if (!assistant_ensure_service()) {
        return false;
    }

    if (!assistant_popup_open()) {
        (void)assistant_close(false);
        return false;
    }

    assistant_stt_clear();
    return true;
}

/**
 * @brief 关闭 assistant 弹窗并释放依赖资源。
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return `true` 表示关闭成功，`false` 表示关闭失败。
 */
bool assistant_close(bool report_close) {
    (void)assistant_popup_close(report_close);
    assistant_release_service();
    return true;
}

/**
 * @brief 处理 assistant popup 被外部删除后的生命周期清理。
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return 无返回值。
 */
void assistant_on_popup_deleted(bool report_close) {
    if (report_close) {
        (void)system_report_assistant_close();
    }
    assistant_release_service();
}
