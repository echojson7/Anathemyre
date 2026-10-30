// Замер кадра (меню НАСТРОЙКИ или --bench) и строки КАДР в журнале раз в 2 с.
// CPU кадра — по зонам профайлера (core/prof.h), GPU — по меткам времени проходов
#ifndef ANA_BENCH_H
#define ANA_BENCH_H

#include "../core/arena.h"
#include "../core/prof.h"
#include "../render/render.h"

// BENCH_WARMUP_S на прогрев, затем BENCH_S секунд замера и итог в лог; в замере по этапам — короче на каждый этап
#define BENCH_WARMUP_S 2.0
#define BENCH_S 10.0
#define BENCH_STAGE_WARMUP_S 1.5
#define BENCH_STAGE_S 5.0
#define BENCH_MAX_FRAMES 200000

// Итог замера, мс на кадр (GPU < 0 — драйвер не отдаёт метки времени)
typedef struct BenchResult {
    f64 frame_ms, fps, gpu_ms, logic_ms, submit_ms, swap_ms;
    f64 pass_ms[PASS_COUNT], gap_ms;   // GPU по проходам и между кадрами
} BenchResult;

typedef struct Bench {
    bool on;
    f64  start, measure_from;     // начало прогрева и замера
    f64  measure_s;
    BenchResult last;             // итог последнего замера
    f64  last_now;                // начало предыдущего кадра замера, 0 — замер ещё не начат
    u32  frames, cpu_frames;
    f64  posed;                   // поз посчитано за замер
    f32 *frame_ms;                // BENCH_MAX_FRAMES
} Bench;

void bench_init(Bench *b, Arena *perm);
void bench_begin(Bench *b, f64 now, f64 warmup_s, f64 measure_s);
// Кадр, начатый в now, закончен; posed — поз в нём. true — замер окончен, итог в журнале
bool bench_frame(Bench *b, f64 now, u32 posed, i32 ww, i32 wh, bool swap_late);
// Итог замера по этапам сцены: строка на этап, прибавка к кадру против прошлого этапа
void bench_stages_report(const BenchResult *r, u32 n, const char *const *names);

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
