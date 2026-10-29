// Арена на экране: ринг у зала, бои из ArenaSession в реальном времени, позы бойцов, зрители.
// Только читает состояние боя и строит позы; симуляция ничего не знает о графике.
#ifndef ANA_ARENA_VIEW_H
#define ANA_ARENA_VIEW_H

#include "../render/render.h"
#include "arena.h"
#include "character.h"
#include "look.h"
#include "fight_anim.h"

// Зрители — обычные люди на поле (main.c): арена только расставляет их за оградой
#define AV_SPECTATORS 14
// Сколько элементов отрисовки и наборов костей нужно арене
#define AV_DRAW_ITEMS (ARENA_MAX_BOUT * 2)
#define AV_BONE_SETS (ARENA_MAX_BOUT * 2)
// Свет арены: два прожектора и две лампы на воротах
#define AV_LIGHTS 4

typedef struct ArenaView {
    V3  center;
    f32 floor_y;
    f32 acc;        // накопленное время до следующего тика боя
    bool in_bout;

    // Бойцы текущего боя: вид каждого (сглаживание, позы, выпавшее оружие)
    FightAnim fa[ARENA_MAX_BOUT];

    // Места зрителей за оградой: точка в мире, куда смотрит, зерно позы
    V3  spos[AV_SPECTATORS];
    f32 sh[AV_SPECTATORS], sseed[AV_SPECTATORS];
} ArenaView;

// Ринг, ограда, прожекторы, круг знаков: статика с тенями
void arena_view_build(MeshBuilder *mb, V3 center);
void arena_view_init(ArenaView *v, V3 center, f32 floor_y);
// Прогнать бои по реальному времени (тики COMBAT_HZ) и обновить анимацию
void arena_view_update(ArenaView *v, ArenaSession *s, f32 dt);
// Заполнить элементы отрисовки; bones — AV_BONE_SETS * RENDER_MAX_BONES матриц.
// body — сетки тел (BODY_KIND_COUNT вариантов), weapons — по W_COUNT (в руке или в ножнах — по позе)
// Внешность бойца ростера ri (матрица слота RENDER_LOOK_SLOT)
M4  arena_view_look(const ArenaSession *s, u32 ri);
// Свет арены: зажигается с темнотой night (Sky.night), в out — до AV_LIGHTS источников
u32 arena_view_lights(const ArenaView *v, f32 night, f32 time, PointLight *out);
u32 arena_view_draw(const ArenaView *v, const ArenaSession *s, f32 time, const GpuMesh *body, u32 variants,
                    const GpuMesh *weapons, M4 *bones, DrawItem *items);

#endif
