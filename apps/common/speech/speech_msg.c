/**
 * @file speech_msg.c
 * @brief Speech 手机桥接消息解析与 STT 配置更新实现。
 */
#include "common/speech/speech_runtime.h"

#include "app_def.h"
#include "common/app_framework/app_router.h"
#include "common/floatair_fs.h"
#include "elf_common.h"
#include "floatair_dbg.h"
#include "common/speech/speech_view.h"
#include "system/stt_common.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const speech_app_profile_t* speech_profile_from_msg(const msg_pack_t* msg) {
    if (msg == NULL) {
        return NULL;
    }
    return speech_profile_from_msg_id(msg->id);
}

static bool speech_copy_menu_string(mpack_node_t node,
                                    const char* key,
                                    char* output,
                                    size_t output_size,
                                    bool optional) {
    mpack_node_t value;
    size_t length;

    if (key == NULL || output == NULL || output_size == 0) {
        return false;
    }
    output[0] = '\0';
    value = mpack_node_map_cstr_optional(node, key);
    if (mpack_node_is_missing(value)) {
        return optional;
    }
    if (mpack_node_is_nil(value) || mpack_node_type(value) != mpack_type_str) {
        return false;
    }
    length = mpack_node_strlen(value);
    if ((!optional && length == 0) || length >= output_size) {
        return false;
    }
    if (length > 0) {
        memcpy(output, mpack_node_str(value), length);
    }
    output[length] = '\0';
    return true;
}

static bool speech_parse_function_menu_item(mpack_node_t node,
                                            speech_function_menu_item_t* item) {
    if (item == NULL || mpack_node_type(node) != mpack_type_map) {
        return false;
    }
    memset(item, 0, sizeof(*item));
    return app_msg_get_u32(node, false, "id", &item->id) &&
           speech_copy_menu_string(node,
                                   "label",
                                   item->label,
                                   sizeof(item->label),
                                   false);
}

static bool speech_setfunctionmenu(mpack_node_t node, msg_pack_t* msg) {
    const speech_app_profile_t* profile = speech_profile_from_msg(msg);
    speech_function_menu_t menu = {0};
    mpack_node_t items;
    size_t item_count;
    bool result = false;

    if (profile == NULL) {
        return app_mpack_send_ack(msg, ErrCmdNotImplemented);
    }
    if (!app_msg_get_u32(node, false, "menuId", &menu.menu_id) ||
        !app_msg_get_u32(node, false, "selectedItemId", &menu.selected_item_id)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    items = mpack_node_map_cstr_optional(node, "items");
    if (mpack_node_is_missing(items) || mpack_node_is_nil(items) ||
        mpack_node_type(items) != mpack_type_array) {
        return app_mpack_send_ack(msg, ErrPayloadErr);
    }
    item_count = mpack_node_array_length(items);
    if (item_count == 0 || item_count > UINT32_MAX ||
        item_count > SIZE_MAX / sizeof(*menu.items)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    menu.item_count = item_count;
    menu.items = (speech_function_menu_item_t*)calloc(item_count, sizeof(*menu.items));
    if (menu.items == NULL) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    for (size_t i = 0; i < item_count; ++i) {
        if (!speech_parse_function_menu_item(mpack_node_array_at(items, i), &menu.items[i])) {
            result = app_mpack_send_ack(msg, ErrBadParam);
            goto cleanup;
        }
    }

    if (!speech_function_menu_show(&menu)) {
        result = app_mpack_send_ack(msg, ErrBadParam);
        goto cleanup;
    }
    result = app_mpack_send_ack(msg, Dp_ErrNone);

cleanup:
    free(menu.items);
    return result;
}

/**
 * @brief 校验 Speech 消息是否属于当前页面。
 * @param[in] msg 消息上下文。
 * @return `true` 表示消息 ID 与当前 Speech 页面一致。
 */
static bool speech_msg_matches_current_view(const msg_pack_t* msg) {
    const speech_app_profile_t* profile = NULL;
    const char* current_app_name = NULL;

    if (msg == NULL) {
        return false;
    }

    profile = speech_profile_from_msg(msg);
    current_app_name = app_router_get_app();
    if (profile == NULL || current_app_name == NULL) {
        return false;
    }
    return strcmp(current_app_name, profile->app_name) == 0;
}

static bool speech_clearview(mpack_node_t node, msg_pack_t* msg) {
    const speech_app_profile_t* profile = speech_profile_from_msg(msg);

    (void)node;
    floatair_assert(msg != NULL, "msg is NULL");
    if (profile == NULL) {
        return app_mpack_send_ack(msg, ErrIDErr);
    }
    if (!app_router_set_app(profile->app_name, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("speech page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    speech_stt_clear();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool speech_setfontconfig(mpack_node_t node, msg_pack_t* msg) {
    const speech_app_profile_t* profile = speech_profile_from_msg(msg);
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};

    if (profile == NULL) {
        return app_mpack_send_ack(msg, ErrIDErr);
    }
    if (!floatair_fs_get_app_config_file(profile->config_name,
                                         config_path,
                                         sizeof(config_path))) {
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
    bool skipped = stt_update_sttinfo_was_skipped();

    if (ret || skipped) {
        speech_stt_request_update();
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
        speech_activate_language_hint();
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

/**
 * @brief 仅在当前 Speech 会话内切换正文左右带框气泡。
 * @param[in] node 请求数据。
 * @param[in] msg 消息上下文。
 * @return 消息处理和 ACK 发送结果。
 */
static bool speech_setborderedstt(mpack_node_t node, msg_pack_t* msg) {
    uint64_t enabled = 0;

    if (!app_msg_get_u64(node, false, "borderedStt", &enabled) || enabled > 1) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    speech_set_bordered_stt_enabled(enabled == 1);
    return app_mpack_send_ack(msg, Dp_ErrNone);
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

static bool speech_setheadertext(mpack_node_t node, msg_pack_t* msg) {
    char text[MSG_STR_MAX_LEN] = {0};
    bool visible = false;
    uint8_t visible_u8 = 0;

    if (msg == NULL) {
        return false;
    }
    if (app_msg_get_u8(node, false, "visible", &visible_u8)) {
        visible = visible_u8 != 0;
    } else if (!app_msg_get_bool(node, false, "visible", &visible)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (visible && app_msg_get_str(node, "text", text, sizeof(text)) == 0) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!speech_set_header_text(text, visible)) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t s_speech_cmd_funcs[] = {
    {"clearView", speech_clearview},
    {"setFunctionMenu", speech_setfunctionmenu},
    {"setFontConfig", speech_setfontconfig},
    {"updateSttInfo", speech_updatesttinfo},
    {"setState", speech_setstate},
    {"setHeaderText", speech_setheadertext},
    {"setTextMode", speech_settextmode},
    {"setAudioTrackState", stt_set_audiotrackstate},
    {"setTransMode", speech_settransmode_with_update},
    {"setMaxLine", speech_setmaxline_with_update},
    {"setAudioSourceIndicator", speech_setaudiosourceindicator},
    {"setMicDirectional", speech_setmicdirectional},
    {"setLanguageHint", speech_setlanguagehint},
    {"setTextDirection", speech_settextdirection},
    {"setBorderedStt", speech_setborderedstt},
};

bool speech_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    const speech_app_profile_t* profile = speech_profile_from_msg(msg);

    if (msg == NULL) {
        floatair_err("input err");
        return false;
    }
    if (profile == NULL) {
        return app_mpack_send_ack(msg, ErrIDErr);
    }
    if (!speech_msg_matches_current_view(msg)) {
        floatair_warn("speech msg mismatch: id=%" PRIu32 " biz=%s current=%s",
                      msg->id,
                      msg->biz,
                      app_router_get_app() ? app_router_get_app() : "N/A");
        return app_mpack_send_ack(msg, ErrIDErr);
    }

    if (strcmp(msg->cmd, "setFunctionMenu") != 0) {
        speech_function_menu_hide();
    }

    for (size_t i = 0; i < sizeof(s_speech_cmd_funcs) / sizeof(s_speech_cmd_funcs[0]); ++i) {
        if (strcmp(msg->cmd, s_speech_cmd_funcs[i].cmd) == 0) {
            return s_speech_cmd_funcs[i].func(node, msg);
        }
    }
    floatair_err("unknown cmd: %s", msg->cmd);
    return app_mpack_send_ack(msg, ErrCmdErr);
}
