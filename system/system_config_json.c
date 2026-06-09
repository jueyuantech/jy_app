/**
 * @file system_config_json.c
 * @brief System configuration JSON read/write utilities implementation
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system_config_json.h"

#include "cJSON.h"
#include "floatair_dbg.h"
#include "app_def.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "floatair_fs.h"

static char* read_all(const char* path);
static int write_all(const char* path, size_t len, const char* buf);
static bool mkdir_parent(const char* path);

static bool mkdir_parent(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    const char* last = strrchr(path, '/');
    if (!last || last == path) {
        return false;
    }
    char dir[128] = {0};
    size_t n = (size_t)(last - path);
    if (n >= sizeof(dir)) {
        return false;
    }
    memcpy(dir, path, n);
    dir[n] = '\0';
    return floatair_fs_mkdirs(dir) == FLOATAIR_FS_OK;
}

cJSON* load_json(const char* path) {
    cJSON* root = NULL;
    if (!path) {
        floatair_err("path is NULL");
        return NULL;
    }
    char* buf = read_all(path);
    if (buf) {
        root = cJSON_Parse(buf);
        free(buf);
    }
    return root;
}

int save_json(const char* path, cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    if (!json) {
        return -1;
    }
    size_t len   = strlen(json);
    int ret_code = write_all(path, len, json);
    free(json);
    return ret_code;
}

static char* read_all(const char* path) {
    if (!path) {
        floatair_err("path is NULL");
        return NULL;
    }
    void* h = floatair_fs_open(path, FLOATAIR_FS_MODE_RD);
    if (!h) {
        floatair_err("open %s failed", path);
        return NULL;
    }
    if (floatair_fs_seek(h, 0, SEEK_END) != FLOATAIR_FS_OK) {
        floatair_fs_close(h);
        floatair_err("seek end %s failed", path);
        return NULL;
    }
    uint32_t size = 0;
    if (floatair_fs_tell(h, &size) != FLOATAIR_FS_OK) {
        floatair_fs_close(h);
        floatair_err("tell %s failed", path);
        return NULL;
    }
    if (floatair_fs_seek(h, 0, SEEK_SET) != FLOATAIR_FS_OK) {
        floatair_fs_close(h);
        floatair_err("seek set %s failed", path);
        return NULL;
    }
    char* buf = (char*) malloc(size + 1);
    if (!buf) { floatair_fs_close(h); }
    floatair_assert(buf != NULL, "malloc read_all buf failed");
    uint32_t br = 0;
    if (floatair_fs_read(h, buf, size, &br) != FLOATAIR_FS_OK || br != size) {
        floatair_fs_close(h);
        floatair_err("read %s failed, br=%lu size=%lu", path, (unsigned long)br, (unsigned long)size);
        free(buf);
        return NULL;
    }
    floatair_fs_close(h);
    buf[size] = 0;
    return buf;
}

static int write_all(const char* path, size_t len, const char* buf) {
    if (!path || len == 0 || !buf) {
        floatair_err("path is NULL or len is 0 or buf is NULL");
        return -1;
    }
    (void)mkdir_parent(path);
    void* h = floatair_fs_open(path, FLOATAIR_FS_MODE_WR | FLOATAIR_FS_MODE_CREATE | FLOATAIR_FS_MODE_TRUNC);
    if (!h) {
        floatair_err("open %s failed", path);
        return -1;
    }
    uint32_t bw = 0;
    int r = floatair_fs_write(h, buf, (uint32_t)len, &bw);
    floatair_fs_close(h);
    if (r != FLOATAIR_FS_OK || bw != (uint32_t)len) {
        floatair_err("write %s failed, bw=%lu len=%lu", path, (unsigned long)bw, (unsigned long)len);
        return -2;
    }
    return 0;
}

void parse_bool_key(cJSON* root, const char* key, bool* out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item) {
        if (cJSON_IsBool(item)) {
            *out = (item->type == cJSON_True);
        } else if (cJSON_IsNumber(item)) {
            *out = (item->valueint != 0);
        }
    }
}

void parse_u8_key(cJSON* root, const char* key, uint8_t* out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        *out = (uint8_t) item->valueint;
    }
}

void parse_u16_key(cJSON* root, const char* key, uint16_t* out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        *out = (uint16_t) item->valueint;
    }
}

void parse_u32_key(cJSON* root, const char* key, uint32_t* out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        *out = (uint32_t)item->valuedouble;
    }
}

void parse_uint_key(cJSON* root, const char* key, unsigned int* out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        *out = (unsigned int) item->valueint;
    }
}

bool parse_string_key_dup(cJSON* root, const char* key, char** out) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring) {
        char* dup = strdup(item->valuestring);
        floatair_assert(dup != NULL, "strdup parse_string_key_dup failed");
        if (*out) {
            free(*out);
            *out = NULL;
        }
        *out = dup;
    }
    return true;
}

bool system_config_set_font(char* config_file, app_font_info_t* font) {
    if (!font || !config_file) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = load_json(config_file);
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        root = cJSON_CreateObject();
        if (!root) {
            return false;
        }
    }

    cJSON* font_upd = cJSON_GetObjectItemCaseSensitive(root, "fontinfo");
    if (!cJSON_IsObject(font_upd)) {
        font_upd = cJSON_AddObjectToObject(root, "fontinfo");
    }
    cJSON_DeleteItemFromObjectCaseSensitive(font_upd, "weight");
    cJSON_AddItemToObject(font_upd, "weight", cJSON_CreateNumber((double) font->weight));
    cJSON_DeleteItemFromObjectCaseSensitive(font_upd, "wordSpace");
    cJSON_AddItemToObject(font_upd, "wordSpace", cJSON_CreateNumber((double) font->wordSpace));
    cJSON_DeleteItemFromObjectCaseSensitive(font_upd, "rowSpace");
    cJSON_AddItemToObject(font_upd, "rowSpace", cJSON_CreateNumber((double) font->rowSpace));

    int ret_code = save_json(config_file, root);
    cJSON_Delete(root);
    return ret_code == 0;
}

bool system_config_get_font(const char* config_file, app_font_info_t* font) {
    if (!font || !config_file) {
        floatair_info("input err");
        return false;
    }
    cJSON* root = load_json(config_file);
    cJSON* item = NULL;
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        return false;
    }

    cJSON* fontinfo = cJSON_GetObjectItemCaseSensitive(root, "fontinfo");
    if (cJSON_IsObject(fontinfo)) {
        item = cJSON_GetObjectItemCaseSensitive(fontinfo, "weight");
        if (cJSON_IsNumber(item)) {
            font->weight = (unsigned int) item->valueint;
        } else {
            floatair_err("weight not found");
            cJSON_Delete(root);
            return false;
        }
    } else {
        floatair_err("fontinfo not found");
        cJSON_Delete(root);
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(fontinfo, "wordSpace");
    if (cJSON_IsNumber(item)) {
        font->wordSpace = (unsigned int) item->valueint;
    } else {
        floatair_err("wordSpace not found");
        cJSON_Delete(root);
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(fontinfo, "rowSpace");
    if (cJSON_IsNumber(item)) {
        font->rowSpace = (unsigned int) item->valueint;
    } else {
        floatair_err("rowSpace not found");
        cJSON_Delete(root);
        return false;
    }
    cJSON_Delete(root);
    return true;
}

char* system_config_get_str(const char* config_file, const char* key) {
    if (!config_file || !key) {
        floatair_err("input err");
        return NULL;
    }
    cJSON* root = load_json(config_file);
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        return NULL;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item)) {
        char* v = strdup(item->valuestring);
        floatair_assert(v != NULL, "strdup system_config_get_str failed");
        cJSON_Delete(root);
        return v;
    }
    floatair_err("%s not found", key);
    cJSON_Delete(root);
    return NULL;
}

bool system_config_set_str(const char* config_file, const char* key, const char* value) {
    if (!config_file || !key || !value) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = load_json(config_file);
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item)) {
        if (item->valuestring) {
            free(item->valuestring);
            item->valuestring = NULL;
        }
        item->valuestring = strdup(value);
        floatair_assert(item->valuestring != NULL, "strdup system_config_set_str failed");
    } else {
        floatair_err("%s not found", key);
        cJSON_Delete(root);
        return false;
    }
    cJSON_Delete(root);
    return true;
}

int system_config_get_number(const char* config_file, const char* key) {
    if (!config_file || !key) {
        floatair_err("input err");
        return -1;
    }
    cJSON* root = load_json(config_file);
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        return -1;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        int v = item->valueint;
        cJSON_Delete(root);
        return v;
    }
    floatair_err("%s not found", key);
    cJSON_Delete(root);
    return -1;
}

bool system_config_set_number(const char* config_file, const char* key, int value) {
    if (!config_file || !key) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = load_json(config_file);
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        item->valueint = value;
    } else {
        floatair_err("%s not found", key);
        cJSON_Delete(root);
        return false;
    }
    cJSON_Delete(root);
    return true;
}

bool system_config_get_bool(const char* config_file, const char* key) {
    if (!config_file || !key) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = load_json(config_file);
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item)) {
        bool v = item->valueint != 0;
        cJSON_Delete(root);
        return v;
    }
    floatair_err("%s not found", key);
    cJSON_Delete(root);
    return false;
}

bool system_config_set_bool(const char* config_file, const char* key, bool value) {
    if (!config_file || !key) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = load_json(config_file);
    if (!root) {
        floatair_err("load_json %s failed", config_file);
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item)) {
        item->valueint = value ? 1 : 0;
    } else {
        floatair_err("%s not found", key);
        cJSON_Delete(root);
        return false;
    }
    cJSON_Delete(root);
    return true;
}
