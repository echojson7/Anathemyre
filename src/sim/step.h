// Шаг мира: один фиксированный шаг 1/60 с. Только он пишет состояние мира
#pragma once
#include "intent.h"
#include "world.h"

struct StepCtx {
    Span<const Intent> intents;  // намерения этого шага
    Arena *step_arena;           // сбрасывается в начале шага
    DebugDraw *dbg;              // может быть nullptr
};

void sim_step(World &w, StepCtx &ctx);
