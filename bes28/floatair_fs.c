#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "floatair_fs.h"
#include "app_def.h"
#include "floatair_dbg.h"

typedef struct {
    int fd;
} fa_file_t;

struct floatair_dir {
    DIR* dirp;
};

#define SYSTEM_ROOT_PATH "/jyt_d"
#define SYSTEM_IMAGES_PATH "/romfs/system/images/"
#define SYSTEM_FONT_FILE "/romfs/system/font/font.ttf"
#define SYSTEM_I18N_PATH "/jyt_d/system/i18n"
#define SYSTEM_CONFIG_FILE "/jyt_d/system/config.json"

#define APPS_BASE_PATH "/jyt_d/apps/"

const char *floatair_fs_get_root_path(void)
{
    return SYSTEM_ROOT_PATH;
}

const char *floatair_fs_get_system_images_path(void)
{
    return SYSTEM_IMAGES_PATH;
}

const char *floatair_fs_get_system_font_file(void)
{
    return SYSTEM_FONT_FILE;
}

const char *floatair_fs_get_system_i18n_path(void)
{
    return SYSTEM_I18N_PATH;
}

const char *floatair_fs_get_system_config_file(void)
{
    return SYSTEM_CONFIG_FILE;
}

bool floatair_fs_get_app_images_path(const char* app_name, char* out_path, size_t outsz) {
    if (!app_name || !out_path || outsz == 0) {
        floatair_err("floatair_fs_get_app_images_path invalid arg");
        return false;
    }
    if (strlen(app_name) + strlen(APPS_BASE_PATH) + strlen("/images/") >= outsz) {
        floatair_err("floatair_fs_get_app_images_path outsz too small");
        return false;
    }
    snprintf(out_path, outsz, "%s%s/images/", APPS_BASE_PATH, app_name);
    return true;
}

bool floatair_fs_get_app_config_file(const char* app_name, char* out_path, size_t outsz) {
    if (!app_name || !out_path || outsz == 0) {
        floatair_err("floatair_fs_get_app_config_file invalid arg");
        return false;
    }
    if (strlen(app_name) + strlen(APPS_BASE_PATH) + strlen("/config.json") >= outsz) {
        floatair_err("floatair_fs_get_app_config_file outsz too small");
        return false;
    }
    snprintf(out_path, outsz, "%s%s/config.json", APPS_BASE_PATH, app_name);
    return true;
}

void* floatair_fs_open(const char* path_in, uint32_t mode) {
    if (!path_in) return NULL;
    int flags = 0;
    if ((mode & FLOATAIR_FS_MODE_RD) && (mode & FLOATAIR_FS_MODE_WR)) flags = O_RDWR;
    else if (mode & FLOATAIR_FS_MODE_WR) flags = O_WRONLY;
    else flags = O_RDONLY;
    if (mode & FLOATAIR_FS_MODE_CREATE) flags |= O_CREAT;
    if (mode & FLOATAIR_FS_MODE_TRUNC) flags |= O_TRUNC;
    if (mode & FLOATAIR_FS_MODE_APPEND) flags |= O_APPEND;
    floatair_dbg("open %s flags=%d", path_in, flags);
    int fd = open(path_in, flags, 0666);
    if (fd < 0) {
        floatair_err("open fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
        return NULL;
    }
    fa_file_t* h = (fa_file_t*)malloc(sizeof(fa_file_t));
    if (!h) {
        floatair_err("open oom");
        close(fd);
        return NULL;
    }
    h->fd = fd;
    return h;
}

int floatair_fs_read(void* handle, void* buf, uint32_t btr, uint32_t* br) {
    if (!handle || !buf) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    ssize_t r = read(h->fd, buf, btr);
    if (r < 0) {
        floatair_err("read fail errno=%d(%s)", errno, strerror(errno));
        return FLOATAIR_FS_ERR_IO;
    }
    if (br) *br = (uint32_t)r;
    return FLOATAIR_FS_OK;
}

int floatair_fs_write(void* handle, const void* buf, uint32_t btw, uint32_t* bw) {
    if (!handle || !buf) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    ssize_t w = write(h->fd, buf, btw);
    if (w < 0) {
        floatair_err("write fail errno=%d(%s)", errno, strerror(errno));
        return FLOATAIR_FS_ERR_IO;
    }
    if (bw) *bw = (uint32_t)w;
    return FLOATAIR_FS_OK;
}

int floatair_fs_seek(void* handle, int32_t offset, int whence) {
    if (!handle) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    off_t r = lseek(h->fd, offset, whence);
    if (r < 0) {
        floatair_err("seek fail off=%" PRId32 " whence=%d errno=%d(%s)", offset, whence, errno, strerror(errno));
        return FLOATAIR_FS_ERR_IO;
    }
    floatair_dbg("seek off=%" PRId32 " whence=%d", offset, whence);
    return FLOATAIR_FS_OK;
}

int floatair_fs_tell(void* handle, uint32_t* pos) {
    if (!handle || !pos) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    off_t r = lseek(h->fd, 0, SEEK_CUR);
    if (r < 0) {
        floatair_err("tell fail errno=%d(%s)", errno, strerror(errno));
        return FLOATAIR_FS_ERR_IO;
    }
    *pos = (uint32_t)r;
    floatair_dbg("tell pos=%" PRIu32, *pos);
    return FLOATAIR_FS_OK;
}

int floatair_fs_close(void* handle) {
    if (!handle) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    int r = close(h->fd);
    free(h);
    if (r != 0) {
        floatair_err("close fail errno=%d(%s)", errno, strerror(errno));
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_remove(const char* path_in) {
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    struct stat st;
    if (stat(path_in, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            if (rmdir(path_in) == 0) return FLOATAIR_FS_OK;
            floatair_err("rmdir fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
        } else {
            if (unlink(path_in) == 0) return FLOATAIR_FS_OK;
            floatair_err("unlink fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
        }
    } else {
        floatair_err("stat fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
        return FLOATAIR_FS_ERR_NOENT;
    }
    return FLOATAIR_FS_ERR_IO;
}

int floatair_fs_rename(const char* old_path_in, const char* new_path_in) {
    if (!old_path_in || !new_path_in) return FLOATAIR_FS_ERR_PARAM;
    if (rename(old_path_in, new_path_in) == 0) return FLOATAIR_FS_OK;
    floatair_err("rename fail: %s -> %s errno=%d(%s)", old_path_in, new_path_in, errno, strerror(errno));
    return FLOATAIR_FS_ERR_IO;
}

static int mkdir_one(const char* p) {
    if (mkdir(p, 0777) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

int floatair_fs_mkdirs(const char* path_in) {
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    size_t len = strlen(path_in);
    if (len == 0) return FLOATAIR_FS_ERR_PARAM;

    char path[SYSTEM_MAX_PATH_LEN];
    if (len >= sizeof(path)) return FLOATAIR_FS_ERR_PARAM;
    strncpy(path, path_in, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }

    const char* root = SYSTEM_ROOT_PATH;
    size_t root_len = strlen(root);
    if (strncmp(path, root, root_len) != 0 || (path[root_len] != '\0' && path[root_len] != '/')) {
        floatair_err("mkdirs denied: %s", path);
        return FLOATAIR_FS_ERR_DENIED;
    }

    struct stat root_st;
    if (stat(root, &root_st) != 0 || !S_ISDIR(root_st.st_mode)) {
        floatair_err("mkdirs root missing: %s errno=%d(%s)", root, errno, strerror(errno));
        return FLOATAIR_FS_ERR_NOENT;
    }

    if (strcmp(path, root) == 0) {
        return FLOATAIR_FS_OK;
    }

    char tmp[SYSTEM_MAX_PATH_LEN];
    memset(tmp, 0, sizeof(tmp));
    for (size_t i = 0; i < len; ++i) {
        tmp[i] = path[i];
        if (tmp[i] == '/') {
            if (i <= root_len) continue;
            if (mkdir_one(tmp) != 0) {
                floatair_err("mkdir step fail: %s errno=%d(%s)", tmp, errno, strerror(errno));
                return FLOATAIR_FS_ERR_IO;
            }
        }
    }
    if (mkdir_one(path) != 0) {
        floatair_err("mkdir final fail: %s errno=%d(%s)", path, errno, strerror(errno));
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_is_dir(const char* path_in, bool* is_dir) {
    if (!path_in || !is_dir) return FLOATAIR_FS_ERR_PARAM;
    struct stat st;
    if (stat(path_in, &st) != 0) {
        floatair_err("is_dir stat fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
        return FLOATAIR_FS_ERR_NOENT;
    }
    *is_dir = S_ISDIR(st.st_mode);
    return FLOATAIR_FS_OK;
}

int floatair_fs_stat(const char* path_in, floatair_stat_t* st_out) {
    if (!path_in || !st_out) return FLOATAIR_FS_ERR_PARAM;
    struct stat st;
    if (stat(path_in, &st) != 0) {
        floatair_err("stat fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
        return FLOATAIR_FS_ERR_NOENT;
    }
    st_out->is_dir = S_ISDIR(st.st_mode);
    st_out->size = (uint32_t)st.st_size;
    return FLOATAIR_FS_OK;
}

bool floatair_fs_is_exist(const char* path_in) {
    if (!path_in) return false;
    struct stat st;
    return stat(path_in, &st) == 0;
}

int floatair_fs_dir_open(const char* path_in, floatair_dir_t** out_dir) {
    if (!path_in || !out_dir) return FLOATAIR_FS_ERR_PARAM;
    DIR* dp = opendir(path_in);
    if (!dp) {
        floatair_err("dir_open fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
        return FLOATAIR_FS_ERR_NOENT;
    }
    floatair_dir_t* d = (floatair_dir_t*)malloc(sizeof(floatair_dir_t));
    if (!d) {
        floatair_err("dir_open oom");
        closedir(dp);
        return FLOATAIR_FS_ERR_IO;
    }
    d->dirp = dp;
    *out_dir = d;
    return FLOATAIR_FS_OK;
}

int floatair_fs_dir_read(floatair_dir_t* dir, char* namebuf, uint32_t buflen, bool* out_is_dir) {
    if (!dir || !namebuf || buflen == 0) {
        floatair_err("dir_read invalid arg");
        return FLOATAIR_FS_ERR_PARAM;
    }
    struct dirent* ent = readdir(dir->dirp);
    if (!ent) return 1;
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
        return floatair_fs_dir_read(dir, namebuf, buflen, out_is_dir);
    }
    bool isdir = false;
#ifdef DT_DIR
    if (ent->d_type == DT_DIR) isdir = true;
#else
    isdir = false;
#endif
    if (out_is_dir) *out_is_dir = isdir;
    if (isdir) {
        if (buflen < 2) return FLOATAIR_FS_ERR_PARAM;
        namebuf[0] = '/';
        strncpy(namebuf + 1, ent->d_name, buflen - 2);
        namebuf[buflen - 1] = '\0';
    } else {
        strncpy(namebuf, ent->d_name, buflen - 1);
        namebuf[buflen - 1] = '\0';
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_dir_close(floatair_dir_t* dir) {
    if (!dir) {
        floatair_err("dir_close invalid arg");
        return FLOATAIR_FS_ERR_PARAM;
    }
    int r = closedir(dir->dirp);
    free(dir);
    if (r != 0) {
        floatair_err("dir_close fail errno=%d(%s)", errno, strerror(errno));
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_mkdir(const char* path_in) {
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    if (mkdir(path_in, 0777) == 0) return FLOATAIR_FS_OK;
    if (errno == EEXIST) return FLOATAIR_FS_OK;
    floatair_err("mkdir fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
    return FLOATAIR_FS_ERR_IO;
}

int floatair_fs_rmdir(const char* path_in) {
    if (!path_in) return FLOATAIR_FS_ERR_PARAM;
    if (rmdir(path_in) == 0) return FLOATAIR_FS_OK;
    floatair_err("rmdir fail: %s errno=%d(%s)", path_in, errno, strerror(errno));
    return FLOATAIR_FS_ERR_IO;
}
