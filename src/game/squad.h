// Свой отряд: тела на земле (Player) и листы персонажей; выбор и приказы как в Kenshi
#ifndef ANA_SQUAD_H
#define ANA_SQUAD_H

#include "../core/rng.h"
#include "../sim/inventory.h"
#include "player.h"

#define SQUAD_MAX 8

typedef struct Squad {
    u32       count;
    Player    body[SQUAD_MAX];
    CharSheet sheet[SQUAD_MAX];
    u8        variant[SQUAD_MAX];   // одежда модели
    bool      selected[SQUAD_MAX];
    bool      formation;            // идущие по одному приказу держат темп самого медленного
    u16       next_group;
} Squad;

// Стартовый отряд из четырёх у точки origin
void squad_init(Squad *s, Rng *rng, V3 origin, f32 heading, GroundFn ground);
// Выбранные идут к точке строем: ряды по три поперёк направления движения
void squad_order_move(Squad *s, V3 target);
// Режим передвижения выбранным
void squad_set_gait(Squad *s, Gait g);
// Предел скорости персонажа в режиме g: ноги, атлетика, ноша
f32  squad_member_speed(const Squad *s, u32 i, Gait g);
void squad_update(Squad *s, f32 dt, GroundFn ground);
// Первый выбранный или -1
i32  squad_primary(const Squad *s);

#endif
