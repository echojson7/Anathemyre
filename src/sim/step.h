// Шаг мира: один фиксированный шаг 1/60 с. Только он пишет состояние мира
#pragma once
#include "intent.h"
#include "world.h"

// Счётчики работы шага для журнала и бенча (не мир: в хеш не входят)
struct StepStats {
    u32 paths = 0;      // поисков пути
    u64 expanded = 0;   // раскрыто узлов A*
    u64 (*now_ns)() = nullptr;  // часы для замера (бенч --sim); мир от них не зависит
    u64 people_ns = 0;  // время систем людей
    u32 strikes = 0;    // начатых ударов (этап 10)
    u32 hits = 0;       // попаданий
    u32 blocks = 0;     // отбитых блоком (этап 11)
    u32 defends = 0;    // начатых защит: блоков и уклонов
};

struct StepCtx {
    Span<const Intent> intents;  // намерения этого шага
    Arena *step_arena;           // сбрасывается в начале шага
    DebugDraw *dbg;              // может быть nullptr
    StepStats *stats = nullptr;
};

void sim_step(World &w, StepCtx &ctx);
