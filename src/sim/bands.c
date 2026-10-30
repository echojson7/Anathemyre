#include "bands.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "../core/rng.h"

#define BAND_SPEED 1.3f      // м/с: отряд идёт шагом самого медленного, с привалами
#define BAND_ENGAGE_R 50.0f  // враги ближе — бой
#define BAND_HIT 0.012f      // урон за секунду на единицу силы врага, в новичках
#define BAND_CALM_S 60.0f    // бежавшие столько не ввязываются
#define TAG_GONE 31

static const f32 tier_w[BAND_TIERS] = { 1.0f, 1.8f, 3.0f };  // сила и стойкость по выучке

// Случайное число из зерна, номера отряда и секунды: не зависит от порядка обхода
static u32 mix(u64 a, u64 b, u64 c)
{
    u64 z = a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^ c * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (u32)((z ^ (z >> 31)) >> 32);
}
static f32 mix01(u64 a, u64 b, u64 c) { return (f32)(mix(a, b, c) >> 8) / 16777216.0f; }

void bands_init(Bands *b, Arena *perm, const Places *places, u64 seed, u32 phases)
{
    memset(b, 0, sizeof *b);
    b->places = places;
    b->seed = seed;
    b->phases = phases ? phases : 1;
    // Ячейка 512 м: запрос на BAND_ENGAGE_R задевает не больше четырёх
    grid_init(&b->grid, perm, BANDS_MAX, 65536, 512.0f);
}

i32 bands_add(Bands *b, u8 team, u32 place, const u16 men[BAND_TIERS])
{
    if (b->count >= BANDS_MAX || team >= BAND_TEAMS || place >= b->places->count) return -1;
    u32 i = b->count++;
    b->team[i] = team;
    b->state[i] = BAND_REST;
    b->from[i] = b->to[i] = b->goal[i] = (u8)place;
    b->x[i] = b->places->x[place];
    b->z[i] = b->places->z[place];
    b->wait[i] = 1.0f + mix01(b->seed, i, 0) * 120.0f;
    memcpy(b->men[i], men, sizeof b->men[i]);
    return (i32)i;
}

void bands_hostile(Bands *b, u8 a, u8 c)
{
    if (a >= BAND_TEAMS || c >= BAND_TEAMS) return;
    b->hostile[a] |= 1u << c;
    b->hostile[c] |= 1u << a;
}

u32 bands_men(const Bands *b, u32 i) { return (u32)b->men[i][0] + b->men[i][1] + b->men[i][2]; }

static f32 strength(const Bands *b, u32 i)
{
    return (f32)b->men[i][0] * tier_w[0] + (f32)b->men[i][1] * tier_w[1] + (f32)b->men[i][2] * tier_w[2];
}

// Где отряд, для журнала: у места (ближе 100 м к нему) или на дороге
static const char *where(const Bands *b, u32 i, char *buf, size_t n)
{
    const Places *p = b->places;
    u32 a = b->from[i], c = b->to[i];
    f32 len = places_dist(p, a, c), t = b->along[i];
    if (a != c && t > 100.0f && t < len - 100.0f) {
        if (p->name[a] && p->name[c]) snprintf(buf, n, "на дороге «%s» — «%s»", p->name[a], p->name[c]);
        else snprintf(buf, n, "на дороге %u — %u", a, c);
        return buf;
    }
    if (t * 2.0f > len) a = c;
    if (p->name[a]) snprintf(buf, n, "у места «%s»", p->name[a]);
    else snprintf(buf, n, "у места %u", a);
    return buf;
}

// В журнал событий: где отряд — его место или дорога
static void note(Bands *b, u8 kind, u32 i, u32 j, i32 n, i32 m)
{
    chron_add(b->chron, (ChronRec){ .kind = kind, .from = b->from[i], .to = b->to[i], .a = i, .b = j, .n = n, .m = m,
                                    .x = b->x[i], .z = b->z[i] });
}

static void place_pos(Bands *b, u32 i)
{
    const Places *p = b->places;
    u32 a = b->from[i], c = b->to[i];
    f32 len = places_dist(p, a, c), t = len > 0.0f ? b->along[i] / len : 0.0f;
    b->x[i] = p->x[a] + (p->x[c] - p->x[a]) * t;
    b->z[i] = p->z[a] + (p->z[c] - p->z[a]) * t;
}

// В путь к случайному месту, до которого есть дорога
static void set_out(Bands *b, u32 i)
{
    const Places *p = b->places;
    u32 here = b->from[i];
    if (p->count < 2) return;
    u32 g = mix(b->seed, i, b->sec * 2 + 1) % (p->count - 1);
    if (g >= here) g++;
    u8 nx = places_next(p, here, g);
    if (nx == PLACE_NONE) {
        b->wait[i] = 60.0f;
        return;
    }
    b->goal[i] = (u8)g;
    b->to[i] = nx;
    b->along[i] = 0.0f;
    b->state[i] = BAND_TRAVEL;
    b->wait[i] = 0.0f;
}

static void walk(Bands *b, u32 i, f32 dt)
{
    const Places *p = b->places;
    b->along[i] += BAND_SPEED * dt;
    f32 len = places_dist(p, b->from[i], b->to[i]);
    while (b->along[i] >= len) {
        b->along[i] -= len;
        b->from[i] = b->to[i];
        if (b->from[i] == b->goal[i]) {
            b->state[i] = BAND_REST;
            b->along[i] = 0.0f;
            b->wait[i] = 60.0f + mix01(b->seed, i, b->sec * 2) * 540.0f;
            break;
        }
        b->to[i] = places_next(p, b->from[i], b->goal[i]);
        len = places_dist(p, b->from[i], b->to[i]);
    }
    place_pos(b, i);
}

// Враг рядом — бой с ближайшим, кто ни с кем не дерётся
static void engage(Bands *b, u32 i)
{
    u32 mask = b->hostile[b->team[i]];
    if (!mask || !(grid_tags(&b->grid, b->x[i], b->z[i], BAND_ENGAGE_R) & mask)) return;
    u32 near[64];
    u32 n = grid_query(&b->grid, b->x[i], b->z[i], BAND_ENGAGE_R, mask, near, 64);
    i32 best = -1;
    f32 best_d = BAND_ENGAGE_R * BAND_ENGAGE_R;
    for (u32 k = 0; k < n; k++) {
        u32 j = near[k];
        if (j == i || b->near[j] || (b->state[j] != BAND_REST && b->state[j] != BAND_TRAVEL)) continue;
        if (b->state[j] == BAND_TRAVEL && b->wait[j] > 0.0f) continue;
        f32 dx = b->x[j] - b->x[i], dz = b->z[j] - b->z[i], d = dx * dx + dz * dz;
        if (d < best_d) {
            best_d = d;
            best = (i32)j;
        }
    }
    if (best < 0) return;
    u32 j = (u32)best;
    b->state[i] = b->state[j] = BAND_FIGHT;
    b->foe[i] = (u16)j;
    b->foe[j] = (u16)i;
    b->men0[i] = (u16)bands_men(b, i);
    b->men0[j] = (u16)bands_men(b, j);
    b->hurt[i] = b->hurt[j] = 0.0f;
    b->fights++;
    note(b, CH_BAND_CLASH, i, j, b->men0[i], b->men0[j]);
    if (b->log_left) {
        b->log_left--;
        char buf[96];
        log_info("ОТРЯДЫ: стычка %s: %u против %u", where(b, i, buf, sizeof buf), b->men0[i], b->men0[j]);
    }
}

// Бой окончен: на месте — стоять, в пути — идти дальше
static void resume(Bands *b, u32 i)
{
    b->state[i] = b->from[i] == b->to[i] ? BAND_REST : BAND_TRAVEL;
    if (b->state[i] == BAND_REST) b->wait[i] = fmaxf(b->wait[i], 30.0f);
}

// Бегство: назад по дороге, а если стоял на месте — к случайному месту
static void rout(Bands *b, u32 i)
{
    const Places *p = b->places;
    b->routs++;
    note(b, CH_BAND_ROUT, i, CHRON_NOBODY, (i32)bands_men(b, i), b->men0[i]);
    if (b->log_left) {
        b->log_left--;
        char buf[96];
        log_info("ОТРЯДЫ: отряд %u бежит %s, осталось %u из %u", i, where(b, i, buf, sizeof buf), bands_men(b, i), b->men0[i]);
    }
    if (b->from[i] != b->to[i]) {
        u8 a = b->from[i];
        b->along[i] = places_dist(p, a, b->to[i]) - b->along[i];
        b->from[i] = b->to[i];
        b->to[i] = b->goal[i] = a;
        b->state[i] = BAND_TRAVEL;
    } else {
        set_out(b, i);
        if (b->state[i] != BAND_TRAVEL) b->state[i] = BAND_REST;
    }
    b->wait[i] = BAND_CALM_S;
}

static void fight(Bands *b, u32 i, f32 dt)
{
    u32 j = b->foe[i];
    if (b->state[j] != BAND_FIGHT || b->foe[j] != i) {
        resume(b, i);
        return;
    }
    // Урон по силе врага, с разбросом; потери — с младших, как только урона хватает на бойца
    b->hurt[i] += BAND_HIT * strength(b, j) * dt * (0.6f + 0.8f * mix01(b->seed, i, b->sec * 2));
    for (u32 t = 0; t < BAND_TIERS;) {
        if (!b->men[i][t]) {
            t++;
            continue;
        }
        if (b->hurt[i] < tier_w[t]) break;
        b->hurt[i] -= tier_w[t];
        b->men[i][t]--;
        b->dead++;
    }
    u32 left = bands_men(b, i);
    if (!left) {
        b->state[i] = BAND_GONE;
        b->wiped++;
        note(b, CH_BAND_WIPED, i, j, 0, b->men0[i]);
        if (b->log_left) {
            b->log_left--;
            char buf[96];
            log_info("ОТРЯДЫ: отряд %u истреблён %s", i, where(b, i, buf, sizeof buf));
        }
    } else if (left * 2 < b->men0[i]) rout(b, i);
}

static void step(Bands *b, u32 i)
{
    f32 dt = BAND_STEP_S;
    switch (b->state[i]) {
    case BAND_GONE: return;
    case BAND_FIGHT: fight(b, i, dt); return;
    case BAND_REST:
        b->wait[i] -= dt;
        if (b->wait[i] <= 0.0f) set_out(b, i);
        break;
    case BAND_TRAVEL:
        b->wait[i] = fmaxf(b->wait[i] - dt, 0.0f);
        if (!(b->near[i] && b->hold[i])) walk(b, i, dt);
        break;
    }
    if (b->near[i]) return;  // рядом бьются тела, не формула
    if (b->state[i] == BAND_REST || (b->state[i] == BAND_TRAVEL && b->wait[i] <= 0.0f)) engage(b, i);
}

void bands_tick(Bands *b)
{
    if (!b->count) return;
    // Сетка — раз в секунду: за секунду отряд проходит пару метров
    if (b->phase == 0) {
        for (u32 i = 0; i < b->count; i++) b->tag[i] = b->state[i] == BAND_GONE ? TAG_GONE : b->team[i];
        grid_build(&b->grid, b->x, b->z, b->tag, TAG_GONE + 1, b->count);
    }
    for (u32 i = b->phase; i < b->count; i += b->phases) step(b, i);
    if (++b->phase == b->phases) {
        b->phase = 0;
        b->sec++;
    }
}

bool bands_send(Bands *b, u32 i, u32 goal)
{
    if (i >= b->count || b->state[i] != BAND_REST || goal >= b->places->count || goal == b->from[i]) return false;
    u8 nx = places_next(b->places, b->from[i], goal);
    if (nx == PLACE_NONE) return false;
    b->goal[i] = (u8)goal;
    b->to[i] = nx;
    b->along[i] = 0.0f;
    b->state[i] = BAND_TRAVEL;
    b->wait[i] = 0.0f;
    return true;
}

void bands_set_near(Bands *b, u32 i, bool on)
{
    if (i >= b->count) return;
    b->near[i] = on;
    b->hold[i] = 0;
    if (on && b->state[i] == BAND_FIGHT) resume(b, i);
}

void bands_set_men(Bands *b, u32 i, const u16 men[BAND_TIERS])
{
    if (i >= b->count || b->state[i] == BAND_GONE) return;
    memcpy(b->men[i], men, sizeof b->men[i]);
    if (bands_men(b, i)) return;
    b->state[i] = BAND_GONE;
    b->wiped++;
    note(b, CH_BAND_WIPED, i, CHRON_NOBODY, 0, 0);
    if (b->log_left) {
        b->log_left--;
        char buf[96];
        log_info("ОТРЯДЫ: отряд %u истреблён %s", i, where(b, i, buf, sizeof buf));
    }
}

void bands_ser(Ser *s, Bands *b)
{
    ser_count(s, &b->count, BANDS_MAX);
    SER(s, b->phase);
    SER(s, b->sec);
    ser_check(s, b->phase < b->phases);
    u32 n = b->count;
    SER_N(s, b->x, n);
    SER_N(s, b->z, n);
    SER_N(s, b->along, n);
    SER_N(s, b->wait, n);
    SER_N(s, b->hurt, n);
    SER_N(s, b->men, n);
    SER_N(s, b->men0, n);
    SER_N(s, b->foe, n);
    SER_N(s, b->from, n);
    SER_N(s, b->to, n);
    SER_N(s, b->goal, n);
    SER_N(s, b->team, n);
    SER_N(s, b->state, n);
    SER_N(s, b->tag, n);
    SER_N(s, b->near, n);
    SER_N(s, b->hold, n);
    SER(s, b->hostile);
    grid_ser(s, &b->grid);
    SER(s, b->fights);
    SER(s, b->routs);
    SER(s, b->wiped);
    SER(s, b->dead);
}

u64 bands_checksum(const Bands *b)
{
    u64 h = 1469598103934665603ull;
    for (u32 i = 0; i < b->count; i++) {
        u32 v[5];
        memcpy(&v[0], &b->x[i], 4);
        memcpy(&v[1], &b->z[i], 4);
        v[2] = bands_men(b, i);
        v[3] = b->state[i];
        v[4] = (u32)b->from[i] << 8 | b->to[i];
        for (u32 k = 0; k < 5; k++) h = (h ^ v[k]) * 1099511628211ull;
    }
    return h;
}

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
