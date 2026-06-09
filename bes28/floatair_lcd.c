#include "floatair_dbg.h"
#include "app_lcd.h"
#include "elf_common.h"
#include <nuttx/lcd/lcd_dev.h>
#include <lvgl.h>
#include "system/system.h"

static lcd_state_t current_lcd_state = LCD_ON;

lcd_state_t floatair_lcd_get_state(void)
{
    floatair_info("current_lcd_state: %d", current_lcd_state);
    return current_lcd_state;
}

void floatair_lcd_set_state(lcd_state_t state)
{
    floatair_info("lcd state: %d -> %d", current_lcd_state, state);
    if (current_lcd_state == state) {
        return;
    }
    if (state == LCD_ON) {
        system_request_os_sleep(false);
        floatair_lcd_set_brightness(system_config_get_brightness());
        system_update_time();
    } else {
        floatair_lcd_set_brightness(0);
        system_request_os_sleep(true);
    }
    current_lcd_state = state;
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
    // if (brightness > 0) {
        lv_obj_t* screen = lv_screen_active();
        if (screen != NULL && lv_obj_is_valid(screen)) {
            lv_obj_invalidate(screen);
            lv_refr_now(NULL);
        }
    // }
    return;
}

uint16_t floatair_lcd_get_brightness(void)
{
    uint8_t brightness = system_config_get_brightness();
    floatair_info("current_lcd_brightness: %u", (unsigned)brightness);
    return brightness;
}
