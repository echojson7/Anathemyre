// Вещи на земле в мире: переходы рука ↔ земля и мешок ↔ земля, вид. Всё, что меняет мир, — из шага мира.
// Выронил в бою — оружие падает телом из руки и лежит, пока хозяин не поднимет (или не подберёт другой).
// Мёртвый и оставшийся без руки уже не поднимут: их оружие становится ничьим. Брошенное из мешка — ничьё.
// Подбор — приказом (ПКМ по вещи): первый выбранный идёт к ней и кладёт в мешок (docs/physics.md)
#ifndef ANA_LOOT_H
#define ANA_LOOT_H

#include "../render/render.h"
#include "../sim/ground.h"
#include "arena.h"
#include "field.h"
#include "squad.h"

#define LOOT_DRAW_MAX 256  // рисуется ближайших
#define LOOT_PICK_R   1.4f // подбирает, подойдя ближе, м

typedef struct LootView {
    GpuMesh bundle;  // узел: одежда, броня, товар на земле
    M4     *bones;   // RENDER_MAX_BONES на вещь: у оружия своя кость (CHAR_BONE_WEAPON), у узла — 0
} LootView;

void loot_view_init(LootView *v, MeshBuilder *mb, Arena *perm);
// Шаг мира после боя поля: выронил — на землю, поднял — в руку, мёртвый — ничьё
void loot_field(Ground *g, Phys *p, Field *f, GroundFn ground, u64 tick);
// То же для боя арены: выроненное лежит до конца схватки и исчезает с ней (бойцы арены — не наши)
void loot_arena(Ground *g, Phys *p, ArenaSession *s, bool in_bout, V3 center, f32 floor_y, u64 tick);
// Брошенное из мешка (просьбы интерфейса)
void loot_asks(Ground *g, Phys *p, GroundAsks *a, Squad *sq, u64 tick);
// Подбор по приказу: дошёл — в мешок. Чужое выроненное хозяин уже не поднимет
void loot_pickups(Ground *g, Phys *p, Squad *sq, Field *f);
// Отладка: n случайных вещей из общего запаса падают у точки at
void loot_drop_test(Ground *g, Phys *p, V3 at, u32 n, u64 seed);
// Кто и сколько; weapon — сетки оружия по номеру описания
u32  loot_draw(LootView *v, const Ground *g, const Phys *p, const GpuMesh *weapon, f32 alpha, V3 eye, DrawItem *out,
               u32 cap);

#endif
