/**
 * @file speech_msg.c
 * @brief Speech 手机桥接消息解析与 STT 配置更新实现。
 */
#include "speech.h"

#include "app_def.h"
#include "common/app_framework/app_router.h"
#include "common/floatair_fs.h"
#include "elf_common.h"
#include "floatair_dbg.h"
#include "speech_view.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <inttypes.h>
#include <string.h>

static const speech_app_entry_t* speech_entry_from_msg(const msg_pack_t* msg) {
    if (msg == NULL) {
        return NULL;
    }
    return speech_entry_from_msg_id(msg->id);
}

/**
 * @brief 校验 Speech 消息是否属于当前页面。
 * @param[in] msg 消息上下文。
 * @return `true` 表示消息 ID 与当前 Speech 页面一致。
 */
static bool speech_msg_matches_current_view(const msg_pack_t* msg) {
    const speech_app_entry_t* entry = NULL;
    const char* current_app_name = NULL;

    if (msg == NULL) {
        return false;
    }

    entry = speech_entry_from_msg(msg);
    current_app_name = app_router_get_app();
    if (entry == NULL || current_app_name == NULL) {
        return false;
    }
    return strcmp(current_app_name, entry->app_name) == 0;
}

static bool speech_clearview(mpack_node_t node, msg_pack_t* msg) {
    const speech_app_entry_t* entry = speech_entry_from_msg(msg);

    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");
    if (entry == NULL) {
        return app_mpack_send_ack(msg, ErrIDErr);
    }
    if (!app_router_set_app(entry->app_name, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("speech page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    speech_stt_clear();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool speech_setfontconfig(mpack_node_t node, msg_pack_t* msg) {
    const speech_app_entry_t* entry = speech_entry_from_msg(msg);
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};

    if (entry == NULL) {
        return app_mpack_send_ack(msg, ErrIDErr);
    }
    if (!floatair_fs_get_app_config_file(SPEECH_CONFIG_APP_NAME, config_path, sizeof(config_path))) {
        floatair_err("get app config file failed");
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    bool ret = stt_set_fontconfig(node, msg, config_path);
    if (ret) {
        speech_on_fontconfig_changed();
    }
    return ret;
}

static bool speech_updatesttinfo(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_update_sttinfo(node, msg);
    if (ret && !stt_update_sttinfo_was_skipped()) {
        system_progress_hint_param_t hint_param = {
            .visible = false,
            .text = "",
            .bg_opa = 0,
        };

        (void)system_ui_send_progress_hint(&hint_param);
        speech_stt_update();
    }
    return ret;
}

static bool speech_settextmode(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_textmode(node, msg);

    if (ret) {
        speech_stt_update();
    }
    return ret;
}

static bool speech_settransmode_with_update(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_transmode(node, msg);

    if (ret) {
        speech_stt_update();
    }
    return ret;
}

static bool speech_setmaxline_with_update(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_maxline(node, msg);

    if (ret) {
        speech_stt_update();
    }
    return ret;
}

static bool speech_setaudiosourceindicator(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_audiosourceindicator(node, msg);

    if (ret) {
        speech_stt_update();
    }
    return ret;
}

static bool speech_setmicdirectional(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_micdirectional(node, msg);

    if (ret) {
        speech_stt_update();
    }
    return ret;
}

static bool speech_setlanguagehint(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_languagehint(node, msg);

    if (ret) {
        speech_stt_update();
    }
    return ret;
}

static bool speech_settextdirection(mpack_node_t node, msg_pack_t* msg) {
    bool ret = stt_set_textdirection(node, msg);

    if (ret) {
        speech_stt_update();
    }
    return ret;
}

static bool speech_setstate(mpack_node_t node, msg_pack_t* msg) {
    uint8_t state = 0;

    if (msg == NULL) {
        return false;
    }
    if (!app_msg_get_u8(node, false, "state", &state)) {
        floatair_err("state err");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!speech_set_state(state)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t s_speech_cmd_funcs[] = {
    {"clearView", speech_clearview},
    {"setFontConfig", speech_setfontconfig},
    {"updateSttInfo", speech_updatesttinfo},
    {"setState", speech_setstate},
    {"setTextMode", speech_settextmode},
    {"setAudioTrackState", stt_set_audiotrackstate},
    {"setTransMode", speech_settransmode_with_update},
    {"setMaxLine", speech_setmaxline_with_update},
    {"setAudioSourceIndicator", speech_setaudiosourceindicator},
    {"setMicDirectional", speech_setmicdirectional},
    {"setLanguageHint", speech_setlanguagehint},
    {"textDirection", speech_settextdirection},
};

bool speech_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    const speech_app_entry_t* entry = speech_entry_from_msg(msg);

    if (msg == NULL) {
        floatair_err("input err");
        return false;
    }
    if (entry == NULL) {
        return app_mpack_send_ack(msg, ErrIDErr);
    }
    if (!speech_msg_matches_current_view(msg)) {
        floatair_warn("speech msg mismatch: id=%" PRIu32 " biz=%s current=%s",
                      msg->id,
                      msg->biz,
                      app_router_get_app() ? app_router_get_app() : "N/A");
        return app_mpack_send_ack(msg, ErrIDErr);
    }

    for (size_t i = 0; i < sizeof(s_speech_cmd_funcs) / sizeof(s_speech_cmd_funcs[0]); ++i) {
        if (strcmp(msg->cmd, s_speech_cmd_funcs[i].cmd) == 0) {
            return s_speech_cmd_funcs[i].func(node, msg);
        }
    }
    floatair_err("unknown cmd: %s", msg->cmd);
    return app_mpack_send_ack(msg, ErrCmdErr);
}
