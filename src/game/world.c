#include "world.h"

#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "../core/prof.h"
#include "../defs/defs.h"
#include "../io/save.h"
#include "../world/map.h"
#include "names.h"
#include "ostov.h"
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
    mb_init(mb, perm, 400000, 1200000);
    ostov_build(mb);
    gpu_mesh_upload(&w->ostov, mb);
    log_info("МИР: Остов — %u треугольников", mb->icount / 3);
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
    w->near = ARENA_PUSH_ARRAY(perm, NearBands, 1);
    if (!near_init(w->near, p, w->field)) return false;
    memcpy(p->last, p->body, p->count * sizeof *p->body);

    // Места и дороги: пока три места лагеря и долины, отрядов вдали нет. Места лагеря — с севера, на песке:
    // дороги обходят зал, посёлок и древнюю стену
    Places *pl = &w->places;
    u32 hall = places_add(pl, "зал", 36.0f, -45.0f), town = places_add(pl, "Скудель", 78.0f, -32.0f);
    u32 valley = places_add(pl, "долина у реки", w->valley->spawn.x, w->valley->spawn.z);
    places_road(pl, hall, town);
    places_road(pl, hall, valley);
    places_road(pl, town, valley);
    if (!places_build(pl, perm)) return false;
    w->bands = ARENA_PUSH_ARRAY(perm, Bands, 1);
    bands_init(w->bands, perm, pl, seed, TICK_HZ);
    w->bands->log_left = 0xFFFFFFFFu;  // стычки и бегство — в журнал
    // Журнал событий: пишут отряды вдали и поле боя
    w->chron = ARENA_PUSH_ARRAY(perm, Chronicle, 1);
    w->bands->chron = w->field->chron = w->chron;
    w->strings = ARENA_PUSH_ARRAY(perm, char, WORLD_STRINGS);
    w->focus = squad_home();
    // Физика: лом расставлен в долине над землёй и ждёт, когда туда придут (до того висит, где поставлен)
    w->phys = ARENA_PUSH_ARRAY(perm, Phys, 1);
    phys_init(w->phys, perm);
    scrap_view_init(&w->scrap, mb, perm);
    w->ground = ARENA_PUSH_ARRAY(perm, Ground, 1);
    ground_init(w->ground, perm);
    loot_view_init(&w->loot, mb, perm);
    Rng pr;
    rng_seed(&pr, seed, 13);
    V3 vs = w->valley->spawn;
    f32 vh = w->valley->spawn_heading;
    V3 sp = v3(vs.x + sinf(vh) * 9.0f, 0.0f, vs.z + cosf(vh) * 9.0f);
    scrap_place(w->phys, &pr, sp, 14, world_ground);
    log_stat("ФИЗИКА: лом в долине у %.0f %.0f ждёт прихода", (f64)sp.x, (f64)sp.z);
    return true;
}

// Один шаг мира. Порядок систем постоянный: ходьба, арена, поле боя (видит тела после ходьбы), сутки
static void world_tick(World *w)
{
    People *p = w->people;
    w->chron->tick = w->tick;
    memcpy(p->last, p->body, p->count * sizeof *p->body);
    prof_zone(PZ_PEOPLE);
    people_update(p, &w->rng, &w->town_rng, TICK_DT, world_ground);
    squad_update(&w->squad, TICK_DT, world_ground);
    near_tick(w->near, w->bands, p, w->field, &w->squad, w->focus, TICK_DT, world_ground);
    prof_zone(PZ_ARENA);
    arena_view_tick(&w->aview, w->arena);
    prof_zone(PZ_FIELD);
    field_tick(w->field, world_ground);
    bands_tick(w->bands);
    // Физика вокруг своих и точки взгляда камеры. Лом по кнопке — тоже здесь: мир меняется только в шаге
    prof_zone(PZ_PHYS);
    // Вещи на земле: выронили и подняли в бою, бросили из мешка, подобрали по приказу
    loot_arena(w->ground, w->phys, w->arena, w->aview.in_bout, w->aview.center, w->aview.floor_y, w->tick);
    loot_field(w->ground, w->phys, w->field, world_ground, w->tick);
    loot_asks(w->ground, w->phys, &w->ground_ask, &w->squad, w->tick);
    loot_pickups(w->ground, w->phys, &w->squad, w->field);
    if (w->loot_drop) {
        loot_drop_test(w->ground, w->phys, v3(w->focus.x, world_ground(w->focus.x, w->focus.z), w->focus.z), w->loot_drop,
                       w->seed ^ w->tick);
        w->loot_drop = 0;
    }
    if (w->phys_drop) {
        Rng r;
        rng_seed(&r, w->seed ^ w->tick, 17);
        scrap_drop(w->phys, &r, v3(w->focus.x, world_ground(w->focus.x, w->focus.z), w->focus.z), w->phys_drop);
        w->phys_drop = 0;
    }
    V3 zone[2] = { w->focus, squad_body(&w->squad, (u32)MAX(0, squad_primary(&w->squad)))->pos };
    phys_zone(w->phys, zone, 2);
    phys_tick(w->phys, TICK_DT, world_ground);
    if (w->day_run) {
        // Время суток — целое число шагов от полуночи: сутки ровно DAY_TICKS, ошибка f32 не копится.
        // Ползунок может поставить любое время — оно встаёт на ближайший шаг
        u32 k = (u32)(w->tod * (f32)DAY_TICKS + 0.5f) + 1u;
        if (k >= DAY_TICKS) {
            k -= DAY_TICKS;
            w->day++;
        }
        w->tod = (f32)k / (f32)DAY_TICKS;
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

void world_bands_test(World *w)
{
    Bands *b = w->bands;
    if (b->count) {
        log_info("ОТРЯДЫ: пробные уже идут");
        return;
    }
    // Места мира: 0 — зал, 1 — Скудель, 2 — долина (world_init)
    static const u16 men[BAND_TIERS] = { 3, 2, 1 };
    i32 a = bands_add(b, 0, 1, men), c = bands_add(b, 1, 2, men);
    if (a < 0 || c < 0) return;
    bands_hostile(b, 0, 1);
    field_enmity(w->field, TEAM_BANDS + 0, TEAM_BANDS + 1);
    bands_send(b, (u32)a, 2);
    bands_send(b, (u32)c, 1);
    log_info("ОТРЯДЫ: два пробных отряда, у Скудели и в долине, идут навстречу");
}

void world_phys_drop(World *w) { w->phys_drop = 24; }

void world_loot_drop(World *w) { w->loot_drop = 8; }

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

// Раскладка таблиц этой сборки: другие размеры или ёмкости — сохранение не читается
static u64 world_layout(void)
{
    const u64 v[] = { SAVE_VERSION, sizeof(People), sizeof(Player), sizeof(CharSheet), sizeof(FighterDesc),
                      sizeof(CombatEvent), sizeof(Bands), sizeof(NearBands), sizeof(ArenaSession),
                      sizeof(ArenaView), sizeof(Squad), sizeof(Field), sizeof(Chronicle), PEOPLE_MAX, FIELD_MAX, BANDS_MAX,
                      CHRON_CAP, PART_COUNT, SK_COUNT, SLOT_COUNT, COMBAT_MAX_TEAMS, TICK_HZ,
                      PHYS_MAX, PHYS_SPHERES, GROUND_MAX, sizeof(GroundAsks) };
    return ser_fnv(SER_HASH_START, v, sizeof v);
}

// Обход мира в одном порядке для записи, чтения и суммы
static void world_ser(Ser *s, void *user)
{
    World *w = user;
    // Имена описаний — первыми: нет нужного описания — мир не читается вовсе
    DefMap map;
    defs_ser(s, &map);
    if (s->refused) return;
    SER(s, w->tick);
    SER(s, w->acc);
    SER(s, w->tod);
    SER(s, w->day);
    SER(s, w->rng);
    SER(s, w->town_rng);
    names_ser(s);
    chron_ser(s, w->chron);
    people_ser(s, w->people);
    squad_ser(s, &w->squad);
    field_ser(s, w->field);
    // Слот поля — номер в таблице людей: тела и листы поля указывают на тех же людей
    ser_check(s, w->field->w.count == w->people->count);
    arena_ser(s, w->arena);
    arena_view_ser(s, &w->aview);
    bands_ser(s, w->bands);
    near_ser(s, w->near, w->bands->count);
    phys_ser(s, w->phys);
    ground_ser(s, w->ground);
    ser_count(s, &w->ground_ask.n, GROUND_ASKS);
    SER(s, w->ground_ask.it);
    SER(s, w->ground_ask.at);
    SER(s, w->ground_ask.yaw);
    SER(s, w->ground_ask.who);
    // Описания с тех пор поменяли порядок (мод, правка файлов): номера вещей — под нынешние
    if (s->mode == SER_LOAD && !map.same && !s->bad) {
        for (u32 i = 0; i < w->people->count; i++) defs_remap_sheet(&w->people->sheet[i], &map);
        defs_remap_combat(&w->field->w, &map);
        defs_remap_combat(&w->arena->w, &map);
        for (u32 k = 0; k < ARENA_ROSTER; k++) defs_remap_desc(&w->arena->roster[k].d, &map);
        for (u32 k = 0; k < w->ground->count; k++) defs_remap_item(&w->ground->item[k], &map);
        for (u32 k = 0; k < w->ground_ask.n; k++) defs_remap_item(&w->ground_ask.it[k], &map);
    }
}

bool world_save(World *w, const char *path)
{
    f64 t0 = time_now();
    SaveInfo info = { .layout = world_layout(), .seed = w->seed, .tick = w->tick };
    if (!save_write(path, &info, world_ser, w)) return false;
    u32 mins = (u32)(w->tod * 24.0f * 60.0f);
    log_info("СОХРАНЕНИЕ: мир записан, день %u, %02u:%02u, %.2f МБ за %.0f мс", w->day, mins / 60 % 24, mins % 60,
             (f64)info.bytes / 1048576.0, (time_now() - t0) * 1000.0);
    return true;
}

bool world_load(World *w, const char *path)
{
    f64 t0 = time_now();
    SaveInfo info = { .layout = world_layout(), .seed = w->seed };
    if (!save_read(path, &info, world_ser, w, w->strings, WORLD_STRINGS)) return false;
    field_view_restore(w->field);
    arena_view_restore(&w->aview, w->arena);
    w->alpha = w->acc / TICK_DT;
    u32 mins = (u32)(w->tod * 24.0f * 60.0f);
    log_info("СОХРАНЕНИЕ: мир прочитан, день %u, %02u:%02u, за %.0f мс", w->day, mins / 60 % 24, mins % 60,
             (time_now() - t0) * 1000.0);
    return true;
}

u64 world_checksum(World *w)
{
    Ser s = { .mode = SER_HASH, .hash = SER_HASH_START };
    world_ser(&s, w);
    return s.hash;
}
