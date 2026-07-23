#include "simulator_platform.h"

#if defined(_WIN32)
#include "windows/simulator_platform.c"
#else
#include "linux/simulator_platform.c"
#endif

/**
 * @brief 模拟器无需控制 RPMsg TTF 位图缓存。
 * @param[in] enable 是否启用位图缓存。
 * @return 无返回值。
 */
void rpmsgttf_cache_bitmap_enable(bool enable) {
    (void)enable;
}