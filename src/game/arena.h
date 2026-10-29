// Арена: ростер бойцов, подбор пар, поединки и схватки 4 на 4, журнал.
#ifndef ANA_ARENA_GAME_H
#define ANA_ARENA_GAME_H

#include "../core/rng.h"
#include "../sim/combat.h"

// Прогнать N боёв без окна и напечатать статистику; log_bouts — сколько первых боёв печатать подробно
int arena_bench(u32 bouts, u64 seed, u32 log_bouts);
// Случайный боец уровня tier: 0 новичок, 1 бывалый, 2 ветеран
void arena_random_fighter(FighterDesc *d, Rng *r, u32 tier);

#endif
