// Эрозия водой крупного слоя земли (docs/terrain.md, «Эрозия»). Только при создании мира
#pragma once
#include "../core/mem.h"

struct ErodeParams {
    u32 droplets = 0;
    u32 seed = 0;
    u32 lifetime = 40;       // шагов капли (шаг — клетка)
    i32 radius = 2;          // кисть смыва, клеток
    f32 inertia = 0.05f;     // доля прежнего направления
    f32 capacity = 4.0f;     // сколько грунта несёт капля на единицу перепада, скорости и воды
    f32 min_slope = 0.01f;   // на ровном капля всё равно чуть несёт
    f32 erode = 0.3f;        // доля недобора, смываемая за шаг
    f32 deposit = 0.3f;      // доля излишка, откладываемая за шаг
    f32 evaporate = 0.02f;   // доля воды, уходящая за шаг
    f32 gravity = 4.0f;
    f32 init_speed = 1.0f;
    f32 max_speed = 8.0f;
};

// h — n·n высот в клетках (метры / шаг сетки), меняется на месте. deposit и flow (могут быть nullptr, n·n,
// обнулены вызывающим) копят отложенный грунт и воду, прошедшую через узел. Временная память — scratch
void terrain_erode(f32 *h, u32 n, const ErodeParams &p, f32 *deposit, f32 *flow, Arena &scratch);
