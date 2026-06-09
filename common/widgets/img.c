#include "img.h"

#include <string.h>

/**
 * @brief 图片组件内部数据结构。
 */
struct img_t {
    ui_widget_t base;
    lv_image_dsc_t owned_dsc;
    uint8_t* owned_buf;
};

/**
 * @brief 判断图片句柄及底层对象是否有效。
 *
 * @param img 目标图片句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static bool img_is_valid(img_t* img) {
    return img && img->base.obj && lv_obj_is_valid(img->base.obj);
}

static void img_release_owned_src(img_t* img) {
    if (!img) {
        return;
    }

    if (img->base.obj && lv_obj_is_valid(img->base.obj)) {
        lv_image_set_src(img->base.obj, NULL);
    }

    if (img->owned_buf) {
        lv_free(img->owned_buf);
        img->owned_buf = NULL;
    }

    memset(&img->owned_dsc, 0, sizeof(img->owned_dsc));
}

/**
 * @brief 图片删除事件回调。
 *
 * @param e LVGL 事件对象。
 * @return 无返回值。
 */
static void img_on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    img_t* img = (img_t*)lv_obj_get_user_data(obj);

    if (!img) {
        return;
    }

    img_release_owned_src(img);
    lv_free(img);
    lv_obj_set_user_data(obj, NULL);
}

/**
 * @brief 获取图片组件默认配置。
 *
 * @return 返回填充好默认值的图片配置结构体。
 */
img_cfg_t img_default_cfg(void) {
    img_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.x = 0;
    cfg.y = 0;
    cfg.w = LV_SIZE_CONTENT;
    cfg.h = LV_SIZE_CONTENT;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    cfg.zoom = LV_SCALE_NONE;
    cfg.rotation = 0;
    cfg.opa = LV_OPA_COVER;
    cfg.src = NULL;

    return cfg;
}

/**
 * @brief 创建图片组件。
 *
 * @param parent 父对象，为 `NULL` 时默认挂载到当前活动屏幕。
 * @param cfg 图片配置，为 `NULL` 时使用默认配置。
 * @return 成功返回图片组件句柄，失败返回 `NULL`。
 */
img_t* img_create(lv_obj_t* parent, const img_cfg_t* cfg) {
    img_cfg_t default_cfg;
    img_t* img = NULL;
    lv_obj_t* obj = NULL;

    if (!parent) {
        parent = lv_screen_active();
    }
    if (!parent) {
        return NULL;
    }

    default_cfg = img_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    obj = lv_image_create(parent);
    if (!obj) {
        return NULL;
    }

    img = (img_t*)lv_malloc(sizeof(img_t));
    if (!img) {
        lv_obj_delete(obj);
        return NULL;
    }

    memset(img, 0, sizeof(*img));
    ui_widget_init(&img->base, obj, UI_WIDGET_TYPE_IMG);

    lv_obj_set_user_data(obj, img);
    lv_obj_add_event_cb(obj, img_on_delete, LV_EVENT_DELETE, NULL);

    img_apply_cfg(img, cfg);
    return img;
}

/**
 * @brief 使用默认配置和图片源快速创建图片组件。
 *
 * @param parent 父对象，为 `NULL` 时默认挂载到当前活动屏幕。
 * @param src 图片资源指针，可为文件路径、符号或内存图片描述。
 * @return 成功返回图片组件句柄，失败返回 `NULL`。
 */
img_t* img_create_from_src(lv_obj_t* parent, const void* src) {
    img_cfg_t cfg = img_default_cfg();

    cfg.src = src;
    return img_create(parent, &cfg);
}

/**
 * @brief 设置图片资源。
 *
 * @param img 目标图片组件句柄。
 * @param src 图片资源指针，可为文件路径、符号或内存图片描述。
 * @return 无返回值。
 */
void img_set_src(img_t* img, const void* src) {
    if (!img_is_valid(img)) {
        return;
    }

    img_release_owned_src(img);
    lv_image_set_src(img->base.obj, src);
}

bool img_set_l8_data(img_t* img,
                     const void* data,
                     size_t data_size,
                     int32_t width,
                     int32_t height) {
    size_t required_size = 0;

    if (!img_is_valid(img) || !data || width <= 0 || height <= 0) {
        return false;
    }

    required_size = (size_t)width * (size_t)height;
    if (data_size < required_size || required_size > UINT32_MAX) {
        return false;
    }

    img_release_owned_src(img);

    img->owned_buf = (uint8_t*)lv_malloc(required_size);
    if (!img->owned_buf) {
        img_release_owned_src(img);
        return false;
    }

    memcpy(img->owned_buf, data, required_size);
    memset(&img->owned_dsc, 0, sizeof(img->owned_dsc));
    img->owned_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    img->owned_dsc.header.cf = LV_COLOR_FORMAT_L8;
    img->owned_dsc.header.flags = 0;
    img->owned_dsc.header.w = (uint32_t)width;
    img->owned_dsc.header.h = (uint32_t)height;
    img->owned_dsc.header.stride = (uint32_t)width;
    img->owned_dsc.data_size = (uint32_t)required_size;
    img->owned_dsc.data = img->owned_buf;

    lv_image_set_src(img->base.obj, &img->owned_dsc);
    return true;
}

/**
 * @brief 获取当前图片资源。
 *
 * @param img 目标图片组件句柄。
 * @return 返回当前绑定的图片资源指针，失败时返回 `NULL`。
 */
const void* img_get_src(img_t* img) {
    if (!img_is_valid(img)) {
        return NULL;
    }

    return lv_image_get_src(img->base.obj);
}

/**
 * @brief 设置图片显示偏移。
 *
 * @param img 目标图片组件句柄。
 * @param offset_x 水平偏移量。
 * @param offset_y 垂直偏移量。
 * @return 无返回值。
 */
void img_set_offset(img_t* img, int32_t offset_x, int32_t offset_y) {
    if (!img_is_valid(img)) {
        return;
    }

    lv_image_set_offset_x(img->base.obj, (int32_t)offset_x);
    lv_image_set_offset_y(img->base.obj, (int32_t)offset_y);
}

/**
 * @brief 设置图片缩放值。
 *
 * @param img 目标图片组件句柄。
 * @param zoom 缩放值，`LV_SCALE_NONE` 表示原始比例。
 * @return 无返回值。
 */
void img_set_zoom(img_t* img, uint16_t zoom) {
    if (!img_is_valid(img)) {
        return;
    }

    lv_image_set_scale(img->base.obj, zoom);
}

/**
 * @brief 设置图片旋转角度。
 *
 * @param img 目标图片组件句柄。
 * @param rotation 旋转角度，单位遵循 LVGL 约定。
 * @return 无返回值。
 */
void img_set_rotation(img_t* img, int16_t rotation) {
    if (!img_is_valid(img)) {
        return;
    }

    lv_image_set_rotation(img->base.obj, rotation);
}

/**
 * @brief 设置图片透明度。
 *
 * @param img 目标图片组件句柄。
 * @param opa 透明度值。
 * @return 无返回值。
 */
void img_set_opacity(img_t* img, uint8_t opa) {
    if (!img_is_valid(img)) {
        return;
    }

    lv_obj_set_style_image_opa(img->base.obj, (lv_opa_t)opa, 0);
}

/**
 * @brief 按配置结构批量应用图片属性。
 *
 * @param img 目标图片组件句柄。
 * @param cfg 图片配置，为 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
void img_apply_cfg(img_t* img, const img_cfg_t* cfg) {
    img_cfg_t default_cfg;

    if (!img_is_valid(img)) {
        return;
    }

    default_cfg = img_default_cfg();
    if (!cfg) {
        cfg = &default_cfg;
    }

    img_set_src(img, cfg->src);
    ui_widget_set_bounds(UI_WIDGET(img), cfg->x, cfg->y, cfg->w, cfg->h);
    img_set_offset(img, cfg->offset_x, cfg->offset_y);
    img_set_zoom(img, cfg->zoom);
    img_set_rotation(img, cfg->rotation);
    img_set_opacity(img, cfg->opa);
}
