// Мир тестовой сцены: пустошь с залом, ареной и Скуделью, долина у реки, люди одной таблицей,
// свой отряд, поле боя, арена, время суток. Шаг мира не знает о кадре: кадр только читает мир
#ifndef ANA_WORLD_H
#define ANA_WORLD_H

#include "../core/arena.h"
#include "../core/rng.h"
#include "../render/render.h"
#include "arena.h"
#include "arena_view.h"
#include "field.h"
#include "people.h"
#include "squad.h"
#include "town.h"
#include "valley_view.h"

#define DAY_LENGTH_S 240.0f  // полные сутки, с; пока быстро, чтобы смену было видно

typedef struct World {
    u64           seed;
    Rng           rng, town_rng;
    Town          town;
    People       *people;
    Squad         squad;
    Field        *field;
    ArenaSession *arena;
    ArenaView     aview;
    ValleyView   *valley;        // строится при первом переходе
    GpuMesh       hall, far, town_mesh;  // статика пустоши: зал и обломки, рельеф и мегаструктуры, посёлок
    f32           time;          // время симуляции, с
    f32           tod;           // время суток: 0 полночь, 0.5 полдень
    u32           day;
    bool          day_run;
} World;

bool world_init(World *w, Arena *perm, MeshBuilder *mb, u64 seed, f32 tod, bool day_run);
// Шаг мира: люди, арена, отряд, поле боя, сутки
void world_update(World *w, f32 dt);
// Высота под ногами там, где сейчас отряд: пустошь (пол зала, ярусы посёлка, плита арены) или долина
f32  world_ground(f32 x, f32 z);
bool world_in_valley(void);
// Перевести отряд в другую локацию. false — нельзя (в бою) или долина не построилась
bool world_travel(World *w, Arena *perm);
// Отряд дерётся
bool world_squad_fighting(const World *w);

#endif
