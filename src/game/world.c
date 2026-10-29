#include "world.h"

#include "../core/clock.h"
#include "../core/log.h"
#include "../core/prof.h"
#include "../world/valley.h"
#include "testroom.h"

#define CROWD_SIZE 9
#define TOWN_WALKERS 20
#define ARENA_SHOW_AFTER_BOUT_S 12.0f

// Где сейчас отряд: пустошь с залом и ареной или долина у реки
static bool g_in_valley;

f32 world_ground(f32 x, f32 z) { return g_in_valley ? valley_height(x, z) : testroom_ground(x, z); }
bool world_in_valley(void) { return g_in_valley; }

// Своя точка появления в пустоши: у края зала, лицом к арене
static V3  squad_home(void) { return v3(9.0f, 0.0f, 22.0f); }
static f32 squad_home_heading(void) { return atan2f(ARENA_CENTER.x - 9.0f, ARENA_CENTER.z - 22.0f); }

bool world_init(World *w, Arena *perm, MeshBuilder *mb, u64 seed, f32 tod, bool day_run)
{
    *w = (World){ .seed = seed, .tod = tod, .day = 1, .day_run = day_run };
    rng_seed(&w->rng, seed, 1);
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
    w->valley = ARENA_PUSH_ARRAY(perm, ValleyView, 1);
    return true;
}

void world_update(World *w, f32 dt)
{
    w->time += dt;
    prof_zone(PZ_PEOPLE);
    people_update(w->people, &w->rng, &w->town_rng, dt, testroom_ground);
    squad_update(&w->squad, dt, world_ground);
    prof_zone(PZ_ARENA);
    arena_view_update(&w->aview, w->arena, dt);
    prof_zone(PZ_FIELD);
    field_update(w->field, dt, world_ground);
    if (w->day_run) {
        w->tod += dt / DAY_LENGTH_S;
        if (w->tod >= 1.0f) {
            w->tod -= 1.0f;
            w->day++;
        }
    }
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

bool world_travel(World *w, Arena *perm)
{
    bool to_valley = !g_in_valley;
    if (to_valley && !w->valley->built) {
        f64 t0 = time_now();
        if (!valley_view_build(w->valley, perm, w->seed)) {
            log_error("ДОЛИНА: не построена");
            return false;
        }
        log_stat("ДОЛИНА: построена за %.2f с", time_now() - t0);
    }
    // В бою не уйти; приказы атаки снимаются
    if (world_squad_fighting(w)) {
        log_info("ПЕРЕХОД: нельзя, отряд в бою");
        return false;
    }
    Squad *sq = &w->squad;
    for (u32 i = 0; i < sq->count; i++)
        if (squad_body(sq, i)->slot >= 0) field_cancel(w->field, (u32)squad_body(sq, i)->slot, false);
    g_in_valley = to_valley;
    if (g_in_valley) {
        squad_place(sq, w->valley->spawn, w->valley->spawn_heading, world_ground);
        log_info("ПЕРЕХОД: отряд в долине у реки");
    } else {
        squad_place(sq, squad_home(), squad_home_heading(), world_ground);
        log_info("ПЕРЕХОД: отряд в пустоши у арены");
    }
    for (u32 i = 0; i < sq->count; i++) sq->selected[i] = false;
    return true;
}
