#include "arena.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../core/arena.h"
#include "../core/clock.h"
#include "../core/log.h"
#include "../core/rng.h"
#include "../sim/combat.h"

#define BOUT_LIMIT_S 180.0f
#define AFTER_BOUT_S 45.0f   // тела лежат, кровь течёт, кто-то приходит в себя (бенчмарк)
#define REST_S 300.0f        // отдых между боями
#define BANDAGE_S 10.0f      // через столько после исхода раны перетягивают

static const char *g_names[] = {
    "ЖДАН", "КСАНФ", "СЕРВИЙ", "ВЫШАТА", "АГАФОН", "ДЕЦИМ", "ГОРАЗД", "КЛЕОН", "НУМЕРИЙ", "ПУТЯТА",
    "ФОКА", "ТИТ", "МСТИША", "ЛИСАНДР", "КАССИЙ", "ТВЕРДИЛО", "ФЕАГЕН", "СЕКСТ", "НЕЖАТА", "ЕВДОКС",
    "АВЛ", "ОСТРОМИР", "ГЕРМОН", "СПУРИЙ", "РАТИБОР", "ЕВМЕН", "КВИНТ", "БУДИМИР", "ФИЛОН", "ЛУЦИЙ",
    "ХОТЕН", "ДАМОН", "ГАЙ", "МЕСТЯТА", "ПАРАМОН", "ПОСТУМ", "СМЕЯН", "ДИОКЛ", "ВИБИЙ", "ЯРОПОЛК",
};

static f32 jitter(Rng *r, f32 base, f32 spread) { return clampf(base + rng_range(r, -spread, spread), 1.0f, 100.0f); }

void arena_random_fighter(FighterDesc *d, Rng *r, u32 tier)
{
    static const f32 tier_skill[3] = { 9.0f, 32.0f, 62.0f };
    memset(d, 0, sizeof *d);
    f32 b = tier_skill[tier];
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
    d->armor[SLOT_PANTS] = A_PANTS;
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
}

static void make_fighter(ArenaSession *s, ArenaFighter *f, u32 tier)
{
    memset(f, 0, sizeof *f);
    arena_random_fighter(&f->d, &s->rng, tier);
    memcpy(f->start, f->d.skill, sizeof f->start);
    f->name = s->next_name++ % ARRAY_COUNT(g_names);
}

// Следующее имя, которого нет среди живых в ростере
static void rename_unique(ArenaSession *s, u32 k)
{
    ArenaFighter *roster = s->roster;
    for (u32 tries = 0; tries < ARRAY_COUNT(g_names); tries++) {
        bool taken = false;
        for (u32 o = 0; o < ARENA_ROSTER; o++)
            if (o != k && !roster[o].gone && roster[o].name == roster[k].name) taken = true;
        if (!taken) return;
        roster[k].name = s->next_name++ % ARRAY_COUNT(g_names);
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
    for (u32 k = 0; k < ARENA_ROSTER; k++) {
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

const char *arena_name(const ArenaSession *s, u32 fighter)
{
    return fighter < s->n ? g_names[s->roster[s->idx[fighter]].name] : "?";
}

const char *arena_roster_name(const ArenaSession *s, u32 roster)
{
    return roster < ARENA_ROSTER ? g_names[s->roster[roster].name] : "?";
}

// Строка журнала собирается кусками и уходит в журнал целиком
static char g_ln[LOG_LINE_MAX];
static size_t g_lp;

static void ln_add(const char *fmt, ...) ANA_PRINTF(1, 2);
static void ln_add(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(g_ln + g_lp, sizeof g_ln - g_lp, fmt, ap);
    va_end(ap);
    if (k > 0) g_lp = MIN(g_lp + (size_t)k, sizeof g_ln - 1);
}

static void ln_flush(void)
{
    log_info("%s", g_ln);
    g_lp = 0;
    g_ln[0] = 0;
}

static void print_event(const ArenaSession *s, const CombatEvent *e)
{
    char tb[16];
    fmt_time(tb, e->tick);
    const char *a = arena_name(s, e->a), *b = arena_name(s, e->b);
    switch (e->kind) {
    case EV_HIT:
        ln_add("[%s] %s → %s: %s, %s.", tb, a, b, g_attack_name[e->arg], g_part_name[e->part]);
        if (e->cut >= 0.5f) ln_add(" РЕЗ %.0f.", (f64)e->cut);
        if (e->blunt >= 0.5f) ln_add(" ДРОБЛ %.0f.", (f64)e->blunt);
        if (e->pierce >= 0.5f) ln_add(" УКОЛ %.0f.", (f64)e->pierce);
        if (e->flags & EVF_FRACTURE) ln_add(" ПЕРЕЛОМ.");
        if (e->flags & EVF_SEVER) ln_add(" ОТСЕЧЕНИЕ.");
        if (e->flags & EVF_STAGGER) ln_add(" ОШЕЛОМЛЁН.");
        if (e->flags & EVF_ALLY) ln_add(" ПО СВОЕМУ.");
        ln_flush();
        break;
    case EV_BLOCK:
        ln_add("[%s] %s: БЛОК ПРОТИВ %s.%s", tb, b, a, (e->flags & EVF_STAGGER) ? " ОШЕЛОМЛЁН." : ""); ln_flush();
        break;
    case EV_DODGE:     ln_add("[%s] %s: УКЛОНИЛСЯ ОТ %s.", tb, b, a); ln_flush(); break;
    case EV_COUNTER:   ln_add("[%s] %s: КОНТРАТАКА.", tb, a); ln_flush(); break;
    case EV_KNOCKDOWN: ln_add("[%s] %s: СБИТ С НОГ.", tb, b); ln_flush(); break;
    case EV_FALL:      ln_add("[%s] %s: УПАЛ.", tb, a); ln_flush(); break;
    case EV_KO:        ln_add("[%s] %s: БЕЗ СОЗНАНИЯ.", tb, a); ln_flush(); break;
    case EV_DEATH:     ln_add("[%s] %s: МЁРТВ.", tb, a); ln_flush(); break;
    case EV_DISARM:    ln_add("[%s] %s: ВЫРОНИЛ ОРУЖИЕ.", tb, a); ln_flush(); break;
    case EV_RETREAT:   ln_add("[%s] %s: ОТСТУПАЕТ.", tb, a); ln_flush(); break;
    case EV_SURRENDER: ln_add("[%s] %s: СДАЁТСЯ.", tb, a); ln_flush(); break;
    case EV_GETUP:     ln_add("[%s] %s: ПОДНЯЛСЯ.", tb, a); ln_flush(); break;
    default: break;
    }
}

static void count_event(ArenaStats *st, const CombatEvent *e)
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

bool arena_session_init(ArenaSession *s, Arena *mem, u64 seed)
{
    memset(s, 0, sizeof *s);
    combat_init(&s->w, mem, 16, 4096);
    fighter_ai_init(&s->ai, mem, 16);
    s->seed = seed;
    rng_seed(&s->rng, seed, 7);
    for (u32 k = 0; k < ARENA_ROSTER; k++) make_fighter(s, &s->roster[k], k < 4 ? 0 : (k < 9 ? 1 : 2));
    s->st.dur_min = 1e9f;
    s->hash = 0xcbf29ce484222325ull;
    s->after_bout_s = AFTER_BOUT_S;
    return true;
}

// Подбор пары или двух четвёрок из отдохнувших; false — драться некому
static bool pick_bout(ArenaSession *s)
{
    ArenaFighter *roster = s->roster;
    u32 avail[ARENA_ROSTER], na = 0;
    for (u32 k = 0; k < ARENA_ROSTER; k++)
        if (available(&roster[k])) avail[na++] = k;
    // По числу боёв, затем по номеру
    for (u32 a = 1; a < na; a++)
        for (u32 c = a; c > 0 && roster[avail[c]].bouts < roster[avail[c - 1]].bouts; c--) {
            u32 tmp = avail[c]; avail[c] = avail[c - 1]; avail[c - 1] = tmp;
        }
    if (na < 2) return false;
    s->group = (s->bout + 1) % 5 == 0 && na >= 8;
    if (s->group) {
        // 8 самых свободных, змейкой по силе: 1,4,5,8 против 2,3,6,7
        for (u32 k = 0; k < 8; k++) s->idx[k] = avail[k];
        for (u32 a = 1; a < 8; a++)
            for (u32 c = a; c > 0 && combat_rating(&roster[s->idx[c]].d) > combat_rating(&roster[s->idx[c - 1]].d); c--) {
                u32 tmp = s->idx[c]; s->idx[c] = s->idx[c - 1]; s->idx[c - 1] = tmp;
            }
        static const u8 snake[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };
        memcpy(s->team, snake, sizeof snake);
        s->n = 8;
    } else {
        u32 a = avail[0], best = avail[1];
        f32 ra = combat_rating(&roster[a].d), bs = 1e9f;
        for (u32 k = 1; k < na; k++) {
            u32 c = avail[k];
            f32 sc = fabsf(combat_rating(&roster[c].d) - ra) + 2.0f * (f32)roster[c].bouts;
            if (sc < bs) { bs = sc; best = c; }
        }
        s->idx[0] = a; s->idx[1] = best;
        s->team[0] = 0; s->team[1] = 1;
        s->n = 2;
    }
    return true;
}

bool arena_bout_begin(ArenaSession *s)
{
    ArenaFighter *roster = s->roster;
    rest(roster, REST_S);
    s->log = s->bout < s->log_bouts;
    // Замена выбывших новичками
    for (u32 k = 0; k < ARENA_ROSTER; k++) {
        if (!roster[k].gone) continue;
        if (s->log) ln_add("[АРЕНА] %s ВЫБЫЛ. НА ЕГО МЕСТО: ", g_names[roster[k].name]);
        make_fighter(s, &roster[k], 0);
        rename_unique(s, k);
        if (s->log) {
            ln_add("%s.", g_names[roster[k].name]);
            ln_flush();
        }
        s->st.replaced++;
    }
    if (!pick_bout(s)) return false;

    u32 n = s->n;
    s->rating[0] = combat_rating(&roster[s->idx[0]].d);
    s->rating[1] = n == 2 ? combat_rating(&roster[s->idx[1]].d) : 0.0f;
    if (s->log) {
        ln_add("[АРЕНА] %s %u.", s->group ? "СХВАТКА" : "ПОЕДИНОК", s->bout + 1);
        for (u8 side = 0; side < 2; side++) {
            ln_add(side ? " —" : "");
            bool first = true;
            for (u32 k = 0; k < n; k++) {
                if (s->team[k] != side) continue;
                const FighterDesc *d = &roster[s->idx[k]].d;
                ln_add("%s%s (АТК %.0f, %s %s)", first ? " " : ", ", g_names[roster[s->idx[k]].name],
                       (f64)d->skill[SK_ATTACK], g_quality_name[d->weapon_q], g_weapons[d->weapon].name);
                first = false;
            }
        }
        ln_flush();
    }

    CombatWorld *w = &s->w;
    combat_reset(w, s->seed * 0x9E3779B97F4A7C15ull + s->bout * 0xBF58476D1CE4E5B9ull + 1);
    w->mercy = true;
    w->halted = false;
    w->fence_r = ARENA_RADIUS;
    u32 slot[2] = { 0, 0 }, per_team[2] = { 0, 0 };
    for (u32 k = 0; k < n; k++) per_team[s->team[k]]++;
    for (u32 k = 0; k < n; k++) {
        FighterDesc d = roster[s->idx[k]].d;
        u8 t = s->team[k];
        f32 side = t == 0 ? -1.0f : 1.0f;
        f32 row = (f32)slot[t]++ - 0.5f * (f32)(per_team[t] - 1);
        d.team = t;
        d.pos = v3(side * (n > 2 ? 3.0f : 2.5f), 0, row * 1.5f);
        d.heading = t == 0 ? PI_F * 0.5f : -PI_F * 0.5f;
        combat_add(w, &d);
    }
    s->winner = -1;
    s->decided = 0;
    s->end_tick = (u32)((BOUT_LIMIT_S + s->after_bout_s) * COMBAT_HZ);
    return true;
}

bool arena_bout_tick(ArenaSession *s)
{
    CombatWorld *w = &s->w;
    ArenaStats *st = &s->st;
    fighter_think_all(&s->ai, w);
    combat_tick(w);
    u32 t = w->tick - 1;
    s->hash = combat_hash_events(w, s->hash);
    for (u32 k = 0; k < w->ev_count; k++) {
        count_event(st, &w->ev[k]);
        if (s->decided && w->ev[k].kind == EV_DEATH) st->deaths_after++;
        if (s->log) print_event(s, &w->ev[k]);
    }
    combat_events_done(w);
    if (s->decided && t == s->decided + (u32)(BANDAGE_S * COMBAT_HZ))
        for (u32 k = 0; k < w->count; k++) w->bleed[k] = 0.0f;
    if (!s->decided) {
        u32 a0 = combat_team_active(w, 0), a1 = combat_team_active(w, 1);
        if (a0 == 0 || a1 == 0 || t + 1 >= (u32)(BOUT_LIMIT_S * COMBAT_HZ)) {
            s->decided = t + 1;
            s->winner = a0 == 0 && a1 > 0 ? 1 : (a1 == 0 && a0 > 0 ? 0 : -1);
            w->halted = true;
            s->end_tick = MIN(s->end_tick, s->decided + (u32)(s->after_bout_s * COMBAT_HZ));
            if (s->log) {
                char tb[16];
                fmt_time(tb, w->tick);
                ln_add("[%s] ИСХОД ЗАФИКСИРОВАН. %s", tb,
                       s->winner < 0 ? "НИЧЬЯ." : (s->winner == 0 ? "ПЕРВАЯ СТОРОНА." : "ВТОРАЯ СТОРОНА.")); ln_flush();
            }
        }
    }
    return w->tick >= s->end_tick;
}

void arena_bout_end(ArenaSession *s)
{
    CombatWorld *w = &s->w;
    ArenaStats *st = &s->st;
    f32 dur = (f32)s->decided * COMBAT_DT;
    s->ticks += w->tick;
    for (u32 k = 0; k < s->n; k++) {
        ArenaFighter *f = &s->roster[s->idx[k]];
        combat_store(w, k, &f->d);
        f->bouts++;
        if (s->winner == (i32)s->team[k]) f->wins++;
        u8 st8 = w->state[k];
        if (s->winner >= 0 && s->team[k] != (u8)s->winner) {
            if (st8 == FS_DEAD) st->lose_dead++;
            else if (st8 == FS_UNCONSCIOUS) st->lose_ko++;
            else if (st8 == FS_CRAWL) st->lose_crawl++;
            else st->lose_surrender++;
        }
        if (st8 == FS_DEAD || f->d.severed) f->gone = true;
    }
    if (s->winner < 0) st->draws++;
    if (s->group) {
        st->groups++;
        st->gdur_sum += dur;
    } else {
        st->duels++;
        st->dur_sum += dur;
        st->dur_min = fminf(st->dur_min, dur);
        st->dur_max = fmaxf(st->dur_max, dur);
        f32 diff = fabsf(s->rating[0] - s->rating[1]);
        u32 bk = diff < 5.0f ? 0 : (diff < 15.0f ? 1 : 2);
        if (s->winner >= 0) {
            st->bucket_n[bk]++;
            if ((s->winner == 0) == (s->rating[0] >= s->rating[1])) st->bucket_won[bk]++;
        }
    }
    s->bout++;
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
    for (u32 k = 0; k < ARENA_ROSTER; k++) {
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
    print_roster(s->roster);
    printf("\nтиков %llu за %.3f с (%.2f мкс на тик боя)\n", (unsigned long long)s->ticks, secs,
           s->ticks ? secs * 1e6 / (f64)s->ticks : 0.0);
    printf("контрольная сумма журнала: %016llx\n", (unsigned long long)s->hash);
    arena_free(&mem);
    return 0;
}
