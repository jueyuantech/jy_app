/**
 * @file prompter_msg.c
 * @brief Prompter 手机桥接消息解析、文件记录和页面同步数据更新实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_prompter
 */
#include "floatair_dbg.h"
#include "message.h"
#include "app_def.h"
#include "elf_common.h"
#include "prompter.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include "floatair_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

app_font_info_t prompter_font_info       = {0};
const lv_font_t* prompter_font = NULL;

/**
 * @brief 计算指定文本文件的 CRC 与实际大小。
 * @param[in] path 文本文件完整路径。
 * @param[out] out_crc32 输出文件 CRC 值。
 * @param[out] out_size 输出文件大小。
 * @return `true` 表示计算成功，`false` 表示文件无法读取或内存不足。
 */
static bool prompter_calc_file_crc32(const char* path, uint32_t* out_crc32, uint32_t* out_size) {
    uint16_t crc_init = 0;
    uint16_t crc = 0;
    void* fh = NULL;
    uint8_t* buf = NULL;
    uint32_t total_size = 0;

    if (!path || !out_crc32 || !out_size) {
        return false;
    }

    fh = floatair_fs_open(path, FLOATAIR_FS_MODE_RD);
    if (!fh) {
        return false;
    }

    buf = (uint8_t*)malloc(1024);
    if (!buf) {
        floatair_fs_close(fh);
        return false;
    }

    for (;;) {
        uint32_t br = 0;
        if (floatair_fs_read(fh, buf, 1024, &br) != FLOATAIR_FS_OK) {
            free(buf);
            floatair_fs_close(fh);
            return false;
        }
        if (br == 0) {
            break;
        }
        crc = hal_crc16_ccitt_v1(buf, (uint16_t)br, crc_init);
        crc_init = crc;
        total_size += br;
    }

    free(buf);
    floatair_fs_close(fh);
    *out_crc32 = (uint32_t)crc;
    *out_size = total_size;
    return true;
}

/**
 * @brief 从 Prompter 文件命令中解析文本文件路径。
 * @param[in] node 消息数据节点，直接读取平铺 `dir/name` 字段。
 * @param[out] buf 输出路径缓冲区。
 * @param[in] buf_size 输出路径缓冲区大小。
 * @return `true` 表示解析成功，`false` 表示参数缺失或路径超长。
 */
static bool prompter_build_file_path(mpack_node_t node, char* buf, size_t buf_size) {
    char dir[MSG_STR_MAX_LEN] = {0};
    char name[MSG_STR_MAX_LEN] = {0};
    size_t dir_n = app_msg_get_str(node, "dir", dir, sizeof(dir));
    size_t name_n = app_msg_get_str(node, "name", name, sizeof(name));

    if (dir_n == 0 || name_n == 0) {
        floatair_err("prompter file path invalid, dir=%s, name=%s", dir, name);
        return false;
    }

    bool need_slash = dir[dir_n - 1] != '/';
    size_t need_len = dir_n + (need_slash ? 1 : 0) + name_n + 1;
    if (need_len > buf_size) {
        floatair_err("prompter file path too long");
        return false;
    }

    if (need_slash) {
        snprintf(buf, buf_size, "%s/%s", dir, name);
    } else {
        snprintf(buf, buf_size, "%s%s", dir, name);
    }
    return true;
}

bool prompter_report_menu_selected(uint32_t menu_id, uint32_t selected_item_id) {
    msg_pack_t msgpack = {0};
    msg_pack_writer_t* writer = NULL;

    floatair_info("prompter report menu selected: menu=%lu item=%lu",
                  (unsigned long)menu_id,
                  (unsigned long)selected_item_id);

    msgpack.sequence = system_report_next_sequence();
    msgpack.id = APP_MSG_ID_PROMPTER;
    msgpack.type = MSG_TYPE_DATA_RELIABLE;
    strncpy(msgpack.biz, APP_NAME_PROMPTER, sizeof(msgpack.biz));
    msgpack.biz[sizeof(msgpack.biz) - 1] = '\0';
    strncpy(msgpack.cmd, "onMenuSelected", sizeof(msgpack.cmd));
    msgpack.cmd[sizeof(msgpack.cmd) - 1] = '\0';

    writer = app_mpack_create_writer(&msgpack, MSG_TYPE_DATA_RELIABLE);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 2);
    mpack_write_cstr(&writer->writer, "menuId");
    mpack_write_uint(&writer->writer, menu_id);
    mpack_write_cstr(&writer->writer, "selectedItemId");
    mpack_write_uint(&writer->writer, selected_item_id);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

static bool prompter_get_text_layout(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    prompter_text_layout_t layout = {0};
    msg_pack_writer_t* writer = NULL;

    if (!prompter_text_get_layout(&layout)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }

    writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer != NULL, "create writer failed");
    mpack_start_map(&writer->writer, 8);
    mpack_write_cstr(&writer->writer, "breakAll");
    mpack_write_bool(&writer->writer, layout.break_all);
    mpack_write_cstr(&writer->writer, "letterSpacePx");
    mpack_write_uint(&writer->writer, layout.letter_space_px);
    mpack_write_cstr(&writer->writer, "lineSpacePx");
    mpack_write_uint(&writer->writer, layout.line_space_px);
    mpack_write_cstr(&writer->writer, "paddingHorizontalPx");
    mpack_write_uint(&writer->writer, layout.padding_horizontal_px);
    mpack_write_cstr(&writer->writer, "paddingVerticalPx");
    mpack_write_uint(&writer->writer, layout.padding_vertical_px);
    mpack_write_cstr(&writer->writer, "textSizePx");
    mpack_write_uint(&writer->writer, layout.text_size_px);
    mpack_write_cstr(&writer->writer, "totalHeightPx");
    mpack_write_uint(&writer->writer, layout.total_height_px);
    mpack_write_cstr(&writer->writer, "totalWidthPx");
    mpack_write_uint(&writer->writer, layout.total_width_px);
    mpack_finish_map(&writer->writer);

    return app_mpack_send_writer(writer);
}

/**
 * @brief 从 Prompter 字体命令中解析字体配置。
 * @param[in] node 消息数据节点，兼容平铺字段和 `fontConfig` 嵌套字段。
 * @param[out] out 输出字体配置。
 * @return `true` 表示解析成功，`false` 表示字段缺失或超出系统允许范围。
 */
static bool prompter_parse_font_config(mpack_node_t node, app_font_info_t* out) {
    mpack_node_t parse_node = node;
    mpack_node_t font_node;
    int32_t font_size = 0;
    int32_t word_space = 0;
    int32_t row_space = 0;

    if (out == NULL) {
        return false;
    }

    font_node = mpack_node_map_cstr_optional(node, "fontConfig");
    if (!mpack_node_is_missing(font_node) && !mpack_node_is_nil(font_node) &&
        mpack_node_type(font_node) == mpack_type_map) {
        parse_node = font_node;
    }

    memset(out, 0, sizeof(*out));
    if (!app_msg_get_u32(parse_node, false, "weight", &out->weight) ||
        !app_msg_get_u32(parse_node, false, "wordSpace", &out->wordSpace) ||
        !app_msg_get_u32(parse_node, false, "rowSpace", &out->rowSpace)) {
        floatair_err("prompter font config field invalid");
        return false;
    }

    font_size = (int32_t)out->weight;
    word_space = (int32_t)out->wordSpace;
    row_space = (int32_t)out->rowSpace;
    if (!app_fontsize_valid(font_size) ||
        !app_font_wordspace_valid(word_space) ||
        !app_font_rowspace_valid(row_space)) {
        floatair_warn("prompter font config out of range: size=%d word=%d row=%d",
                      (int)font_size,
                      (int)word_space,
                      (int)row_space);
        return false;
    }
    return true;
}

/**
 * @brief 设置 Prompter 字体配置并刷新当前页面字体。
 * @param[in] node 消息数据节点。
 * @param[in] msg 原始消息包，用于回复 ACK/NCK。
 * @return `true` 表示回复发送成功，`false` 表示回复发送失败。
 */
static bool prompter_setfontconfig(mpack_node_t node, msg_pack_t* msg) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};
    app_font_info_t font_info = {0};

    if (!prompter_parse_font_config(node, &font_info)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!floatair_fs_get_app_config_file(APP_NAME_PROMPTER, config_path, sizeof(config_path))) {
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    if (!prompter_config_ensure()) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!system_config_set_font(config_path, &font_info)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!prompter_on_fontconfig_changed()) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 解析 Prompter 文件列表菜单项。
 * @param node 菜单项消息节点。
 * @param out 输出菜单项。
 * @return `true` 表示解析成功，`false` 表示字段缺失或类型错误。
 */
static bool prompter_parse_menu_item(mpack_node_t node, prompter_menu_item_t* out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!app_msg_get_u32(node, false, "id", &out->id)) {
        floatair_err("prompter menu item id invalid");
        return false;
    }
    if (app_msg_get_str(node, "label", out->label, sizeof(out->label)) == 0) {
        floatair_err("prompter menu item label invalid");
        return false;
    }
    return true;
}

static bool prompter_set_file_list_menu(mpack_node_t node, msg_pack_t* msg) {
    uint32_t menu_id = 0;
    uint32_t item_count = 0;
    uint32_t default_item_id = 0;
    prompter_menu_item_t* list = NULL;

    if (!app_msg_get_u32(node, false, "menuId", &menu_id) ||
        !app_msg_get_u32(node, false, "itemCount", &item_count)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(node, true, "defaultItemId", &default_item_id)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    mpack_node_t items = mpack_node_map_cstr_optional(node, "items");
    if (item_count > 0) {
        if (mpack_node_is_missing(items) || mpack_node_is_nil(items) ||
            mpack_node_type(items) != mpack_type_array) {
            floatair_err("prompter menu items invalid");
            return app_mpack_send_ack(msg, ErrPayloadErr);
        }
        size_t items_len = mpack_node_array_length(items);
        if (items_len != item_count) {
            floatair_err("prompter menu itemCount mismatch: %lu/%zu",
                         (unsigned long)item_count,
                         items_len);
            return app_mpack_send_ack(msg, ErrBadParam);
        }

        list = (prompter_menu_item_t*)calloc((size_t)item_count, sizeof(prompter_menu_item_t));
        floatair_assert(list != NULL, "malloc prompter menu items failed");
        for (uint32_t i = 0; i < item_count; i++) {
            mpack_node_t item = mpack_node_array_at(items, i);
            if (!prompter_parse_menu_item(item, &list[i])) {
                free(list);
                return app_mpack_send_ack(msg, ErrBadParam);
            }
        }
    }

    if (!prompter_menu_set(menu_id, list, item_count, default_item_id)) {
        free(list);
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    free(list);

    if (!app_router_set_app(APP_NAME_PROMPTER, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("prompter menu page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool prompter_setfile(mpack_node_t node, msg_pack_t* msg) {
    char buf[MSG_STR_MAX_LEN * 2] = {0};
    floatair_stat_t st = {0};
    uint32_t expected_size = 0;
    uint32_t expected_crc32 = 0;
    uint32_t actual_size = 0;
    uint32_t actual_crc32 = 0;

    if (!prompter_build_file_path(node, buf, sizeof(buf))) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(node, false, "size", &expected_size) ||
        !app_msg_get_u32(node, false, "crc32", &expected_crc32) ||
        expected_size == 0) {
        floatair_err("prompter file size/crc32 invalid");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (floatair_fs_stat(buf, &st) != FLOATAIR_FS_OK || st.is_dir || st.size == 0) {
        floatair_err("prompter file not found: %s", buf);
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    if (!prompter_calc_file_crc32(buf, &actual_crc32, &actual_size)) {
        floatair_err("prompter calc file crc failed: %s", buf);
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (actual_size != expected_size) {
        floatair_err("prompter file size mismatch: %s size %lu/%lu",
                     buf,
                     (unsigned long)actual_size,
                     (unsigned long)expected_size);
        return app_mpack_send_ack(msg, ErrFileNotExistFailed);
    }
    if (actual_crc32 != expected_crc32) {
        floatair_err("prompter file crc mismatch: %s crc %lu/%lu",
                     buf,
                     (unsigned long)actual_crc32,
                     (unsigned long)expected_crc32);
        return app_mpack_send_ack(msg, ErrBadCRC);
    }
    if (!prompter_text_set_file(buf)) {
        return app_mpack_send_ack(msg, ErrDataErr);
    }
    if (!app_router_set_app(APP_NAME_PROMPTER, APP_ROUTER_ENTRY_REMOTE)) {
        floatair_err("prompter page visible failed");
        return app_mpack_send_ack(msg, ErrNotReady);
    }

    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool prompter_seek_to(mpack_node_t node, msg_pack_t* msg) {
    prompter_external_view_t view = {0};
    const char* now = app_router_get_app();
    uint32_t top_mask_height = 0;
    uint32_t bottom_mask_top = 0;

    if (strcmp(now, APP_NAME_PROMPTER) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_u32(node, false, "offset", &view.offset) ||
        !app_msg_get_u32(node, false, "length", &view.length) ||
        !app_msg_get_u32(node, false, "topMaskHeight", &top_mask_height) ||
        !app_msg_get_u32(node, false, "bottomMaskHeight", &bottom_mask_top)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    view.top_mask_height = (int32_t)top_mask_height;
    view.bottom_mask_height = (int32_t)bottom_mask_top;
    prompter_text_apply_external_view(&view);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理提词计时同步指令。
 * @param[in] node 消息数据节点。
 * @param[in] msg 原始消息包，用于回复 ACK/NCK。
 * @return `true` 表示回复发送成功，`false` 表示回复发送失败。
 */
static bool prompter_set_tick(mpack_node_t node, msg_pack_t* msg) {
    const char* now = app_router_get_app();
    uint32_t tick = 0;

    if (strcmp(now, APP_NAME_PROMPTER) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_u32(node, false, "tick", &tick)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    prompter_text_set_tick(tick);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理提词播放状态同步指令。
 * @param[in] node 消息数据节点。
 * @param[in] msg 原始消息包，用于回复 ACK/NCK。
 * @return `true` 表示回复发送成功，`false` 表示回复发送失败。
 */
static bool prompter_set_state(mpack_node_t node, msg_pack_t* msg) {
    const char* now = app_router_get_app();
    uint32_t state = 0;

    if (strcmp(now, APP_NAME_PROMPTER) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_u32(node, false, "state", &state) ||
        state > (uint32_t)PROMPTER_STATE_RUNNING) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    prompter_text_set_state((prompter_state_t)state);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t prompter_cmd_funcs[] = {
    {"getTextLayout", prompter_get_text_layout},
    {"setFontConfig", prompter_setfontconfig},
    {"setFileListMenu", prompter_set_file_list_menu},
    {"setPrompterFile", prompter_setfile},
    {"seekTo", prompter_seek_to},
    {"setTick", prompter_set_tick},
    {"setState", prompter_set_state},
};
static int prompter_cmd_funcs_count = sizeof(prompter_cmd_funcs) / sizeof(prompter_cmd_funcs[0]);

bool prompter_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (!msg) return false;
    for (int i = 0; i < prompter_cmd_funcs_count; i++) {
        if (strcmp(msg->cmd, prompter_cmd_funcs[i].cmd) == 0) {
            return prompter_cmd_funcs[i].func(node, msg);
        }
    }
    return app_mpack_send_ack(msg, ErrCmdErr);
}
