#include "simulator_platform.h"

#if defined(_WIN32)
#include "windows/simulator_platform.c"
#else
#include "linux/simulator_platform.c"
#endif
