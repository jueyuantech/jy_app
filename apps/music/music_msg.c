/**
 * @file music_msg.c
 * @brief Music 手机桥接消息解析与页面数据更新实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_music
 */
#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "music.h"
#include "app_def.h"
#include "system/system.h"

static bool music_updatelyric(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char lyric[512] = {0};
    app_msg_get_str(node, "lyric", lyric, sizeof(lyric));
    if (!app_router_set_app(APP_NAME_MUSIC, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("music page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    music_avrcp_update_lyric(lyric);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t music_cmd_funcs[] = {
    {"updateLyric", music_updatelyric},
};
static int music_cmd_funcs_count = sizeof(music_cmd_funcs) / sizeof(music_cmd_funcs[0]);

bool music_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) return false;
    for (int i = 0; i < music_cmd_funcs_count; i++) {
        if (strcmp(msg->cmd, music_cmd_funcs[i].cmd) == 0) {
            return music_cmd_funcs[i].func(node, msg);
        }
    }
    return app_mpack_send_ack(msg, ErrCmdErr);
}
