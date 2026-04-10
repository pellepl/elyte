#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cpu.h"
#include "minio.h"
#include "uart_driver.h"

#define stringify(x) __str(x)
#define __str(x) #x
#define hexify(z) __hex(z)
#define __hex(z) 0x##z
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

#define ASSERT(x)                                                   \
    do                                                              \
    {                                                               \
        if (!(x))                                                   \
        {                                                           \
            cpu_interrupt_disable();                                \
            printf("\nASSERT FAIL %s @ %d\n", _FILENAME, __LINE__); \
            if (BUILD_RELEASE)                                      \
                cpu_reset();                                        \
            else                                                    \
                while (1)                                           \
                    ;                                               \
        }                                                           \
    } while (0);

#if BUILD_RELEASE
#define DASSERT(...)
#else
#define DASSERT(...) ASSERT(__VA_ARGS__)
#endif

#if BUILD_RELEASE
#define dbg_printf(...) \
    do                  \
    {                   \
    } while (0)
#else
#define dbg_printf(...)      \
    do                       \
    {                        \
        printf(__VA_ARGS__); \
    } while (0)
#endif

bool begins_with(const char *str, const char *prefix);
float strtof(const char *s);
const char *ftostr(float x);

static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t abs_i32(int32_t a) { return a < 0 ? -a : a; }
static inline uint32_t clamp_u32(uint32_t min, uint32_t a, uint32_t max) { return a < min ? min : (a > max ? max : a); }
static inline uint32_t clamp_i32(int32_t min, int32_t a, int32_t max) { return a < min ? min : (a > max ? max : a); }
static inline float max_f(float a, float b) { return a > b ? a : b; }
static inline float min_f(float a, float b) { return a < b ? a : b; }
static inline float abs_f(float a) { return a < 0 ? -a : a; }
static inline int32_t int_f(float x) { return (int32_t)x; }
static inline int32_t round_nearest_f(float a) { return (int32_t)(a + (a < 0 ? -0.5f : 0.5f)); }
static inline uint32_t clz32(uint32_t x)
{
    if (x == 0)
        return 32;

    uint32_t n = 0;

    if (!(x & 0xffff0000))
    {
        n += 16;
        x <<= 16;
    }
    if (!(x & 0xff000000))
    {
        n += 8;
        x <<= 8;
    }
    if (!(x & 0xf0000000))
    {
        n += 4;
        x <<= 4;
    }
    if (!(x & 0xc0000000))
    {
        n += 2;
        x <<= 2;
    }
    if (!(x & 0x80000000))
    {
        n += 1;
    }

    return n;
}