/**
 * @file navigation_msg.c
 * @brief Navigation 手机桥接消息解析与导航页面数据更新实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_navigation
 */
#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "navigation.h"
#include "app_def.h"
#include "system/system.h"

static bool navigation_clearview(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    if (!app_router_set_app(APP_NAME_NAVIGATION, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("navigation page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    navigation_map_clear();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool navigation_updateinfo(mpack_node_t node, msg_pack_t* msg) {
    (void) msg;
    int32_t navMode = 0;
    app_msg_get_32(node, false, "navMode", &navMode);
    char nextRoadName[256] = {0};
    char curStepRetainDistance[128] = {0};
    char remainDistance[128] = {0};
    char remainTime[128] = {0};
    char speed[64] = {0};
    app_msg_get_str(node, "nextRoadName", nextRoadName, sizeof(nextRoadName));
    app_msg_get_str(node, "curStepRetainDistance", curStepRetainDistance, sizeof(curStepRetainDistance));
    app_msg_get_str(node, "remainDistance", remainDistance, sizeof(remainDistance));
    app_msg_get_str(node, "remainTime", remainTime, sizeof(remainTime));
    app_msg_get_str(node, "speed", speed, sizeof(speed));
    size_t icon_sz = 0;
    mpack_node_t icon_node = mpack_node_map_cstr_optional(node, "iconBytes");
    if (!mpack_node_is_missing(icon_node) && !mpack_node_is_nil(icon_node)) {
        if (mpack_node_type(icon_node) == mpack_type_bin) {
            size_t bin_sz = mpack_node_bin_size(icon_node);
            const char* bin_ptr = mpack_node_bin_data(icon_node);
            if (bin_ptr && bin_sz > 0) {
                navigation_map_update_dir_icon_bin((const uint8_t*)bin_ptr, bin_sz);
                icon_sz = bin_sz;
            }
        }
    }

    if (!app_router_set_app(APP_NAME_NAVIGATION, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("navigation page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    navigation_map_update_info((int)navMode, nextRoadName, curStepRetainDistance, remainDistance, remainTime, speed);
    (void)icon_sz;
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool navigation_updatebpm(mpack_node_t node, msg_pack_t* msg) {
    (void) msg;
    char bpm[64] = {0};
    app_msg_get_str(node, "bpm", bpm, sizeof(bpm));
    if (!app_router_set_app(APP_NAME_NAVIGATION, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("navigation page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    navigation_map_update_bpm(bpm);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool navigation_updatespo(mpack_node_t node, msg_pack_t* msg) {
    (void) msg;
    char spo[64] = {0};
    app_msg_get_str(node, "spo", spo, sizeof(spo));
    if (!app_router_set_app(APP_NAME_NAVIGATION, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("navigation page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    navigation_map_update_spo(spo);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t navigation_cmd_funcs[] = {
    {"clearView", navigation_clearview},
    {"updateNav", navigation_updateinfo},
    {"updateBpm", navigation_updatebpm},
    {"updateSpo", navigation_updatespo},
};
static int navigation_cmd_funcs_count = sizeof(navigation_cmd_funcs) / sizeof(navigation_cmd_funcs[0]);

bool navigation_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) return false;
    for (int i = 0; i < navigation_cmd_funcs_count; i++) {
        if (strcmp(msg->cmd, navigation_cmd_funcs[i].cmd) == 0) {
            return navigation_cmd_funcs[i].func(node, msg);
        }
    }
    return app_mpack_send_ack(msg, ErrCmdErr);
}
