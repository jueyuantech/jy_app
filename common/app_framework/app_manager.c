/**
 * @file app_manager.c
 * @brief 轻量 App 与页面栈框架实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-21
 * @copyright JYTek
 * @ingroup common_app_framework
 */
#include "common/app_framework/app_manager.h"

#include "common/app_framework/app_layers.h"

#include "floatair_dbg.h"

#include <stdlib.h>
#include <string.h>

static app_t* g_apps[APP_MANAGER_MAX_APPS] = {0};             ///< 已注册 App 表
static int g_app_count = 0;                                   ///< 已注册 App 数量
static app_t* g_current_app = NULL;                           ///< 当前 App
static app_manager_config_t g_cfg = {0};                      ///< 框架配置
static bool g_initialized = false;                            ///< 初始化状态
static bool g_busy = false;                                   ///< 切换忙碌状态
static bool g_allow_nested_page_op = false;                   ///< App 启动阶段允许页面操作

/**
 * @brief 判断字符串是否为空。
 * @param[in] str 目标字符串。
 * @return `true` 表示为空，`false` 表示非空。
 */
static bool app_manager_is_empty_str(const char* str) {
    return str == NULL || str[0] == '\0';
}

/**
 * @brief 查找 App。
 * @param[in] name App 名称。
 * @return 找到返回 App 指针；未找到返回 `NULL`。
 */
static app_t* app_manager_find(const char* name) {
    if (app_manager_is_empty_str(name)) {
        return NULL;
    }

    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i] != NULL && g_apps[i]->name != NULL && strcmp(g_apps[i]->name, name) == 0) {
            return g_apps[i];
        }
    }
    return NULL;
}

/**
 * @brief 获取 App 栈顶页面项。
 * @param[in] app App 描述符。
 * @return 返回页面项；无页面时返回 `NULL`。
 */
static app_page_entry_t* app_manager_top_entry(app_t* app) {
    if (app == NULL || app->stack_top < 0 || app->stack_top >= APP_PAGE_STACK_MAX) {
        return NULL;
    }
    return &app->stack[app->stack_top];
}

/**
 * @brief 释放页面入参缓存。
 * @param[in,out] entry 页面栈项。
 * @return 无返回值。
 */
static void app_manager_free_entry_data(app_page_entry_t* entry) {
    if (entry == NULL) {
        return;
    }

    if (entry->data.ptr != NULL) {
        free(entry->data.ptr);
    }
    entry->data.ptr = NULL;
    entry->data.size = 0;
}

/**
 * @brief 复制页面入参缓存。
 * @param[in,out] entry 页面栈项。
 * @param[in] data 页面入参。
 * @param[in] size 页面入参长度。
 * @return `true` 表示复制成功，`false` 表示复制失败。
 */
static bool app_manager_copy_entry_data(app_page_entry_t* entry, const void* data, size_t size) {
    if (entry == NULL) {
        return false;
    }

    entry->data.ptr = NULL;
    entry->data.size = 0;
    if (data == NULL || size == 0) {
        return true;
    }

    entry->data.ptr = malloc(size);
    if (entry->data.ptr == NULL) {
        return false;
    }

    memcpy(entry->data.ptr, data, size);
    entry->data.size = size;
    return true;
}

/**
 * @brief 判断 App 是否有可见栈顶页面。
 * @param[in] app App 描述符。
 * @return `true` 表示栈顶页面已创建视图，`false` 表示没有可见视图。
 */
static bool app_manager_has_visible_top(app_t* app) {
    app_page_entry_t* entry = app_manager_top_entry(app);

    return entry != NULL && entry->page != NULL && entry->view_created;
}

/**
 * @brief 创建页面栈顶视图。
 * @param[in,out] app App 描述符。
 * @return `true` 表示创建成功，`false` 表示创建失败。
 */
static bool app_manager_create_top_view(app_t* app) {
    app_page_entry_t* entry = app_manager_top_entry(app);
    lv_obj_t* content = NULL;
    lv_obj_t* host_parent = NULL;
    uint32_t ts_begin = lv_tick_get();

    if (entry == NULL || entry->page == NULL) {
        return false;
    }
    if (entry->view_created) {
        return true;
    }

    floatair_dbg("ts=%u view create begin app=%s page=%s",
                 (unsigned)ts_begin,
                 app != NULL ? app->name : "N/A",
                 entry->page->name != NULL ? entry->page->name : "N/A");
    host_parent = (app != NULL && app->use_top_layer) ? app_layers_get_top() : g_cfg.app_layer;
    if (!app_page_host_create(host_parent, &g_cfg.page_host, &entry->view)) {
        return false;
    }

    content = app_page_host_get_content(&entry->view);
    if (content == NULL) {
        app_page_host_destroy(&entry->view);
        return false;
    }

    if (entry->page->on_create != NULL) {
        uint32_t ts_cb0 = lv_tick_get();
        entry->page->on_create(content, &entry->data);
        uint32_t ts_cb1 = lv_tick_get();
        floatair_dbg("ts=%u view on_create done app=%s page=%s cost=%u",
                     (unsigned)ts_cb1,
                     app != NULL ? app->name : "N/A",
                     entry->page->name != NULL ? entry->page->name : "N/A",
                     (unsigned)(ts_cb1 - ts_cb0));
    }

    /* 对齐旧 page_manager 语义：页面 on_create 可能清掉 root 样式，回调后要补回承载层尺寸。 */
    app_page_host_resize(&entry->view, g_cfg.page_host.width, g_cfg.page_host.height);
    entry->view_created = true;

    if (entry->page->on_appear != NULL) {
        uint32_t ts_cb0 = lv_tick_get();
        entry->page->on_appear(content);
        uint32_t ts_cb1 = lv_tick_get();
        floatair_dbg("ts=%u view on_appear done app=%s page=%s cost=%u",
                     (unsigned)ts_cb1,
                     app != NULL ? app->name : "N/A",
                     entry->page->name != NULL ? entry->page->name : "N/A",
                     (unsigned)(ts_cb1 - ts_cb0));
    }
    /* on_appear 也可能切换状态栏模式，显示前再同步一次最终尺寸。 */
    app_page_host_resize(&entry->view, g_cfg.page_host.width, g_cfg.page_host.height);
    floatair_dbg("ts=%u view create end app=%s page=%s cost=%u",
                 (unsigned)lv_tick_get(),
                 app != NULL ? app->name : "N/A",
                 entry->page->name != NULL ? entry->page->name : "N/A",
                 (unsigned)(lv_tick_get() - ts_begin));
    return true;
}

/**
 * @brief 销毁页面栈顶视图。
 * @param[in,out] app App 描述符。
 * @param[in] call_disappear 是否触发 disappear 回调。
 * @return 无返回值。
 */
static void app_manager_destroy_top_view(app_t* app, bool call_disappear) {
    app_page_entry_t* entry = app_manager_top_entry(app);
    uint32_t ts_begin = lv_tick_get();

    if (entry == NULL || entry->page == NULL || !entry->view_created) {
        return;
    }

    floatair_dbg("ts=%u view destroy begin app=%s page=%s call_disappear=%d",
                 (unsigned)ts_begin,
                 app != NULL ? app->name : "N/A",
                 entry->page->name != NULL ? entry->page->name : "N/A",
                 (int)call_disappear);
    if (call_disappear && entry->page->on_disappear != NULL) {
        uint32_t ts_cb0 = lv_tick_get();
        entry->page->on_disappear();
        uint32_t ts_cb1 = lv_tick_get();
        floatair_dbg("ts=%u view on_disappear done app=%s page=%s cost=%u",
                     (unsigned)ts_cb1,
                     app != NULL ? app->name : "N/A",
                     entry->page->name != NULL ? entry->page->name : "N/A",
                     (unsigned)(ts_cb1 - ts_cb0));
    }
    if (entry->page->on_destroy != NULL) {
        uint32_t ts_cb0 = lv_tick_get();
        entry->page->on_destroy();
        uint32_t ts_cb1 = lv_tick_get();
        floatair_dbg("ts=%u view on_destroy done app=%s page=%s cost=%u",
                     (unsigned)ts_cb1,
                     app != NULL ? app->name : "N/A",
                     entry->page->name != NULL ? entry->page->name : "N/A",
                     (unsigned)(ts_cb1 - ts_cb0));
    }
    app_page_host_destroy(&entry->view);
    entry->view_created = false;
    floatair_dbg("ts=%u view destroy end app=%s page=%s cost=%u",
                 (unsigned)lv_tick_get(),
                 app != NULL ? app->name : "N/A",
                 entry->page->name != NULL ? entry->page->name : "N/A",
                 (unsigned)(lv_tick_get() - ts_begin));
}

/**
 * @brief 清空 App 全部页面栈。
 * @param[in,out] app App 描述符。
 * @return 无返回值。
 */
static void app_manager_clear_stack(app_t* app) {
    if (app == NULL) {
        return;
    }

    while (app->stack_top >= 0) {
        app_page_entry_t* entry = &app->stack[app->stack_top];
        if (entry->view_created) {
            app_manager_destroy_top_view(app, true);
        }
        if (entry->page != NULL && entry->page->on_unload != NULL) {
            entry->page->on_unload();
        }
        app_manager_free_entry_data(entry);
        *entry = (app_page_entry_t){0};
        app->stack_top--;
    }
}

/**
 * @brief 因不可恢复的页面视图创建失败停止 App。
 * @param[in,out] app App 描述符。
 * @return 无返回值。
 */
static void app_manager_stop_after_view_failure(app_t* app) {
    if (app == NULL) {
        return;
    }

    if (app->on_stop != NULL) {
        app->on_stop();
    }
    app_manager_clear_stack(app);
    if (g_current_app == app) {
        g_current_app = NULL;
    }
}

/**
 * @brief 进入公共操作忙碌段。
 * @return `true` 表示允许继续，`false` 表示当前忙碌。
 */
static bool app_manager_begin_op(void) {
    if (g_busy && !g_allow_nested_page_op) {
        return false;
    }
    if (!g_busy) {
        g_busy = true;
    }
    return true;
}

/**
 * @brief 退出公共操作忙碌段。
 * @param[in] was_nested 本次操作是否发生在嵌套页面操作窗口。
 * @return 无返回值。
 */
static void app_manager_end_op(bool was_nested) {
    if (!was_nested) {
        g_busy = false;
    }
}

bool app_manager_init(const app_manager_config_t* cfg) {
    if (cfg == NULL || cfg->page_host.width <= 0 || cfg->page_host.height <= 0) {
        return false;
    }

    g_cfg = *cfg;
    if (g_cfg.app_layer == NULL) {
        g_cfg.app_layer = app_layers_get_app();
    }
    if (g_cfg.app_layer == NULL) {
        return false;
    }

    g_initialized = true;
    return true;
}

bool app_manager_deinit(void) {
    if (!g_initialized) {
        return true;
    }
    if (g_busy) {
        floatair_warn("app manager deinit skipped, manager busy");
        return false;
    }

    g_busy = true;
    for (int i = 0; i < g_app_count; i++) {
        app_t* app = g_apps[i];

        if (app == NULL) {
            continue;
        }
        if (app->stack_top >= 0 || app == g_current_app) {
            if (app->on_stop != NULL) {
                app->on_stop();
            }
            if (app == g_current_app) {
                app_manager_destroy_top_view(app, true);
            }
            app_manager_clear_stack(app);
        }
        app->stack_top = -1;
    }

    memset(g_apps, 0, sizeof(g_apps));
    g_app_count = 0;
    g_current_app = NULL;
    g_cfg = (app_manager_config_t){0};
    g_initialized = false;
    g_allow_nested_page_op = false;
    g_busy = false;
    return true;
}

bool app_manager_register(app_t* app) {
    if (!g_initialized || app == NULL || app_manager_is_empty_str(app->name)) {
        return false;
    }
    if (g_app_count >= APP_MANAGER_MAX_APPS) {
        return false;
    }
    if (app_manager_find(app->name) != NULL) {
        return false;
    }

    app->stack_top = -1;
    g_apps[g_app_count++] = app;
    return true;
}

bool app_manager_has_app(const char* name) {
    return app_manager_find(name) != NULL;
}

bool app_manager_switch(const char* name) {
    app_t* next = NULL;
    app_t* prev = NULL;
    bool next_ready = false;
    uint32_t ts_begin = lv_tick_get();

    if (!g_initialized || g_busy) {
        return false;
    }

    next = app_manager_find(name);
    if (next == NULL) {
        return false;
    }
    if (next == g_current_app) {
        return true;
    }

    floatair_dbg("ts=%u app switch begin from=%s to=%s",
                 (unsigned)ts_begin,
                 g_current_app != NULL ? g_current_app->name : "N/A",
                 next != NULL ? next->name : "N/A");
    g_busy = true;
    prev = g_current_app;
    if (prev != NULL) {
        app_manager_destroy_top_view(prev, true);
        if (prev->on_pause != NULL) {
            prev->on_pause();
        }
    }

    g_current_app = next;
    g_allow_nested_page_op = true;
    if (g_current_app->stack_top < 0) {
        if (g_current_app->on_start != NULL) {
            g_current_app->on_start();
        }
        next_ready = app_manager_has_visible_top(g_current_app);
    } else {
        if (g_current_app->on_resume != NULL) {
            g_current_app->on_resume();
        }
        next_ready = app_manager_create_top_view(g_current_app);
    }
    g_allow_nested_page_op = false;

    if (!next_ready) {
        if (g_current_app == next && next->stack_top < 0 && next->on_stop != NULL) {
            next->on_stop();
        } else if (g_current_app == next && next->stack_top >= 0 && next->on_pause != NULL) {
            next->on_pause();
        }
        g_current_app = prev;
        if (prev != NULL) {
            if (prev->on_resume != NULL) {
                prev->on_resume();
            }
            if (!app_manager_create_top_view(prev)) {
                app_manager_stop_after_view_failure(prev);
            }
        }
        g_busy = false;
        return false;
    }

    g_busy = false;
    floatair_dbg("ts=%u app switch end to=%s ok=1 cost=%u",
                 (unsigned)lv_tick_get(),
                 g_current_app != NULL ? g_current_app->name : "N/A",
                 (unsigned)(lv_tick_get() - ts_begin));
    return true;
}

bool app_manager_refresh_current(void) {
    if (!g_initialized || g_busy || g_current_app == NULL) {
        return false;
    }
    if (g_current_app->stack_top < 0) {
        return false;
    }

    g_busy = true;
    app_manager_destroy_top_view(g_current_app, true);
    if (!app_manager_create_top_view(g_current_app)) {
        app_manager_stop_after_view_failure(g_current_app);
        g_busy = false;
        return false;
    }
    g_busy = false;
    return true;
}

bool app_manager_stop(const char* name) {
    app_t* app = NULL;

    if (!g_initialized || g_busy) {
        return false;
    }

    app = app_manager_find(name);
    if (app == NULL) {
        return false;
    }
    if (app != g_current_app && app->stack_top < 0) {
        return true;
    }

    g_busy = true;
    /* 保持旧 page_manager 语义：App 清理先于页面卸载，避免清理函数访问已销毁的 LVGL 对象。 */
    if (app->on_stop != NULL) {
        app->on_stop();
    }
    if (app == g_current_app) {
        app_manager_destroy_top_view(app, true);
    }
    app_manager_clear_stack(app);
    if (app == g_current_app) {
        g_current_app = NULL;
    }
    g_busy = false;
    return true;
}

app_t* app_manager_current(void) {
    return g_current_app;
}

const char* app_manager_current_name(void) {
    return (g_current_app != NULL) ? g_current_app->name : NULL;
}

lv_obj_t* app_manager_current_content_root(void) {
    app_page_entry_t* entry = app_manager_top_entry(g_current_app);

    if (entry == NULL || !entry->view_created) {
        return NULL;
    }

    return app_page_host_get_content(&entry->view);
}

void app_manager_sync_current_view_layout(int32_t width, int32_t height) {
    app_page_entry_t* entry = app_manager_top_entry(g_current_app);

    if (!g_initialized || width <= 0 || height <= 0) {
        return;
    }

    g_cfg.page_host.width = width;
    g_cfg.page_host.height = height;
    if (entry == NULL) {
        return;
    }

    /* on_create 期间页面可能切换状态栏模式，此时 host 已存在但 view_created 仍为 false。 */
    app_page_host_resize(&entry->view, width, height);
}

bool app_manager_is_busy(void) {
    return g_busy;
}

bool app_page_push(app_page_t* page, const void* data, size_t size) {
    app_page_entry_t* entry = NULL;
    bool nested = g_busy;
    uint32_t ts_begin = lv_tick_get();

    if (!g_initialized || g_current_app == NULL || page == NULL || g_current_app->stack_top + 1 >= APP_PAGE_STACK_MAX) {
        return false;
    }
    if (!app_manager_begin_op()) {
        return false;
    }

    app_manager_destroy_top_view(g_current_app, true);

    g_current_app->stack_top++;
    entry = &g_current_app->stack[g_current_app->stack_top];
    *entry = (app_page_entry_t){0};
    entry->page = page;
    if (!app_manager_copy_entry_data(entry, data, size)) {
        *entry = (app_page_entry_t){0};
        g_current_app->stack_top--;
        if (!app_manager_create_top_view(g_current_app)) {
            app_manager_stop_after_view_failure(g_current_app);
        }
        app_manager_end_op(nested);
        return false;
    }

    if (!app_manager_create_top_view(g_current_app)) {
        app_manager_free_entry_data(entry);
        *entry = (app_page_entry_t){0};
        g_current_app->stack_top--;
        if (!app_manager_create_top_view(g_current_app)) {
            app_manager_stop_after_view_failure(g_current_app);
        }
        app_manager_end_op(nested);
        return false;
    }

    app_manager_end_op(nested);
    floatair_dbg("ts=%u page push end app=%s page=%s cost=%u",
                 (unsigned)lv_tick_get(),
                 g_current_app != NULL ? g_current_app->name : "N/A",
                 page->name != NULL ? page->name : "N/A",
                 (unsigned)(lv_tick_get() - ts_begin));
    return true;
}

bool app_page_pop(void) {
    app_page_entry_t* entry = NULL;
    bool nested = g_busy;
    uint32_t ts_begin = lv_tick_get();

    if (!g_initialized || g_current_app == NULL || g_current_app->stack_top <= 0) {
        return false;
    }
    if (!app_manager_begin_op()) {
        return false;
    }

    entry = app_manager_top_entry(g_current_app);
    app_manager_destroy_top_view(g_current_app, true);
    if (entry != NULL && entry->page != NULL && entry->page->on_unload != NULL) {
        entry->page->on_unload();
    }
    if (entry != NULL) {
        app_manager_free_entry_data(entry);
        *entry = (app_page_entry_t){0};
    }
    g_current_app->stack_top--;

    if (!app_manager_create_top_view(g_current_app)) {
        app_manager_stop_after_view_failure(g_current_app);
        app_manager_end_op(nested);
        return false;
    }

    app_manager_end_op(nested);
    floatair_dbg("ts=%u page pop end app=%s cost=%u",
                 (unsigned)lv_tick_get(),
                 g_current_app != NULL ? g_current_app->name : "N/A",
                 (unsigned)(lv_tick_get() - ts_begin));
    return true;
}

bool app_page_replace(app_page_t* page, const void* data, size_t size) {
    app_page_entry_t* entry = NULL;
    app_page_entry_t new_entry = {0};
    int old_top = -1;
    bool nested = g_busy;
    uint32_t ts_begin = lv_tick_get();

    if (!g_initialized || g_current_app == NULL || page == NULL) {
        return false;
    }
    if (!app_manager_begin_op()) {
        return false;
    }

    new_entry.page = page;
    if (!app_manager_copy_entry_data(&new_entry, data, size)) {
        app_manager_end_op(nested);
        return false;
    }

    old_top = g_current_app->stack_top;
    if (g_current_app->stack_top >= 0) {
        entry = app_manager_top_entry(g_current_app);
        app_manager_destroy_top_view(g_current_app, true);
        if (entry != NULL && entry->page != NULL && entry->page->on_unload != NULL) {
            entry->page->on_unload();
        }
        if (entry != NULL) {
            app_manager_free_entry_data(entry);
            *entry = (app_page_entry_t){0};
        }
    } else {
        g_current_app->stack_top = 0;
        entry = app_manager_top_entry(g_current_app);
    }

    if (entry == NULL) {
        app_manager_free_entry_data(&new_entry);
        app_manager_end_op(nested);
        return false;
    }
    *entry = new_entry;

    if (!app_manager_create_top_view(g_current_app)) {
        app_manager_free_entry_data(entry);
        *entry = (app_page_entry_t){0};
        if (old_top > 0) {
            g_current_app->stack_top = old_top - 1;
            if (!app_manager_create_top_view(g_current_app)) {
                app_manager_stop_after_view_failure(g_current_app);
            }
        } else {
            g_current_app->stack_top = -1;
            app_manager_stop_after_view_failure(g_current_app);
        }
        app_manager_end_op(nested);
        return false;
    }

    app_manager_end_op(nested);
    floatair_dbg("ts=%u page replace end app=%s page=%s cost=%u",
                 (unsigned)lv_tick_get(),
                 g_current_app != NULL ? g_current_app->name : "N/A",
                 page->name != NULL ? page->name : "N/A",
                 (unsigned)(lv_tick_get() - ts_begin));
    return true;
}

bool app_page_back(void) {
    app_page_entry_t* entry = NULL;

    if (!g_initialized || g_current_app == NULL) {
        return false;
    }

    entry = app_manager_top_entry(g_current_app);
    if (entry != NULL && entry->page != NULL && entry->page->on_back != NULL && entry->page->on_back()) {
        return true;
    }

    if (g_current_app->stack_top > 0) {
        return app_page_pop();
    }

    if (g_current_app->on_back != NULL) {
        return g_current_app->on_back();
    }
    return false;
}
