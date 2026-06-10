#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>
#include "floatair_dbg.h"
#include "app_def.h"

/**
 * @brief Get the current LCD state
 * @return lcd_state_t Current LCD state
 */
typedef enum
{
    LCD_ON,
    LCD_OFF
} lcd_state_t;

/**
 * @brief Get the current LCD state
 * @return lcd_state_t Current LCD state
 */
lcd_state_t floatair_lcd_get_state(void);

/**
 * @brief Set the LCD state
 * @param state LCD state to set
 */
void floatair_lcd_set_state(lcd_state_t state);

/**
 * @brief Set the LCD brightness
 * @param brightness Brightness value (0-255)
 */
void floatair_lcd_set_brightness(uint8_t brightness);

/**
 * @brief Get the current LCD brightness
 * @return uint16_t Current LCD brightness (0-255)
 */
uint16_t floatair_lcd_get_brightness(void);