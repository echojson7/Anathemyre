#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // CreateWaitableTimerExW
#endif
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

#if defined(_WIN32)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
// Таймер высокого разрешения есть с Windows 10 1803; без него только ждём на месте
static void sleep_for(f64 s)
{
    static HANDLE timer;
    static bool tried;
    if (!tried) {
        tried = true;
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    if (!timer) {
        Sleep(0);
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(s * 1e7); // в единицах 100 нс, минус — относительно
    if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
}
#else
static void sleep_for(f64 s)
{
    struct timespec ts = { (time_t)s, (long)((s - (f64)(time_t)s) * 1e9) };
    nanosleep(&ts, NULL);
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

void time_wait_until(f64 t)
{
    // Спим с запасом в 0.5 мс на неточность планировщика, остаток досчитываем на месте
    f64 left = t - time_now();
    if (left > 0.0006) sleep_for(left - 0.0005);
    while (time_now() < t) {}
}
