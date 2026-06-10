/**
 * @file translate_msg.c
 * @brief Translate 手机桥接消息解析与 STT 配置更新实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_translate
 */
#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "common/app_framework/app_router.h"
#include "common/floatair_fs.h"
#include "translate.h"
#include "system/system_config_json.h"
#include "system/stt_common.h"
#include "system/system.h"

static bool translate_config_ensure_by_path(const char* config_path) {
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
    cJSON_AddItemToObject(fontinfo, "weight", cJSON_CreateNumber(24));
    cJSON_AddItemToObject(fontinfo, "wordSpace", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(fontinfo, "rowSpace", cJSON_CreateNumber(0));
    int ret = save_json(config_path, root);
    cJSON_Delete(root);
    return ret == 0;
}

static bool translate_clearview(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    floatair_assert(msg != NULL, "msg is NULL");
    if (!app_router_set_app(APP_NAME_TRANSLATE, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("translate page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    translate_stt_clear();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool translate_setfontconfig(mpack_node_t node, msg_pack_t* msg) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    if (!floatair_fs_get_app_config_file(APP_NAME_TRANSLATE, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    if (!translate_config_ensure_by_path(config_path)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    bool ret = stt_set_fontconfig(node, msg, config_path);
    if (ret) {
        translate_on_fontconfig_changed();
    }
    return ret;
}

static bool translate_updatesttinfo(mpack_node_t node, msg_pack_t* msg) {
    if (strcmp(app_router_get_app(), APP_NAME_TRANSLATE) != 0) {
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }

    bool ret = stt_update_sttinfo(node, msg);
    if (ret && !stt_update_sttinfo_was_skipped()) {
        translate_stt_update();
    }
    return ret;
}


static bool translate_settextmode(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_textmode(node, msg);
    if (ret) {
        translate_stt_update();
    }
    return ret;
}

static bool translate_settransmode(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_transmode(node, msg);
    if (ret) {
        translate_stt_update();
    }
    return ret;
}

static bool translate_setmaxline(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_maxline(node, msg);
    if (ret) {
        translate_stt_update();
    }
    return ret;
}

static bool translate_setaudiosourceindicator(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_audiosourceindicator(node, msg);
    if (ret) {
        translate_stt_update();
    }
    return ret;
}

static bool translate_setmicdirectional(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_micdirectional(node, msg);
    if (ret) {
        translate_stt_update();
    }
    return ret;
}

static bool translate_setlanguagehint(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_languagehint(node, msg);
    if (ret) {
        translate_stt_update();
    }
    return ret;
}

static bool translate_settextdirection(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_textdirection(node, msg);
    if (ret) {
        translate_stt_update();
    }
    return ret;
}

static app_cmd_func_t translate_cmd_funcs[] = {
    {"clearView", translate_clearview},
    {"setFontConfig", translate_setfontconfig},
    {"updateSttInfo", translate_updatesttinfo},
    {"setTextMode", translate_settextmode},
    {"setAudioTrackState", stt_set_audiotrackstate},
    {"setTransMode", translate_settransmode},
    {"setMaxLine", translate_setmaxline},
    {"setAudioSourceIndicator", translate_setaudiosourceindicator},
    {"setMicDirectional", translate_setmicdirectional},
    {"setLanguageHint", translate_setlanguagehint},
    {"textDirection", translate_settextdirection},
}; 

static int translate_cmd_funcs_count = sizeof(translate_cmd_funcs) / sizeof(translate_cmd_funcs[0]);

bool translate_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    int index = -1;
    if (!msg) {
        floatair_err("input err");
        return false;
    }
    for (index = 0; index < translate_cmd_funcs_count; index++) {
        if (strcmp(msg->cmd, translate_cmd_funcs[index].cmd) == 0) {
            return translate_cmd_funcs[index].func(node, msg);
        }
    }
    floatair_err("unknown cmd: %s", msg->cmd);
    return app_mpack_send_ack(msg, ErrCmdErr);
}
