#include "floatair_dbg.h"
#include "app_lcd.h"
#include "elf_common.h"
#include <nuttx/lcd/lcd_dev.h>
#include <lvgl.h>
#include "lvgl/src/core/lv_refr_private.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

static lcd_state_t current_lcd_state = LCD_ON;

/**
 * @brief 亮屏恢复后强制标记完整显示区域，确保双眼画面都重新刷新。
 * @return 无返回值。
 */
static void floatair_lcd_invalidate_full_display(void)
{
    lv_display_t *disp = lv_display_get_default();
    if (disp == NULL) {
        return;
    }

    int32_t hor_res = lv_display_get_horizontal_resolution(disp);
    int32_t ver_res = lv_display_get_vertical_resolution(disp);
    if (hor_res <= 0 || ver_res <= 0) {
        return;
    }

    lv_area_t area;
    lv_area_set(&area, 0, 0, hor_res - 1, ver_res - 1);
    lv_inv_area(disp, &area);
    floatair_info("lcd wake invalidate full display %ldx%ld", (long)hor_res, (long)ver_res);
}

lcd_state_t floatair_lcd_get_state(void)
{
    floatair_info("current_lcd_state: %d", current_lcd_state);
    return current_lcd_state;
}

/**
 * @brief 判断 LCD 是否处于灭屏状态。
 * @return true 表示 LCD 已灭屏，false 表示 LCD 处于亮屏状态。
 */
bool floatair_lcd_is_off(void)
{
    return current_lcd_state == LCD_OFF;
}

void floatair_lcd_set_state(lcd_state_t state)
{
    floatair_info("lcd state: %d -> %d", current_lcd_state, state);
    if (current_lcd_state == state) {
        return;
    }
    current_lcd_state = state;
    if (state == LCD_ON) {
        system_request_os_sleep(false);
        floatair_lcd_set_brightness(system_config_get_brightness());
        system_update_time();
        floatair_lcd_invalidate_full_display();
        system_ui_flush_pending_after_screen_on();
    } else {
        floatair_lcd_set_brightness(0);
        system_request_os_sleep(true);
    }
}

void floatair_lcd_set_brightness(uint8_t brightness)
{
    int ret=0;
    floatair_info("set lcd brightness: %d", brightness);
    int fd = *(int *)lv_display_get_driver_data(lv_display_get_default());
    ret = ioctl(fd, LCDDEVIO_SETPOWER, (long)brightness);
    if (ret < 0)
    {
        floatair_err("Error: ioctl(LCDDEVIO_SETPOWER) failed");
        return;
    }
    return;
}

uint16_t floatair_lcd_get_brightness(void)
{
    uint8_t brightness = system_config_get_brightness();
    floatair_info("current_lcd_brightness: %u", (unsigned)brightness);
    return brightness;
}
