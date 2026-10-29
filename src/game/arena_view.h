// Арена на экране: ринг у зала, бои из ArenaSession в реальном времени, вид бойцов, места зрителей.
// Только читает состояние боя; позы бойцов и отрисовка — в figures.c. Симуляция ничего не знает о графике.
#ifndef ANA_ARENA_VIEW_H
#define ANA_ARENA_VIEW_H

#include "../render/render.h"
#include "arena.h"
#include "anim.h"
#include "look.h"
#include "fight_anim.h"

// Зрители — обычные люди из таблицы (People, ROLE_WATCHER): арена только расставляет их за оградой
#define AV_SPECTATORS 14
// Сколько наборов костей нужно бойцам арены (с выпавшим оружием); рисует их figures.c
#define AV_BONE_SETS (ARENA_MAX_BOUT * 2)
// Свет арены: два прожектора и две лампы на воротах
#define AV_LIGHTS 4

typedef struct ArenaView {
    V3  center;
    f32 floor_y;
    f32 alpha;      // для кадра: доля шага мира после последнего тика боя
    bool in_bout;

    // Бойцы текущего боя: вид каждого (сглаживание, позы, выпавшее оружие); пишет кадр
    FightAnim fa[ARENA_MAX_BOUT];

    // Места зрителей за оградой: точка в мире, куда смотрит, зерно позы
    V3  spos[AV_SPECTATORS];
    f32 sh[AV_SPECTATORS], sseed[AV_SPECTATORS];
} ArenaView;

// Ринг, ограда, прожекторы, круг знаков: статика с тенями
void arena_view_build(MeshBuilder *mb, V3 center);
void arena_view_init(ArenaView *v, V3 center, f32 floor_y);
// Тик арены (шаг мира): бой идёт, между боями ищется следующий
void arena_view_tick(ArenaView *v, ArenaSession *s);
// Вид бойцов для кадра: alpha — доля шага после тика, dt — игровое время кадра
void arena_view_present(ArenaView *v, const ArenaSession *s, f32 alpha, f32 dt);
// Внешность бойца ростера ri (матрица слота RENDER_LOOK_SLOT)
M4  arena_view_look(const ArenaSession *s, u32 ri, CharLook *lk);
// Свет арены: зажигается с темнотой night (Sky.night), в out — до AV_LIGHTS источников
u32 arena_view_lights(const ArenaView *v, f32 night, f32 time, PointLight *out);

#endif
