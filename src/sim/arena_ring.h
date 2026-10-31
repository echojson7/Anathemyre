// Ринг арены у зала: бои из ArenaSession в реальном времени (тикает отладка, debug/), вид бойцов.
// Геометрия ринга — content/ring.c, позы бойцов и отрисовка — view/figures.c
#ifndef ANA_ARENA_RING_H
#define ANA_ARENA_RING_H

#include "arena.h"
#include "anim.h"
#include "look.h"
#include "fight_anim.h"

// Зрители — обычные люди мира (People, ROLE_WATCHER, world/crowd.c), стоят за оградой
#define RING_SPECTATORS 14
// Ринг у зала, перед его открытым краем; верх песка
#define ARENA_CENTER v3(14.0f, 0.0f, 34.0f)
#define RING_FLOOR_Y 0.125f
// Сколько наборов костей нужно бойцам арены; рисует их figures.c
#define RING_BONE_SETS ARENA_MAX_BOUT

typedef struct ArenaRing {
    V3  center;
    f32 floor_y;
    f32 alpha;      // для кадра: доля шага мира после последнего тика боя
    bool in_bout;

    // Бойцы текущего боя: вид каждого (сглаживание, позы, выпавшее оружие); пишет кадр
    FightAnim fa[ARENA_MAX_BOUT];
} ArenaRing;

void arena_ring_init(ArenaRing *v, V3 center, f32 floor_y);
// Тик арены (шаг мира): бой идёт, между боями ищется следующий
void arena_ring_tick(ArenaRing *v, ArenaSession *s);
// Вид бойцов для кадра: alpha — доля шага после тика, dt — игровое время кадра
void arena_ring_present(ArenaRing *v, const ArenaSession *s, f32 alpha, f32 dt);
// Внешность бойца ростера ri (матрица слота RENDER_LOOK_SLOT)
M4  arena_ring_look(const ArenaSession *s, u32 ri, CharLook *lk);

#endif
