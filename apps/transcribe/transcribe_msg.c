/**
 * @file transcribe_msg.c
 * @brief Transcribe 手机桥接消息解析与 STT 配置更新实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_transcribe
 */
#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "common/app_framework/app_router.h"
#include "common/floatair_fs.h"
#include "transcribe.h"
#include "system/system_config_json.h"
#include "system/stt_common.h"
#include "system/system.h"

static bool transcribe_config_ensure_by_path(const char* config_path) {
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

static bool transcribe_clearview(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    if (!app_router_set_app(APP_NAME_TRANSCRIBE, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("transcribe page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    transcribe_stt_clear();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool transcribe_setfontconfig(mpack_node_t node, msg_pack_t* msg) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_TRANSCRIBE, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    if (!transcribe_config_ensure_by_path(config_path)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    bool ret = stt_set_fontconfig(node, msg, config_path);
    if (ret) {
        transcribe_on_fontconfig_changed();
    }
    return ret;
}

static bool transcribe_updatesttinfo(mpack_node_t node, msg_pack_t* msg) {
    if (strcmp(app_router_get_app(), APP_NAME_TRANSCRIBE) != 0) {
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    bool ret = stt_update_sttinfo(node, msg);
    if (ret && !stt_update_sttinfo_was_skipped()) {
        transcribe_stt_update();
    }
    return ret;
}


static bool transcribe_settextmode(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_textmode(node, msg);
    if (ret) {
        transcribe_stt_update();
    }
    return ret;
}

static bool transcribe_setaudiosourceindicator(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_audiosourceindicator(node, msg);
    if (ret) {
        transcribe_stt_update();
    }
    return ret;
}

static bool transcribe_setmicdirectional(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_micdirectional(node, msg);
    if (ret) {
        transcribe_stt_update();
    }
    return ret;
}

static bool transcribe_setlanguagehint(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_languagehint(node, msg);
    if (ret) {
        transcribe_stt_update();
    }
    return ret;
}

static bool transcribe_settextdirection(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_textdirection(node, msg);
    if (ret) {
        transcribe_stt_update();
    }
    return ret;
}

static app_cmd_func_t transcribe_cmd_funcs[] = {
    {"clearView", transcribe_clearview},
    {"setFontConfig", transcribe_setfontconfig},
    {"updateSttInfo", transcribe_updatesttinfo},
    {"setTextMode", transcribe_settextmode},
    {"setAudioTrackState", stt_set_audiotrackstate},
    {"setTransMode", stt_set_transmode},
    {"setMaxLine", stt_set_maxline},
    {"setAudioSourceIndicator", transcribe_setaudiosourceindicator},
    {"setMicDirectional", transcribe_setmicdirectional},
    {"setLanguageHint", transcribe_setlanguagehint},
    {"textDirection", transcribe_settextdirection},
};

static int transcribe_cmd_funcs_count = sizeof(transcribe_cmd_funcs) / sizeof(transcribe_cmd_funcs[0]);

bool transcribe_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    int index = -1;
    if (!msg) {
        floatair_err("input err");
        return false;
    }
    for (index = 0; index < transcribe_cmd_funcs_count; index++) {
        if (strcmp(msg->cmd, transcribe_cmd_funcs[index].cmd) == 0) {
            return transcribe_cmd_funcs[index].func(node, msg);
        }
    }
    floatair_err("unknown cmd: %s", msg->cmd);
    return app_mpack_send_ack(msg, ErrCmdErr);
}
