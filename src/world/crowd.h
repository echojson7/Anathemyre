// Люди зала (тестовые): бродящие, зрители арены и мобы. Ставятся на землю ground (app/ground.c)
#ifndef ANA_CROWD_H
#define ANA_CROWD_H

#include "../core/rng.h"
#include "../sim/people.h"
#include "../sim/player.h"

// Бродящие по залу (People, ROLE_WANDER): ходят шагом от точки к точке, у каждого лист и оружие —
// на них можно напасть, и они отобьются
void crowd_add(People *p, Rng *rng, u32 count, GroundFn ground);
// Зрители арены (ROLE_WATCHER, не больше RING_SPECTATORS): стоят полукругом за оградой ринга лицом к нему,
// у некоторых факелы
void watchers_add(People *p, V3 center, f32 floor_y, u32 count, u8 team, GroundFn ground);
// Мобы (ROLE_MOB): люди со снаряжением ходят кругом вокруг локации, на углах стоят;
// одни шагом, другие трусцой, один бегом. Лист у каждого полный, как у своего отряда
void mobs_add(People *p, Rng *rng, GroundFn ground);

#endif
