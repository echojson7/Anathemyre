// Замер кадра (меню НАСТРОЙКИ или --bench) и строки КАДР в журнале раз в 2 с.
// CPU кадра — по зонам профайлера (core/prof.h), GPU — по меткам времени проходов
#ifndef ANA_BENCH_H
#define ANA_BENCH_H

#include "../core/arena.h"
#include "../core/prof.h"

// BENCH_WARMUP_S на прогрев, затем BENCH_S секунд замера и итог в лог
#define BENCH_WARMUP_S 2.0
#define BENCH_S 10.0
#define BENCH_MAX_FRAMES 200000

typedef struct Bench {
    bool on;
    f64  start, measure_from;     // начало прогрева и замера
    f64  last_now;                // начало предыдущего кадра замера, 0 — замер ещё не начат
    u32  frames, cpu_frames;
    f64  posed;                   // поз посчитано за замер
    f32 *frame_ms;                // BENCH_MAX_FRAMES
} Bench;

void bench_init(Bench *b, Arena *perm);
void bench_begin(Bench *b, f64 now);
// Кадр, начатый в now, закончен; posed — поз в нём. true — замер окончен, итог в журнале
bool bench_frame(Bench *b, f64 now, u32 posed, i32 ww, i32 wh, bool swap_late);

// Строка КАДР раз в 2 с и счётчик кадров для меню
typedef struct FrameStats {
    f64 t, fps_t;
    u32 frames, fps_frames;
    f64 posed;
    f32 shown_ms, shown_fps;      // для меню НАСТРОЙКИ
    f32 fps, fps_ms;              // для счётчика кадров, раз в 0.5 с
} FrameStats;

void stats_reset(FrameStats *s, f64 now);
void stats_frame(FrameStats *s, f64 now, u32 posed, bool swap_late);

#endif
