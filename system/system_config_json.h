/**
 * @file system_config_json.h
 * @brief System configuration file I/O and JSON utilities
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 */
#pragma once
/** @ingroup app_system */

#include "cJSON.h"
#include "elf_common.h"
#include "floatair_dbg.h"
#include "message.h"
#include "system/system.h"
#include "system_res.h"

#include <lvgl/lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set font configuration
 * @param[in] config_file configuration file path
 * @param[in] font font configuration
 * @return true on success
 */
bool system_config_set_font(char* config_file, app_font_info_t* font);
/**
 * @brief Get font configuration
 * @param[in] config_file configuration file path
 * @param[out] font font configuration
 * @return true on success
 */
bool system_config_get_font(const char* config_file, app_font_info_t* font);

/**
 * @brief Get system string key
 * @param[in] config_file configuration file path
 * @param[in] key key name
 * @return value (must be freed)
 */
char* system_config_get_str(const char* config_file, const char* key);

/**
 * @brief Set system string key
 * @param[in] config_file configuration file path
 * @param[in] key key name
 * @param[in] value value string
 * @return true on success
 */
bool system_config_set_str(const char* config_file, const char* key, const char* value);

/**
 * @brief Get system number key
 * @param[in] config_file configuration file path
 * @param[in] key key name
 * @return number value
 */
int system_config_get_number(const char* config_file, const char* key);

/**
 * @brief Set system number key
 * @param[in] config_file configuration file path
 * @param[in] key key name
 * @param[in] value number value
 * @return true on success
 */
bool system_config_set_number(const char* config_file, const char* key, int value);

/**
 * @brief Get system boolean key
 * @param[in] config_file configuration file path
 * @param[in] key key name
 * @return boolean value
 */
bool system_config_get_bool(const char* config_file, const char* key);

/**
 * @brief Set system boolean key
 * @param[in] config_file configuration file path
 * @param[in] key key name
 * @param[in] value boolean value
 * @return true on success
 */
bool system_config_set_bool(const char* config_file, const char* key, bool value);

/* helpers */
/**
 * @param[in] path file path
 * @return cJSON root node (must be freed)
 */
cJSON* load_json(const char* path);
/**
 * @brief Save JSON file
 * @param[in] path file path
 * @param[in] root root node
 * @return 0 success; non-zero failure
 */
int save_json(const char* path, cJSON* root);
/**
 * @brief Parse boolean key
 * @param[in] root root node
 * @param[in] key key name
 * @param[out] out output value
 */
void parse_bool_key(cJSON* root, const char* key, bool* out);
/**
 * @brief Parse uint8 key
 * @param[in] root root node
 * @param[in] key key name
 * @param[out] out output value
 */
void parse_u8_key(cJSON* root, const char* key, uint8_t* out);
/**
 * @brief Parse uint16 key
 * @param[in] root root node
 * @param[in] key key name
 * @param[out] out output value
 */
void parse_u16_key(cJSON* root, const char* key, uint16_t* out);
/**
 * @brief Parse uint32 key
 * @param[in] root root node
 * @param[in] key key name
 * @param[out] out output value
 */
void parse_u32_key(cJSON* root, const char* key, uint32_t* out);
/**
 * @brief Parse unsigned int key
 * @param[in] root root node
 * @param[in] key key name
 * @param[out] out output value
 */
void parse_uint_key(cJSON* root, const char* key, unsigned int* out);
/**
 * @brief Parse and duplicate string key
 * @param[in] root root node
 * @param[in] key key name
 * @param[out] out output string (must be freed)
 * @return true on success
 */
bool parse_string_key_dup(cJSON* root, const char* key, char** out);

#ifdef __cplusplus
}
#endif
