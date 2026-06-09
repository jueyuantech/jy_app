/**
 * @file system_msg_file.c
 * @brief System file message handling
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include <time.h>
#include "elf_common.h"
#include "floatair_dbg.h"
#include "app_def.h"
#include "message.h"
#include "system/system.h"
#include "system/system_def.h"
#include "floatair_fs.h"
#include "lvgl.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "sys_adapter.h"

static bool system_file_parse_path(mpack_node_t node, char** path, uint8_t* type) {
    floatair_assert(path != NULL && type != NULL, "input err");
    char dir[MSG_STR_MAX_LEN]  = {0};
    size_t dir_len             = 0;
    char name[MSG_STR_MAX_LEN] = {0};
    size_t name_len            = 0;
    dir_len                    = app_msg_get_str(node, "dir", dir, sizeof(dir));
    if (dir_len == 0) {
        floatair_err("dir is empty");
        return false;
    }
    if (!app_msg_get_u8(node, false, "type", type)) {
        floatair_err("type is empty");
        return false;
    }
    floatair_info("type: %u", *type);

    name_len = app_msg_get_str(node, "name", name, sizeof(name));
    if (*type == SYSTEM_FILE_TYPE_FILE && name_len == 0) {
        floatair_err("name is empty");
        return false;
    }
    size_t total_len = dir_len + name_len + 2;
    if (total_len > MSG_STR_MAX_LEN) {
        floatair_err("length err [%zu][%zu]", dir_len, name_len);
        return false;
    }
    char* compose = malloc(total_len);
    floatair_assert(compose != NULL && type != NULL, "input err");
    memset(compose, 0, total_len);
    if (*type == SYSTEM_FILE_TYPE_FILE && name_len > 0) {
        memcpy(compose, dir, dir_len);
        compose[dir_len] = '/';
        memcpy(compose + dir_len + 1, name, name_len);
        compose[dir_len + 1 + name_len] = '\0';
    } else {
        memcpy(compose, dir, dir_len);
        compose[dir_len] = '\0';
    }
    floatair_info("compose[%s]", compose);
    *path = compose;
    return true;
}

static uint32_t system_file_crc32_path(const char* full_path) {
    uint16_t init = 0;
    uint16_t crc = 0;
    char lvpath[SYSTEM_MAX_PATH_LEN] = {0};
    snprintf(lvpath, sizeof(lvpath), "%s", full_path);
    void* h = floatair_fs_open(lvpath, FLOATAIR_FS_MODE_RD);
    if (!h) {
        return 0;
    }
    uint32_t chunk = 1024;
    uint8_t* buf = (uint8_t*)malloc(chunk);
    if (!buf) {
        floatair_fs_close(h);
        return 0;
    }
    for (;;) {
        uint32_t br = 0;
        if (floatair_fs_read(h, buf, chunk, &br) != FLOATAIR_FS_OK) {
            crc = 0;
            break;
        }
        if (br == 0) {
            break;
        }
        crc = hal_crc16_ccitt_v1(buf, (uint16_t)br, init);
        //floatair_info("Crc-0x%04" PRIX16 "-0x%04" PRIX16, init, crc);
        init = crc;
    }
    free(buf);
    floatair_fs_close(h);
    return (uint32_t)crc;
}

static size_t system_file_count_dir_shallow(const char* dir_path) {
    size_t count = 0;
    floatair_dir_t* d = NULL;
    if (!dir_path) {
        return 0;
    }
    if (floatair_fs_dir_open(dir_path, &d) != FLOATAIR_FS_OK) {
        return 0;
    }
    char fn[SYSTEM_MAX_PATH_LEN] = {0};
    for (;;) {
        int r = floatair_fs_dir_read(d, fn, sizeof(fn), NULL);
        if (r > 0) {
            break;
        }
        if (r != 0) {
            break;
        }
        if (fn[0] == '\0') break;
        if (strcmp(fn, ".") == 0 || strcmp(fn, "..") == 0) continue;
        count++;
    }
    floatair_fs_dir_close(d);
    floatair_info("count %zu", count);
    return count;
}

static bool system_file_parse_list_dir(mpack_node_t node, char** path) {
    floatair_assert(path != NULL, "input err");
    char dir[MSG_STR_MAX_LEN] = {0};
    size_t dir_len = app_msg_get_str(node, "dir", dir, sizeof(dir));
    if (dir_len == 0) {
        floatair_err("dir is empty");
        return false;
    }
    char* compose = malloc(dir_len + 1);
    floatair_assert(compose != NULL, "malloc dir path failed");
    memcpy(compose, dir, dir_len);
    compose[dir_len] = '\0';
    *path = compose;
    floatair_info("list dir[%s]", compose);
    return true;
}

static bool system_file_get_file_list(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char* path = NULL;
    if (!system_file_parse_list_dir(node, &path)) {
        const char* root = floatair_fs_get_root_path();
        path = strdup(root ? root : "");
        floatair_assert(path != NULL, "strdup default path failed");
        floatair_info("use default path %s", path);
    }

    size_t cnt = system_file_count_dir_shallow(path);
    if (cnt == 0) {
        floatair_err("dir %s is empty", path);
        bool r = app_mpack_send_ack(msg, ErrFileNotExistFailed);
        free(path);
        return r;
    }
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    floatair_assert(writer, "writer err");
    mpack_start_array(&writer->writer, (uint32_t) cnt);

    floatair_dir_t* dir = NULL;
    int dret = floatair_fs_dir_open(path, &dir);
    floatair_assert(dret == FLOATAIR_FS_OK, "opendir %s failed", path);

    char namebuf[SYSTEM_MAX_PATH_LEN] = {0};
    for (;;) {
        int rr = floatair_fs_dir_read(dir, namebuf, sizeof(namebuf), NULL);
        if (rr > 0) {
            break;
        }
        if (rr != 0) {
            break;
        }
        if (namebuf[0] == '\0') break;
        if (strcmp(namebuf, ".") == 0 || strcmp(namebuf, "..") == 0) continue;
        const char* name = namebuf[0] == '/' ? namebuf + 1 : namebuf;
        size_t needed = strlen(path) + strlen(name) + 2;
        char* full = (char*) malloc(needed);
        floatair_assert(full != NULL, "malloc failed for path join");
        memset(full, 0, needed);
        snprintf(full, needed, "%s/%s", path, name);
        uint8_t item_type = (namebuf[0] == '/') ? SYSTEM_FILE_TYPE_DIR : SYSTEM_FILE_TYPE_FILE;
        uint32_t item_size = 0;
        if (item_type == SYSTEM_FILE_TYPE_FILE) {
            floatair_stat_t st = {0};
            if (floatair_fs_stat(full, &st) == FLOATAIR_FS_OK && !st.is_dir) item_size = st.size;
        }
        mpack_start_map(&writer->writer, 5);
        mpack_write_cstr(&writer->writer, "dir");
        mpack_write_cstr(&writer->writer, path);
        mpack_write_cstr(&writer->writer, "name");
        mpack_write_cstr(&writer->writer, name);
        mpack_write_cstr(&writer->writer, "type");
        mpack_write_u8(&writer->writer, item_type);
        mpack_write_cstr(&writer->writer, "size");
        mpack_write_u32(&writer->writer, item_size);
        mpack_write_cstr(&writer->writer, "crc32");
        if (item_type == SYSTEM_FILE_TYPE_FILE) {
            uint32_t c = system_file_crc32_path(full);
            mpack_write_u32(&writer->writer, c);
        } else {
            mpack_write_u32(&writer->writer, 0);
        }
        mpack_finish_map(&writer->writer);
        free(full);
    }
    floatair_fs_dir_close(dir);

    mpack_finish_array(&writer->writer);
    bool ret = app_mpack_send_writer(writer);
    free(path);
    return ret;
}

static uint32_t write_length = 0;
static uint32_t last_pkt = 0;
static uint32_t last_pkt_len = 0;
static char last_full_path[SYSTEM_MAX_PATH_LEN] = {0};

static bool system_file_write_file(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char dir[MSG_STR_MAX_LEN]  = {0};
    char name[MSG_STR_MAX_LEN] = {0};
    uint32_t type               = SYSTEM_FILE_TYPE_FILE;
    uint32_t size              = 0;
    uint32_t total_crc32              = 0;
    if (!app_msg_get_u32(node, false, "type", &type)) {
        floatair_err("type invalid[%" PRIu32 "]", type);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (app_msg_get_str(node, "dir", dir, sizeof(dir)) == 0) {
        floatair_err("dir is empty");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (type == SYSTEM_FILE_TYPE_DIR) {
        if (floatair_fs_mkdirs(dir) != FLOATAIR_FS_OK) {
            return app_mpack_send_ack(msg, ErrFileNotExistFailed);
        }
        return app_mpack_send_ack(msg, Dp_ErrNone);
    }
    if (type != SYSTEM_FILE_TYPE_FILE) {
        floatair_err("type %" PRIu32 " is not a file", type);
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    if (app_msg_get_str(node, "name", name, sizeof(name)) == 0) {
        floatair_err("name is empty");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(node, false, "crc32", &total_crc32)) {
        floatair_err("crc32 invalid[%" PRIu32 "]", total_crc32);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(node, false, "size", &size)) {
        floatair_err("size invalid[%" PRIu32 "]", size);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    size_t dir_len  = strlen(dir);
    size_t name_len = strlen(name);
    if (dir_len == 0 || name_len == 0) {
        floatair_err("dir or name is empty");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    size_t needed = dir_len + name_len + ((dir[dir_len - 1] == '/') ? 1 : 2);
    if (needed > SYSTEM_MAX_PATH_LEN) {
        floatair_err("path too long");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    char full_path[SYSTEM_MAX_PATH_LEN] = {0};
    memcpy(full_path, dir, dir_len);
    size_t full_path_len = dir_len;
    if (dir[dir_len - 1] != '/') {
        full_path[full_path_len++] = '/';
    }
    memcpy(full_path + full_path_len, name, name_len);
    full_path[full_path_len + name_len] = '\0';
    mpack_node_t pkt_node = mpack_node_map_cstr(node, "pkt");
    if (mpack_node_type(pkt_node) != mpack_type_map) {
        floatair_err("pkt not map");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    uint32_t total = 0;
    uint32_t cur   = 0;
    uint32_t pkt_crc = 0;
    if (!app_msg_get_u32(pkt_node, false, "total", &total) || total == 0) {
        floatair_err("total invalid[%" PRIu32 "]", total);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(pkt_node, false, "cur", &cur) || cur == 0 || cur > total) {
        floatair_err("cur invalid[%" PRIu32 "/%" PRIu32 "]", cur, total);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!app_msg_get_u32(pkt_node, false, "crc32", &pkt_crc)) {
        floatair_err("pkt crc32 invalid");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    mpack_node_t raw_node = mpack_node_map_cstr(pkt_node, "bytes");
    size_t raw_len = 0;
    const char* raw_ptr = NULL;
    if (mpack_node_type(raw_node) == mpack_type_bin) {
        raw_len = mpack_node_bin_size(raw_node);
        raw_ptr = mpack_node_bin_data(raw_node);
    } else if (mpack_node_type(raw_node) == mpack_type_str) {
        raw_len = mpack_node_strlen(raw_node);
        raw_ptr = mpack_node_str(raw_node);
    }
    if (!raw_ptr || raw_len == 0) {
        floatair_err("raw empty");
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (raw_len > UINT16_MAX) {
        floatair_err("raw too large[%zu]", raw_len);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    uint32_t crc_calc = (uint32_t)hal_crc16_ccitt((void*)raw_ptr, (uint16_t)raw_len);
    if (crc_calc != pkt_crc) {
        floatair_err("crc mismatch[%" PRIu32 " != %" PRIu32 "]", crc_calc, pkt_crc);
        return app_mpack_send_ack(msg, ErrBadCRC);
    }

    if (cur == 1) {
        if (floatair_fs_is_exist(full_path)) {
            floatair_dbg("remove %s", full_path);
            floatair_fs_remove(full_path);
        }
        last_pkt = 0;
        write_length = 0;
        last_pkt_len = 0;
        strncpy(last_full_path, full_path, sizeof(last_full_path) - 1);
        last_full_path[sizeof(last_full_path) - 1] = '\0';
    } else {
        if (last_full_path[0] == '\0' || strncmp(last_full_path, full_path, sizeof(last_full_path)) != 0) {
            floatair_err("path mismatch[%s][%s]", last_full_path, full_path);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }

    if (last_pkt == cur) {
        floatair_dbg("retransmit %" PRIu32 " is same", cur);
        if (last_pkt_len == 0) {
            floatair_err("retransmit len invalid");
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    } else if (cur == last_pkt + 1) {
        floatair_dbg("new %" PRIu32 " is next", cur);
    } else {
        floatair_err("pkt %" PRIu32 " is not next", cur);
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    if (last_pkt != cur) {
        if (write_length > size) {
            floatair_err("write_length overflow[%" PRIu32 " > %" PRIu32 "]", write_length, size);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
        if ((uint64_t)write_length + (uint64_t)raw_len > (uint64_t)size) {
            floatair_err("file size overflow[%" PRIu32 " + %zu > %" PRIu32 "]", write_length, raw_len, size);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    } else {
        if (last_pkt_len != (uint32_t)raw_len) {
            floatair_err("retransmit len mismatch[%" PRIu32 " != %zu]", last_pkt_len, raw_len);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
        if (write_length < last_pkt_len) {
            floatair_err("retransmit offset invalid[%" PRIu32 " < %" PRIu32 "]", write_length, last_pkt_len);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
    }

    uint32_t mode = FLOATAIR_FS_MODE_WR | FLOATAIR_FS_MODE_CREATE;
    if (cur == 1) mode |= FLOATAIR_FS_MODE_TRUNC;
    void* fh = floatair_fs_open(full_path, mode);
    if (!fh) {
        floatair_err("open %s failed", full_path);
        return app_mpack_send_ack(msg, ErrBadFilePath);
    }
    uint32_t offset_u32 = write_length;
    if (last_pkt == cur) offset_u32 = write_length - last_pkt_len;
    if (offset_u32 > (uint32_t)INT32_MAX) {
        floatair_fs_close(fh);
        floatair_err("seek offset overflow[%" PRIu32 "]", offset_u32);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (floatair_fs_seek(fh, (int32_t)offset_u32, SEEK_SET) != FLOATAIR_FS_OK) {
        floatair_fs_close(fh);
        floatair_err("seek %s failed", full_path);
        return app_mpack_send_ack(msg, ErrBadFilePath);
    }
    uint32_t bw = 0;
    if (floatair_fs_write(fh, raw_ptr, (uint32_t)raw_len, &bw) != FLOATAIR_FS_OK || bw != raw_len) {
        floatair_fs_close(fh);
        floatair_err("write %s failed", full_path);
        return app_mpack_send_ack(msg, ErrBadFilePath);
    }
    floatair_fs_close(fh);

    if (last_pkt != cur) {
        write_length += (uint32_t)raw_len;
        last_pkt_len = (uint32_t)raw_len;
    }
    last_pkt = cur;

    if (cur == total) {
        uint32_t sum = system_file_crc32_path(full_path);
        if (sum != total_crc32) {
            floatair_err("remove file %s ,total crc32 mismatch[%" PRIu32 " != %" PRIu32 "]", full_path, sum, total_crc32);
            floatair_fs_remove(full_path);
            return app_mpack_send_ack(msg, ErrBadCRC);
        }
        floatair_dbg("last pkt %" PRIu32 " is last", cur);
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_file_write_file_by_binary(mpack_node_t node, msg_pack_t* msg) {
    return system_file_write_file(node, msg);
}

static bool system_file_remove_dir(char* path, bool keeptop) {
    if (!path || strlen(path) == 0) {
        return false;
    }
    floatair_dir_t* d = NULL;
    if (floatair_fs_dir_open(path, &d) != FLOATAIR_FS_OK) {
        floatair_err("opendir %s failed", path);
        return false;
    }
    bool ok = true;
    char namebuf[SYSTEM_MAX_PATH_LEN] = {0};
    while (ok) {
        int rr = floatair_fs_dir_read(d, namebuf, sizeof(namebuf), NULL);
        if (rr < 0) {
            floatair_err("readdir %s failed", path);
            break;
        }
        if (rr > 0) {
            floatair_info("readdir %s end", path);
            break;
        }
        if (namebuf[0] == '\0') {
            break;
        }
        if (strcmp(namebuf, ".") == 0 || strcmp(namebuf, "..") == 0) continue;
        const char* name = namebuf[0] == '/' ? namebuf + 1 : namebuf;
        size_t needed = strlen(path) + strlen(name) + 2;
        char* full = (char*) malloc(needed);
        floatair_assert(full != NULL, "malloc failed for path join");
        memset(full, 0, needed);
        snprintf(full, needed, "%s/%s", path, name);
        if (namebuf[0] == '/') {
            if (!system_file_remove_dir(full, false)) {
                ok = false;
            }
        } else {
            if (floatair_fs_remove(full) != FLOATAIR_FS_OK) {
                ok = false;
            }
        }
        free(full);
    }
    floatair_fs_dir_close(d);
    if (!keeptop) {
        if (ok) {
            if (floatair_fs_remove(path) != FLOATAIR_FS_OK) {
                ok = false;
            }
        }
    }
    return ok;
}

static bool system_file_remove_file(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char* path   = NULL;
    uint8_t type = SYSTEM_FILE_TYPE_OTHER;
    if (!system_file_parse_path(node, &path, &type)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (type == SYSTEM_FILE_TYPE_FILE) {
        if (floatair_fs_remove(path) == FLOATAIR_FS_OK) {
            bool r = app_mpack_send_ack(msg, Dp_ErrNone);
            free(path);
            return r;
        }
    } else if (type == SYSTEM_FILE_TYPE_DIR && system_file_remove_dir(path, false)) {
        bool r = app_mpack_send_ack(msg, Dp_ErrNone);
        free(path);
        return r;
    }
    bool r = app_mpack_send_ack(msg, ErrBadFilePath);
    free(path);
    return r;
}

static bool system_file_is_file_exist(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char* path   = NULL;
    uint8_t type = SYSTEM_FILE_TYPE_OTHER;
    if (!system_file_parse_path(node, &path, &type)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (type == SYSTEM_FILE_TYPE_FILE) {
        uint32_t size = 0;
        uint32_t crc32 = 0;
        floatair_stat_t st = {0};
        if (!app_msg_get_u32(node, false, "size", &size) ||
            !app_msg_get_u32(node, false, "crc32", &crc32)) {
            floatair_err("size or crc32 invalid");
            free(path);
            return app_mpack_send_ack(msg, ErrBadParam);
        }
        if (floatair_fs_stat(path, &st) == FLOATAIR_FS_OK && !st.is_dir) {
            if (st.size != size) {
                floatair_err("size mismatch[%" PRIu32 " != %" PRIu32 "]", st.size, size);
                free(path);
                return app_mpack_send_ack(msg, ErrFileNotExistFailed);
            }
            uint32_t sum = system_file_crc32_path(path);
            if (sum != crc32) {
                floatair_err("crc32 mismatch[%" PRIu32 " != %" PRIu32 "]", sum, crc32);
                free(path);
                return app_mpack_send_ack(msg, ErrBadCRC);
            }
            bool r = app_mpack_send_ack(msg, Dp_ErrNone);
            free(path);
            return r;
        }
    } else if (type == SYSTEM_FILE_TYPE_DIR) {
        floatair_dir_t* d4 = NULL;
        if (floatair_fs_dir_open(path, &d4) == FLOATAIR_FS_OK) {
            floatair_fs_dir_close(d4);
            bool r = app_mpack_send_ack(msg, Dp_ErrNone);
            free(path);
            return r;
        }
    }
    bool r = app_mpack_send_ack(msg, ErrFileNotExistFailed);
    free(path);
    return r;
}

static bool system_file_clear_folder(mpack_node_t node, msg_pack_t* msg) {
    floatair_assert(msg != NULL, "msg is NULL");
    char* path   = NULL;
    uint8_t type = SYSTEM_FILE_TYPE_OTHER;
    if (!system_file_parse_path(node, &path, &type)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (type != SYSTEM_FILE_TYPE_DIR) {
        bool r = app_mpack_send_ack(msg, ErrBadParam);
        free(path);
        return r;
    }
    if (!system_file_remove_dir(path, true)) {
        bool r = app_mpack_send_ack(msg, ErrBadFilePath);
        free(path);
        return r;
    }
    bool r = app_mpack_send_ack(msg, Dp_ErrNone);
    free(path);
    return r;
}

app_cmd_func_t system_file_cmd_funcs[] = {
    {"getFileList", system_file_get_file_list},
    {"writeFile", system_file_write_file},
    {"writeFileByBinary", system_file_write_file_by_binary},
    {"removeFile", system_file_remove_file},
    {"isFileExist", system_file_is_file_exist},
    {"clearFolder", system_file_clear_folder},
};
const size_t system_file_cmd_funcs_count =
    sizeof(system_file_cmd_funcs) / sizeof(system_file_cmd_funcs[0]);
