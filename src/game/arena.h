// Арена: ростер бойцов, подбор пар, поединки и схватки 4 на 4, журнал.
// Один и тот же код ведёт бои и в бенчмарке без окна, и на экране.
#ifndef ANA_ARENA_GAME_H
#define ANA_ARENA_GAME_H

#include "../ai/fighter.h"
#include "../core/rng.h"
#include "../sim/combat.h"

#define ARENA_ROSTER 12
#define ARENA_RADIUS 7.0f
#define ARENA_MAX_BOUT 8   // бойцов в одном бою

typedef struct ArenaFighter {
    FighterDesc d;
    f32 start[SK_COUNT];
    u32 name;
    u32 bouts, wins;
    bool gone;  // погиб или покалечен, ушёл из ростера
} ArenaFighter;

typedef struct ArenaStats {
    u32 duels, groups, draws;
    u32 bucket_n[3], bucket_won[3];  // |Δсилы| < 5, 5..15, > 15: сколько выиграл сильный
    f32 dur_sum, dur_min, dur_max;
    f32 gdur_sum;
    u32 lose_ko, lose_surrender, lose_crawl, lose_dead;
    u32 deaths, deaths_after, disarms, severs, fractures, ally_hits, knockdowns, hits, blocks, dodges, counters;
    u32 replaced;
} ArenaStats;

typedef struct ArenaSession {
    CombatWorld w;
    FighterAI ai;
    Rng rng;
    u64 seed;
    u32 next_name;
    ArenaFighter roster[ARENA_ROSTER];
    ArenaStats st;
    u64 hash, ticks;
    u32 log_bouts;       // сколько первых боёв печатать подробно
    f32 after_bout_s;    // сколько тела лежат после исхода, прежде чем бой закрыт

    // Текущий бой
    u32 bout;            // номер боя с нуля
    u32 n;
    u32 idx[ARENA_MAX_BOUT];  // участники: номера в ростере
    u8  team[ARENA_MAX_BOUT];
    bool group, log;
    f32 rating[2];
    u32 decided;         // тик исхода, 0 — бой идёт
    u32 end_tick;
    i32 winner;
} ArenaSession;

bool arena_session_init(ArenaSession *s, Arena *mem, u64 seed);
// Начать следующий бой. false — все отдыхают, вызвать ещё раз
bool arena_bout_begin(ArenaSession *s);
// Один тик боя; true — бой закрыт и его надо завершить arena_bout_end
bool arena_bout_tick(ArenaSession *s);
void arena_bout_end(ArenaSession *s);
// Имя бойца по его номеру в текущем бою
const char *arena_name(const ArenaSession *s, u32 fighter);
// Имя бойца по номеру в ростере
const char *arena_roster_name(const ArenaSession *s, u32 roster);

// Прогнать N боёв без окна и напечатать статистику; log_bouts — сколько первых боёв печатать подробно
int arena_bench(u32 bouts, u64 seed, u32 log_bouts);
// Случайный боец уровня tier: 0 новичок, 1 бывалый, 2 ветеран
void arena_random_fighter(FighterDesc *d, Rng *r, u32 tier);

#endif
