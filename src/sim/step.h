// Шаг мира: один фиксированный шаг 1/60 с. Только он пишет состояние мира
#pragma once
#include "intent.h"
#include "world.h"

// Системы шага для замера по частям (бенч --sim, docs/sim.md «Замер шага»): точки — три системы заглушки этапа 0
enum StepSysId : u32 {
    SYS_INTENTS, SYS_DOTS, SYS_ACT, SYS_PATHS, SYS_STEER, SYS_SEPARATE, SYS_SETTLE, SYS_REST, SYS_COUNT
};
extern const char *const STEP_SYS_NAMES[SYS_COUNT];
constexpr u32 SYS_PEOPLE_FIRST = SYS_ACT, SYS_PEOPLE_LAST = SYS_SETTLE;  // системы людей — подряд

// Счётчики работы шага для журнала и бенча (не мир: в хеш не входят)
struct StepStats {
    u32 paths = 0;      // поисков пути
    u64 expanded = 0;   // раскрыто узлов A*
    u64 (*now_ns)() = nullptr;  // часы для замера (бенч --sim); мир от них не зависит
    u64 sys_ns[SYS_COUNT] = {};  // время по системам (с часами now_ns)
    u32 strikes = 0;    // начатых ударов (этап 10)
    u32 hits = 0;       // попаданий
    u32 blocks = 0;     // отбитых блоком (этап 11)
    u32 defends = 0;    // начатых защит: блоков и уклонов
    u32 friendly = 0;   // попаданий по своим одной стороны (этап 12)
};

struct StepCtx {
    Span<const Intent> intents;  // намерения этого шага
    Arena *step_arena;           // сбрасывается в начале шага
    DebugDraw *dbg;              // может быть nullptr
    StepStats *stats = nullptr;
};

void sim_step(World &w, StepCtx &ctx);
