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

// Кадр по частям (docs/frame_time.md): время каждой части кадра, среднее и наибольшее за секунду, худший кадр
// секунды целиком (из чего он сложился) и история полного времени последних кадров для графика.
// Время даёт вызывающий (ядро часов не знает): prof_part — от прошлой отметки до now, prof_add — готовое
constexpr u32 PROF_MAX_PARTS = 16;
constexpr u32 PROF_HISTORY = 256;
struct FrameProf {
    u32 nparts = 0;
    const char *names[PROF_MAX_PARTS] = {};
    u32 rest = 0;                  // часть «прочее»: кадр минус сумма остальных
    u64 mark = 0;                  // конец прошлой отметки, нс
    f64 cur[PROF_MAX_PARTS] = {};  // текущий кадр
    // окно
    f64 sum[PROF_MAX_PARTS] = {}, max[PROF_MAX_PARTS] = {}, worst[PROF_MAX_PARTS] = {};
    f64 total_sum = 0, worst_total = 0;
    u32 n = 0;
    // итог прошлого окна
    f64 avg_last[PROF_MAX_PARTS] = {}, max_last[PROF_MAX_PARTS] = {}, worst_last[PROF_MAX_PARTS] = {};
    f64 total_avg_last = 0, worst_total_last = 0;
    u32 n_last = 0;
    f32 hist[PROF_HISTORY] = {};   // полное время кадра, мс; кольцо
    u32 hist_pos = 0, hist_count = 0;
};

inline void prof_begin(FrameProf &p, u64 now_ns) {
    p.mark = now_ns;
    for (u32 i = 0; i < p.nparts; ++i) p.cur[i] = 0;
}
inline void prof_part(FrameProf &p, u32 part, u64 now_ns) {
    if (part < p.nparts) p.cur[part] += (f64)(now_ns - p.mark) / 1e6;
    p.mark = now_ns;
}
inline void prof_add(FrameProf &p, u32 part, f64 ms) {
    if (part < p.nparts) p.cur[part] += ms;
}
// Кадр закончен: total_ms — от начала этого кадра до начала следующего
inline void prof_end(FrameProf &p, f64 total_ms) {
    f64 known = 0;
    for (u32 i = 0; i < p.nparts; ++i)
        if (i != p.rest) known += p.cur[i];
    if (p.rest < p.nparts) p.cur[p.rest] = total_ms > known ? total_ms - known : 0;
    for (u32 i = 0; i < p.nparts; ++i) {
        p.sum[i] += p.cur[i];
        if (p.cur[i] > p.max[i]) p.max[i] = p.cur[i];
    }
    if (total_ms > p.worst_total || p.n == 0) {
        p.worst_total = total_ms;
        for (u32 i = 0; i < p.nparts; ++i) p.worst[i] = p.cur[i];
    }
    p.total_sum += total_ms;
    p.n++;
    p.hist[p.hist_pos] = (f32)total_ms;
    p.hist_pos = (p.hist_pos + 1) % PROF_HISTORY;
    if (p.hist_count < PROF_HISTORY) p.hist_count++;
}
inline void prof_roll(FrameProf &p) {
    for (u32 i = 0; i < p.nparts; ++i) {
        p.avg_last[i] = p.n ? p.sum[i] / p.n : 0;
        p.max_last[i] = p.max[i];
        p.worst_last[i] = p.worst[i];
        p.sum[i] = p.max[i] = p.worst[i] = 0;
    }
    p.total_avg_last = p.n ? p.total_sum / p.n : 0;
    p.worst_total_last = p.worst_total;
    p.n_last = p.n;
    p.total_sum = p.worst_total = 0;
    p.n = 0;
}
// Отметка без части: время до неё уходит в «прочее»
inline void prof_mark(FrameProf &p, u64 now_ns) { p.mark = now_ns; }
