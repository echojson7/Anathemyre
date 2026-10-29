#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif

#include "clock.h"

static f64 g_start;

#if defined(_WIN32)
static f64 raw_now(void)
{
    static f64 inv_freq;
    LARGE_INTEGER t;
    if (inv_freq == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        inv_freq = 1.0 / (f64)f.QuadPart;
    }
    QueryPerformanceCounter(&t);
    return (f64)t.QuadPart * inv_freq;
}
#else
static f64 raw_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec + (f64)ts.tv_nsec * 1e-9;
}
#endif

void time_init(void)
{
    g_start = raw_now();
}

f64 time_now(void)
{
    return raw_now() - g_start;
}
