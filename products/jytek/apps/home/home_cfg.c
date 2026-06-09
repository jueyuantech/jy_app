/**
 * @file home_cfg.c
 * @brief Home configuration handler
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_home
 */
#include "home.h"
#include "system/system_config_json.h"
#include "floatair_fs.h"

bool only_center_name    = false;

int32_t idle_img_center_h = 80;
int32_t idle_img_center_w = 80;
int32_t idle_img_left_h = 48;
int32_t idle_img_left_w = 48;
int32_t idle_img_right_h = 48;
int32_t idle_img_right_w = 48;
int32_t layout_gap = LVGL_UI_MARGIN_100;
bool home_enable_app_float = true;

const app_home_unit_t g_home_units_arr[] = {
    {APP_NAME_PROMPTER, FLOATAIR_SYS_IMG("prompterB.jpg"), FLOATAIR_SYS_IMG("prompterS.jpg"), "IDLE_PROMP"},
    {APP_NAME_TRANSLATE, FLOATAIR_SYS_IMG("translateB.jpg"), FLOATAIR_SYS_IMG("translateS.jpg"), "IDLE_TRANS"},
    {APP_NAME_TRANSCRIBE, FLOATAIR_SYS_IMG("transcribeB.jpg"), FLOATAIR_SYS_IMG("transcribeS.jpg"), "IDLE_ASR"},
    {APP_NAME_AI, FLOATAIR_SYS_IMG("assistantB.jpg"), FLOATAIR_SYS_IMG("assistantS.jpg"), "IDLE_AI"},
    // {APP_NAME_READER, FLOATAIR_SYS_IMG("readerB.jpg"), FLOATAIR_SYS_IMG("readerS.jpg"), "IDLE_BOOK"},
    {APP_NAME_NAVIGATION, FLOATAIR_SYS_IMG("navigationB.jpg"), FLOATAIR_SYS_IMG("navigationS.jpg"), "IDLE_NAVI"},
    //{APP_NAME_MUSIC, FLOATAIR_SYS_IMG("musicB.jpg"), FLOATAIR_SYS_IMG("musicS.jpg"), "IDLE_MUSIC"},
};

const size_t g_home_units_count = sizeof(g_home_units_arr) / sizeof(g_home_units_arr[0]);
