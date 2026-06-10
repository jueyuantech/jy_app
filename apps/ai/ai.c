/**
 * @file ai.c
 * @brief AI 应用生命周期实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_ai
 */
#include "ai.h"

#include "app_def.h"
#include "common/app_framework/app_nav.h"
#include "message.h"
#include "floatair_fs.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_config_json.h"

/**
 * @brief AI 应用消息注册项。
 */
static app_message_t ai_msg = {
    .id   = APP_MSG_ID_AI,
    .name = APP_NAME_AI,
    .cb   = ai_route_cmd,
};

static bool s_ai_msg_registered = false;    ///< AI 消息是否已注册

static bool ai_config_is_valid_root(cJSON* root) {
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

static cJSON* ai_config_create_default_root(void) {
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

static bool ai_config_write_default(const char* config_path) {
    cJSON* root = ai_config_create_default_root();
    if (!root) {
        return false;
    }
    int ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

static bool ai_config_ensure(const char* config_path) {
    cJSON* root = load_json(config_path);
    if (root) {
        bool ok = ai_config_is_valid_root(root);
        cJSON_Delete(root);
        if (ok) {
            return true;
        }
    }
    return ai_config_write_default(config_path);
}

/**
 * @brief 注册 AI 消息处理器。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
static bool ai_msg_register_once(void) {
    int ret = 0;

    if (s_ai_msg_registered) {
        return true;
    }

    ret = app_msg_register(&ai_msg);
    if (ret != 0) {
        return false;
    }
    s_ai_msg_registered = true;
    return true;
}

/**
 * @brief 注销 AI 消息处理器。
 * @return 无返回值。
 */
static void ai_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_ai_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_AI);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_ai_msg_registered = false;
}

/**
 * @brief 初始化 AI 的 STT 服务。
 * @return `true` 表示初始化成功，`false` 表示初始化失败。
 */
static bool ai_service_init(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};

    if (!floatair_fs_get_app_config_file(APP_NAME_AI, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return false;
    }

    if (!ai_config_ensure(config_path)) {
        return false;
    }
    stt_service_init(config_path);
    stt_config.textMode = TEXTMODE_HISTORY;
    stt_config.transMode = TRANSMODE_SHOW_DUAL;
    return true;
}

/**
 * @brief AI App 启动。
 * @return 无返回值。
 */
static void ai_app_on_start(void) {
    if (!ai_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!ai_service_init()) {
        floatair_assert(false, "ai service init failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)ai_page_get(), NULL, 0)) {
        floatair_assert(false, "ai page replace failed");
    }
}

/**
 * @brief AI App 停止。
 * @return 无返回值。
 */
static void ai_app_on_stop(void) {
    ai_msg_unregister_if_needed();
    ai_stt_clear();
    stt_service_deinit();
}

static app_t s_ai_app = {
    .name = APP_NAME_AI,
    .on_start = ai_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = ai_app_on_stop,
    .on_back = NULL,
};

bool ai_app_register(void) {
    return app_manager_register(&s_ai_app);
}
