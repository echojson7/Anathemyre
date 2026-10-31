#include "scene.h"

#include <string.h>

#include "../core/clock.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../content/hall.h"
#include "../content/ostov.h"
#include "../content/ring.h"
#include "../content/town_build.h"
#include "../defs/material.h"

// Постройка в одну сетку: mb заводится заново, память сборки отдаётся после загрузки
#define BUILD_VERTS   400000
#define BUILD_INDICES 1200000

bool scene_init(Scene *s, MeshBuilder *mb, Arena *perm, u64 seed, Town *town)
{
    memset(s, 0, sizeof *s);
    // Долина: подробная карта высот, вода, руины, растения (карта мира уже есть)
    f64 t0 = time_now();
    s->valley = ARENA_PUSH_ARRAY(perm, ValleyView, 1);
    if (!valley_view_build(s->valley, perm, seed)) {
        log_error("ДОЛИНА: не построена");
        return false;
    }
    log_stat("МИР: долина %.2f с", time_now() - t0);
    // Статика пустоши. У построек свои потоки случайных чисел: вид не зависит от мира и наоборот.
    // Посёлок отдельной сеткой: вне кадра и вне области теней отсекается целиком
    Rng rng;
    rng_seed(&rng, seed, 1);
    size_t mark = perm->used;
    mb_init(mb, perm, BUILD_VERTS, BUILD_INDICES);
    hall_build_near(mb, &rng);
    ring_build(mb, ARENA_CENTER);
    gpu_mesh_upload(&s->hall, mb);
    u32 tris = mb->icount / 3;
    perm->used = mark;
    mb_init(mb, perm, BUILD_VERTS, BUILD_INDICES);
    hall_build_far(mb, &rng);
    gpu_mesh_upload(&s->far, mb);
    log_info("МИР: зал %u треугольников, рельеф и мегаструктуры %u", tris, mb->icount / 3);
    perm->used = mark;
    rng_seed(&rng, seed, 7);
    mb_init(mb, perm, BUILD_VERTS, BUILD_INDICES);
    memset(town, 0, sizeof *town);
    town_build(mb, &rng, town);
    gpu_mesh_upload(&s->town, mb);
    log_info("МИР: Скудель — %u треугольников, домов %u, прилавков %u", mb->icount / 3, town->houses, town->stalls);
    perm->used = mark;
    mb_init(mb, perm, BUILD_VERTS, BUILD_INDICES);
    ostov_build(mb);
    gpu_mesh_upload(&s->ostov, mb);
    log_info("МИР: Остов — %u треугольников", mb->icount / 3);
    perm->used = mark;
    scrap_view_init(&s->scrap, mb, perm);
    loot_view_init(&s->loot, mb, perm);
    return true;
}

u32 scene_statics(const Scene *s, DrawItem *out)
{
    out[0] = (DrawItem){ .mesh = &s->hall };
    out[1] = (DrawItem){ .mesh = &s->far, .no_shadow = true };
    out[2] = (DrawItem){ .mesh = &s->town };
    out[3] = (DrawItem){ .mesh = &s->ostov };
    return SCENE_STATICS;
}

u32 scene_hall_lights(PointLight *out, u32 max)
{
    u32 n = 0;
    for (u32 k = 0; k < HALL_LAMPS && n < max; k++)
        out[n++] = (PointLight){ .pos = v3_add(g_hall_lamp[k], v3_mul(HALL_LAMP_DIR, 0.05f)), .radius = 6.0f,
                                 .color = v3_mul(v3(1.0f, 0.70f, 0.36f), 1.5f), .cone = 0.45f, .dir = HALL_LAMP_DIR,
                                 .glow = 0.0009f };
    return n;
}

u32 scene_ring_lights(const ArenaRing *v, f32 night, f32 time, PointLight *out)
{
    u32 n = 0;
    V3 c = v->center;
    for (u32 k = 0; k < RING_LIGHTS; k++) {
        // Зажигаются не разом; пока лампа разгорается, она мигает
        f32 on = smoothstepf(0.30f + 0.06f * (f32)k, 0.42f + 0.06f * (f32)k, night);
        if (on <= 0.0f) continue;
        if (on < 1.0f && hashf((u32)(time * 9.0f) * 7u + k) < 0.35f) continue;
        // Дрожь напряжения: свет едва заметно дышит, изредка проседает
        f32 hum = 0.96f + 0.04f * sinf(time * 13.0f + (f32)k * 2.1f);
        if (hashf((u32)(time * 4.0f) * 13u + k * 101u) < 0.02f) hum *= 0.6f;
        PointLight *l = &out[n++];
        if (k < 2) {
            // Прожектор на мачте: конусом в середину ринга (как в content/ring.c)
            f32 a = PI_F * (k ? 1.375f : 0.375f);
            V3 dir = v3(sinf(a), 0.0f, cosf(a));
            l->pos = v3(c.x + dir.x * 9.3f, 10.0f, c.z + dir.z * 9.3f);
            l->dir = v3_norm(v3_sub(v3(c.x, v->floor_y, c.z), l->pos));
            // Узкий конус с чёткой кромкой: на полу ринга лежит круг света, за ним темно
            l->cone = 0.96f;
            l->radius = 30.0f;
            l->color = v3_mul(mat_c(g_mat.lamp_white), 90.0f * on * hum);
            l->glow = 0.00012f;
        } else {
            // Лампа в сетке на стойке ворот
            V3 p = ring_corner(c, RING_FENCE_R, (int)k + 1, 2.6f);
            V3 in = v3_norm(v3(c.x - p.x, 0.0f, c.z - p.z));
            l->pos = v3_add(p, v3_mul(in, 0.2f));
            l->dir = v3(0, -1, 0);
            l->cone = -1.0f;
            l->radius = 9.0f;
            l->color = v3_mul(mat_c(g_mat.lamp_white), 2.2f * on * hum);
            l->glow = 0.0015f;
        }
    }
    return n;
}
