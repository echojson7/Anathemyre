// Общие мелочи окон отладки (panel.cpp, metrics.cpp): числа в текст. Только для src/debug
#pragma once
#include "debug.h"
#include <stdio.h>

// Час суток «чч:мм»
inline void dbg_hm(f64 hour, char *out, usize n) {
    i32 m = (i32)(hour * 60.0) % 1440;
    if (m < 0) m += 1440;
    snprintf(out, n, "%02d:%02d", m / 60, m % 60);
}

// Счётчик коротко: 950, 12.3к, 862к, 4.56М
inline void dbg_count(u64 v, char *out, usize n) {
    if (v < 10000) snprintf(out, n, "%llu", (unsigned long long)v);
    else if (v < 100000) snprintf(out, n, "%.1fк", (double)v / 1e3);
    else if (v < 1000000) snprintf(out, n, "%.0fк", (double)v / 1e3);
    else snprintf(out, n, "%.2fМ", (double)v / 1e6);
}

// Имена скоростей суток (CLOCK_SPEEDS)
inline const char *const *dbg_day_speeds() {
    static const char *const names[] = {"обычный", "×20", "×120", "стоп"};
    static_assert(sizeof names / sizeof *names == sizeof CLOCK_SPEEDS / sizeof *CLOCK_SPEEDS, "скорости суток");
    return names;
}

// Окно показателей (metrics.cpp): справа сверху, ширина w, не выше max_h
void debug_metrics_panel(const DebugInfo &in, DebugState &d, f32 x, f32 y, f32 w, f32 max_h);
