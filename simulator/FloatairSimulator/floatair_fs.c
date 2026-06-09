#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../../common/floatair_fs.h"
#include "../../common/app_def.h"
#include "floatair_dbg.h"
#include "simulator_platform.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    FILE* fp;
} fa_file_t;

struct floatair_dir {
    DIR* d;
    char path[512];
};

#if defined(_MSC_VER) && !defined(S_ISDIR)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif

static bool fs_path_is_dir(const char* path)
{
    simulator_platform_stat_t st = {0};
    return path && simulator_platform_stat_path(path, &st) == 0 && st.is_dir;
}

static int fs_stat_path(const char* path, floatair_stat_t* st_out)
{
    simulator_platform_stat_t st = {0};

    if (!path || !st_out) {
        return -1;
    }

    if (simulator_platform_stat_path(path, &st) != 0) {
        return -1;
    }

    st_out->is_dir = st.is_dir;
    st_out->size = st.size;
    return 0;
}

#define SYSTEM_ROOT_PATH "/jyt_d"
#define LVGL_SYSTEM_IMAGES_PATH "/romfs/system/images/"
#define LVGL_SYSTEM_FONT_FILE   "/romfs/system/font/font.ttf"
#define LVGL_SYSTEM_I18N_PATH   "/jyt_d/system/i18n"
#define SYSTEM_CONFIG_FILE "/jyt_d/system/config.json"

static void to_host_path(const char* in, char* out, size_t outsz)
{
    if (!in || !out || outsz == 0) return;
    memset(out, 0, outsz);

    char exe_path[SYSTEM_MAX_PATH_LEN];
    simulator_platform_get_executable_dir(exe_path, sizeof(exe_path));

    if (strncmp(in, "/jyt_d", 6) == 0 || strncmp(in, "/romfs", 6) == 0) {
        snprintf(out, outsz, "%s%s", exe_path, in);
    } else {
        strncpy(out, in, outsz - 1);
    }
    for (size_t i = 0; out[i] != '\0'; ++i) {
        if (out[i] == '\\') {
            out[i] = '/';
        }
    }
    
    if (out[0] == '\0') {
        if (strncmp(in, "/jyt_d", 6) == 0 || strncmp(in, "/romfs", 6) == 0) {
            snprintf(out, outsz, ".%s", in);
        } else {
            strncpy(out, in, outsz - 1);
        }
    }
    
    // floatair_info("Mapped path: %s -> %s", in, out);
}

const char *floatair_fs_get_root_path(void)
{
    return SYSTEM_ROOT_PATH;
}

const char *floatair_fs_get_system_images_path(void)
{
    return LVGL_SYSTEM_IMAGES_PATH;
}

const char *floatair_fs_get_system_font_file(void)
{
    return LVGL_SYSTEM_FONT_FILE;
}

const char *floatair_fs_get_system_i18n_path(void)
{
    return LVGL_SYSTEM_I18N_PATH;
}

const char *floatair_fs_get_system_config_file(void)
{
    return SYSTEM_CONFIG_FILE;
}

bool floatair_fs_get_app_images_path(const char* app_name, char* out_path, size_t outsz) {
    if (!app_name || !out_path || outsz == 0) {
        return false;
    }
    if (strlen(app_name) + strlen("/jyt_d/apps/") + strlen("/images/") >= outsz) {
        return false;
    }
    snprintf(out_path, outsz, "/jyt_d/apps/%s/images/", app_name);
    return true;
}

bool floatair_fs_get_app_config_file(const char* app_name, char* out_path, size_t outsz) {
    if (!app_name || !out_path || outsz == 0) {
        return false;
    }
    if (strlen(app_name) + strlen("/jyt_d/apps/") + strlen("config.json") + 1 >= outsz) {
        return false;
    }
    snprintf(out_path, outsz, "/jyt_d/apps/%s/config.json", app_name);
    return true;
}

static int map_fseek_whence(int whence) {
    return whence;
}

void* floatair_fs_open(const char* path_in, uint32_t mode) {
    if (!path_in) return NULL;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(path_in, path, sizeof(path));
    char m[5] = {0};
    bool wants_write = (mode & FLOATAIR_FS_MODE_WR) != 0;
    bool wants_read = (mode & FLOATAIR_FS_MODE_RD) != 0;
    bool wants_append = (mode & FLOATAIR_FS_MODE_APPEND) != 0;
    bool wants_create = (mode & FLOATAIR_FS_MODE_CREATE) != 0;
    bool wants_trunc = (mode & FLOATAIR_FS_MODE_TRUNC) != 0;
    bool file_exists = (simulator_platform_path_exists(path) == 0);
    if (wants_write && wants_append) {
        if (wants_read) strcpy(m, "a+b");
        else strcpy(m, "ab");
    } else if (wants_write) {
        if (wants_trunc) {
            if (wants_read) strcpy(m, "w+b");
            else strcpy(m, "wb");
        } else if (wants_create && !file_exists) {
            if (wants_read) strcpy(m, "w+b");
            else strcpy(m, "wb");
        } else {
            if (wants_read) strcpy(m, "r+b");
            else strcpy(m, "r+b");
        }
    } else {
        strcpy(m, "rb");
    }
    FILE* fp = simulator_platform_file_open(path, m);
    if (!fp) {
        floatair_err("open fail: %s (mapped: %s) errno=%d", path_in, path, errno);
        return NULL;
    }
    fa_file_t* h = (fa_file_t*)malloc(sizeof(fa_file_t));
    if (!h) {
        fclose(fp);
        return NULL;
    }
    h->fp = fp;
    return h;
}

int floatair_fs_read(void* handle, void* buf, uint32_t btr, uint32_t* br) {
    if (!handle || !buf) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    size_t r = fread(buf, 1, btr, h->fp);
    if (br) *br = (uint32_t)r;
    if (r < btr && ferror(h->fp)) {
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_write(void* handle, const void* buf, uint32_t btw, uint32_t* bw) {
    if (!handle || !buf) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    size_t w = fwrite(buf, 1, btw, h->fp);
    if (bw) *bw = (uint32_t)w;
    if (w < btw && ferror(h->fp)) {
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_seek(void* handle, int32_t offset, int whence) {
    if (!handle) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    if (fseek(h->fp, offset, map_fseek_whence(whence)) != 0) {
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_tell(void* handle, uint32_t* pos) {
    if (!handle || !pos) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    long p = ftell(h->fp);
    if (p < 0) {
        return FLOATAIR_FS_ERR_IO;
    }
    *pos = (uint32_t)p;
    return FLOATAIR_FS_OK;
}

int floatair_fs_close(void* handle) {
    if (!handle) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    int r = fclose(h->fp);
    free(h);
    if (r != 0) {
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_remove(const char* path_in) {
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(path_in, path, sizeof(path));
    int r = simulator_platform_remove_path(path);
    if (r == 0) {
        return FLOATAIR_FS_OK;
    }
    if (r == 1) {
        return FLOATAIR_FS_ERR_NOENT;
    }
    return FLOATAIR_FS_ERR_IO;
}

int floatair_fs_rename(const char* old_path_in, const char* new_path_in) {
    if (!old_path_in || !new_path_in) return FLOATAIR_FS_ERR_PARAM;
    char oldp[SYSTEM_MAX_PATH_LEN] = {0};
    char newp[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(old_path_in, oldp, sizeof(oldp));
    to_host_path(new_path_in, newp, sizeof(newp));
    if (simulator_platform_rename_path(oldp, newp) == 0) return FLOATAIR_FS_OK;
    return FLOATAIR_FS_ERR_IO;
}

int floatair_fs_mkdirs(const char* path_in) {
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    size_t len;
    char tmp[SYSTEM_MAX_PATH_LEN];
    size_t i;
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    to_host_path(path_in, path, sizeof(path));
    len = strlen(path);
    if (len == 0) return FLOATAIR_FS_ERR_PARAM;
    memset(tmp, 0, sizeof(tmp));
    for (i = 0; i < len; ++i) {
        tmp[i] = path[i];
        if (tmp[i] == '/') {
            if (i > 0) {
                if (simulator_platform_mkdir_one(tmp) != 0) {
                    return FLOATAIR_FS_ERR_IO;
                }
            }
        }
    }
    if (simulator_platform_mkdir_one(path) != 0) {
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_is_dir(const char* path_in, bool* is_dir) {
    if (!path_in || !is_dir) return FLOATAIR_FS_ERR_PARAM;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    floatair_stat_t st;
    to_host_path(path_in, path, sizeof(path));
    if (fs_stat_path(path, &st) == 0) {
        *is_dir = st.is_dir;
        return FLOATAIR_FS_OK;
    }
    return FLOATAIR_FS_ERR_NOENT;
}

int floatair_fs_stat(const char* path_in, floatair_stat_t* st_out) {
    if (!path_in || !st_out) return FLOATAIR_FS_ERR_PARAM;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(path_in, path, sizeof(path));
    if (fs_stat_path(path, st_out) == 0) {
        return FLOATAIR_FS_OK;
    }
    return FLOATAIR_FS_ERR_NOENT;
}

bool floatair_fs_is_exist(const char* path_in) {
    if (!path_in) return false;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(path_in, path, sizeof(path));
    return simulator_platform_path_exists(path) == 0;
}

int floatair_fs_dir_open(const char* path_in, floatair_dir_t** out_dir) {
    DIR* d = NULL;
    floatair_dir_t* fd = NULL;

    if (!path_in || !out_dir) return FLOATAIR_FS_ERR_PARAM;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(path_in, path, sizeof(path));
    d = opendir(path);
    if (!d) {
        return FLOATAIR_FS_ERR_NOENT;
    }
    fd = (floatair_dir_t*)calloc(1, sizeof(floatair_dir_t));
    if (!fd) {
        closedir(d);
        return FLOATAIR_FS_ERR_IO;
    }
    fd->d = d;
    strncpy(fd->path, path, sizeof(fd->path) - 1U);
    *out_dir = fd;
    return FLOATAIR_FS_OK;
}

int floatair_fs_dir_read(floatair_dir_t* dir, char* namebuf, uint32_t buflen, bool* out_is_dir) {
    struct dirent* entry = NULL;

    if (!dir || !namebuf || buflen == 0) {
        return FLOATAIR_FS_ERR_PARAM;
    }

    while ((entry = readdir(dir->d)) != NULL) {
        bool isdir = false;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t full_path_len = strlen(dir->path) + 1U + strlen(entry->d_name) + 1U;
        char* full_path = (char*)malloc(full_path_len);
        if (!full_path) {
            return FLOATAIR_FS_ERR_IO;
        }
        snprintf(full_path, full_path_len, "%s/%s", dir->path, entry->d_name);
        isdir = fs_path_is_dir(full_path);
        free(full_path);
        if (out_is_dir) {
            *out_is_dir = isdir;
        }
        if (isdir) {
            if (buflen < 2) {
                return FLOATAIR_FS_ERR_PARAM;
            }
            namebuf[0] = '/';
            strncpy(namebuf + 1, entry->d_name, buflen - 2U);
            namebuf[buflen - 1U] = '\0';
        } else {
            strncpy(namebuf, entry->d_name, buflen - 1U);
            namebuf[buflen - 1U] = '\0';
        }
        return FLOATAIR_FS_OK;
    }

    return FLOATAIR_FS_ERR_NOENT;
}

int floatair_fs_dir_close(floatair_dir_t* dir) {
    if (!dir) {
        return FLOATAIR_FS_ERR_PARAM;
    }
    closedir(dir->d);
    free(dir);
    return FLOATAIR_FS_OK;
}

int floatair_fs_mkdir(const char* path_in) {
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(path_in, path, sizeof(path));
    if (simulator_platform_mkdir_one(path) == 0) return FLOATAIR_FS_OK;
    if (errno == EEXIST) return FLOATAIR_FS_OK;
    return FLOATAIR_FS_ERR_IO;
}

int floatair_fs_rmdir(const char* path_in) {
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    char path[SYSTEM_MAX_PATH_LEN] = {0};
    to_host_path(path_in, path, sizeof(path));
    if (simulator_platform_rmdir(path) == 0) return FLOATAIR_FS_OK;
    return FLOATAIR_FS_ERR_IO;
}
