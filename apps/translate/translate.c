/**
 * @file translate.c
 * @brief Translate 应用生命周期、STT 服务初始化和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_translate
 */
#include <time.h>
#include "translate.h"

#include "common/app_framework/app_nav.h"
#include "message.h"
#include "app_def.h"
#include "floatair_fs.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "system/stt_common.h"

#include <stdio.h>
#include <string.h>

static app_message_t translate_msg = {
    .id   = APP_MSG_ID_TRANSLATE,
    .name = APP_NAME_TRANSLATE,
    .cb   = translate_route_cmd,
};

static bool s_translate_msg_registered = false;    ///< Translate 消息是否已注册

static bool translate_config_is_valid_root(cJSON* root) {
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

static cJSON* translate_config_create_default_root(void) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON* fontinfo = cJSON_AddObjectToObject(root, "fontinfo");
    if (!fontinfo) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(24));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(0));
    return root;
}

static bool translate_config_write_default(const char* config_path) {
    cJSON* root = translate_config_create_default_root();
    if (!root) {
        return false;
    }
    int ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

static bool translate_config_ensure(const char* config_path) {
    cJSON* root = load_json(config_path);
    if (root) {
        bool ok = translate_config_is_valid_root(root);
        cJSON_Delete(root);
        if (ok) {
            return true;
        }
    }
    return translate_config_write_default(config_path);
}

static bool translate_factoryreset_handler(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_TRANSLATE, config_path, sizeof(config_path))) {
        return false;
    }
    return translate_config_write_default(config_path);
}

/**
 * @brief 注册 Translate 消息处理器。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
static bool translate_msg_register_once(void) {
    int ret = 0;

    if (s_translate_msg_registered) {
        return true;
    }

    ret = app_msg_register(&translate_msg);
    if (ret != 0) {
        return false;
    }
    s_translate_msg_registered = true;
    return true;
}

/**
 * @brief 注销 Translate 消息处理器。
 * @return 无返回值。
 */
static void translate_msg_unregister_if_needed(void) {
    int ret = 0;

    if (!s_translate_msg_registered) {
        return;
    }

    ret = app_msg_delete(APP_MSG_ID_TRANSLATE);
    floatair_assert(ret == 0, "app_msg_delete failed");
    s_translate_msg_registered = false;
}

/**
 * @brief 初始化 Translate 的 STT 服务。
 * @return `true` 表示初始化成功，`false` 表示初始化失败。
 */
static bool translate_service_init(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};

    if (!floatair_fs_get_app_config_file(APP_NAME_TRANSLATE, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return false;
    }
    if (!translate_config_ensure(config_path)) {
        return false;
    }
    stt_service_init(config_path);
    return true;
}

/**
 * @brief Translate App 启动。
 * @return 无返回值。
 */
static void translate_app_on_start(void) {
    if (!translate_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }
    if (!translate_service_init()) {
        floatair_assert(false, "translate service init failed");
        return;
    }
    if (!app_nav_replace((app_page_t*)translate_page_get(), NULL, 0)) {
        floatair_assert(false, "translate page replace failed");
    }
}

/**
 * @brief Translate App 停止。
 * @return 无返回值。
 */
static void translate_app_on_stop(void) {
    translate_msg_unregister_if_needed();
    translate_stt_clear();
    stt_service_deinit();
}

static app_t s_translate_app = {
    .name = APP_NAME_TRANSLATE,
    .on_start = translate_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = translate_app_on_stop,
    .on_back = NULL,
};

bool translate_app_register(void) {
    (void)system_factoryreset_register(APP_NAME_TRANSLATE, translate_factoryreset_handler);
    return app_manager_register(&s_translate_app);
}
