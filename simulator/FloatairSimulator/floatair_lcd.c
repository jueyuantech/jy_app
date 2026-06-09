#include "../../common/app_lcd.h"
#include "../../common/elf_common.h"
#include "../../system/system.h"
#include "sys_adapter.h"
#include "floatair_dbg.h"
static lcd_state_t current_lcd_state = LCD_ON;
static uint8_t current_lcd_brightness = UINT8_MAX;

lcd_state_t floatair_lcd_get_state(void) {
    return current_lcd_state;
}
void floatair_lcd_set_state(lcd_state_t state) {
    floatair_info("set lcd state: %d", state);
    current_lcd_state = state;
    if (state == LCD_ON) {
        floatair_lcd_set_brightness(system_config_get_brightness());
    } else {
        simulator_update_lcd_visual(current_lcd_brightness, state);
    }
}
void floatair_lcd_set_brightness(uint8_t brightness) {
    current_lcd_brightness = brightness;
    floatair_info("set lcd brightness (Linux): %u", (unsigned)brightness);
    simulator_update_lcd_visual(current_lcd_brightness, current_lcd_state);
}
uint16_t floatair_lcd_get_brightness(void) {
    floatair_info("get lcd brightness (Linux): %u", (unsigned)current_lcd_brightness);
    return current_lcd_brightness;
}
