#include "arena.h"

#include <stdio.h>
#include <string.h>

#include "../ai/fighter.h"
#include "../core/arena.h"
#include "../core/clock.h"
#include "../core/rng.h"
#include "../sim/combat.h"

#define ROSTER 12
#define ARENA_RADIUS 7.0f
#define BOUT_LIMIT_S 180.0f
#define AFTER_BOUT_S 45.0f   // тела лежат, кровь течёт, кто-то приходит в себя
#define REST_S 300.0f        // отдых между боями
#define BANDAGE_S 10.0f      // через столько после исхода раны перетягивают

static const char *g_names[] = {
    "ЖДАН", "КСАНФ", "СЕРВИЙ", "ВЫШАТА", "АГАФОН", "ДЕЦИМ", "ГОРАЗД", "КЛЕОН", "НУМЕРИЙ", "ПУТЯТА",
    "ФОКА", "ТИТ", "МСТИША", "ЛИСАНДР", "КАССИЙ", "ТВЕРДИЛО", "ФЕАГЕН", "СЕКСТ", "НЕЖАТА", "ЕВДОКС",
    "АВЛ", "ОСТРОМИР", "ГЕРМОН", "СПУРИЙ", "РАТИБОР", "ЕВМЕН", "КВИНТ", "БУДИМИР", "ФИЛОН", "ЛУЦИЙ",
    "ХОТЕН", "ДАМОН", "ГАЙ", "МЕСТЯТА", "ПАРАМОН", "ПОСТУМ", "СМЕЯН", "ДИОКЛ", "ВИБИЙ", "ЯРОПОЛК",
};

typedef struct ArenaFighter {
    FighterDesc d;
    f32 start[SK_COUNT];
    u32 name;
    u32 bouts, wins;
    bool gone;  // погиб или покалечен, ушёл из ростера
} ArenaFighter;

typedef struct Stats {
    u32 duels, groups, draws;
    u32 bucket_n[3], bucket_won[3];  // |Δсилы| < 5, 5..15, > 15: сколько выиграл сильный
    f32 dur_sum, dur_min, dur_max;
    f32 gdur_sum;
    u32 lose_ko, lose_surrender, lose_crawl, lose_dead;
    u32 deaths, deaths_after, disarms, surrenders_morale, severs, fractures, ally_hits, knockdowns, hits, blocks, dodges, counters;
    u32 replaced;
} Stats;

static u32 g_next_name;

static f32 jitter(Rng *r, f32 base, f32 spread) { return clampf(base + rng_range(r, -spread, spread), 1.0f, 100.0f); }

static void make_fighter(ArenaFighter *f, Rng *r, u32 tier)
{
    static const f32 tier_skill[3] = { 9.0f, 32.0f, 62.0f };
    memset(f, 0, sizeof *f);
    f32 b = tier_skill[tier];
    FighterDesc *d = &f->d;
    for (u32 s = 0; s < SK_COUNT; s++) d->skill[s] = jitter(r, b * 0.4f, 3.0f);
    d->skill[SK_ATTACK] = jitter(r, b, 5.0f);
    d->skill[SK_DEFENSE] = jitter(r, b, 5.0f);
    d->skill[SK_DODGE] = jitter(r, b * 0.8f, 5.0f);
    d->skill[SK_STRENGTH] = jitter(r, b * 0.9f + 8.0f, 6.0f);
    d->skill[SK_DEXTERITY] = jitter(r, b * 0.8f + 5.0f, 6.0f);
    d->skill[SK_TOUGHNESS] = jitter(r, b * 0.9f, 5.0f);
    d->skill[SK_ATHLETICS] = jitter(r, b * 0.7f + 5.0f, 5.0f);
    d->weapon = (u8)rng_below(r, W_COUNT);
    // Балка только тем, кто её поднимет
    if (d->weapon == W_BALKA && tier == 0) d->weapon = W_ARMATURA;
    d->skill[SK_WEAPON + g_weapons[d->weapon].wclass] = jitter(r, b + 5.0f, 5.0f);
    static const u8 q[3][2] = { { Q_JUNK, Q_WORN }, { Q_WORN, Q_SOUND }, { Q_SOUND, Q_MASTER } };
    d->weapon_q = q[tier][rng_below(r, 2)];
    if (tier == 2 && rng_f32(r) < 0.2f) d->weapon_q = Q_ANCIENT;
    for (u32 s = 0; s < SLOT_COUNT; s++) d->armor[s] = ARMOR_NONE;
    d->armor[SLOT_TORSO_UNDER] = A_RAGS;
    if (tier >= 1) {
        d->armor[SLOT_TORSO_UNDER] = rng_f32(r) < 0.5f ? A_QUILT : A_RAGS;
        d->armor[SLOT_TORSO_ARMOR] = A_JACKET;
        if (rng_f32(r) < 0.5f) d->armor[SLOT_HEAD] = A_BUCKET;
    }
    if (tier == 2) {
        d->armor[SLOT_TORSO_ARMOR] = rng_f32(r) < 0.6f ? A_PLATE : A_JACKET;
        d->armor[SLOT_HEAD] = A_BUCKET;
        if (rng_f32(r) < 0.5f) d->armor[SLOT_LEGS] = A_GREAVES;
    }
    d->mass = rng_range(r, 60.0f, 90.0f);
    d->height = rng_range(r, 1.70f, 1.90f);
    d->morale_base = rng_range(r, 50.0f, 65.0f) + 10.0f * (f32)tier;
    memcpy(f->start, d->skill, sizeof f->start);
    f->name = g_next_name++ % ARRAY_COUNT(g_names);
}

// Следующее имя, которого нет среди живых в ростере
static void rename_unique(ArenaFighter *roster, u32 k)
{
    for (u32 tries = 0; tries < ARRAY_COUNT(g_names); tries++) {
        bool taken = false;
        for (u32 o = 0; o < ROSTER; o++)
            if (o != k && !roster[o].gone && roster[o].name == roster[k].name) taken = true;
        if (!taken) return;
        roster[k].name = g_next_name++ % ARRAY_COUNT(g_names);
    }
}

static f32 blood_max(const FighterDesc *d) { return 100.0f * d->mass / 75.0f; }

static bool fresh(const FighterDesc *d) { return d->blood <= 0.0f; }

static bool available(const ArenaFighter *f)
{
    if (f->gone) return false;
    const FighterDesc *d = &f->d;
    if (fresh(d)) return true;
    for (u32 p = 0; p < PART_COUNT; p++) {
        if (p <= PART_STOMACH && d->hp[p] < 0.7f * g_part_hp[p]) return false;
        if (d->hp[p] <= 0.0f) return false;
    }
    return d->blood >= 0.8f * blood_max(d);
}

static void rest(ArenaFighter *roster, f32 secs)
{
    for (u32 k = 0; k < ROSTER; k++) {
        FighterDesc *d = &roster[k].d;
        if (fresh(d)) continue;
        bool healed = true;
        for (u32 p = 0; p < PART_COUNT; p++) {
            if (d->severed & (1u << p)) continue;
            f32 rate = (d->fracture & (1u << p)) ? 0.025f : 0.1f;
            d->hp[p] = fminf(g_part_hp[p], d->hp[p] + rate * secs);
            if (d->hp[p] >= g_part_hp[p]) d->fracture &= (u8)~(1u << p);
            else healed = false;
        }
        d->blood = fminf(blood_max(d), d->blood + 0.1f * secs);
        if (healed && d->blood >= blood_max(d) && !d->severed) d->blood = 0.0f;  // снова здоров
    }
}

static void fmt_time(char *buf, u32 tick)
{
    f32 t = (f32)tick * COMBAT_DT;
    sprintf(buf, "%02u:%05.2f", (u32)(t / 60.0f), (f64)fmodf(t, 60.0f));
}

static void print_event(const CombatEvent *e, const u32 *names)
{
    char tb[16];
    fmt_time(tb, e->tick);
    const char *a = g_names[names[e->a]], *b = g_names[names[e->b]];
    switch (e->kind) {
    case EV_HIT:
        printf("[%s] %s → %s: %s, %s.", tb, a, b, g_attack_name[e->arg], g_part_name[e->part]);
        if (e->cut >= 0.5f) printf(" РЕЗ %.0f.", (f64)e->cut);
        if (e->blunt >= 0.5f) printf(" ДРОБЛ %.0f.", (f64)e->blunt);
        if (e->pierce >= 0.5f) printf(" УКОЛ %.0f.", (f64)e->pierce);
        if (e->flags & EVF_FRACTURE) printf(" ПЕРЕЛОМ.");
        if (e->flags & EVF_SEVER) printf(" ОТСЕЧЕНИЕ.");
        if (e->flags & EVF_STAGGER) printf(" ОШЕЛОМЛЁН.");
        if (e->flags & EVF_ALLY) printf(" ПО СВОЕМУ.");
        printf("\n");
        break;
    case EV_BLOCK:
        printf("[%s] %s: БЛОК ПРОТИВ %s.%s\n", tb, b, a, (e->flags & EVF_STAGGER) ? " ОШЕЛОМЛЁН." : "");
        break;
    case EV_DODGE:     printf("[%s] %s: УКЛОНИЛСЯ ОТ %s.\n", tb, b, a); break;
    case EV_COUNTER:   printf("[%s] %s: КОНТРАТАКА.\n", tb, a); break;
    case EV_KNOCKDOWN: printf("[%s] %s: СБИТ С НОГ.\n", tb, b); break;
    case EV_FALL:      printf("[%s] %s: УПАЛ.\n", tb, a); break;
    case EV_KO:        printf("[%s] %s: БЕЗ СОЗНАНИЯ.\n", tb, a); break;
    case EV_DEATH:     printf("[%s] %s: МЁРТВ.\n", tb, a); break;
    case EV_DISARM:    printf("[%s] %s: ВЫРОНИЛ ОРУЖИЕ.\n", tb, a); break;
    case EV_RETREAT:   printf("[%s] %s: ОТСТУПАЕТ.\n", tb, a); break;
    case EV_SURRENDER: printf("[%s] %s: СДАЁТСЯ.\n", tb, a); break;
    case EV_GETUP:     printf("[%s] %s: ПОДНЯЛСЯ.\n", tb, a); break;
    default: break;
    }
}

static void count_event(Stats *st, const CombatEvent *e)
{
    switch (e->kind) {
    case EV_HIT:
        st->hits++;
        if (e->flags & EVF_ALLY) st->ally_hits++;
        if (e->flags & EVF_SEVER) st->severs++;
        if (e->flags & EVF_FRACTURE) st->fractures++;
        break;
    case EV_BLOCK:     st->blocks++; break;
    case EV_DODGE:     st->dodges++; break;
    case EV_COUNTER:   st->counters++; break;
    case EV_KNOCKDOWN: st->knockdowns++; break;
    case EV_DEATH:     st->deaths++; break;
    case EV_DISARM:    st->disarms++; break;
    default: break;
    }
}

// Один бой: участники idx[0..n), команды по team[]; возвращает победившую команду или -1
static i32 run_bout(CombatWorld *w, ArenaFighter *roster, const u32 *idx, const u8 *team, u32 n,
                    u64 seed, bool log, Stats *st, u64 *hash, f32 *dur, u64 *ticks)
{
    combat_reset(w, seed);
    w->mercy = true;
    w->halted = false;
    w->fence_r = ARENA_RADIUS;
    u32 names[16];
    u32 slot[2] = { 0, 0 }, per_team[2] = { 0, 0 };
    for (u32 k = 0; k < n; k++) per_team[team[k]]++;
    for (u32 k = 0; k < n; k++) {
        FighterDesc d = roster[idx[k]].d;
        u8 t = team[k];
        f32 side = t == 0 ? -1.0f : 1.0f;
        f32 row = (f32)slot[t]++ - 0.5f * (f32)(per_team[t] - 1);
        d.team = t;
        d.pos = v3(side * (n > 2 ? 3.0f : 2.5f), 0, row * 1.5f);
        d.heading = t == 0 ? PI_F * 0.5f : -PI_F * 0.5f;
        names[combat_add(w, &d)] = roster[idx[k]].name;
    }

    i32 winner = -1;
    u32 end_tick = (u32)((BOUT_LIMIT_S + AFTER_BOUT_S) * COMBAT_HZ);
    u32 decided = 0;
    for (u32 t = 0; t < end_tick; t++) {
        fighter_think_all(w);
        combat_tick(w);
        *hash = combat_hash_events(w, *hash);
        for (u32 k = 0; k < w->ev_count; k++) {
            count_event(st, &w->ev[k]);
            if (decided && w->ev[k].kind == EV_DEATH) st->deaths_after++;
            if (log) print_event(&w->ev[k], names);
        }
        w->ev_count = 0;
        if (decided && t == decided + (u32)(BANDAGE_S * COMBAT_HZ))
            for (u32 k = 0; k < w->count; k++) w->bleed[k] = 0.0f;
        if (!decided) {
            u32 a0 = combat_team_active(w, 0), a1 = combat_team_active(w, 1);
            if (a0 == 0 || a1 == 0 || t + 1 >= (u32)(BOUT_LIMIT_S * COMBAT_HZ)) {
                decided = t + 1;
                winner = a0 == 0 && a1 > 0 ? 1 : (a1 == 0 && a0 > 0 ? 0 : -1);
                w->halted = true;
                end_tick = MIN(end_tick, decided + (u32)(AFTER_BOUT_S * COMBAT_HZ));
                if (log) {
                    char tb[16];
                    fmt_time(tb, w->tick);
                    printf("[%s] ИСХОД ЗАФИКСИРОВАН. %s\n", tb, winner < 0 ? "НИЧЬЯ." : (winner == 0 ? "ПЕРВАЯ СТОРОНА." : "ВТОРАЯ СТОРОНА."));
                }
            }
        }
    }
    *dur = (f32)decided * COMBAT_DT;
    *ticks += w->tick;

    for (u32 k = 0; k < n; k++) {
        ArenaFighter *f = &roster[idx[k]];
        combat_store(w, k, &f->d);
        f->bouts++;
        if (winner == (i32)team[k]) f->wins++;
        u8 s = w->state[k];
        if (winner >= 0 && team[k] != (u8)winner) {
            if (s == FS_DEAD) st->lose_dead++;
            else if (s == FS_UNCONSCIOUS) st->lose_ko++;
            else if (s == FS_CRAWL) st->lose_crawl++;
            else st->lose_surrender++;
        }
        if (s == FS_DEAD || f->d.severed) f->gone = true;
    }
    return winner;
}

// printf считает байты, а не буквы: дополнить строку UTF-8 пробелами до ширины
static void print_pad(const char *s, u32 width)
{
    u32 n = 0;
    for (const char *p = s; *p; p++) n += ((u8)*p & 0xC0) != 0x80;
    printf("%s", s);
    for (; n < width; n++) putchar(' ');
}

static void print_roster(const ArenaFighter *roster)
{
    printf("\n");
    print_pad("ИМЯ", 10);
    printf("  БОИ  ПОБ | АТК  ЗАЩ  УКЛ  СИЛ СТОЙ ОРУЖ | БЫЛО                          | ОРУЖИЕ    СОСТОЯНИЕ\n");
    for (u32 k = 0; k < ROSTER; k++) {
        const ArenaFighter *f = &roster[k];
        const FighterDesc *d = &f->d;
        u32 wc = SK_WEAPON + g_weapons[d->weapon].wclass;
        print_pad(g_names[f->name], 10);
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
    CombatWorld w;
    combat_init(&w, &mem, 16, 4096);
    Rng rng;
    rng_seed(&rng, seed, 7);
    g_next_name = 0;

    ArenaFighter roster[ROSTER];
    for (u32 k = 0; k < ROSTER; k++) make_fighter(&roster[k], &rng, k < 4 ? 0 : (k < 9 ? 1 : 2));

    Stats st;
    memset(&st, 0, sizeof st);
    st.dur_min = 1e9f;
    u64 hash = 0xcbf29ce484222325ull, ticks = 0;
    f64 t0 = time_now();

    printf("АРЕНА: %u БОЁВ, SEED %llu\n", bouts, (unsigned long long)seed);
    for (u32 b = 0; b < bouts; b++) {
        rest(roster, REST_S);
        // Замена выбывших новичками
        for (u32 k = 0; k < ROSTER; k++) {
            if (!roster[k].gone) continue;
            if (b < log_bouts) printf("[АРЕНА] %s ВЫБЫЛ. НА ЕГО МЕСТО: ", g_names[roster[k].name]);
            make_fighter(&roster[k], &rng, 0);
            rename_unique(roster, k);
            if (b < log_bouts) printf("%s.\n", g_names[roster[k].name]);
            st.replaced++;
        }
        u32 avail[ROSTER], na = 0;
        for (u32 k = 0; k < ROSTER; k++)
            if (available(&roster[k])) avail[na++] = k;
        // По числу боёв, затем по номеру
        for (u32 a = 1; a < na; a++)
            for (u32 c = a; c > 0 && roster[avail[c]].bouts < roster[avail[c - 1]].bouts; c--) {
                u32 tmp = avail[c]; avail[c] = avail[c - 1]; avail[c - 1] = tmp;
            }
        bool group = (b + 1) % 5 == 0 && na >= 8;
        if (na < 2) {
            b--;  // все отдыхают
            continue;
        }

        u32 idx[8];
        u8 team[8];
        u32 n;
        if (group) {
            // 8 самых свободных, змейкой по силе: 1,4,5,8 против 2,3,6,7
            for (u32 k = 0; k < 8; k++) idx[k] = avail[k];
            for (u32 a = 1; a < 8; a++)
                for (u32 c = a; c > 0 && combat_rating(&roster[idx[c]].d) > combat_rating(&roster[idx[c - 1]].d); c--) {
                    u32 tmp = idx[c]; idx[c] = idx[c - 1]; idx[c - 1] = tmp;
                }
            static const u8 snake[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };
            memcpy(team, snake, sizeof team);
            n = 8;
        } else {
            u32 a = avail[0], best = avail[1];
            f32 ra = combat_rating(&roster[a].d), bs = 1e9f;
            for (u32 k = 1; k < na; k++) {
                u32 c = avail[k];
                f32 sc = fabsf(combat_rating(&roster[c].d) - ra) + 2.0f * (f32)roster[c].bouts;
                if (sc < bs) { bs = sc; best = c; }
            }
            idx[0] = a; idx[1] = best;
            team[0] = 0; team[1] = 1;
            n = 2;
        }

        f32 r0 = combat_rating(&roster[idx[0]].d), r1 = n == 2 ? combat_rating(&roster[idx[1]].d) : 0.0f;
        bool log = b < log_bouts;
        if (log) {
            printf("\n[АРЕНА] %s %u.", group ? "СХВАТКА" : "ПОЕДИНОК", b + 1);
            for (u8 side = 0; side < 2; side++) {
                printf(side ? " —" : "");
                bool first = true;
                for (u32 k = 0; k < n; k++) {
                    if (team[k] != side) continue;
                    const FighterDesc *d = &roster[idx[k]].d;
                    printf("%s%s (АТК %.0f, %s %s)", first ? " " : ", ", g_names[roster[idx[k]].name],
                           (f64)d->skill[SK_ATTACK], g_quality_name[d->weapon_q], g_weapons[d->weapon].name);
                    first = false;
                }
            }
            printf("\n");
        }
        f32 dur;
        u64 bout_seed = seed * 0x9E3779B97F4A7C15ull + b * 0xBF58476D1CE4E5B9ull + 1;
        i32 win = run_bout(&w, roster, idx, team, n, bout_seed, log, &st, &hash, &dur, &ticks);
        if (win < 0) st.draws++;
        if (group) {
            st.groups++;
            st.gdur_sum += dur;
        } else {
            st.duels++;
            st.dur_sum += dur;
            st.dur_min = fminf(st.dur_min, dur);
            st.dur_max = fmaxf(st.dur_max, dur);
            f32 diff = fabsf(r0 - r1);
            u32 bk = diff < 5.0f ? 0 : (diff < 15.0f ? 1 : 2);
            if (win >= 0) {
                st.bucket_n[bk]++;
                if ((win == 0) == (r0 >= r1)) st.bucket_won[bk]++;
            }
        }
    }
    f64 secs = time_now() - t0;

    printf("\nИТОГ\n");
    printf("поединков %u, схваток 4×4 %u, ничьих %u\n", st.duels, st.groups, st.draws);
    static const char *bk_name[3] = { "разница силы < 5  ", "разница силы 5–15 ", "разница силы > 15 " };
    for (u32 k = 0; k < 3; k++)
        if (st.bucket_n[k])
            printf("%s: сильный победил %u из %u (%.0f%%)\n", bk_name[k], st.bucket_won[k], st.bucket_n[k],
                   100.0 * st.bucket_won[k] / st.bucket_n[k]);
    if (st.duels)
        printf("длительность поединка: средняя %.1f с, от %.1f до %.1f с\n", (f64)(st.dur_sum / (f32)st.duels),
               (f64)st.dur_min, (f64)st.dur_max);
    if (st.groups) printf("длительность схватки: средняя %.1f с\n", (f64)(st.gdur_sum / (f32)st.groups));
    printf("проигравшие: без сознания %u, сдались %u, не могут стоять %u, мертвы %u\n",
           st.lose_ko, st.lose_surrender, st.lose_crawl, st.lose_dead);
    printf("попаданий %u, блоков %u, уклонений %u, контратак %u, сбиты с ног %u\n",
           st.hits, st.blocks, st.dodges, st.counters, st.knockdowns);
    printf("переломов %u, отсечений %u, выронили оружие %u, ударов по своим %u, замен в ростере %u\n",
           st.fractures, st.severs, st.disarms, st.ally_hits, st.replaced);
    printf("смертей %u, из них после боя от потери крови %u\n", st.deaths, st.deaths_after);
    print_roster(roster);
    printf("\nтиков %llu за %.3f с (%.2f мкс на тик боя)\n", (unsigned long long)ticks, secs,
           ticks ? secs * 1e6 / (f64)ticks : 0.0);
    printf("контрольная сумма журнала: %016llx\n", (unsigned long long)hash);
    arena_free(&mem);
    return 0;
}
