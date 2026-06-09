#define SDL_MAIN_HANDLED

#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "lvgl.h"
#include LV_SDL_INCLUDE_PATH
#include "../../common/app_lcd.h"
#include "floatair_dbg.h"
#include "sys_adapter.h"
#include "sim_socket.h"
#include "simulator_platform.h"
#include "simulator_event_fifo.h"
#include "lv_port_fs.h"
#include "../../system/system_config_json.h"
#include "../../system/system_timer.h"
#include "../../common/app_framework/app_stereo.h"

// Forward declarations for application entry points
void floatair_init(void);
void floatair_load(void);
void floatair_unload(void);
void* jyt_timer_create_and_start(uint32_t timeout_ms, uint32_t timer_id, int auto_destroy);

// Global display pointer
lv_display_t* g_disp = NULL;

static pthread_t tick_thread;
static pthread_t app_thread;
static int tick_thread_running = 1;
static lv_obj_t* g_status_label = NULL;
static lv_timer_t* g_status_timer = NULL;
static lv_obj_t* g_lcd_mask = NULL;
static lv_obj_t* g_business_area_frames[2] = {NULL, NULL};
static uint8_t g_lcd_visual_brightness = UINT8_MAX;
static lcd_state_t g_lcd_visual_state = LCD_ON;

static const char* g_screenshot_dir = NULL;
static uint32_t g_screenshot_period_ms = 300;
static uint32_t g_last_screenshot_ms = 0;
static uint32_t g_screenshot_seq = 0;
static int32_t g_screenshot_width = 0;
static int32_t g_screenshot_height = 0;

/**
 * @brief 将模拟器配置中的显示模式转换为 app framework 显示模式。
 * @param[in] mode 模拟器显示模式。
 * @return 返回 app framework 显示模式。
 */
static app_stereo_output_mode_t simulator_to_stereo_output_mode(sim_socket_display_mode_t mode) {
    switch (mode) {
        case SIM_SOCKET_DISPLAY_SINGLE:
            return APP_STEREO_OUTPUT_SINGLE;
        case SIM_SOCKET_DISPLAY_HORIZONTAL:
            return APP_STEREO_OUTPUT_HORIZONTAL;
        case SIM_SOCKET_DISPLAY_VERTICAL:
        default:
            return APP_STEREO_OUTPUT_VERTICAL;
    }
}

static lv_opa_t simulator_brightness_to_mask_opa(uint8_t brightness) {
    return (lv_opa_t)(UINT8_MAX - brightness);
}

void simulator_update_lcd_visual(uint8_t brightness, lcd_state_t state) {
    lv_opa_t mask_opa = LV_OPA_TRANSP;

    if (!g_lcd_mask || !lv_obj_is_valid(g_lcd_mask)) {
        return;
    }

    if (g_lcd_visual_brightness == brightness &&
        g_lcd_visual_state == state) {
        return;
    }

    if (state == LCD_OFF) {
        mask_opa = LV_OPA_COVER;
    } else {
        mask_opa = simulator_brightness_to_mask_opa(brightness);
    }

    lv_obj_set_style_bg_opa(g_lcd_mask, mask_opa, 0);
    g_lcd_visual_brightness = brightness;
    g_lcd_visual_state = state;
}

static void* tick_thread_func(void* arg) {
    (void)arg;
    while (tick_thread_running) {
        lv_tick_inc(5);
        simulator_platform_sleep_ms(5);
    }
    return NULL;
}

// Update window title with connection status
static void update_window_title(void) {
    if (!g_disp) return;

    int connected = sim_socket_get_connection_status();
    const char* server_info = sim_socket_get_server_info();

    static char title[128];
    if (connected) {
        snprintf(title, sizeof(title), "FloatairSimulator - Connected to %s", server_info);
    } else {
        snprintf(title, sizeof(title), "FloatairSimulator - Disconnected (%s)", server_info);
    }

    lv_sdl_window_set_title(g_disp, title);
}

static void simulator_screenshot_init(void) {
    const char* period = NULL;
    char* end = NULL;
    unsigned long parsed = 0;

    g_screenshot_dir = getenv("FLOATAIR_SIM_SCREENSHOT_DIR");
    if (!g_screenshot_dir || g_screenshot_dir[0] == '\0') {
        g_screenshot_dir = NULL;
        return;
    }

    period = getenv("FLOATAIR_SIM_SCREENSHOT_PERIOD_MS");
    if (period && period[0] != '\0') {
        parsed = strtoul(period, &end, 10);
        if (end != period && *end == '\0' && parsed >= 50 && parsed <= 60000) {
            g_screenshot_period_ms = (uint32_t)parsed;
        }
    }
    floatair_info("simulator screenshots enabled: %s period=%u ms",
                  g_screenshot_dir,
                  (unsigned)g_screenshot_period_ms);
}

static void simulator_screenshot_poll(void) {
    SDL_Renderer* renderer = NULL;
    uint32_t now_ms = 0;
    int width = 0;
    int height = 0;
    uint32_t* pixels = NULL;
    char path[512];
    char tmp_path[520];
    FILE* fp = NULL;
    int x = 0;
    int y = 0;

    if (!g_screenshot_dir || !g_disp) {
        return;
    }

    now_ms = lv_tick_get();
    if (g_screenshot_seq > 0 && now_ms - g_last_screenshot_ms < g_screenshot_period_ms) {
        return;
    }

    width = (int)g_screenshot_width;
    height = (int)g_screenshot_height;
    if (width <= 0 || height <= 0) {
        return;
    }

    renderer = (SDL_Renderer*)lv_sdl_window_get_renderer(g_disp);
    if (!renderer) {
        return;
    }

    pixels = (uint32_t*)malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (!pixels) {
        floatair_warn("screenshot malloc failed");
        return;
    }

    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, width * (int)sizeof(uint32_t)) != 0) {
        floatair_warn("screenshot read failed: %s", SDL_GetError());
        free(pixels);
        return;
    }

    snprintf(path, sizeof(path), "%s/frame_%05u.ppm", g_screenshot_dir, (unsigned)g_screenshot_seq);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    fp = fopen(tmp_path, "wb");
    if (!fp) {
        floatair_warn("screenshot open failed: %s", path);
        free(pixels);
        return;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint32_t argb = pixels[(size_t)y * (size_t)width + (size_t)x];
            uint8_t rgb[3] = {
                (uint8_t)((argb >> 16) & 0xFF),
                (uint8_t)((argb >> 8) & 0xFF),
                (uint8_t)(argb & 0xFF),
            };
            fwrite(rgb, 1, sizeof(rgb), fp);
        }
    }
    fclose(fp);
    free(pixels);
    if (rename(tmp_path, path) != 0) {
        floatair_warn("screenshot rename failed: %s", path);
        return;
    }

    g_last_screenshot_ms = now_ms;
    g_screenshot_seq++;
}

// Status timer callback (similar to Windows version)
static void status_timer_cb(lv_timer_t* timer) {
    (void) timer;

    int connected = sim_socket_get_connection_status();
    const char* info = sim_socket_get_server_info();

    update_window_title();

    if (!g_status_label || !lv_obj_is_valid(g_status_label)) return;

    static char buf[96];
    if (connected) {
        lv_snprintf(buf, sizeof(buf), "TCP Connected %s", info);
    } else {
        lv_snprintf(buf, sizeof(buf), "TCP Disconnected %s", info);
    }
    lv_label_set_text(g_status_label, buf);
}

/**
 * @brief 创建单眼业务可见区域调试红框。
 * @param[in] eye_index 眼区索引。
 * @param[in] eye 目标眼位。
 * @return 无返回值。
 */
static void simulator_create_business_area_frame(size_t eye_index, app_stereo_eye_t eye) {
    lv_obj_t* frame = NULL;
    int32_t eye_x = 0;
    int32_t eye_y = 0;

    if (eye_index >= (sizeof(g_business_area_frames) / sizeof(g_business_area_frames[0]))) {
        return;
    }

    app_stereo_get_eye_origin(eye, &eye_x, &eye_y);
    frame = lv_obj_create(lv_layer_top());
    if (frame == NULL) {
        return;
    }

    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, (int32_t)SYSTEM_LCD_UI_WIDTH, (int32_t)SYSTEM_LCD_UI_HEIGHT);
    lv_obj_align(frame,
                 LV_ALIGN_TOP_LEFT,
                 eye_x + (int32_t)SYSTEM_LCD_UI_X_BEGIN,
                 eye_y + (int32_t)SYSTEM_LCD_UI_Y_BEGIN);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(frame, 0, LV_PART_MAIN);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    g_business_area_frames[eye_index] = frame;
}

/**
 * @brief 创建双眼业务可见区域调试红框。
 * @return 无返回值。
 */
static void simulator_create_business_area_frames(void) {
    simulator_create_business_area_frame(0, APP_STEREO_EYE_LEFT);
    if (app_stereo_is_enabled()) {
        simulator_create_business_area_frame(1, APP_STEREO_EYE_RIGHT);
    }
}

static void display_delete_cb(lv_event_t* e) {
    lv_display_t* disp = lv_event_get_current_target(e);
    if (disp == g_disp) {
        g_disp = NULL;
        simulator_request_shutdown();
        if (g_status_timer) {
            lv_timer_delete(g_status_timer);
            g_status_timer = NULL;
        }
        g_status_label = NULL;
        g_lcd_mask = NULL;
        g_business_area_frames[0] = NULL;
        g_business_area_frames[1] = NULL;
    }
}

static void* app_thread_func(void* arg) {
    (void)arg;
    simulator_lvgl_enter_ui_critical();
    floatair_load();
    simulator_lvgl_leave_ui_critical();
    return NULL;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    simulator_platform_init_text_io();

    floatair_info("=======================================");
    floatair_info("Starting Floatair Simulator...");
    floatair_info("=======================================");

    sim_socket_config_t simulator_config;
    if (sim_socket_config_load(&simulator_config) != 0) {
        floatair_err("Failed to load simulator config");
        return -1;
    }
    if (!app_stereo_set_output_mode(simulator_to_stereo_output_mode(simulator_config.display_mode))) {
        floatair_warn("failed to apply simulator display mode, keep default");
    }
    floatair_info("Simulator config: %s:%u display=%s",
                  simulator_config.host,
                  (unsigned)simulator_config.port,
                  sim_socket_display_mode_name(simulator_config.display_mode));

    // 1. Initialize LVGL
    floatair_info("1. Initializing LVGL...");
    lv_init();
    floatair_info("LVGL initialized successfully");
    simulator_screenshot_init();

    floatair_info("2. Initializing file system...");
    lv_port_fs_init();
    floatair_info("File system initialized successfully");

    // 3. Get LCD config (compile-time constants)
    floatair_info("3. Reading LCD configuration...");
    system_lcd_t lcd_config = {
        .ui_x_begin = SYSTEM_LCD_UI_X_BEGIN,
        .ui_y_begin = SYSTEM_LCD_UI_Y_BEGIN,
        .ui_width = SYSTEM_LCD_UI_WIDTH,
        .ui_height = SYSTEM_LCD_UI_HEIGHT,
    };
    int32_t output_width = app_stereo_get_output_width();
    int32_t output_height = app_stereo_get_output_height();
    floatair_info("LCD resolution: logical=%dx%d output=%dx%d",
                  lcd_config.ui_width,
                  lcd_config.ui_height,
                  output_width,
                  output_height);
    g_screenshot_width = output_width;
    g_screenshot_height = output_height;

    // 4. Create SDL window
    floatair_info("4. Creating SDL window (%dx%d)...", output_width, output_height);
    g_disp = lv_sdl_window_create(output_width, output_height);
    if (g_disp) {
        floatair_info("SDL window created successfully: %p", g_disp);
        if (!app_stereo_install_display_mirror(g_disp)) {
            floatair_warn("app stereo display mirror install failed");
        }
        lv_sdl_window_set_title(g_disp, "FloatairSimulator");
        lv_display_add_event_cb(g_disp, display_delete_cb, LV_EVENT_DELETE, NULL);
        floatair_info("Window title set to 'FloatairSimulator'");
    } else {
        floatair_err("Failed to create SDL window. Check LVGL SDL driver configuration.");
        return -1;
    }

    // 5. Create a black background (similar to Windows/Mac)
    floatair_info("5. Creating black background...");
    lv_obj_t* bg = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, output_width, output_height);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_move_background(bg);
    floatair_info("Black background created");

    // 6. Add status label (similar to Windows)
    g_status_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(g_status_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_RIGHT, -8, 8);
    floatair_info("Status label created");

    simulator_create_business_area_frames();
    floatair_info("Business area frames created");

    g_lcd_mask = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_lcd_mask);
    lv_obj_set_size(g_lcd_mask, output_width, output_height);
    lv_obj_set_style_bg_color(g_lcd_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_lcd_mask, LV_OPA_TRANSP, 0);
    lv_obj_center(g_lcd_mask);
    lv_obj_move_foreground(g_lcd_mask);
    floatair_info("LCD mask created");

    // 7. Initialize socket connection from simulator-specific config
    floatair_info("6. Initializing socket connection from simulator config...");
    int side_role = 0; // 0 for client
    int socket_init_result = sim_socket_tx_init(simulator_config.host, simulator_config.port, side_role);
    if (socket_init_result < 0) {
        floatair_warn("Socket initialization failed, will retry in background");
    } else {
        floatair_info("Socket initialized successfully");
    }

    // 8. Start tick thread
    floatair_info("7. Starting tick thread...");
    if (pthread_create(&tick_thread, NULL, tick_thread_func, NULL) != 0) {
        floatair_err("Failed to create tick thread");
        return -1;
    }
    floatair_info("Tick thread started successfully");

    // 9. Start fifo control input
    if (!simulator_event_fifo_start()) {
        floatair_warn("simulator_event_fifo_start failed");
    }

    // 10. Initialize application
    floatair_info("8. Initializing application...");

    g_status_timer = lv_timer_create_basic();
    lv_timer_set_period(g_status_timer, 1000);
    lv_timer_set_repeat_count(g_status_timer, -1);
    lv_timer_set_cb(g_status_timer, status_timer_cb);
    status_timer_cb(g_status_timer);

    if (!system_timer_lvgl_period_start()) {
        floatair_warn("system_timer_lvgl_period_start failed");
    }

    floatair_info("9. Loading application...");
    lv_timer_handler();
    if (pthread_create(&app_thread, NULL, app_thread_func, NULL) != 0) {
        floatair_err("Failed to create application thread: %s", strerror(errno));
        simulator_event_fifo_stop();
        simulator_shutdown_runtime();
        tick_thread_running = 0;
        pthread_join(tick_thread, NULL);
        return -1;
    }
    floatair_info("Application thread started successfully");

    floatair_info("10. Application loaded, entering message loop...");

    while (!simulator_shutdown_requested()) {
        uint32_t ms = lv_timer_handler();
        simulator_screenshot_poll();
        if (ms > 20) ms = 20;
        simulator_platform_sleep_ms(ms);
    }

    simulator_event_fifo_stop();
    simulator_shutdown_runtime();

    pthread_join(app_thread, NULL);

    tick_thread_running = 0;
    pthread_join(tick_thread, NULL);

    if (g_status_timer) {
        lv_timer_delete(g_status_timer);
        g_status_timer = NULL;
    }

    g_disp = NULL;
    g_status_label = NULL;

    floatair_unload();

    lv_deinit();
    return 0;
}
