/**
 * @file ai_msg.c
 * @brief AI 应用消息处理。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @ingroup app_ai
 */
#include "ai.h"

#include "app_def.h"
#include "common/app_framework/app_router.h"
#include "floatair_fs.h"
#include "message.h"
#include "system/system_config_json.h"
#include "system/stt_common.h"

static bool ai_config_ensure_by_path(const char* config_path) {
    cJSON* root = load_json(config_path);
    if (root) {
        cJSON* fontinfo = cJSON_GetObjectItemCaseSensitive(root, "fontinfo");
        cJSON* weight = cJSON_IsObject(fontinfo) ? cJSON_GetObjectItemCaseSensitive(fontinfo, "weight") : NULL;
        cJSON* word_space = cJSON_IsObject(fontinfo) ? cJSON_GetObjectItemCaseSensitive(fontinfo, "wordSpace") : NULL;
        cJSON* row_space = cJSON_IsObject(fontinfo) ? cJSON_GetObjectItemCaseSensitive(fontinfo, "rowSpace") : NULL;
        bool ok = cJSON_IsObject(fontinfo) && cJSON_IsNumber(weight) && cJSON_IsNumber(word_space) && cJSON_IsNumber(row_space);
        cJSON_Delete(root);
        if (ok) {
            return true;
        }
    }
    root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON* fontinfo = cJSON_AddObjectToObject(root, "fontinfo");
    if (!fontinfo) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(32));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(0));
    int ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

/**
 * @brief 处理 AI 清屏命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示命令处理成功，`false` 表示处理失败。
 */
static bool ai_clearview(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");
    if (!app_router_set_app(APP_NAME_AI, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("ai page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    ai_stt_clear();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理 AI 字体配置命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示配置写入成功，`false` 表示处理失败。
 */
static bool ai_setfontconfig(mpack_node_t node, msg_pack_t* msg) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_AI, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    if (!ai_config_ensure_by_path(config_path)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    bool ret = stt_set_fontconfig(node, msg, config_path);
    if (ret) {
        ai_on_fontconfig_changed();
    }
    return ret;
}

/**
 * @brief 处理 AI STT 文本更新命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示 STT 数据更新成功，`false` 表示处理失败。
 */
static bool ai_updatesttinfo(mpack_node_t node, msg_pack_t* msg) {
    uint8_t area = 0;
    uint8_t msg_type = 0;

    if (strcmp(app_router_get_app(), APP_NAME_AI) != 0) {
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    (void)app_msg_get_u8(node, true, "area", &area);
    (void)app_msg_get_u8(node, false, "msgType", &msg_type);

    bool ret = stt_update_sttinfo(node, msg);
    if (ret && !stt_update_sttinfo_was_skipped()) {
        ai_stt_note_update(area, msg_type);
        ai_stt_update();
    }
    return ret;
}

/**
 * @brief 处理 AI 音频来源指示命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示配置更新成功，`false` 表示处理失败。
 */
static bool ai_setaudiosourceindicator(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_audiosourceindicator(node, msg);
    if (ret) {
        ai_stt_update();
    }
    return ret;
}

/**
 * @brief 处理 AI 麦克风方向命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示配置更新成功，`false` 表示处理失败。
 */
static bool ai_setmicdirectional(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_micdirectional(node, msg);
    if (ret) {
        ai_stt_update();
    }
    return ret;
}

/**
 * @brief AI 命令路由表。
 */
static app_cmd_func_t ai_cmd_funcs[] = {
    {"clearView", ai_clearview},
    {"setFontConfig", ai_setfontconfig},
    {"updateSttInfo", ai_updatesttinfo},
    {"setAudioTrackState", stt_set_audiotrackstate},
    {"setMaxLine", stt_set_maxline},
    {"setAudioSourceIndicator", ai_setaudiosourceindicator},
    {"setMicDirectional", ai_setmicdirectional},
};

/**
 * @brief 路由 AI 应用命令。
 * @param[in] node mpack 数据节点。
 * @param[in,out] msg 消息头信息。
 * @return `true` 表示命令已成功处理，`false` 表示处理失败。
 */
bool ai_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        floatair_err("input err");
        return false;
    }

    size_t cmd_count = sizeof(ai_cmd_funcs) / sizeof(ai_cmd_funcs[0]);
    for (size_t index = 0; index < cmd_count; index++) {
        if (strcmp(msg->cmd, ai_cmd_funcs[index].cmd) == 0) {
            return ai_cmd_funcs[index].func(node, msg);
        }
    }

    floatair_err("unknown cmd: %s", msg->cmd);
    return app_mpack_send_ack(msg, ErrCmdErr);
}
