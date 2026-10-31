#include "scene.h"

#include <string.h>

#include "../core/clock.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../content/hall.h"
#include "../content/ostov.h"
#include "../content/ring.h"
#include "../content/shapes.h"
#include "../content/town_build.h"

// Постройка в одну сетку: mb заводится заново, память сборки отдаётся после загрузки
#define BUILD_VERTS   400000
#define BUILD_INDICES 1200000
#define BATCH_SPARE   40000  // столько вершин в запасе — следующий экземпляр уже в новый пакет

// Статические пакеты объектов: у каждой локации в каждом чанке свой, отдельно с тенью и без. Пакет —
// одна сетка: вне кадра и вне области теней отсекается целиком, как раньше посёлок
static bool batches_build(Scene *s, MeshBuilder *mb, Arena *perm, const Objects *o)
{
    if (!shapes_bind()) return false;
    size_t mark = perm->used;
    u32 tris = 0;
    for (u32 c = 0; c < OBJ_SIDE * OBJ_SIDE; c++) {
        u32 a = o->chunk[c], b = o->chunk[c + 1];
        if (a == b) continue;
        for (u32 g = 0; g < o->groups; g++)
            for (u32 ns = 0; ns < 2; ns++) {
                bool any = false;
                for (u32 i = a; i < b && !any; i++)
                    any = o->group[i] == g && ((o->flags[i] & OBJF_NO_SHADOW) != 0) == ns && g_objects[o->def[i]].shape;
                if (!any) continue;
                mb_init(mb, perm, BUILD_VERTS, BUILD_INDICES);
                for (u32 i = a; i <= b; i++) {
                    bool last = i == b;
                    if (!last && (o->group[i] != g || ((o->flags[i] & OBJF_NO_SHADOW) != 0) != ns)) continue;
                    if ((last || mb->vcount > BUILD_VERTS - BATCH_SPARE) && mb->vcount) {
                        if (s->batches == SCENE_BATCHES) {
                            log_error("МИР: статических пакетов больше %u", SCENE_BATCHES);
                            return false;
                        }
                        if (!gpu_mesh_upload(&s->batch[s->batches], mb)) return false;
                        s->batch_no_shadow[s->batches++] = ns;
                        tris += mb->icount / 3;
                        perm->used = mark;
                        mb_init(mb, perm, BUILD_VERTS, BUILD_INDICES);
                    }
                    if (!last) shape_build(mb, o, i);
                }
                perm->used = mark;
            }
    }
    log_info("МИР: объекты — пакетов %u, треугольников %u", s->batches, tris);
    return true;
}

bool scene_init(Scene *s, MeshBuilder *mb, Arena *perm, u64 seed, const Objects *objs, Town *town)
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
    s->batch = ARENA_PUSH_ARRAY(perm, GpuMesh, SCENE_BATCHES);
    s->batch_no_shadow = ARENA_PUSH_ARRAY(perm, bool, SCENE_BATCHES);
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
    // Объекты по заготовкам (Скудель); места жителей — вместе с посёлком
    if (!batches_build(s, mb, perm, objs)) return false;
    town_spots(town);
    log_info("МИР: Скудель — мест жителей %u", town->spot_count);
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
    u32 n = 0;
    out[n++] = (DrawItem){ .mesh = &s->hall };
    out[n++] = (DrawItem){ .mesh = &s->far, .no_shadow = true };
    for (u32 i = 0; i < s->batches; i++) out[n++] = (DrawItem){ .mesh = &s->batch[i], .no_shadow = s->batch_no_shadow[i] };
    out[n++] = (DrawItem){ .mesh = &s->ostov };
    return n;
}
