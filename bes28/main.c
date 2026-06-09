
#include <fcntl.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "floatair_dbg.h"
#include "app_def.h"
#include "common/app_framework/app_stereo.h"
#include "system/system_timer.h"

#include <lvgl/lvgl.h>

int main(int argc, char* argv[]) {
    (void) argc;
    (void) argv;
    lv_init();

    lv_nuttx_dsc_t info;
    lv_nuttx_result_t result;
    lv_nuttx_dsc_init(&info);
    info.fb_path = "/dev/lcd0";
    lv_nuttx_init(&info, &result);
    if (!app_stereo_install_display_mirror(result.disp)) {
        floatair_warn("app stereo display mirror install failed");
    }

    // ---------- create timer for lvgl task -----------
    system_timer_init();
    bool ok = system_timer_lvgl_period_start();
    floatair_assert(ok, "system_timer_lvgl_period_start failed");
    
    floatair_load();

    lv_nuttx_deinit(&result);
    
    floatair_unload();
    system_timer_deinit();
    lv_deinit();
    return 0;
}
