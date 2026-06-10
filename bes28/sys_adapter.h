#pragma once
#include <stddef.h>
#include <stdint.h>
#include "system/system.h"

#define FLOATAIR_UNUSED(x) (void)(x)

uint8_t system_get_sys_state(void);
void system_set_sys_state(uint8_t state);
uint8_t system_get_charge_state(void);
uint8_t system_get_battery(void);
bool system_get_btconn_state(void);
void system_set_btconn_state(bool connected);
uint32_t system_get_rom_total(void);
uint32_t system_get_rom_used(void);
uint32_t system_get_rom_remaining(void);
const char* system_get_manufacture(void);
const char* system_get_model(void);
const char* system_get_edition(void);
const char* system_get_sn(void);
const char* system_get_ssn(void);
const char* system_get_btaddr(void);
const char* system_get_btname(void);
const char* system_get_bleaddr(void);
const char* system_get_blename(void);
const char* system_get_fwver(void);
const char* system_get_bthver(void);
const char* system_get_protocolver(void);
