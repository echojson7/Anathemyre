// Замеры без окна (--bench, data/debug/bench.txt): сценка приказа атаки и нагрузка поля
#include "bench.h"

#include <stdio.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "../defs/defs.h"
#include "../sim/field.h"
#include "../sim/squad.h"

static f32 flat_ground(f32 x, f32 z) { (void)x; (void)z; return 0.0f; }

// Тела без отряда идут сами: бродящие держат свой шаг, гонящиеся — бег, заданный полем
static void walk_bodies(Player *b, u32 n, f32 dt)
{
    for (u32 i = 0; i < n; i++) {
        if (b[i].ctrl == CTRL_FREE) b[i].vmax = g_gait_speed[GAIT_WALK];
        player_update(&b[i], dt, flat_ground);
    }
}

static void print_slots(const Field *f, f32 t)
{
    printf("%6.1f с |", (f64)t);
    for (u32 i = 0; i < f->w.count; i++) {
        const char *s = !f->w.idle[i] ? g_state_name[f->w.state[i]] : (f->body[i]->ctrl == CTRL_CHASE ? "БЕЖИТ К ЦЕЛИ" : (f->body[i]->moving ? "ИДЁТ" : "-"));
        printf(" %s: %s |", f->sheet[i]->name, s);
    }
    printf("\n");
}

// Сценка: свой отряд из четырёх атакует одного из трёх чужих; рядом двое прохожих
static int bench_scene(Arena *mem, u64 seed)
{
    Rng rng;
    rng_seed(&rng, seed, 77);
    Field *f = ARENA_PUSH_ARRAY(mem, Field, 1);
    field_init(f, mem, seed);
    Squad *sq = ARENA_PUSH_ARRAY(mem, Squad, 1);
    People *people = ARENA_PUSH_ARRAY(mem, People, 1);
    squad_init(sq, people, &rng, v3(0, 0, 0), 0.0f, flat_ground);
    static const char *names[] = { "ЧУРИЛА", "ВЕЛИМИР", "ГОСТЯТА", "ФЕОДУЛ", "ПРОКЛ" };
    Player npc[5];
    CharSheet *sheet = ARENA_PUSH_ARRAY(mem, CharSheet, 5);
    for (u32 i = 0; i < sq->count; i++) {
        field_add(f, squad_body(sq, i), squad_sheet(sq, i), 0, FF_PLAYER | FF_ASSIST);
        sq->selected[i] = true;
    }
    for (u32 k = 0; k < 5; k++) {
        V3 p = k < 3 ? v3((f32)k * 1.5f - 1.5f, 0, 18.0f) : v3(7.0f + (f32)k, 0, 9.0f);
        player_init(&npc[k], p, PI_F);
        npc[k].gait = GAIT_WALK;
        sheet_random(&sheet[k], &rng, names[k], k == 0 ? 1 : 0, g_weapon_pool[k % g_weapon_pool_count]);
        field_add(f, &npc[k], &sheet[k], k < 3 ? 1 : (u8)(k - 1), k < 3 ? FF_ASSIST : 0);
    }
    printf("СЦЕНКА: ОТРЯД (4) АТАКУЕТ %s; С НИМ ДВОЕ СВОИХ, РЯДОМ ДВОЕ ПРОХОЖИХ\n", names[0]);
    for (u32 i = 0; i < sq->count; i++) field_attack(f, i, sq->count);
    // Шагами мира, как в игре: ходьба, затем тик поля
    const f32 dt = COMBAT_DT;
    f32 t = 0.0f, next_print = 0.0f;
    bool done = false;
    for (u32 fr = 0; fr < COMBAT_HZ * 180 && !done; fr++, t += dt) {
        squad_update(sq, dt, flat_ground);
        walk_bodies(npc, 5, dt);
        field_tick(f, flat_ground);
        if (t >= next_print) {
            print_slots(f, t);
            next_print += 4.0f;
        }
        // Конец: все спят, приказов нет
        done = t > 5.0f;
        for (u32 i = 0; i < f->w.count; i++)
            if (f->w.order[i] >= 0 || (!f->w.idle[i] && !fs_out(f->w.state[i]))) done = false;
    }
    print_slots(f, t);
    u32 tgt = sq->count;
    bool ok = fs_out(f->w.state[tgt]) || f->sheet[tgt]->d.blood < 0.5f * 100.0f * f->sheet[tgt]->d.mass / 75.0f;
    printf("ИТОГ: %s, ЦЕЛЬ %s, БОЙ ЗАТИХ: %s\n\n", names[0], g_state_name[f->w.state[tgt]], done ? "ДА" : "НЕТ");
    (void)ok;
    return done ? 0 : 1;
}

// Нагрузка: n тел в двух толпах; своя сторона по приказу атакует, чужая нападает и помогает своим
static int bench_scale(Arena *mem, u32 n, u64 seed)
{
    Rng rng;
    rng_seed(&rng, seed, 77);
    Field *f = ARENA_PUSH_ARRAY(mem, Field, 1);
    field_init(f, mem, seed);
    Player *b = ARENA_PUSH_ARRAY(mem, Player, n);
    CharSheet *sh = ARENA_PUSH_ARRAY(mem, CharSheet, n);
    for (u32 i = 0; i < n; i++) {
        u8 side = (u8)(i & 1);
        u32 k = i / 2, row = k / 25;
        f32 x = ((f32)(k % 25) - 12.0f) * 1.6f + rng_range(&rng, -0.3f, 0.3f);
        f32 z = (side ? 25.0f : -25.0f) + (side ? 1.0f : -1.0f) * (f32)row * 1.8f;
        player_init(&b[i], v3(x, 0, z), side ? PI_F : 0.0f);
        b[i].gait = GAIT_RUN;
        f32 roll = rng_f32(&rng);
        sheet_random(&sh[i], &rng, side ? "ЧУЖОЙ" : "СВОЙ", roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2), g_weapon_pool[(u32)rng_range(&rng, 0, (f32)g_weapon_pool_count - 0.01f)]);
        field_add(f, &b[i], &sh[i], side, side ? (FF_ASSIST | FF_AGGRO) : (FF_ASSIST | FF_PLAYER));
    }
    log_set_echo(false);
    // Своя сторона идёт в атаку: каждый на того, кто стоит напротив
    for (u32 i = 0; i + 1 < n; i += 2) field_attack(f, i, i + 1);
    const f32 dt = COMBAT_DT;
    f64 sum = 0.0, worst = 0.0;
    u32 frames = 0, peak = 0;
    for (; frames < COMBAT_HZ * 120; frames++) {
        f64 t0 = time_now();
        for (u32 i = 0; i < n; i++) {
            if (b[i].ctrl == CTRL_FREE) b[i].vmax = g_gait_speed[GAIT_RUN];
            player_update(&b[i], dt, flat_ground);
        }
        field_tick(f, flat_ground);
        f64 ms = (time_now() - t0) * 1000.0;
        sum += ms;
        worst = ms > worst ? ms : worst;
        peak = MAX(peak, f->fights);
    }
    u32 out[2] = { 0, 0 }, hist[2][FS_COUNT] = { { 0 } }, idle[2] = { 0, 0 };
    for (u32 i = 0; i < n; i++) {
        if (fs_out(f->w.state[i])) out[i & 1]++;
        if (f->w.idle[i]) idle[i & 1]++;
        else hist[i & 1][f->w.state[i]]++;
    }
    for (u32 sd = 0; sd < 2; sd++) {
        printf("сторона %u: спят %u;", sd, idle[sd]);
        for (u32 k = 0; k < FS_COUNT; k++) if (hist[sd][k]) printf(" %s %u;", g_state_name[k], hist[sd][k]);
        printf("\n");
    }
    printf("НАГРУЗКА: %u ТЕЛ, 120 С: ШАГ МИРА (ХОДЬБА И ТИК ПОЛЯ) СРЕДНИЙ %.3f МС, ХУДШИЙ %.3f МС; В БОЮ ДО %u; ВЫБЫЛО СВОИХ %u, ЧУЖИХ %u\n",
           n, sum / frames, worst, peak, out[0], out[1]);
    log_set_echo(true);
    return 0;
}

int field_bench(u32 bodies, u64 seed)
{
    Arena mem;
    if (!arena_init(&mem, MB(16) + (size_t)bodies * KB(4))) return 1;
    int rc = bench_scene(&mem, seed);
    arena_reset(&mem);
    if (bodies) rc |= bench_scale(&mem, bodies, seed);
    arena_free(&mem);
    return rc;
}
