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
#include "ui_res.h"

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
    {APP_MSG_ID_PROMPTER, APP_NAME_PROMPTER, UI_RES_IMAGE_PROMPTERB, UI_RES_IMAGE_PROMPTERS, "IDLE_PROMP"},
    {APP_MSG_ID_TRANSLATE, APP_NAME_TRANSLATE, UI_RES_IMAGE_TRANSLATEB, UI_RES_IMAGE_TRANSLATES, "IDLE_TRANS"},
    {APP_MSG_ID_TRANSCRIBE, APP_NAME_TRANSCRIBE, UI_RES_IMAGE_TRANSCRIBEB, UI_RES_IMAGE_TRANSCRIBES, "IDLE_ASR"},
    {APP_MSG_ID_AI, APP_NAME_AI, UI_RES_IMAGE_ASSISTANTB, UI_RES_IMAGE_ASSISTANTS, "IDLE_AI"},
    // {APP_NAME_READER, UI_RES_IMAGE_READERB, UI_RES_IMAGE_READERS, "IDLE_BOOK"},
    {APP_MSG_ID_NAVIGATION, APP_NAME_NAVIGATION, UI_RES_IMAGE_NAVIGATIONB, UI_RES_IMAGE_NAVIGATIONS, "IDLE_NAVI"},
    //{APP_NAME_MUSIC, UI_RES_IMAGE_MUSICB, UI_RES_IMAGE_MUSICS, "IDLE_MUSIC"},
};

const size_t g_home_units_count = sizeof(g_home_units_arr) / sizeof(g_home_units_arr[0]);
