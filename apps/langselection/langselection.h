#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_page_t app_page_t;

#include <lvgl/lvgl.h>

#include "common/app_framework/app_manager.h"

bool langselection_app_register(void);
const app_page_t* langselection_page_get(void);

#ifdef __cplusplus
}
#endif
