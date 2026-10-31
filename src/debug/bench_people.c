// Замер боя людей реестра без кадра (--bench battle512): два строя по 256 через реестр и поле боя, как в мире.
// Шаг мира делится на ходьбу и тик поля (бой), за ним — один проход поз всех людей (figures_draw без сеток).
// Цифры — база для шага 4 (docs/performance.md)
#include "bench.h"

#include <stdio.h>
#include <stdlib.h>

#include "../core/clock.h"
#include "../core/jobs.h"
#include "../core/log.h"
#include "../defs/defs.h"
#include "../sim/field.h"
#include "../view/figures.h"

#define PB_SECONDS 120
#define PB_FRAME_DT (1.0f / 60.0f)  // как будто кадр 60 к/с: позы вдали не реже кадра
#define PB_RANK 16                  // людей в шеренге
#define PB_ITEMS (GEAR_MAX_WORN + 4)

enum { PB_WALK, PB_FIELD, PB_POSE, PB_STAGES };
static const char *g_pb_name[PB_STAGES] = { "ХОДЬБА", "ПОЛЕ БОЯ", "ПОЗЫ" };

static f32 flat_ground(f32 x, f32 z) { (void)x; (void)z; return 0.0f; }

static int cmp_f32(const void *a, const void *b)
{
    f32 x = *(const f32 *)a, y = *(const f32 *)b;
    return (x > y) - (x < y);
}

int people_bench(u32 side_a, u32 side_b, u64 seed, u32 threads)
{
    if (side_a + side_b > PERSONS_MAX) {
        printf("БОЙ ЛЮДЕЙ: не больше %u на двоих (реестр людей)\n", PERSONS_MAX);
        side_b = side_a < PERSONS_MAX ? PERSONS_MAX - side_a : 0;
        side_a = MIN(side_a, PERSONS_MAX);
    }
    u32 n = side_a + side_b, ticks = PB_SECONDS * COMBAT_HZ;
    Arena mem;
    size_t bones = (size_t)(PERSONS_MAX + RING_BONE_SETS + MARK_SETS + 2) * RENDER_MAX_BONES * sizeof(M4);
    if (!arena_init(&mem, MB(24) + sizeof(Persons) + sizeof(Field) + sizeof(Figures) + bones)) return 1;
    if (!jobs_init(threads)) {
        arena_free(&mem);
        return 1;
    }
    Persons *ps = ARENA_PUSH_ARRAY(&mem, Persons, 1);
    persons_init(ps);
    Field *f = ARENA_PUSH_ARRAY(&mem, Field, 1);
    field_init(f, &mem, ps, seed);
    Figures *fig = ARENA_PUSH_ARRAY(&mem, Figures, 1);
    figures_bones(fig, &mem);
    // Сетки не нужны: позы считаются, элементы отрисовки только ссылаются на пустые сетки
    CharMeshes *m = ARENA_PUSH_ARRAY(&mem, CharMeshes, 1);
    m->gear = ARENA_PUSH_ARRAY(&mem, GpuMesh, CHAR_GEAR_MESHES);
    DrawItem *items = ARENA_PUSH_ARRAY(&mem, DrawItem, (size_t)PERSONS_MAX * PB_ITEMS + ARENA_MAX_BOUT * PB_ITEMS);
    PointLight lights[TORCH_LIGHTS_MAX];
    f32 *ms[PB_STAGES];
    for (u32 s = 0; s < PB_STAGES; s++) ms[s] = ARENA_PUSH_ARRAY(&mem, f32, ticks);

    // Два строя лицом друг к другу, шеренги по PB_RANK; оба сами нападают и идут на помощь своим
    Rng rng;
    rng_seed(&rng, seed, 512);
    for (u32 i = 0; i < n; i++) {
        u8 side = i >= side_a;
        u32 k = side ? i - side_a : i, row = k / PB_RANK;
        f32 x = ((f32)(k % PB_RANK) - (PB_RANK - 1) * 0.5f) * 1.4f + rng_range(&rng, -0.2f, 0.2f);
        f32 z = (side ? 1.0f : -1.0f) * (12.0f + (f32)row * 1.7f);
        u8 ff = FF_ASSIST | FF_AGGRO;
        u32 at = person_place(persons_add(ps, ROLE_WANDER, v3(x, 0, z), side ? PI_F : 0.0f, side, ff));
        ps->move[at].gait = GAIT_RUN;
        f32 roll = rng_f32(&rng);
        u8 weapon = g_weapon_pool[(u32)rng_range(&rng, 0, (f32)g_weapon_pool_count - 0.01f)];
        person_random(ps, at, &rng, side ? "ЧУЖОЙ" : "СВОЙ", roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2), weapon);
        ps->seed[at] = rng_f32(&rng) * 100.0f;
        ps->variant[at] = (u8)(i % BODY_KIND_COUNT);
        ps->height[at] = ps->width[at] = 1.0f;
        persons_dress(ps, at);
        field_join(f, at, side, ff);
    }
    log_set_echo(false);
    // Каждый идёт на того, кто стоит напротив, с обеих сторон; лишние втянутся сами
    for (u32 k = 0; k < MIN(side_a, side_b); k++) {
        field_attack(f, k, side_a + k);
        field_attack(f, side_a + k, k);
    }

    FigureView view = { .view_proj = m4_scale(v3(0.01f, 0.01f, 0.01f)), .eye = v3(0, 30, 0),
                        .light_dir = v3(0.3f, -1.0f, 0.2f), .dt = PB_FRAME_DT };
    u32 peak = 0;
    u64 posed = 0;
    f64 total0 = time_now();
    for (u32 t = 0; t < ticks; t++) {
        f64 t0 = time_now();
        persons_keep_last(ps);
        for (u32 i = 0; i < n; i++) {
            if (ps->move[i].ctrl == CTRL_FREE) ps->move[i].vmax = g_gait_speed[GAIT_RUN];
            person_walk(ps, i, COMBAT_DT, flat_ground);
        }
        f64 t1 = time_now();
        field_tick(f, flat_ground);
        f64 t2 = time_now();
        field_present(f, 1.0f, COMBAT_DT);
        figures_view(fig, ps, f, 1.0f, flat_ground);
        view.time = (f32)t * COMBAT_DT;
        u32 nl = 0;
        figures_draw(fig, m, ps, f, NULL, NULL, &view, items, lights, &nl);
        f64 t3 = time_now();
        ms[PB_WALK][t] = (f32)((t1 - t0) * 1e3);
        ms[PB_FIELD][t] = (f32)((t2 - t1) * 1e3);
        ms[PB_POSE][t] = (f32)((t3 - t2) * 1e3);
        peak = MAX(peak, f->fights);
        posed += fig->posed;
    }
    f64 total = time_now() - total0;
    log_set_echo(true);

    u32 out[2] = { 0, 0 };
    u64 sum = SER_HASH_START;
    for (u32 i = 0; i < n; i++) {
        if (fs_out(f->w.state[i])) out[i >= side_a]++;
        sum = ser_fnv(sum, &ps->x[i], sizeof ps->x[i]);
        sum = ser_fnv(sum, &ps->z[i], sizeof ps->z[i]);
        sum = ser_fnv(sum, &f->w.state[i], sizeof f->w.state[i]);
        for (u32 p = 0; p < PART_COUNT; p++) sum = ser_fnv(sum, &ps->hp[p][i], sizeof ps->hp[p][i]);
    }
    printf("БОЙ ЛЮДЕЙ %u × %u, %u С (%u ШАГОВ), SEED %llu, ПОТОКОВ %u, ВЕСЬ ЗАМЕР %.2f С\n", side_a, side_b, PB_SECONDS,
           ticks, (unsigned long long)seed, jobs_threads(), total);
    f64 all = 0.0;
    for (u32 s = 0; s < PB_STAGES; s++) {
        f64 avg = 0.0, worst = 0.0;
        for (u32 t = 0; t < ticks; t++) avg += ms[s][t], worst = MAX(worst, (f64)ms[s][t]);
        avg /= ticks;
        all += avg;
        qsort(ms[s], ticks, sizeof(f32), cmp_f32);
        printf("%s: средний %.3f мс, 99%% %.3f мс, худший %.3f мс\n", g_pb_name[s], avg, (f64)ms[s][ticks * 99 / 100], worst);
    }
    printf("ВСЕГО НА ШАГ %.3f мс; поз за проход в среднем %.0f; в бою до %u; выбыло %u и %u; сумма %016llx\n", all,
           (f64)posed / ticks, peak, out[0], out[1], (unsigned long long)sum);
    jobs_shutdown();
    arena_free(&mem);
    return 0;
}

// Один бой копий. fear: мораль, отход и сдача как в игре. Иначе только раны.
static int mirror_once(u32 side, u64 seed, u32 threads, bool fear, u32 out[2])
{
    u32 n = side * 2, ticks = PB_SECONDS * COMBAT_HZ;
    Arena mem;
    if (!arena_init(&mem, MB(16) + sizeof(Persons) + sizeof(Field))) return 1;
    if (!jobs_init(threads)) {
        arena_free(&mem);
        return 1;
    }
    Persons *ps = ARENA_PUSH_ARRAY(&mem, Persons, 1);
    persons_init(ps);
    Field *f = ARENA_PUSH_ARRAY(&mem, Field, 1);
    field_init(f, &mem, ps, seed);
    f->w.fear = fear;
    for (u32 i = 0; i < n; i++) {
        u8 half = i >= side;
        u32 k = half ? i - side : i, row = k / PB_RANK;
        u8 sd = half;
        Rng one;
        rng_seed(&one, seed, 1000u + k);
        f32 x = ((f32)(k % PB_RANK) - (PB_RANK - 1) * 0.5f) * 1.4f + rng_range(&one, -0.2f, 0.2f);
        f32 z = (sd ? 1.0f : -1.0f) * (12.0f + (f32)row * 1.7f);
        u8 ff = FF_ASSIST | FF_AGGRO;
        u32 at = person_place(persons_add(ps, ROLE_WANDER, v3(x, 0, z), sd ? PI_F : 0.0f, sd, ff));
        ps->move[at].gait = GAIT_RUN;
        f32 roll = rng_f32(&one);
        person_random(ps, at, &one, sd ? "ЧУЖОЙ" : "СВОЙ", roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2), g_weapon_pool[0]);
        ps->seed[at] = rng_f32(&one) * 100.0f;
        ps->variant[at] = (u8)(k % BODY_KIND_COUNT);
        ps->height[at] = ps->width[at] = 1.0f;
        persons_dress(ps, at);
        field_join(f, at, sd, ff);
    }
    log_set_echo(false);
    for (u32 k = 0; k < side; k++) {
        field_attack(f, k, side + k);
        field_attack(f, side + k, k);
    }
    for (u32 t = 0; t < ticks; t++) {
        persons_keep_last(ps);
        for (u32 i = 0; i < n; i++) {
            if (ps->move[i].ctrl == CTRL_FREE) ps->move[i].vmax = g_gait_speed[GAIT_RUN];
            person_walk(ps, i, COMBAT_DT, flat_ground);
        }
        field_tick(f, flat_ground);
    }
    log_set_echo(true);
    out[0] = out[1] = 0;
    for (u32 i = 0; i < n; i++)
        if (fs_out(f->w.state[i])) out[i >= side]++;
    jobs_shutdown();
    arena_free(&mem);
    return 0;
}

// Без морали, одно зерно: разница потерь не больше 10% стороны.
int mirror_bench(u32 side, u64 seed, u32 threads)
{
    if (side == 0 || side * 2 > PERSONS_MAX) {
        printf("ЗЕРКАЛО: сторона от 1 до %u\n", PERSONS_MAX / 2);
        return 1;
    }
    u32 out[2];
    if (mirror_once(side, seed, threads, false, out)) return 1;
    u32 diff = out[0] > out[1] ? out[0] - out[1] : out[1] - out[0];
    u32 allow = side / 10;
    int bad = diff > allow;
    printf("ЗЕРКАЛО БЕЗ МОРАЛИ %u × %u, SEED %llu: выбыло %u и %u, разница %u, допуск %u — %s\n",
           side, side, (unsigned long long)seed, out[0], out[1], diff, allow, bad ? "ПЕРЕКОС" : "РОВНО");
    return bad ? 2 : 0;
}

// С моралью, 16 зерён 1337..1352. Каждая сторона побеждает 5..11 раз. Сумма потерь не критерий.
int mirror_fear_bench(u32 side, u64 seed, u32 threads)
{
    (void)seed;
    if (side == 0 || side * 2 > PERSONS_MAX) {
        printf("ЗЕРКАЛО: сторона от 1 до %u\n", PERSONS_MAX / 2);
        return 1;
    }
    enum { N = 16, SEED0 = 1337 };
    u32 wins[2] = { 0, 0 };
    for (u32 k = 0; k < N; k++) {
        u32 out[2];
        u64 s = SEED0 + k;
        if (mirror_once(side, s, threads, true, out)) return 1;
        if (out[0] < out[1]) wins[0]++;
        else if (out[1] < out[0]) wins[1]++;
        printf("ЗЕРКАЛО С МОРАЛЬЮ seed %llu: выбыло %u и %u\n", (unsigned long long)s, out[0], out[1]);
    }
    int bad = wins[0] < 5 || wins[0] > 11 || wins[1] < 5 || wins[1] > 11;
    printf("ЗЕРКАЛО С МОРАЛЬЮ зёрна %u..%u: победы %u и %u — %s\n",
           SEED0, SEED0 + N - 1, wins[0], wins[1], bad ? "ПЕРЕКОС" : "РОВНО");
    return bad ? 2 : 0;
}
