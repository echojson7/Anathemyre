// Замеры без окна (--bench, data/debug/bench.txt): отряды вдали на случайной карте
#include "bench.h"

#include <stdio.h>

#include <math.h>

#include "../core/clock.h"
#include "../core/rng.h"
#include "../sim/bands.h"
// Случайный мир для замера: места по всей карте, каждое связано с ближайшим из прежних (все достижимы)
// и с двумя ближайшими вообще; команды враждуют через одну пару из трёх
int bands_bench(u32 count, f32 hours, u64 seed, u32 phases, u32 log_events)
{
    if (!count || count > BANDS_MAX || hours <= 0.0f) {
        fprintf(stderr, "bands-bench: от 1 до %u отрядов, часов больше 0\n", BANDS_MAX);
        return 1;
    }
    Arena mem;
    if (!arena_init(&mem, MB(8))) return 1;
    Places *pl = ARENA_PUSH_ARRAY(&mem, Places, 1);
    Bands *b = ARENA_PUSH_ARRAY(&mem, Bands, 1);
    Rng rng;
    rng_seed(&rng, seed, 11);
    const u32 places = 120, teams = 8;
    // Случайные числа — отдельными операторами: порядок вычисления аргументов у компиляторов разный
    for (u32 i = 0; i < places; i++) {
        f32 x = rng_range(&rng, -13000, 13000);
        f32 z = rng_range(&rng, -13000, 13000);
        places_add(pl, NULL, x, z);
    }
    for (u32 i = 0; i < places; i++) {
        u32 best[3] = { PLACE_NONE, PLACE_NONE, PLACE_NONE };
        f32 bd[3] = { INFINITY, INFINITY, INFINITY };
        for (u32 j = 0; j < places; j++) {
            if (j == i) continue;
            f32 d = places_dist(pl, i, j);
            if (j < i && d < bd[0]) {
                bd[0] = d;
                best[0] = j;
            }
            if (d < bd[1]) {
                bd[2] = bd[1];
                best[2] = best[1];
                bd[1] = d;
                best[1] = j;
            } else if (d < bd[2]) {
                bd[2] = d;
                best[2] = j;
            }
        }
        for (u32 k = 0; k < 3; k++)
            if (best[k] != PLACE_NONE) places_road(pl, i, best[k]);
    }
    if (!places_build(pl, &mem)) return 1;
    bands_init(b, &mem, pl, seed, phases);
    for (u32 a = 0; a < teams; a++)
        for (u32 c = a + 1; c < teams; c++)
            if (rng_below(&rng, 3) == 0) bands_hostile(b, (u8)a, (u8)c);
    u32 men_start = 0;
    for (u32 i = 0; i < count; i++) {
        u16 men[BAND_TIERS];
        men[0] = (u16)(3 + rng_below(&rng, 18));
        men[1] = (u16)rng_below(&rng, 11);
        men[2] = (u16)rng_below(&rng, 5);
        u8 team = (u8)rng_below(&rng, teams);
        bands_add(b, team, rng_below(&rng, places), men);
        men_start += (u32)men[0] + men[1] + men[2];
    }
    b->log_left = log_events;

    // Время шага: гистограмма по 0.1 мкс до 1 мс
    enum { BINS = 10000 };
    u32 *hist = ARENA_PUSH_ARRAY(&mem, u32, BINS);
    u64 ticks = (u64)((f64)hours * 3600.0 * phases);
    f64 total = 0.0, worst = 0.0;
    for (u64 t = 0; t < ticks; t++) {
        f64 t0 = time_now();
        bands_tick(b);
        f64 d = time_now() - t0;
        total += d;
        if (d > worst) worst = d;
        u32 bin = (u32)(d * 1e7);
        hist[bin < BINS ? bin : BINS - 1]++;
    }
    u64 acc = 0, p99 = 0;
    for (u32 k = 0; k < BINS; k++) {
        acc += hist[k];
        if (acc * 100 >= ticks * 99) {
            p99 = k;
            break;
        }
    }
    u32 alive = 0;
    for (u32 i = 0; i < b->count; i++) alive += b->state[i] != BAND_GONE;
    printf("ОТРЯДЫ ВДАЛИ: %u отрядов, %u мест, %u дорог, %.1f ч мира, SEED %llu\n", count, places, pl->roads, (f64)hours,
           (unsigned long long)seed);
    printf("шаг мира: средний %.2f мкс, 99%% %.1f мкс, худший %.1f мкс; весь замер %.2f с\n", total * 1e6 / (f64)ticks,
           (f64)p99 * 0.1, worst * 1e6, total);
    printf("стычек %u, бежали %u, истреблено отрядов %u (осталось %u), погибло %llu из %u\n", b->fights, b->routs, b->wiped,
           alive, (unsigned long long)b->dead, men_start);
    printf("сумма %016llx\n", (unsigned long long)bands_checksum(b));
    arena_free(&mem);
    return 0;
}
