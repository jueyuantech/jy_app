#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "app_def.h"
#include "message.h"
#include "gallery.h"
#include "system/system_res.h"
#include <string.h>

static bool gallery_clearview(mpack_node_t node, msg_pack_t* msg) {
    (void) node;
    gallery_clear_view();
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool gallery_setgalleryfile(mpack_node_t node, msg_pack_t* msg) {
    char buf[MSG_STR_MAX_LEN * 2] = {0};
    if (!mpack_node_is_missing(node) && mpack_node_type(node) == mpack_type_map) {
        char dir[MSG_STR_MAX_LEN] = {0};
        char name[MSG_STR_MAX_LEN] = {0};
        size_t dir_n = app_msg_get_str(node, "dir", dir, sizeof(dir));
        size_t name_n = app_msg_get_str(node, "name", name, sizeof(name));
        if (dir_n > 0 && name_n > 0) {
            size_t need = dir_n + 1 + name_n + 1;
            if (need <= sizeof(buf)) {
                bool need_slash = dir[dir_n - 1] != '/';
                if (need_slash) {
                    snprintf(buf, sizeof(buf), "%s/%s", dir, name);
                } else {
                    snprintf(buf, sizeof(buf), "%s%s", dir, name);
                }
            }
        }
    }

    if (buf[0] == '\0') {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_image_path_valid(buf)) {
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    if (!gallery_update_pic(buf)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t gallery_cmd_funcs[] = {
    {"clearView", gallery_clearview},
    {"setGalleryFile", gallery_setgalleryfile},
};

static int gallery_cmd_funcs_count = sizeof(gallery_cmd_funcs) / sizeof(gallery_cmd_funcs[0]);

bool gallery_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) {
        return false;
    }
    for (int i = 0; i < gallery_cmd_funcs_count; i++) {
        if (strcmp(msg->cmd, gallery_cmd_funcs[i].cmd) == 0) {
            return gallery_cmd_funcs[i].func(node, msg);
        }
    }
    return app_mpack_send_ack(msg, ErrCmdErr);
}
