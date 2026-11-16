/**
 * @file system_config_json.c
 * @brief 基于 JYTC 默认值与 LFSD 稀疏覆盖的系统配置读写实现。
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

#define SYSTEM_CONFIG_JYTC_LFSD_ROOT "/jyt_c/lfsd" ///< JYTC 中随固件发布的 LFSD 配置模板根目录。
#define SYSTEM_CONFIG_TEMP_SUFFIX ".tmp"             ///< 配置原子写回使用的临时文件后缀。

static char* read_all(const char* path);
static int write_all(const char* path, size_t len, const char* buf);
static bool mkdir_parent(const char* path);

static bool mkdir_parent(const char* path) {
    bool is_dir = false;

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
    if (floatair_fs_is_dir(dir, &is_dir) == FLOATAIR_FS_OK) {
        return is_dir;
    }
    return floatair_fs_mkdirs(dir) == FLOATAIR_FS_OK;
}

/**
 * @brief 读取并解析单个 JSON 文件，不执行配置合并。
 * @param[in] path JSON 文件路径。
 * @return 解析成功返回 JSON 根节点，否则返回 `NULL`。
 */
static cJSON* system_config_load_file(const char* path) {
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

/**
 * @brief 将 JSON 根节点原子写入单个文件。
 * @param[in] path JSON 文件路径。
 * @param[in] root JSON 根节点。
 * @return 写入成功返回 0，否则返回非 0。
 */
static int system_config_save_file(const char* path, const cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    if (!json) {
        return -1;
    }
    size_t len   = strlen(json);
    int ret_code = write_all(path, len, json);
    free(json);
    return ret_code;
}

/**
 * @brief 将 LFSD 配置路径映射到 JYTC 配置模板路径。
 * @param[in] config_file LFSD 配置路径。
 * @param[out] jytc_path JYTC 模板路径缓冲区。
 * @param[in] path_size JYTC 模板路径缓冲区容量。
 * @return 映射成功返回 `true`。
 */
static bool system_config_get_jytc_path(const char* config_file,
                                         char* jytc_path,
                                         size_t path_size) {
    const char* lfsd_root = floatair_fs_get_root_path();
    size_t lfsd_root_len = 0;
    int path_len = 0;

    if (config_file == NULL || jytc_path == NULL || path_size == 0 ||
        lfsd_root == NULL || lfsd_root[0] == '\0') {
        return false;
    }
    lfsd_root_len = strlen(lfsd_root);
    if (strncmp(config_file, lfsd_root, lfsd_root_len) != 0 ||
        config_file[lfsd_root_len] != '/') {
        floatair_err("config path is outside lfsd root: %s", config_file);
        return false;
    }
    path_len = snprintf(jytc_path,
                        path_size,
                        "%s%s",
                        SYSTEM_CONFIG_JYTC_LFSD_ROOT,
                        config_file + lfsd_root_len);
    if (path_len < 0 || (size_t)path_len >= path_size) {
        floatair_err("jytc config path is too long: %s", config_file);
        return false;
    }
    return true;
}

/**
 * @brief 判断覆盖字段类型是否与 JYTC 默认字段类型一致。
 * @param[in] config_item 覆盖字段。
 * @param[in] template_item JYTC 模板字段。
 * @return 类型一致返回 `true`。
 */
static bool system_config_field_type_matches(const cJSON* config_item,
                                             const cJSON* template_item) {
    if (cJSON_IsBool(config_item) && cJSON_IsBool(template_item)) {
        return true;
    }
    return config_item != NULL && template_item != NULL &&
           (config_item->type & 0xff) == (template_item->type & 0xff);
}

/**
 * @brief 使用 LFSD 稀疏配置递归覆盖 JYTC 完整配置。
 * @param[in,out] config JYTC 完整配置副本。
 * @param[in] overrides LFSD 稀疏覆盖配置。
 * @return 合并成功返回 `true`。
 */
static bool system_config_apply_overrides(cJSON* config, const cJSON* overrides) {
    const cJSON* override_item = NULL;

    if (!cJSON_IsObject(config) || !cJSON_IsObject(overrides)) {
        return false;
    }
    cJSON_ArrayForEach(override_item, overrides) {
        cJSON* config_item = cJSON_GetObjectItemCaseSensitive(config, override_item->string);

        if (config_item == NULL) {
            floatair_warn("ignore unknown config override: %s", override_item->string);
            continue;
        }
        if (!system_config_field_type_matches(override_item, config_item)) {
            floatair_warn("ignore config override with invalid type: %s", override_item->string);
            continue;
        }
        if (cJSON_IsObject(config_item)) {
            if (!system_config_apply_overrides(config_item, override_item)) {
                return false;
            }
        } else {
            cJSON* duplicated_item = cJSON_Duplicate(override_item, true);

            if (duplicated_item == NULL ||
                !cJSON_ReplaceItemInObjectCaseSensitive(config,
                                                        override_item->string,
                                                        duplicated_item)) {
                cJSON_Delete(duplicated_item);
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief 根据完整有效配置与 JYTC 默认配置生成 LFSD 稀疏覆盖。
 * @param[in] config 完整有效配置。
 * @param[in] config_template JYTC 完整默认配置。
 * @return 成功返回覆盖对象，否则返回 `NULL`。
 */
static cJSON* system_config_create_overrides(const cJSON* config,
                                             const cJSON* config_template) {
    const cJSON* template_item = NULL;
    cJSON* overrides = NULL;

    if (!cJSON_IsObject(config) || !cJSON_IsObject(config_template)) {
        return NULL;
    }
    overrides = cJSON_CreateObject();
    if (overrides == NULL) {
        return NULL;
    }
    cJSON_ArrayForEach(template_item, config_template) {
        const cJSON* config_item = cJSON_GetObjectItemCaseSensitive(config,
                                                                    template_item->string);
        cJSON* override_item = NULL;

        if (!system_config_field_type_matches(config_item, template_item)) {
            floatair_err("effective config field missing or invalid: %s", template_item->string);
            cJSON_Delete(overrides);
            return NULL;
        }
        if (cJSON_IsObject(template_item)) {
            override_item = system_config_create_overrides(config_item,
                                                           template_item);
            if (override_item == NULL) {
                cJSON_Delete(overrides);
                return NULL;
            }
            if (cJSON_GetArraySize(override_item) == 0) {
                cJSON_Delete(override_item);
                continue;
            }
        } else if (!cJSON_Compare(config_item, template_item, true)) {
            override_item = cJSON_Duplicate(config_item, true);
        } else {
            continue;
        }
        if (override_item == NULL ||
            !cJSON_AddItemToObject(overrides, template_item->string, override_item)) {
            cJSON_Delete(override_item);
            cJSON_Delete(overrides);
            return NULL;
        }
    }
    return overrides;
}

/**
 * @brief 删除存在的配置文件。
 * @param[in] path 待删除路径。
 * @return 文件不存在或删除成功返回 `true`。
 */
static bool system_config_remove_file(const char* path) {
    if (!floatair_fs_is_exist(path)) {
        return true;
    }
    return floatair_fs_remove(path) == FLOATAIR_FS_OK;
}

cJSON* system_config_load_json(const char* config_file) {
    char jytc_path[SYSTEM_MAX_PATH_LEN] = {0};
    cJSON* config = NULL;
    cJSON* overrides = NULL;

    if (!system_config_get_jytc_path(config_file, jytc_path, sizeof(jytc_path))) {
        return NULL;
    }
    config = system_config_load_file(jytc_path);
    if (!cJSON_IsObject(config)) {
        floatair_err("jytc config is invalid: %s", jytc_path);
        cJSON_Delete(config);
        return NULL;
    }
    if (!floatair_fs_is_exist(config_file)) {
        return config;
    }
    overrides = system_config_load_file(config_file);
    if (!cJSON_IsObject(overrides)) {
        floatair_warn("ignore invalid lfsd config overrides: %s", config_file);
        cJSON_Delete(overrides);
        return config;
    }
    if (!system_config_apply_overrides(config, overrides)) {
        cJSON_Delete(overrides);
        cJSON_Delete(config);
        return NULL;
    }
    cJSON_Delete(overrides);
    return config;
}

/**
 * @brief 删除 LFSD 配置及其临时文件，使配置恢复使用 JYTC 默认值。
 * @param[in] config_file LFSD 配置文件路径。
 * @return 文件均不存在或删除成功返回 `true`。
 */
static bool system_config_reset(const char* config_file) {
    char temp_path[SYSTEM_MAX_PATH_LEN] = {0};
    int path_len = 0;

    if (config_file == NULL || config_file[0] == '\0') {
        return false;
    }
    path_len = snprintf(temp_path, sizeof(temp_path), "%s%s", config_file, SYSTEM_CONFIG_TEMP_SUFFIX);
    if (path_len < 0 || (size_t)path_len >= sizeof(temp_path)) {
        return false;
    }
    return system_config_remove_file(temp_path) && system_config_remove_file(config_file);
}

int system_config_save_json(const char* config_file, const cJSON* config) {
    char jytc_path[SYSTEM_MAX_PATH_LEN] = {0};
    cJSON* config_template = NULL;
    cJSON* overrides = NULL;
    int result = -1;

    if (!cJSON_IsObject(config) ||
        !system_config_get_jytc_path(config_file, jytc_path, sizeof(jytc_path))) {
        return -1;
    }
    config_template = system_config_load_file(jytc_path);
    if (!cJSON_IsObject(config_template)) {
        floatair_err("jytc config is invalid: %s", jytc_path);
        cJSON_Delete(config_template);
        return -1;
    }
    overrides = system_config_create_overrides(config, config_template);
    if (overrides == NULL) {
        cJSON_Delete(overrides);
        cJSON_Delete(config_template);
        return -1;
    }
    if (cJSON_GetArraySize(overrides) == 0) {
        result = system_config_reset(config_file) ? 0 : -1;
    } else {
        result = system_config_save_file(config_file, overrides);
    }
    cJSON_Delete(overrides);
    cJSON_Delete(config_template);
    return result;
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
    char temp_path[SYSTEM_MAX_PATH_LEN] = {0};
    void* h = NULL;
    size_t written = 0;
    int path_len = 0;
    int sync_result = FLOATAIR_FS_OK;
    int close_result = FLOATAIR_FS_OK;

    if (!path || len == 0 || !buf) {
        floatair_err("path is NULL or len is 0 or buf is NULL");
        return -1;
    }
    path_len = snprintf(temp_path, sizeof(temp_path), "%s%s", path, SYSTEM_CONFIG_TEMP_SUFFIX);
    if (path_len < 0 || (size_t)path_len >= sizeof(temp_path)) {
        floatair_err("temp config path is too long: %s", path);
        return -1;
    }
    if (!mkdir_parent(path)) {
        floatair_err("mkdir config parent failed: %s", path);
        return -1;
    }
    h = floatair_fs_open(temp_path,
                         FLOATAIR_FS_MODE_WR |
                             FLOATAIR_FS_MODE_CREATE |
                             FLOATAIR_FS_MODE_TRUNC);
    if (!h) {
        floatair_err("open temp config failed: %s", temp_path);
        return -1;
    }

    while (written < len) {
        size_t remaining = len - written;
        uint32_t write_size = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
        uint32_t current_written = 0;

        if (floatair_fs_write(h,
                              buf + written,
                              write_size,
                              &current_written) != FLOATAIR_FS_OK ||
            current_written == 0) {
            floatair_err("write temp config failed: %s, written=%lu len=%lu",
                         temp_path,
                         (unsigned long)written,
                         (unsigned long)len);
            (void)floatair_fs_close(h);
            (void)floatair_fs_remove(temp_path);
            return -2;
        }
        written += current_written;
    }
    sync_result = floatair_fs_sync(h);
    close_result = floatair_fs_close(h);
    if (sync_result != FLOATAIR_FS_OK || close_result != FLOATAIR_FS_OK) {
        floatair_err("sync temp config failed: %s", temp_path);
        (void)floatair_fs_remove(temp_path);
        return -2;
    }
    if (floatair_fs_rename(temp_path, path) != FLOATAIR_FS_OK) {
        floatair_err("replace config failed: %s -> %s", temp_path, path);
        (void)floatair_fs_remove(temp_path);
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
    cJSON* root = system_config_load_json(config_file);
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
        return false;
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

    int ret_code = system_config_save_json(config_file, root);
    cJSON_Delete(root);
    return ret_code == 0;
}

bool system_config_get_font(const char* config_file, app_font_info_t* font) {
    if (!font || !config_file) {
        floatair_info("input err");
        return false;
    }
    cJSON* root = system_config_load_json(config_file);
    cJSON* item = NULL;
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
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
    cJSON* root = system_config_load_json(config_file);
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
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
    cJSON* replacement = NULL;
    bool result = false;

    if (!config_file || !key || !value) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = system_config_load_json(config_file);
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item)) {
        replacement = cJSON_CreateString(value);
        if (replacement == NULL ||
            !cJSON_ReplaceItemInObjectCaseSensitive(root, key, replacement)) {
            cJSON_Delete(replacement);
        } else {
            result = system_config_save_json(config_file, root) == 0;
        }
    } else {
        floatair_err("%s not found", key);
    }
    cJSON_Delete(root);
    return result;
}

int system_config_get_number(const char* config_file, const char* key) {
    if (!config_file || !key) {
        floatair_err("input err");
        return -1;
    }
    cJSON* root = system_config_load_json(config_file);
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
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
    bool result = false;

    if (!config_file || !key) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = system_config_load_json(config_file);
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item)) {
        cJSON_SetNumberValue(item, value);
        result = system_config_save_json(config_file, root) == 0;
    } else {
        floatair_err("%s not found", key);
    }
    cJSON_Delete(root);
    return result;
}

bool system_config_get_bool(const char* config_file, const char* key) {
    if (!config_file || !key) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = system_config_load_json(config_file);
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
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
    cJSON* replacement = NULL;
    bool result = false;

    if (!config_file || !key) {
        floatair_err("input err");
        return false;
    }
    cJSON* root = system_config_load_json(config_file);
    if (!root) {
        floatair_err("load effective config failed: %s", config_file);
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item)) {
        replacement = cJSON_CreateBool(value);
        if (replacement == NULL ||
            !cJSON_ReplaceItemInObjectCaseSensitive(root, key, replacement)) {
            cJSON_Delete(replacement);
        } else {
            result = system_config_save_json(config_file, root) == 0;
        }
    } else {
        floatair_err("%s not found", key);
    }
    cJSON_Delete(root);
    return result;
}
