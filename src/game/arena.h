// Арена: ростер бойцов, подбор пар, поединки и схватки 4 на 4, журнал.
#ifndef ANA_ARENA_GAME_H
#define ANA_ARENA_GAME_H

#include "../core/types.h"

// Прогнать N боёв без окна и напечатать статистику; log_bouts — сколько первых боёв печатать подробно
int arena_bench(u32 bouts, u64 seed, u32 log_bouts);

#endif
