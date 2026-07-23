#include "../../common/app_lcd.h"
#include "elf_common.h"
#include "../../system/system.h"
#include "../../system/system_runtime_ui.h"
#include "sys_adapter.h"
#include "floatair_dbg.h"
static lcd_state_t current_lcd_state = LCD_ON;
static uint8_t current_lcd_brightness = UINT8_MAX;

lcd_state_t floatair_lcd_get_state(void) {
    return current_lcd_state;
}
/**
 * @brief 判断 LCD 是否处于灭屏状态。
 * @return true 表示 LCD 已灭屏，false 表示 LCD 处于亮屏状态。
 */
bool floatair_lcd_is_off(void) {
    return current_lcd_state == LCD_OFF;
}
void floatair_lcd_set_state(lcd_state_t state) {
    floatair_info("set lcd state: %d", state);
    current_lcd_state = state;
    if (state == LCD_ON) {
        floatair_lcd_set_brightness(system_runtime_state_get_lcd_resume_brightness());
        system_ui_flush_pending_after_screen_on();
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
