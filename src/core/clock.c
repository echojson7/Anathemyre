#define _POSIX_C_SOURCE 200809L
#include "clock.h"

#include <time.h>

static f64 g_start;

static f64 raw_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec + (f64)ts.tv_nsec * 1e-9;
}

void time_init(void)
{
    g_start = raw_now();
}

f64 time_now(void)
{
    return raw_now() - g_start;
}
