/**
 * @file reader_msg.c
 * @brief Reader 手机桥接消息解析、文件切换和阅读进度同步实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_reader
 */
#include "reader.h"
#include "app_def.h"
#include "floatair_dbg.h"
#include "message.h"
#include "system/system.h"
#include "system/system_res.h"
#include <string.h>

static void reader_send_onload(msg_pack_t* msg, const char* cmd) {
    (void) msg;
    floatair_info("reader_send_onload: cmd=%s", cmd);
}

static void reader_send_onpagechanged(msg_pack_t* msg, uint32_t page_idx) {
    (void) msg;
    floatair_info("reader_send_onpagechanged: page_idx=%lu", (unsigned long)page_idx);
}

static bool reader_clearview(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    if (!app_router_set_app(APP_NAME_READER, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("reader page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    reader_text_clear();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool reader_setfile(mpack_node_t node, msg_pack_t* msg) {
    reader_send_onload(msg, "onLoadStart");
    char buf[MSG_STR_MAX_LEN * 2] = {0};
    mpack_node_t vnfile = mpack_node_map_cstr_optional(node, "vnFile");
    if (!mpack_node_is_missing(vnfile) && mpack_node_type(vnfile) == mpack_type_map) {
        char dir[MSG_STR_MAX_LEN] = {0};
        char name[MSG_STR_MAX_LEN] = {0};
        size_t dir_n = app_msg_get_str(vnfile, "dir", dir, sizeof(dir));
        size_t name_n = app_msg_get_str(vnfile, "name", name, sizeof(name));
        if (dir_n > 0 && name_n > 0) {
            bool need_slash = dir[dir_n - 1] != '/';
            if (need_slash) snprintf(buf, sizeof(buf), "%s/%s", dir, name);
            else snprintf(buf, sizeof(buf), "%s%s", dir, name);
        }
    }
    if (buf[0]) {
        bool ok = reader_text_set_file(buf);
        if (ok) {
            if (!app_router_set_app(APP_NAME_READER, APP_ROUTER_ENTRY_REMOTE)) {
                floatair_err("reader page visible failed");
                return app_mpack_send_ack(msg, ErrNotReady);
            }
            reader_send_onpagechanged(msg, 0);
        } else {
            if (!app_router_set_app(APP_NAME_READER, APP_ROUTER_ENTRY_REMOTE)) {
                floatair_err("reader page visible failed");
                return app_mpack_send_ack(msg, ErrNotReady);
            }
            reader_text_show_msg(app_get_str("READER_LOAD_FILE_FAILED"));
        }
    }
    reader_send_onload(msg, "onLoadEnd");
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool reader_setpage(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    return app_mpack_send_ack(msg, ErrCmdNotImplemented);
}

static bool reader_pageup(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    const char* now = app_router_get_app();
    if (strcmp(now, APP_NAME_READER) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    reader_text_page_up();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool reader_pagedown(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    const char* now = app_router_get_app();
    if (strcmp(now, APP_NAME_READER) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    reader_text_page_down();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool reader_setcontextline(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t reader_cmd_funcs[] = {
    {"clearView", reader_clearview},
    {"setPrompterContextLine", reader_setcontextline},
    {"setPrompterFile", reader_setfile},
    {"setPrompterPage", reader_setpage},
    {"setPrompterPageUp", reader_pageup},
    {"setPrompterPageDown", reader_pagedown},
};
static int reader_cmd_funcs_count = sizeof(reader_cmd_funcs) / sizeof(reader_cmd_funcs[0]);

bool reader_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        return false;
    }
    for (int i = 0; i < reader_cmd_funcs_count; i++) {
        if (strcmp(msg->cmd, reader_cmd_funcs[i].cmd) == 0) {
            return reader_cmd_funcs[i].func(node, msg);
        }
    }
    return app_mpack_send_ack(msg, ErrCmdErr);
}
