// Замер: окно значений (кадр, шаг) — среднее и максимум за последнюю секунду
#pragma once
#include "types.h"

struct TimeStat {
    f64 sum_ms = 0;
    f64 max_ms = 0;
    u32 n = 0;
    // итог прошлого окна
    f64 avg_last = 0;
    f64 max_last = 0;
    u32 n_last = 0;
};

inline void stat_add(TimeStat &s, f64 ms) {
    s.sum_ms += ms;
    if (ms > s.max_ms) s.max_ms = ms;
    s.n++;
}
inline void stat_roll(TimeStat &s) {
    s.avg_last = s.n ? s.sum_ms / s.n : 0;
    s.max_last = s.max_ms;
    s.n_last = s.n;
    s.sum_ms = s.max_ms = 0;
    s.n = 0;
}
