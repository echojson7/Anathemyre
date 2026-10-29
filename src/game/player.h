// Свой персонаж: идёт в указанную точку (ПКМ по земле, как в Kenshi).
// Препятствия пока не обходит: поиск пути отдельным этапом.
#ifndef ANA_PLAYER_H
#define ANA_PLAYER_H

#include "../core/math3d.h"

typedef f32 (*GroundFn)(f32 x, f32 z);

typedef struct Player {
    V3   pos;
    f32  heading, phase, walk;
    V3   target;
    bool moving;
    f32  marker;   // сколько ещё секунд видна метка цели
} Player;

void player_init(Player *p, V3 pos, f32 heading);
void player_order_move(Player *p, V3 target);
void player_update(Player *p, f32 dt, GroundFn ground);

#endif
