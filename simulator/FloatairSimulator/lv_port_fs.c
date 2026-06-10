#include "lv_port_fs.h"
#include "../../common/floatair_fs.h"
#include <stdio.h>

static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode);
static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p);
static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br);
static lv_fs_res_t fs_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw);
static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence);
static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p);

void lv_port_fs_init(void)
{
    /*----------------------------------------------------
     * Use the "A" drive letter
     *---------------------------------------------------*/
    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);

    fs_drv.letter = 'A';
    fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.write_cb = fs_write;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;
    
    lv_fs_drv_register(&fs_drv);
}

static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    // Map LVGL mode to floatair mode
    uint32_t fa_mode = 0;
    if(mode == LV_FS_MODE_WR) {
        fa_mode = FLOATAIR_FS_MODE_WR | FLOATAIR_FS_MODE_CREATE | FLOATAIR_FS_MODE_TRUNC;
    } else if(mode == LV_FS_MODE_RD) {
        fa_mode = FLOATAIR_FS_MODE_RD;
    } else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) {
        fa_mode = FLOATAIR_FS_MODE_WR | FLOATAIR_FS_MODE_RD | FLOATAIR_FS_MODE_CREATE;
    }
    
    // floatair_fs_open expects full path starting with /jyt_d
    // If path passed here is "jyt_d/...", we might need to prepend "/"?
    // LVGL strips drive letter. If path was "A:/jyt_d/...", path here is "/jyt_d/...".
    // So it should be fine.
    
    return floatair_fs_open(path, fa_mode);
}

static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p)
{
    int res = floatair_fs_close(file_p);
    return (res == FLOATAIR_FS_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    int res = floatair_fs_read(file_p, buf, btr, br);
    return (res == FLOATAIR_FS_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw)
{
    int res = floatair_fs_write(file_p, buf, btw, bw);
    return (res == FLOATAIR_FS_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    int fa_whence = SEEK_SET;
    if(whence == LV_FS_SEEK_SET) fa_whence = SEEK_SET;
    else if(whence == LV_FS_SEEK_CUR) fa_whence = SEEK_CUR;
    else if(whence == LV_FS_SEEK_END) fa_whence = SEEK_END;
    
    int res = floatair_fs_seek(file_p, (int32_t)pos, fa_whence);
    return (res == FLOATAIR_FS_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    int res = floatair_fs_tell(file_p, pos_p);
    return (res == FLOATAIR_FS_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}
