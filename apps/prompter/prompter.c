/**
 * @file prompter.c
 * @brief Prompter App 生命周期、消息注册和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_prompter
 */
#include "prompter.h"
#include "common/app_framework/app_nav.h"
#include "message.h"
#include "app_def.h"
#include "floatair_fs.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include <stdio.h>
#include <string.h>

static app_message_t prompter_msg = {
    .id   = APP_MSG_ID_PROMPTER,
    .name = APP_NAME_PROMPTER,
    .cb   = prompter_route_cmd,
};

static bool s_prompter_msg_registered = false;    ///< Prompter 消息是否已注册

/**
 * @brief 判断 Prompter 配置根节点是否包含有效字体配置。
 * @param[in] root 配置根节点。
 * @return `true` 表示配置有效，`false` 表示需要重建默认配置。
 */
static bool prompter_config_is_valid_root(cJSON* root) {
    cJSON* fontinfo = NULL;
    cJSON* weight = NULL;
    cJSON* word_space = NULL;
    cJSON* row_space = NULL;

    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    fontinfo = cJSON_GetObjectItemCaseSensitive(root, "fontinfo");
    if (!cJSON_IsObject(fontinfo)) {
        return false;
    }

    weight = cJSON_GetObjectItemCaseSensitive(fontinfo, "weight");
    word_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "wordSpace");
    row_space = cJSON_GetObjectItemCaseSensitive(fontinfo, "rowSpace");
    return cJSON_IsNumber(weight) && cJSON_IsNumber(word_space) && cJSON_IsNumber(row_space);
}

/**
 * @brief 创建 Prompter 默认配置根节点。
 * @return 成功返回 JSON 根节点；失败返回 `NULL`。
 */
static cJSON* prompter_config_create_default_root(void) {
    cJSON* root = cJSON_CreateObject();
    cJSON* fontinfo = NULL;

    if (!root) {
        return NULL;
    }

    fontinfo = cJSON_AddObjectToObject(root, "fontinfo");
    if (!fontinfo) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(26));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(8));
    return root;
}

bool prompter_config_reset_to_default(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    cJSON* root = NULL;
    int ret = 0;

    if (!floatair_fs_get_app_config_file(APP_NAME_PROMPTER, config_path, sizeof(config_path))) {
        return false;
    }

    root = prompter_config_create_default_root();
    if (!root) {
        return false;
    }

    ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

bool prompter_config_ensure(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    cJSON* root = NULL;
    bool ok = false;

    if (!floatair_fs_get_app_config_file(APP_NAME_PROMPTER, config_path, sizeof(config_path))) {
        return false;
    }

    root = load_json(config_path);
    if (root) {
        ok = prompter_config_is_valid_root(root);
        cJSON_Delete(root);
        if (ok) {
            return true;
        }
    }

    return prompter_config_reset_to_default();
}

/**
 * @brief 注册 Prompter 消息处理器。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
static bool prompter_msg_register_once(void) {
    int ret = 0;

    if (s_prompter_msg_registered) {
        return true;
    }

    ret = app_msg_register(&prompter_msg);
    if (ret != 0) {
        return false;
    }
    s_prompter_msg_registered = true;
    return true;
}

/**
 * @brief Prompter App 启动。
 * @return 无返回值。
 */
static void prompter_app_on_start(void) {
    if (!prompter_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }

    if (!app_nav_replace((app_page_t*)prompter_page_get(), NULL, 0)) {
        floatair_assert(false, "prompter page replace failed");
    }
}

/**
 * @brief Prompter App 停止。
 * @return 无返回值。
 */
static void prompter_app_on_stop(void) {
    prompter_view_reset();
}

static app_t s_prompter_app = {
    .name = APP_NAME_PROMPTER,
    .on_start = prompter_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = prompter_app_on_stop,
    .on_back = NULL,
};

bool prompter_app_register(void) {
    (void)system_factoryreset_register(APP_NAME_PROMPTER, prompter_config_reset_to_default);
    if (!prompter_msg_register_once()) {
        return false;
    }
    return app_manager_register(&s_prompter_app);
}
