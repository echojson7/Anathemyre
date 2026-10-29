#include "battle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ai/fighter.h"
#include "../core/arena.h"
#include "../core/clock.h"
#include "../core/jobs.h"
#include "../core/rng.h"
#include "../sim/combat.h"
#include "arena.h"

#define BATTLE_LIMIT_S 600.0f
#define TICK_BUDGET_MS 1.5
#define RANK_WIDTH 20     // бойцов в шеренге
#define FILE_GAP 1.3f     // между соседями в шеренге, м
#define RANK_GAP 1.6f     // между шеренгами, м
#define FRONT_DIST 12.0f  // от середины поля до первой шеренги, м

enum { LEFT_STANDING, LEFT_DOWN, LEFT_SURRENDER, LEFT_DEAD, LEFT_COUNT };

typedef struct BattleResult {
    u64 hash;
    u32 ticks;
    i32 winner;
    f64 secs, tick_max;
    f32 tick_p99;  // мс
    u32 left[2][LEFT_COUNT];
    u32 hits, blocks, dodges, ally_hits, knockdowns;
} BattleResult;

static void place_army(CombatWorld *w, Rng *rng, u8 side, u32 n)
{
    for (u32 k = 0; k < n; k++) {
        FighterDesc d;
        // Половина новички, треть бывалые, остальные ветераны
        f32 roll = rng_f32(rng);
        arena_random_fighter(&d, rng, roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2));
        u32 rank = k / RANK_WIDTH, file = k % RANK_WIDTH;
        u32 in_rank = MIN(RANK_WIDTH, n - rank * RANK_WIDTH);
        f32 x = ((f32)file - 0.5f * (f32)(in_rank - 1)) * FILE_GAP + rng_range(rng, -0.2f, 0.2f);
        f32 z = FRONT_DIST + (f32)rank * RANK_GAP + rng_range(rng, -0.2f, 0.2f);
        d.team = side;
        d.pos = v3(x, 0, side == 0 ? -z : z);
        d.heading = side == 0 ? 0.0f : PI_F;
        combat_add(w, &d);
    }
}

static u64 hash_u32(u64 h, u32 v) { return (h ^ v) * 0x100000001B3ull; }

static u32 f32_bits(f32 f)
{
    u32 u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static int cmp_f32(const void *a, const void *b)
{
    f32 x = *(const f32 *)a, y = *(const f32 *)b;
    return (x > y) - (x < y);
}

// tick_ms — место под время каждого тика (BATTLE_LIMIT_S * COMBAT_HZ)
static void battle_run(CombatWorld *w, FighterAI *ai, f32 *tick_ms, u32 na, u32 nb, u64 seed, BattleResult *res)
{
    memset(res, 0, sizeof *res);
    combat_reset(w, seed);
    w->mercy = true;
    w->halted = false;
    w->fence_r = 0.0f;
    Rng rng;
    rng_seed(&rng, seed, 11);
    place_army(w, &rng, 0, na);
    place_army(w, &rng, 1, nb);

    u64 h = 0xcbf29ce484222325ull;
    res->winner = -1;
    u32 limit = (u32)(BATTLE_LIMIT_S * COMBAT_HZ);
    for (u32 t = 0; t < limit; t++) {
        f64 t0 = time_now();
        fighter_think_all(ai, w);
        combat_tick(w);
        f64 dt = time_now() - t0;
        res->secs += dt;
        if (dt > res->tick_max) res->tick_max = dt;
        tick_ms[t] = (f32)(dt * 1e3);

        h = combat_hash_events(w, h);
        for (u32 k = 0; k < w->ev_count; k++) {
            const CombatEvent *e = &w->ev[k];
            switch (e->kind) {
            case EV_HIT:
                res->hits++;
                if (e->flags & EVF_ALLY) res->ally_hits++;
                break;
            case EV_BLOCK:     res->blocks++; break;
            case EV_DODGE:     res->dodges++; break;
            case EV_KNOCKDOWN: res->knockdowns++; break;
            default: break;
            }
        }
        w->ev_count = 0;
        u32 a0 = combat_team_active(w, 0), a1 = combat_team_active(w, 1);
        if (a0 == 0 || a1 == 0) {
            res->winner = a0 > 0 ? 0 : (a1 > 0 ? 1 : -1);
            break;
        }
    }
    res->ticks = w->tick;
    // 99-й перцентиль: одиночные выбросы от планировщика ОС не заслоняют картину
    if (res->ticks) {
        qsort(tick_ms, res->ticks, sizeof *tick_ms, cmp_f32);
        res->tick_p99 = tick_ms[(res->ticks - 1) * 99 / 100];
    }

    // Итоговое состояние тоже в сумму: позиции и кровь каждого
    for (u32 i = 0; i < w->count; i++) {
        h = hash_u32(h, f32_bits(w->x[i]));
        h = hash_u32(h, f32_bits(w->z[i]));
        h = hash_u32(h, f32_bits(w->blood[i]));
        h = hash_u32(h, w->state[i]);
        u8 s = w->state[i];
        u32 k = s == FS_DEAD ? LEFT_DEAD
              : s == FS_SURRENDER ? LEFT_SURRENDER
              : (s == FS_UNCONSCIOUS || s == FS_CRAWL) ? LEFT_DOWN : LEFT_STANDING;
        res->left[w->team[i]][k]++;
    }
    res->hash = h;
}

static void fmt_time(char *buf, u32 ticks)
{
    f32 t = (f32)ticks * COMBAT_DT;
    sprintf(buf, "%u:%05.2f", (u32)(t / 60.0f), (f64)fmodf(t, 60.0f));
}

static void print_result(const BattleResult *r)
{
    char tb[16];
    fmt_time(tb, r->ticks);
    const char *who = r->winner == 0 ? "ПЕРВАЯ СТОРОНА" : (r->winner == 1 ? "ВТОРАЯ СТОРОНА" : "НИКТО");
    printf("исход: %s, %s боя (%u тиков)\n", who, tb, r->ticks);
    for (u32 s = 0; s < 2; s++)
        printf("сторона %u: в строю %u, лежат %u, сдались %u, мертвы %u\n", s + 1, r->left[s][LEFT_STANDING],
               r->left[s][LEFT_DOWN], r->left[s][LEFT_SURRENDER], r->left[s][LEFT_DEAD]);
    printf("попаданий %u (по своим %u), блоков %u, уклонений %u, сбиты с ног %u\n", r->hits, r->ally_hits,
           r->blocks, r->dodges, r->knockdowns);
    f64 avg = r->ticks ? r->secs * 1e3 / (f64)r->ticks : 0.0;
    printf("тик: средний %.3f мс, 99%% тиков быстрее %.3f мс, худший %.3f мс (бюджет %.1f мс)\n", avg,
           (f64)r->tick_p99, r->tick_max * 1e3, TICK_BUDGET_MS);
    printf("контрольная сумма: %016llx\n", (unsigned long long)r->hash);
}

int battle_bench(u32 side_a, u32 side_b, u64 seed, u32 threads, bool verify)
{
    u32 cap = side_a + side_b;
    if (cap == 0 || cap > 65535) {
        fprintf(stderr, "battle-bench: от 1 до 65535 бойцов\n");
        return 1;
    }
    Arena mem;
    if (!arena_init(&mem, MB(4) + (size_t)cap * KB(2))) return 1;
    CombatWorld w;
    combat_init(&w, &mem, cap, 16384);
    FighterAI ai;
    fighter_ai_init(&ai, &mem, cap);
    f32 *tick_ms = ARENA_PUSH_ARRAY(&mem, f32, (u32)(BATTLE_LIMIT_S * COMBAT_HZ));

    int rc = 0;
    if (!verify) {
        if (!jobs_init(threads)) {
            fprintf(stderr, "battle-bench: не удалось запустить потоки\n");
            arena_free(&mem);
            return 1;
        }
        printf("ПОЛЕВОЙ БОЙ %u × %u, SEED %llu, ПОТОКОВ %u\n", side_a, side_b, (unsigned long long)seed, jobs_threads());
        BattleResult r;
        battle_run(&w, &ai, tick_ms, side_a, side_b, seed, &r);
        print_result(&r);
        jobs_shutdown();
    } else {
        static const u32 counts[] = { 1, 2, 4, 8 };
        u64 first = 0;
        bool same = true;
        printf("ПРОВЕРКА ДЕТЕРМИНИЗМА: %u × %u, SEED %llu\n", side_a, side_b, (unsigned long long)seed);
        for (u32 k = 0; k < ARRAY_COUNT(counts); k++) {
            if (!jobs_init(counts[k])) {
                fprintf(stderr, "battle-bench: не удалось запустить потоки\n");
                rc = 1;
                break;
            }
            BattleResult r;
            battle_run(&w, &ai, tick_ms, side_a, side_b, seed, &r);
            printf("потоков %u: тиков %u, тик средний %.3f мс, 99%% %.3f мс, худший %.3f мс, сумма %016llx\n",
                   jobs_threads(), r.ticks, r.ticks ? r.secs * 1e3 / (f64)r.ticks : 0.0, (f64)r.tick_p99,
                   r.tick_max * 1e3, (unsigned long long)r.hash);
            if (k == 0) first = r.hash;
            else if (r.hash != first) same = false;
            jobs_shutdown();
        }
        if (!rc) {
            printf("%s\n", same ? "ДЕТЕРМИНИЗМ: СОВПАДАЕТ НА ВСЕХ ЧИСЛАХ ПОТОКОВ." : "ДЕТЕРМИНИЗМ: РАСХОЖДЕНИЕ.");
            rc = same ? 0 : 2;
        }
    }
    arena_free(&mem);
    return rc;
}
