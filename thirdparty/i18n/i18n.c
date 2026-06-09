#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include "cJSON.h"
#include "i18n.h"
#include "floatair_dbg.h" 
#include "app_def.h"
#include "lvgl.h"
#include "floatair_fs.h"

typedef struct i18n_kv {
    char* key;
    char* value;
} i18n_kv_t;
typedef struct i18n_handle {
    i18n_kv_t* kvs;
    int count;
    i18n_kv_t* fb_kvs;
    int fb_count;
} i18n_handle;
static int i18n_kv_cmp(const void* a, const void* b) {
    const i18n_kv_t* ka = (const i18n_kv_t*)a;
    const i18n_kv_t* kb = (const i18n_kv_t*)b;
    if (!ka->key && !kb->key) return 0;
    if (!ka->key) return -1;
    if (!kb->key) return 1;
    return strcmp(ka->key, kb->key);
}

static inline char* i18n_strdup(const char* s) {
    if(!s) return NULL;
    size_t n = strlen(s);
    if(n == 0) return NULL;
    char* d = (char*)malloc(n + 1);
    if(!d) return NULL;
    memset(d, 0, n + 1);
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static bool i18n_load_lang(const char* dir, const char* lang, i18n_kv_t** out_kvs, int* out_cnt) {
    if (!dir || !lang || strlen(lang) == 0 || !out_kvs || !out_cnt) return false;
    char path[SYSTEM_MAX_PATH_LEN];
    memset(path, 0, sizeof(path));
    snprintf(path, sizeof(path), "%s/%s.json", dir, lang);
    void* fh = floatair_fs_open(path, FLOATAIR_FS_MODE_RD);
    if (!fh) {
        floatair_err("open fail: %s", path);
        return false;
    }
    if (floatair_fs_seek(fh, 0, SEEK_END) != FLOATAIR_FS_OK) {
        floatair_fs_close(fh);
        floatair_err("seek end fail: %s", path);
        return false;
    }
    uint32_t fsize = 0;
    if (floatair_fs_tell(fh, &fsize) != FLOATAIR_FS_OK) {
        floatair_fs_close(fh);
        floatair_err("tell fail: %s", path);
        return false;
    }
    if (fsize > I18N_FILE_MAX_SIZE) {
        floatair_err("i18n too large: %u > %u (%s)", (unsigned)fsize, I18N_FILE_MAX_SIZE, path);
        floatair_fs_close(fh);
        return false;
    }
    if (floatair_fs_seek(fh, 0, SEEK_SET) != FLOATAIR_FS_OK) {
        floatair_fs_close(fh);
        floatair_err("seek set fail: %s", path);
        return false;
    }
    char *buffer = (char *)malloc((size_t)fsize + 1);
    if (buffer == NULL) {
        floatair_err("malloc buffer fail");
        floatair_fs_close(fh);
        return false;
    }
    memset(buffer, 0, (size_t)fsize + 1);
    uint32_t br = 0;
    if (floatair_fs_read(fh, buffer, fsize, &br) != FLOATAIR_FS_OK || br != fsize) {
        floatair_err("read fail, %ju/%ju, %s", (uintmax_t)br, (uintmax_t)fsize, path);
        floatair_fs_close(fh);
        free(buffer);
        return false;
    }
    floatair_fs_close(fh);
    buffer[(size_t)fsize] = '\0';
    cJSON *json_obj = cJSON_Parse(buffer);
    free(buffer);
    if (json_obj == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            floatair_err("JSON fail: %s", error_ptr);
        }
        return false;
    }
    int count = cJSON_GetArraySize(json_obj);
    if (count < 0) count = 0;
    i18n_kv_t* kvs = (i18n_kv_t*)calloc((size_t)count, sizeof(i18n_kv_t));
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, json_obj) {
        if (!(cJSON_IsString(item) && item->string && item->valuestring)) {
            continue; /* 仅接受字符串键值 */
        }
        /* 去重覆盖：若已有同名 key，则覆盖其 value（保留“最后出现者”） */
        int exist = -1;
        for (int i = 0; i < idx; i++) {
            if (kvs[i].key && strcmp(kvs[i].key, item->string) == 0) {
                exist = i; break;
            }
        }
        if (exist >= 0) {
            char* nv = i18n_strdup(item->valuestring);
            if (nv) {
                free(kvs[exist].value);
                kvs[exist].value = nv;
            }
            continue;
        }
        kvs[idx].key = i18n_strdup(item->string);
        kvs[idx].value = i18n_strdup(item->valuestring);
        if (!kvs[idx].key || !kvs[idx].value) {
            free(kvs[idx].key);
            free(kvs[idx].value);
            kvs[idx].key = NULL;
            kvs[idx].value = NULL;
            continue;
        }
        idx++;
    }
    cJSON_Delete(json_obj);
    if (idx > 1) {
        qsort(kvs, (size_t)idx, sizeof(i18n_kv_t), i18n_kv_cmp);
    }
    *out_kvs = kvs;
    *out_cnt = idx;
    return true;
}

i18n_handle_t *i18n_open(const char *dir, const char *lang) {
    if(!dir || !lang) {
        floatair_err("i18n_open invalid param");
        return NULL;
    }
    i18n_handle* ih = (i18n_handle*)calloc(1, sizeof(i18n_handle));
    if (!ih) return NULL;
    /* 主语言 */
    bool ok = i18n_load_lang(dir, lang, &ih->kvs, &ih->count);
    /* 回退语言：en-US（若与主语言不同且存在则加载） */
    const char* fallback = "en-US";
    if (strcmp(lang, fallback) != 0) {
        (void)i18n_load_lang(dir, fallback, &ih->fb_kvs, &ih->fb_count);
    }
    if (!ok && ih->fb_count <= 0) {
        /* 两者都失败 */
        i18n_close((i18n_handle_t*)ih);
        return NULL;
    }
    return (i18n_handle_t*)ih;
}

const char *i18n_query(i18n_handle_t *h, const char *key) {
    if(!h || !key) return "";
    i18n_handle* ih = (i18n_handle*)h;
    i18n_kv_t needle = { .key = (char*)key, .value = NULL };
    if (ih->kvs && ih->count > 0) {
        i18n_kv_t* res = (i18n_kv_t*)bsearch(&needle, ih->kvs, (size_t)ih->count, sizeof(i18n_kv_t), i18n_kv_cmp);
        if (res && res->value) return res->value;
    }
    /* 回退语言查找 */
    if (ih->fb_kvs && ih->fb_count > 0) {
        i18n_kv_t* res = (i18n_kv_t*)bsearch(&needle, ih->fb_kvs, (size_t)ih->fb_count, sizeof(i18n_kv_t), i18n_kv_cmp);
        if (res && res->value) return res->value;
    }
    return "";
}

void i18n_close(i18n_handle_t *h) {
    if(!h) return;
    i18n_handle* ih = (i18n_handle*)h;
    if (ih->kvs) {
        for (int i = 0; i < ih->count; i++) {
            free(ih->kvs[i].key);
            free(ih->kvs[i].value);
        }
        free(ih->kvs);
        ih->kvs = NULL;
        ih->count = 0;
    }
    if (ih->fb_kvs) {
        for (int i = 0; i < ih->fb_count; i++) {
            free(ih->fb_kvs[i].key);
            free(ih->fb_kvs[i].value);
        }
        free(ih->fb_kvs);
        ih->fb_kvs = NULL;
        ih->fb_count = 0;
    }
    free(ih);
}

char * i18n_get_single_string(const char *dir, const char *lang, const char *key) {
    if(!dir || !lang || !key) return i18n_strdup("");
    floatair_info("i18n_get_single_string %s %s %s", dir, lang, key);
    i18n_handle_t *h = i18n_open(dir, lang);
    if (!h) return i18n_strdup("");
    const char *res = i18n_query(h, key);
    char *dup = i18n_strdup(res ? res : "");
    i18n_close(h);
    floatair_info("get %s", dup);
    return dup;
}
