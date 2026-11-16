/** @brief 模拟器文件系统适配，提供资源路径及文件访问。 */
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
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "../../common/floatair_fs.h"
#include "../../common/app_def.h"
#include "floatair_dbg.h"
#include "simulator_platform.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    FILE* fp;                 ///< 打开的宿主文件。
    void *mapped;             ///< 宿主系统持有的只读文件映射。
    uint32_t mapped_size;     ///< 映射长度，用于关闭时解除映射。
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
#define LVGL_SYSTEM_IMAGES_PATH "/jyt_c/system/images/" ///< JYTC 内置图片目录。
#define LVGL_SYSTEM_FONT_FILE   "/romfs/system/font/font.ttf" ///< 系统常规文字字体文件。
#define LVGL_SYSTEM_FONT_BITMAP_FILE "/romfs/system/font/font.bin" ///< 系统点阵字库文件 (font_type=bitmap)。
#define LVGL_SYSTEM_FONT_BITMAP_DIR "/romfs/system/font/" ///< 点阵字库目录，多字号字库存放于此。
#define LVGL_SYSTEM_FONT_MD5_FILE "/romfs/system/font/font.md5" ///< 系统常规文字字体的 MD5 摘要文件。
#define LVGL_SYSTEM_I18N_PATH   "/jyt_c/system/i18n" ///< JYTC 多语言资源目录。
#define SYSTEM_CONFIG_FILE "/jyt_d/system/config.json"
#define SIMULATOR_LFSD_CAPACITY_BYTES (6U * 1024U * 1024U) ///< 模拟器中 LFSD 分区的模拟总容量。

static void to_host_path(const char* in, char* out, size_t outsz)
{
    if (!in || !out || outsz == 0) return;
    memset(out, 0, outsz);

    char exe_path[SYSTEM_MAX_PATH_LEN];
    simulator_platform_get_executable_dir(exe_path, sizeof(exe_path));

    if (strncmp(in, "/jyt_d", 6) == 0 || strncmp(in, "/jyt_c", 6) == 0 || strncmp(in, "/romfs", 6) == 0) {
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
        if (strncmp(in, "/jyt_d", 6) == 0 || strncmp(in, "/jyt_c", 6) == 0 || strncmp(in, "/romfs", 6) == 0) {
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
#if PRODUCT_FONT_TYPE_BITMAP
    return LVGL_SYSTEM_FONT_BITMAP_FILE;
#else
    return LVGL_SYSTEM_FONT_FILE;
#endif
}

const char *floatair_fs_get_system_font_bitmap_file(void)
{
    return LVGL_SYSTEM_FONT_BITMAP_FILE;
}

bool floatair_fs_get_system_font_bitmap_file_for_size(int32_t size, char *buf, size_t bufsz)
{
    if (buf == NULL || bufsz == 0 || size <= 0) {
        return false;
    }
    int n = snprintf(buf, bufsz, "%sfont_%" PRId32 ".bin", LVGL_SYSTEM_FONT_BITMAP_DIR, size);
    return n > 0 && (size_t)n < bufsz;
}

/**
 * @brief 获取系统字库 MD5 摘要文件路径。
 * @return 返回模拟器 ROMFS 中的字库摘要路径。
 */
const char *floatair_fs_get_system_font_md5_file(void)
{
    return LVGL_SYSTEM_FONT_MD5_FILE;
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
    h->mapped = NULL;
    h->mapped_size = 0;
    return h;
}

/**
 * @brief 使用宿主只读文件映射模拟 ROMFS 零拷贝，关闭文件时解除映射。
 * @param[in] handle 只读文件句柄。
 * @param[out] data 文件映射起始地址。
 * @param[out] size 文件字节数。
 * @return 成功返回 FLOATAIR_FS_OK，映射失败返回错误码。
 */
int floatair_fs_map_readonly(void *handle, const void **data, uint32_t *size) {
    if (!data || !size) return FLOATAIR_FS_ERR_PARAM;
    *data = NULL;
    *size = 0;
    if (!handle) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t *h = (fa_file_t *)handle;
    if (!h->mapped) {
#ifdef _WIN32
        HANDLE file = (HANDLE)_get_osfhandle(_fileno(h->fp));
        LARGE_INTEGER length;
        if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &length) ||
            length.QuadPart <= 0 || (uint64_t)length.QuadPart > UINT32_MAX) {
            return FLOATAIR_FS_ERR_IO;
        }
        HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!mapping) return FLOATAIR_FS_ERR_IO;
        void *mapped = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);
        if (!mapped) return FLOATAIR_FS_ERR_IO;
        h->mapped_size = (uint32_t)length.QuadPart;
#else
        struct stat st;
        if (fstat(fileno(h->fp), &st) < 0 || !S_ISREG(st.st_mode) ||
            st.st_size <= 0 || (uint64_t)st.st_size > UINT32_MAX) {
            return FLOATAIR_FS_ERR_IO;
        }
        void *mapped = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fileno(h->fp), 0);
        if (mapped == MAP_FAILED) return FLOATAIR_FS_ERR_IO;
        h->mapped_size = (uint32_t)st.st_size;
#endif
        h->mapped = mapped;
    }
    *data = h->mapped;
    *size = h->mapped_size;
    return FLOATAIR_FS_OK;
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

/**
 * @brief 刷新模拟器文件流缓冲区。
 * @param[in] handle 已打开的文件句柄。
 * @return `FLOATAIR_FS_OK` 表示刷新完成，其他值表示刷新失败。
 */
int floatair_fs_sync(void* handle) {
    if (!handle) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    if (fflush(h->fp) != 0) {
        return FLOATAIR_FS_ERR_IO;
    }
    return FLOATAIR_FS_OK;
}

int floatair_fs_close(void* handle) {
    if (!handle) return FLOATAIR_FS_ERR_PARAM;
    fa_file_t* h = (fa_file_t*)handle;
    int map_result = 0;
    if (h->mapped) {
#ifdef _WIN32
        if (!UnmapViewOfFile(h->mapped)) map_result = -1;
#else
        map_result = munmap(h->mapped, h->mapped_size);
#endif
    }
    int r = fclose(h->fp);
    free(h);
    if (r != 0 || map_result != 0) {
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
            /* Windows 盘符根目录已经存在，不能作为普通目录重复创建。 */
            if (i > 0 && !(i == 2 && path[1] == ':')) {
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

/**
 * @brief 递归统计模拟器目录中的文件逻辑大小。
 * @param[in] path 待统计目录的虚拟路径。
 * @param[out] size 已累计的文件大小。
 * @return `FLOATAIR_FS_OK` 表示成功，其他值表示目录读取失败。
 */
static int fs_accumulate_file_size(const char* path, uint64_t* size) {
    floatair_dir_t* dir = NULL;
    char name[SYSTEM_MAX_PATH_LEN] = {0};
    bool is_dir = false;
    int result = floatair_fs_dir_open(path, &dir);

    if (result != FLOATAIR_FS_OK) {
        return result;
    }
    while ((result = floatair_fs_dir_read(dir, name, sizeof(name), &is_dir)) == FLOATAIR_FS_OK) {
        const char* child_name = name[0] == '/' ? name + 1 : name;
        char child_path[SYSTEM_MAX_PATH_LEN] = {0};
        floatair_stat_t st = {0};
        int path_length = snprintf(child_path, sizeof(child_path), "%s/%s", path, child_name);

        if (path_length < 0 || (size_t)path_length >= sizeof(child_path) ||
            floatair_fs_stat(child_path, &st) != FLOATAIR_FS_OK) {
            result = FLOATAIR_FS_ERR_IO;
            break;
        }
        if (st.is_dir) {
            result = fs_accumulate_file_size(child_path, size);
            if (result != FLOATAIR_FS_OK) {
                break;
            }
        } else {
            *size += st.size;
        }
    }
    if (result > FLOATAIR_FS_OK) {
        result = FLOATAIR_FS_OK;
    }
    if (floatair_fs_dir_close(dir) != FLOATAIR_FS_OK && result == FLOATAIR_FS_OK) {
        result = FLOATAIR_FS_ERR_IO;
    }
    return result;
}

int floatair_fs_get_usage(const char* path, floatair_fs_usage_t* usage) {
    uint64_t used = 0;
    int result = FLOATAIR_FS_OK;

    if (path == NULL || usage == NULL) {
        return FLOATAIR_FS_ERR_PARAM;
    }
    result = fs_accumulate_file_size(path, &used);
    if (result != FLOATAIR_FS_OK || used > SIMULATOR_LFSD_CAPACITY_BYTES) {
        return result != FLOATAIR_FS_OK ? result : FLOATAIR_FS_ERR_IO;
    }
    usage->total = SIMULATOR_LFSD_CAPACITY_BYTES;
    usage->used = (uint32_t)used;
    usage->remaining = usage->total - usage->used;
    return FLOATAIR_FS_OK;
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

    return 1;
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
