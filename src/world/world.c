#include "world.h"

#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "../core/prof.h"
#include "../defs/defs.h"
#include "../io/save.h"
#include "map.h"
#include "../sim/arena_ring.h"
#include "../sim/names.h"
#include "crowd.h"
#include "valley.h"

#define CROWD_SIZE 9
#define TOWN_WALKERS 20

bool world_squad_in_valley(const World *w)
{
    V3 p = squad_pos(&w->squad, (u32)MAX(0, squad_primary(&w->squad)));
    return map_valley(p.x, p.z) > 0.5f;
}

// Своя точка появления в пустоши: у края зала, лицом к арене
static V3  squad_home(void) { return v3(9.0f, 0.0f, 22.0f); }
static f32 squad_home_heading(void) { return atan2f(ARENA_CENTER.x - 9.0f, ARENA_CENTER.z - 22.0f); }

bool world_init(World *w, Arena *perm, u64 seed, f32 tod, bool day_run, const Town *town, GroundFn ground)
{
    *w = (World){ .seed = seed, .tod = tod, .day = 1, .day_run = day_run, .floor = ground };
    // Свои потоки случайных чисел: постройки (view/scene.c) берут свои, мир от них не зависит
    rng_seed(&w->rng, seed, 2);
    rng_seed(&w->town_rng, seed, 8);
    // Отряд появляется в долине на восточном берегу, ниже акведука, лицом вверх по течению
    w->valley_spawn = v3_add(valley_spawn(), VALLEY_ORIGIN);
    w->valley_heading = PI_F;

    // Люди. Порядок в реестре — порядок шага и отрисовки; от него зависят случайные числа
    Persons *ps = w->persons = ARENA_PUSH_ARRAY(perm, Persons, 1);
    persons_init(ps);
    People *p = w->people = ARENA_PUSH_ARRAY(perm, People, 1);
    people_init(p, ps);
    crowd_add(p, &w->rng, CROWD_SIZE, ground);
    // Зрители — одна толпа, Скудель — один посёлок: команды за бродящими
    watchers_add(p, ARENA_CENTER, RING_FLOOR_Y, RING_SPECTATORS, TEAM_WANDER + CROWD_SIZE, ground);
    town_folk_add(p, town, &w->town_rng, TOWN_WALKERS, BODY_KIND_COUNT, TEAM_WANDER + CROWD_SIZE + 1);
    u32 folk = 0;
    for (u32 i = 0; i < ps->count; i++) folk += ps->role[i] == ROLE_TOWN;
    log_info("СКУДЕЛЬ: жителей %u", folk);
    mobs_add(p, &w->rng, ground);
    squad_init(&w->squad, ps, &w->rng, squad_home(), squad_home_heading(), w->floor);
    // Поле боя: все люди, слот = место в реестре
    w->field = ARENA_PUSH_ARRAY(perm, Field, 1);
    field_init(w->field, perm, ps, seed);
    for (u32 i = 0; i < ps->count; i++) field_join(w->field, i, ps->team[i], ps->fflags[i]);
    w->near = ARENA_PUSH_ARRAY(perm, NearBands, 1);
    if (!near_init(w->near, ps, w->field)) return false;
    persons_keep_last(ps);

    // Места и дороги: пока три места лагеря и долины, отрядов вдали нет. Места лагеря — с севера, на песке:
    // дороги обходят зал, посёлок и древнюю стену
    Places *pl = &w->places;
    u32 hall = places_add(pl, "зал", 36.0f, -45.0f), skudel = places_add(pl, "Скудель", 78.0f, -32.0f);
    u32 valley = places_add(pl, "долина у реки", w->valley_spawn.x, w->valley_spawn.z);
    places_road(pl, hall, skudel);
    places_road(pl, hall, valley);
    places_road(pl, skudel, valley);
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
    w->ground = ARENA_PUSH_ARRAY(perm, Ground, 1);
    ground_init(w->ground, perm);
    Rng pr;
    rng_seed(&pr, seed, 13);
    V3 vs = w->valley_spawn;
    f32 vh = w->valley_heading;
    V3 sp = v3(vs.x + sinf(vh) * 9.0f, 0.0f, vs.z + cosf(vh) * 9.0f);
    scrap_place(w->phys, &pr, sp, 14, w->floor);
    log_stat("ФИЗИКА: лом в долине у %.0f %.0f ждёт прихода", (f64)sp.x, (f64)sp.z);
    return true;
}

// Один шаг мира. Порядок систем постоянный: ходьба, поле боя (видит тела после ходьбы), вещи, физика, сутки
static void world_tick(World *w)
{
    w->chron->tick = w->tick;
    persons_keep_last(w->persons);
    prof_zone(PZ_PEOPLE);
    people_update(w->people, &w->rng, &w->town_rng, TICK_DT, w->floor);
    squad_update(&w->squad, TICK_DT, w->floor);
    near_tick(w->near, w->bands, w->persons, w->field, &w->squad, w->focus, TICK_DT, w->floor);
    prof_zone(PZ_FIELD);
    field_tick(w->field, w->floor);
    bands_tick(w->bands);
    if (w->hook) w->hook(w, w->hook_user);
    // Физика вокруг своих и точки взгляда камеры
    prof_zone(PZ_PHYS);
    // Вещи на земле: выронили и подняли в бою, бросили из мешка, подобрали по приказу
    loot_field(w->ground, w->phys, w->field, w->floor, w->tick);
    loot_asks(w->ground, w->phys, &w->ground_ask, &w->squad, w->tick);
    loot_pickups(w->ground, w->phys, &w->squad, w->field);
    V3 zone[2] = { w->focus, squad_pos(&w->squad, (u32)MAX(0, squad_primary(&w->squad))) };
    phys_zone(w->phys, zone, 2);
    phys_tick(w->phys, TICK_DT, w->floor);
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
        fighting |= field_awake(w->field, squad_at(&w->squad, i));
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
        field_cancel(w->field, squad_at(sq, i), false);
    if (to_valley) {
        squad_place(sq, w->valley_spawn, w->valley_heading, w->floor);
        log_info("ПЕРЕХОД: отряд в долине у реки");
    } else {
        squad_place(sq, squad_home(), squad_home_heading(), w->floor);
        log_info("ПЕРЕХОД: отряд в пустоши у арены");
    }
    // Перенос — не движение: кадр не должен тянуть отряд через всю карту между шагами
    for (u32 i = 0; i < sq->count; i++) {
        sq->selected[i] = false;
        w->persons->last[squad_at(sq, i)] = person_body(w->persons, squad_at(sq, i));
    }
    return true;
}

// Раскладка таблиц этой сборки: другие размеры или ёмкости — сохранение не читается
static u64 world_layout(void)
{
    const u64 v[] = { SAVE_VERSION, sizeof(Persons), sizeof(People), sizeof(Player), sizeof(CharSheet), sizeof(FighterDesc),
                      sizeof(CombatEvent), sizeof(Bands), sizeof(NearBands), sizeof(Squad), sizeof(Field), sizeof(Chronicle), PERSONS_MAX, PEOPLE_MAX, FIELD_MAX, BANDS_MAX,
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
    persons_ser(s, w->persons);
    people_ser(s, w->people);
    squad_ser(s, &w->squad);
    field_ser(s, w->field);
    // Слот поля — место в реестре
    ser_check(s, w->field->w.count == w->persons->count);
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
        for (u32 i = 0; i < w->persons->count; i++) defs_remap_sheet(&w->persons->sheet[i], &map);
        defs_remap_combat(&w->field->w, &map);
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
    persons_relook_all(w->persons);
    field_view_restore(w->field);
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
