/**
 * @file app_nav.c
 * @brief App 内页面导航门面实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-21
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#include "common/app_framework/app_nav.h"

bool app_nav_push(app_page_t* page, const void* data, size_t size) {
    return app_page_push(page, data, size);
}

bool app_nav_pop(void) {
    return app_page_pop();
}

bool app_nav_replace(app_page_t* page, const void* data, size_t size) {
    return app_page_replace(page, data, size);
}

bool app_nav_back(void) {
    return app_page_back();
}
