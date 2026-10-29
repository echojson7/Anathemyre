// Свой персонаж: идёт в указанную точку (ПКМ по земле, как в Kenshi).
// Препятствия пока не обходит: поиск пути отдельным этапом.
#ifndef ANA_PLAYER_H
#define ANA_PLAYER_H

#include "../core/math3d.h"

typedef f32 (*GroundFn)(f32 x, f32 z);

// Режим передвижения, выбирается игроком для каждого
typedef enum Gait { GAIT_WALK, GAIT_JOG, GAIT_RUN, GAIT_COUNT } Gait;

// Скорость режима для здорового среднего человека, м/с
extern const f32 g_gait_speed[GAIT_COUNT];

typedef struct Player {
    V3   pos;
    f32  heading, phase, walk;
    f32  run;      // походка: 0 шаг .. 1 бег, следует за фактической скоростью
    f32  vmax;     // предел скорости на этот кадр, ставит отряд
    u8   gait;
    u16  group;    // номер приказа: кто идёт вместе, держит общий темп
    V3   target;
    bool moving;
    f32  marker;   // сколько ещё секунд видна метка цели
} Player;

void player_init(Player *p, V3 pos, f32 heading);
void player_order_move(Player *p, V3 target);
void player_update(Player *p, f32 dt, GroundFn ground);

#endif
