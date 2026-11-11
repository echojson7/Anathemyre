// Перемотка мира назад (отладка этапа 10, docs/people.md «Инструменты удара»): кольцо снимков столбцов мира раз в
// REWIND_EVERY шагов и журнал намерений. Назад на шаг — снимок не позже нужного шага и повтор шагов с теми же
// намерениями: шаг мира повторяется бит в бит, поэтому вперёд он придёт в то же место
#include "debug.h"
#include "../core/log.h"
#include "../sim/step.h"
#include <string.h>

void rewind_init(Rewind &r, const World &w) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    r.snap_bytes = 0;
    for (u32 i = 0; i < n; ++i) r.snap_bytes += cols[i].bytes;
    r.buf = (u8 *)platform_alloc(r.snap_bytes * REWIND_SNAPS);
    r.log = (Intent *)platform_alloc(sizeof(Intent) * REWIND_LOG);
    r.head = r.count = 0;
    r.log_head = r.log_count = 0;
    r.ok = r.buf && r.log;
    if (r.ok)
        log_info("перемотка: %u снимков по %.0f КБ (раз в %u шагов, %.0f с)", REWIND_SNAPS, (f64)r.snap_bytes / 1024.0,
                 REWIND_EVERY, (f64)(REWIND_SNAPS * REWIND_EVERY) / (f64)SIM_HZ);
}

void rewind_free(Rewind &r) {
    if (r.buf) platform_free(r.buf);
    if (r.log) platform_free(r.log);
    r = Rewind{};
}

static void snap_save(Rewind &r, const World &w, u32 at) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    u8 *p = r.buf + (usize)at * r.snap_bytes;
    for (u32 i = 0; i < n; ++i) {
        memcpy(p, cols[i].data, cols[i].bytes);
        p += cols[i].bytes;
    }
    r.snap_step[at] = w.step;
}

static void snap_load(const Rewind &r, World &w, u32 at) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    const u8 *p = r.buf + (usize)at * r.snap_bytes;
    for (u32 i = 0; i < n; ++i) {
        memcpy((void *)cols[i].data, p, cols[i].bytes);  // столбцы мира — его же массивы
        p += cols[i].bytes;
    }
}

void rewind_after_step(Rewind &r, const World &w, Span<const Intent> its) {
    if (!r.ok) return;
    for (usize i = 0; i < its.count; ++i) {
        r.log[(r.log_head + r.log_count) % REWIND_LOG] = its.data[i];
        if (r.log_count < REWIND_LOG) r.log_count++;
        else r.log_head = (r.log_head + 1) % REWIND_LOG;
    }
    if (w.step % REWIND_EVERY) return;
    u32 at = (r.head + r.count) % REWIND_SNAPS;
    if (r.count < REWIND_SNAPS) r.count++;
    else r.head = (r.head + 1) % REWIND_SNAPS;
    snap_save(r, w, at);
}

bool rewind_to(Rewind &r, World &w, u64 target, Arena &step_arena) {
    if (!r.ok || !r.count) return false;
    i32 best = -1;
    for (u32 k = 0; k < r.count; ++k) {
        u32 at = (r.head + k) % REWIND_SNAPS;
        if (r.snap_step[at] <= target) best = (i32)at;
    }
    if (best < 0) return false;
    snap_load(r, w, (u32)best);
    // Снимки новее точки — уже будущее другой ветки: забыть их
    while (r.count && r.snap_step[(r.head + r.count - 1) % REWIND_SNAPS] > w.step) r.count--;
    Intent its[256];
    while (w.step < target) {
        u32 n = 0;
        for (u32 k = 0; k < r.log_count && n < 256; ++k) {
            const Intent &it = r.log[(r.log_head + k) % REWIND_LOG];
            if (it.step == w.step) its[n++] = it;
        }
        StepCtx ctx{{its, n}, &step_arena, nullptr};
        sim_step(w, ctx);
    }
    // Намерения будущего (после точки) не повторятся сами: журнал до точки
    u32 keep = 0;
    while (keep < r.log_count && r.log[(r.log_head + keep) % REWIND_LOG].step < w.step) keep++;
    r.log_count = keep;
    return true;
}
