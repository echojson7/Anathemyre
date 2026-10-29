#include "world.h"

#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "../core/prof.h"
#include "../world/map.h"
#include "testroom.h"

#define CROWD_SIZE 9
#define TOWN_WALKERS 20
#define ARENA_SHOW_AFTER_BOUT_S 12.0f

f32 world_ground(f32 x, f32 z) { return testroom_ground(x, z); }

bool world_squad_in_valley(const World *w)
{
    V3 p = squad_body(&w->squad, (u32)MAX(0, squad_primary(&w->squad)))->pos;
    return map_valley(p.x, p.z) > 0.5f;
}

// Своя точка появления в пустоши: у края зала, лицом к арене
static V3  squad_home(void) { return v3(9.0f, 0.0f, 22.0f); }
static f32 squad_home_heading(void) { return atan2f(ARENA_CENTER.x - 9.0f, ARENA_CENTER.z - 22.0f); }

bool world_init(World *w, Arena *perm, MeshBuilder *mb, u64 seed, f32 tod, bool day_run)
{
    *w = (World){ .seed = seed, .tod = tod, .day = 1, .day_run = day_run };
    rng_seed(&w->rng, seed, 1);
    // Карта: крупный рельеф из зерна, затем долина на ней (подробная карта высот, вода, руины, растения)
    f64 t0 = time_now();
    if (!map_init(perm, seed)) return false;
    f64 t1 = time_now();
    w->valley = ARENA_PUSH_ARRAY(perm, ValleyView, 1);
    if (!valley_view_build(w->valley, perm, seed)) {
        log_error("ДОЛИНА: не построена");
        return false;
    }
    log_stat("МИР: карта %.2f с, долина %.2f с", t1 - t0, time_now() - t1);
    // Статика пустоши. Посёлок отдельной сеткой: вне кадра и вне области теней отсекается целиком.
    // У посёлка свой поток случайных чисел, чтобы остальная сцена от него не зависела
    size_t mark = perm->used;
    mb_init(mb, perm, 400000, 1200000);
    testroom_build_near(mb, &w->rng);
    arena_view_build(mb, ARENA_CENTER);
    gpu_mesh_upload(&w->hall, mb);
    u32 tris = mb->icount / 3;
    perm->used = mark;
    mb_init(mb, perm, 400000, 1200000);
    testroom_build_far(mb, &w->rng);
    gpu_mesh_upload(&w->far, mb);
    log_info("МИР: зал %u треугольников, рельеф и мегаструктуры %u", tris, mb->icount / 3);
    perm->used = mark;
    rng_seed(&w->town_rng, seed, 7);
    mb_init(mb, perm, 400000, 1200000);
    town_build(mb, &w->town_rng, &w->town);
    gpu_mesh_upload(&w->town_mesh, mb);
    log_info("МИР: Скудель — %u треугольников, домов %u, прилавков %u", mb->icount / 3, w->town.houses, w->town.stalls);
    perm->used = mark;

    // Арена: те же бои, что в --arena-bench с этим seed, но в реальном времени
    w->arena = ARENA_PUSH_ARRAY(perm, ArenaSession, 1);
    if (!arena_session_init(w->arena, perm, seed)) return false;
    w->arena->log_bouts = 0xFFFFFFFFu;
    w->arena->after_bout_s = ARENA_SHOW_AFTER_BOUT_S;
    arena_view_init(&w->aview, ARENA_CENTER, 0.125f);

    // Люди. Порядок в таблице — порядок шага и отрисовки; от него зависят случайные числа
    People *p = w->people = ARENA_PUSH_ARRAY(perm, People, 1);
    crowd_add(p, &w->rng, CROWD_SIZE);
    // Зрители — одна толпа, Скудель — один посёлок: команды за бродящими
    watchers_add(p, w->aview.spos, w->aview.sh, w->aview.sseed, AV_SPECTATORS, TEAM_WANDER + CROWD_SIZE);
    town_folk_add(p, &w->town, &w->town_rng, TOWN_WALKERS, BODY_KIND_COUNT, TEAM_WANDER + CROWD_SIZE + 1);
    u32 folk = 0;
    for (u32 i = 0; i < p->count; i++) folk += p->role[i] == ROLE_TOWN;
    log_info("СКУДЕЛЬ: жителей %u", folk);
    mobs_add(p, &w->rng);
    squad_init(&w->squad, p, &w->rng, squad_home(), squad_home_heading(), world_ground);
    // Поле боя: все люди, слот = номер в таблице
    w->field = ARENA_PUSH_ARRAY(perm, Field, 1);
    field_init(w->field, perm, seed);
    people_join_field(p, w->field);
    memcpy(p->last, p->body, p->count * sizeof *p->body);
    return true;
}

// Один шаг мира. Порядок систем постоянный: ходьба, арена, поле боя (видит тела после ходьбы), сутки
static void world_tick(World *w)
{
    People *p = w->people;
    memcpy(p->last, p->body, p->count * sizeof *p->body);
    prof_zone(PZ_PEOPLE);
    people_update(p, &w->rng, &w->town_rng, TICK_DT, testroom_ground);
    squad_update(&w->squad, TICK_DT, world_ground);
    prof_zone(PZ_ARENA);
    arena_view_tick(&w->aview, w->arena);
    prof_zone(PZ_FIELD);
    field_tick(w->field, world_ground);
    if (w->day_run) {
        w->tod += TICK_DT / DAY_LENGTH_S;
        if (w->tod >= 1.0f) {
            w->tod -= 1.0f;
            w->day++;
        }
    }
    w->tick++;
}

void world_update(World *w, f32 dt)
{
    w->acc += dt;
    for (u32 n = 0; w->acc >= TICK_DT && n < TICKS_PER_FRAME_MAX; n++) {
        w->acc -= TICK_DT;
        world_tick(w);
    }
    if (w->acc > TICK_DT) w->acc = TICK_DT;  // не успел: остаток не копится
    w->alpha = w->acc / TICK_DT;
}

void world_present(World *w, f32 dt)
{
    arena_view_present(&w->aview, w->arena, w->alpha, dt);
    field_present(w->field, w->alpha, dt);
}

f32 world_time(const World *w) { return ((f32)(w->tick % TIME_WRAP_TICKS) + w->alpha) * TICK_DT; }

f32 world_tod(const World *w)
{
    if (!w->day_run) return w->tod;
    f32 t = w->tod + w->alpha * TICK_DT / DAY_LENGTH_S;
    return t >= 1.0f ? t - 1.0f : t;
}

bool world_squad_fighting(const World *w)
{
    bool fighting = false;
    for (u32 i = 0; i < w->squad.count; i++) {
        i32 s = squad_body(&w->squad, i)->slot;
        fighting |= s >= 0 && field_awake(w->field, (u32)s);
    }
    return fighting;
}

bool world_travel(World *w)
{
    bool to_valley = !world_squad_in_valley(w);
    // В бою не уйти; приказы атаки снимаются
    if (world_squad_fighting(w)) {
        log_info("ПЕРЕХОД: нельзя, отряд в бою");
        return false;
    }
    Squad *sq = &w->squad;
    for (u32 i = 0; i < sq->count; i++)
        if (squad_body(sq, i)->slot >= 0) field_cancel(w->field, (u32)squad_body(sq, i)->slot, false);
    if (to_valley) {
        squad_place(sq, w->valley->spawn, w->valley->spawn_heading, world_ground);
        log_info("ПЕРЕХОД: отряд в долине у реки");
    } else {
        squad_place(sq, squad_home(), squad_home_heading(), world_ground);
        log_info("ПЕРЕХОД: отряд в пустоши у арены");
    }
    // Перенос — не движение: кадр не должен тянуть отряд через всю карту между шагами
    for (u32 i = 0; i < sq->count; i++) {
        sq->selected[i] = false;
        w->people->last[sq->member[i]] = w->people->body[sq->member[i]];
    }
    return true;
}
