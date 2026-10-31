// Вид вещей на земле (sim/loot.h): оружие своей сеткой, остальное — узлом по размеру
#ifndef ANA_LOOT_VIEW_H
#define ANA_LOOT_VIEW_H

#include "../render/render.h"
#include "../sim/ground.h"

#define LOOT_DRAW_MAX 256  // рисуется ближайших

typedef struct LootView {
    GpuMesh bundle;  // узел: одежда, броня, товар на земле
    M4     *bones;   // RENDER_MAX_BONES на вещь: у оружия своя кость (CHAR_BONE_WEAPON), у узла — 0
} LootView;

void loot_view_init(LootView *v, MeshBuilder *mb, Arena *perm);
// Кто и сколько; weapon — сетки оружия по номеру описания
u32  loot_draw(LootView *v, const Ground *g, const Phys *p, const GpuMesh *weapon, f32 alpha, V3 eye, DrawItem *out,
               u32 cap);

#endif
