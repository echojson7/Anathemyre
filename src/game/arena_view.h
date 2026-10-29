// Арена на экране: ринг у зала, бои из ArenaSession в реальном времени, позы бойцов, зрители.
// Только читает состояние боя и строит позы; симуляция ничего не знает о графике.
#ifndef ANA_ARENA_VIEW_H
#define ANA_ARENA_VIEW_H

#include "../render/render.h"
#include "arena.h"
#include "character.h"
#include "look.h"

#define AV_SPECTATORS 14
// Сколько элементов отрисовки и наборов костей нужно арене
#define AV_DRAW_ITEMS (ARENA_MAX_BOUT * 2 + AV_SPECTATORS)
#define AV_BONE_SETS (ARENA_MAX_BOUT * 2 + AV_SPECTATORS)

typedef struct ArenaView {
    V3  center;
    f32 floor_y;
    f32 acc;        // накопленное время до следующего тика боя
    bool in_bout;

    // Бойцы текущего боя: прошлый тик для сглаживания и состояние анимации
    f32 prev_x[ARENA_MAX_BOUT], prev_z[ARENA_MAX_BOUT], prev_h[ARENA_MAX_BOUT];
    f32 phase[ARENA_MAX_BOUT], walk[ARENA_MAX_BOUT], move_dir[ARENA_MAX_BOUT];
    f32 guard[ARENA_MAX_BOUT], block[ARENA_MAX_BOUT], dodge[ARENA_MAX_BOUT], stagger[ARENA_MAX_BOUT];
    f32 down[ARENA_MAX_BOUT], down_dir[ARENA_MAX_BOUT], kneel[ARENA_MAX_BOUT], limp[ARENA_MAX_BOUT], crawl[ARENA_MAX_BOUT];
    bool dropped[ARENA_MAX_BOUT];
    V3  drop_pos[ARENA_MAX_BOUT];
    f32 drop_yaw[ARENA_MAX_BOUT];

    // Зрители за оградой
    f32 sx[AV_SPECTATORS], sz[AV_SPECTATORS], sh[AV_SPECTATORS], sseed[AV_SPECTATORS];
} ArenaView;

// Ринг, ограда, прожекторы, круг знаков: статика с тенями
void arena_view_build(MeshBuilder *mb, V3 center);
void arena_view_init(ArenaView *v, V3 center, f32 floor_y);
// Прогнать бои по реальному времени (тики COMBAT_HZ) и обновить анимацию
void arena_view_update(ArenaView *v, ArenaSession *s, f32 dt);
// Заполнить элементы отрисовки; bones — AV_BONE_SETS * RENDER_MAX_BONES матриц.
// body — сетки тел (BODY_KIND_COUNT вариантов), weapons — по W_COUNT
// Внешность бойца ростера ri (матрица слота RENDER_LOOK_SLOT)
M4  arena_view_look(const ArenaSession *s, u32 ri);
u32 arena_view_draw(const ArenaView *v, const ArenaSession *s, f32 time, const GpuMesh *body, u32 variants,
                    const GpuMesh *weapons, M4 *bones, DrawItem *items);

#endif
