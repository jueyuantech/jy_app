#pragma once

#ifndef FAR
#define FAR
#endif

#ifndef CODE
#define CODE
#endif

#ifndef DATA
#define DATA
#endif

#ifndef __EXPORT
#define __EXPORT
#endif

#ifndef __unused
#if defined(__GNUC__) || defined(__clang__)
#define __unused __attribute__((unused))
#else
#define __unused
#endif
#endif

#ifndef __weak_function
#if defined(__GNUC__) || defined(__clang__)
#define __weak_function __attribute__((weak))
#else
#define __weak_function
#endif
#endif

#ifndef __packed_struct
#if defined(__GNUC__) || defined(__clang__)
#define __packed_struct __attribute__((packed))
#else
#define __packed_struct
#endif
#endif

#ifndef __aligned_data
#if defined(__GNUC__) || defined(__clang__)
#define __aligned_data(x) __attribute__((aligned(x)))
#else
#define __aligned_data(x)
#endif
#endif

#ifndef aligned_data
#define aligned_data(x) __aligned_data(x)
#endif
