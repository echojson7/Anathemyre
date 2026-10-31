// Замеры без окна (--bench, data/debug/bench.txt): бои арены подряд
#include "bench.h"

#include <stdio.h>

#include "../core/clock.h"
#include "../defs/defs.h"
#include "../sim/arena.h"
// printf считает байты, а не буквы: дополнить строку UTF-8 пробелами до ширины
static void print_pad(const char *s, u32 width)
{
    u32 n = 0;
    for (const char *p = s; *p; p++) n += ((u8)*p & 0xC0) != 0x80;
    printf("%s", s);
    for (; n < width; n++) putchar(' ');
}

static void print_roster(const ArenaSession *s)
{
    printf("\n");
    print_pad("ИМЯ", 10);
    printf("  БОИ  ПОБ | АТК  ЗАЩ  УКЛ  СИЛ СТОЙ ОРУЖ | БЫЛО                          | ОРУЖИЕ    СОСТОЯНИЕ\n");
    for (u32 k = 0; k < ARENA_ROSTER; k++) {
        const ArenaFighter *f = &s->roster[k];
        const FighterDesc *d = &f->d;
        u32 wc = SK_WEAPON + g_weapons[d->weapon].wclass;
        print_pad(arena_roster_name(s, k), 10);
        printf(" %4u %4u | %4.0f %4.0f %4.0f %4.0f %4.0f %4.0f | %4.0f %4.0f %4.0f %4.0f %4.0f %4.0f | ",
               f->bouts, f->wins,
               (f64)d->skill[SK_ATTACK], (f64)d->skill[SK_DEFENSE], (f64)d->skill[SK_DODGE],
               (f64)d->skill[SK_STRENGTH], (f64)d->skill[SK_TOUGHNESS], (f64)d->skill[wc],
               (f64)f->start[SK_ATTACK], (f64)f->start[SK_DEFENSE], (f64)f->start[SK_DODGE],
               (f64)f->start[SK_STRENGTH], (f64)f->start[SK_TOUGHNESS], (f64)f->start[wc]);
        print_pad(g_weapons[d->weapon].name, 10);
        printf("%s\n", f->gone ? "ВЫБЫЛ" : (d->fracture ? "ПЕРЕЛОМ" : ""));
    }
}

int arena_bench(u32 bouts, u64 seed, u32 log_bouts)
{
    Arena mem;
    if (!arena_init(&mem, MB(4))) return 1;
    ArenaSession *s = ARENA_PUSH_ARRAY(&mem, ArenaSession, 1);
    arena_session_init(s, &mem, seed);
    s->log_bouts = log_bouts;
    f64 t0 = time_now();

    printf("АРЕНА: %u БОЁВ, SEED %llu\n", bouts, (unsigned long long)seed);
    while (s->bout < bouts) {
        if (!arena_bout_begin(s)) continue;  // все отдыхают
        while (!arena_bout_tick(s)) {}
        arena_bout_end(s);
    }
    f64 secs = time_now() - t0;
    const ArenaStats *st = &s->st;
    printf("\nИТОГ\n");
    printf("поединков %u, схваток 4×4 %u, ничьих %u\n", st->duels, st->groups, st->draws);
    static const char *bk_name[3] = { "разница силы < 5  ", "разница силы 5–15 ", "разница силы > 15 " };
    for (u32 k = 0; k < 3; k++)
        if (st->bucket_n[k])
            printf("%s: сильный победил %u из %u (%.0f%%)\n", bk_name[k], st->bucket_won[k], st->bucket_n[k],
                   100.0 * st->bucket_won[k] / st->bucket_n[k]);
    if (st->duels)
        printf("длительность поединка: средняя %.1f с, от %.1f до %.1f с\n", (f64)(st->dur_sum / (f32)st->duels),
               (f64)st->dur_min, (f64)st->dur_max);
    if (st->groups) printf("длительность схватки: средняя %.1f с\n", (f64)(st->gdur_sum / (f32)st->groups));
    printf("проигравшие: без сознания %u, сдались %u, не могут стоять %u, мертвы %u\n",
           st->lose_ko, st->lose_surrender, st->lose_crawl, st->lose_dead);
    printf("попаданий %u, блоков %u, уклонений %u, контратак %u, сбиты с ног %u\n",
           st->hits, st->blocks, st->dodges, st->counters, st->knockdowns);
    printf("переломов %u, отсечений %u, выронили оружие %u, ударов по своим %u, замен в ростере %u\n",
           st->fractures, st->severs, st->disarms, st->ally_hits, st->replaced);
    printf("смертей %u, из них после боя от потери крови %u\n", st->deaths, st->deaths_after);
    print_roster(s);
    printf("\nтиков %llu за %.3f с (%.2f мкс на тик боя)\n", (unsigned long long)s->ticks, secs,
           s->ticks ? secs * 1e6 / (f64)s->ticks : 0.0);
    printf("контрольная сумма журнала: %016llx\n", (unsigned long long)s->hash);
    arena_free(&mem);
    return 0;
}
