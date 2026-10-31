#include "scene.h"

#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "../content/shapes.h"
#include "../content/town_build.h"

// Постройка в одну сетку: mb заводится заново, память сборки отдаётся после загрузки
#define BUILD_VERTS   400000
#define BUILD_INDICES 1200000
#define BATCH_SPARE   40000  // столько вершин в запасе — следующий экземпляр уже в новый пакет

// Статические пакеты объектов. С тенью — у каждой локации в каждом чанке свой: пакет вне кадра и вне
// области теней отсекается целиком. Без тени (силуэты у горизонта) — один на локацию: их видно всегда
typedef struct Batching {
    Scene       *s;
    MeshBuilder *mb;
    Arena       *perm;
    size_t       mark;
    u32          tris;
} Batching;

static bool flush(Batching *b, bool ns)
{
    MeshBuilder *mb = b->mb;
    if (!mb->vcount) return true;
    Scene *s = b->s;
    if (s->batches == SCENE_BATCHES) {
        log_error("МИР: статических пакетов больше %u", SCENE_BATCHES);
        return false;
    }
    if (!gpu_mesh_upload(&s->batch[s->batches], mb)) return false;
    s->batch_no_shadow[s->batches++] = ns;
    b->tris += mb->icount / 3;
    b->perm->used = b->mark;
    mb_init(mb, b->perm, BUILD_VERTS, BUILD_INDICES);
    return true;
}

// Экземпляры [from, to) локации g с тенью или без в пакеты
static bool batch_run(Batching *b, const Objects *o, u32 from, u32 to, u32 g, bool ns)
{
    bool any = false;
    for (u32 i = from; i < to && !any; i++)
        any = o->group[i] == g && ((o->flags[i] & OBJF_NO_SHADOW) != 0) == ns && g_objects[o->def[i]].shape;
    if (!any) return true;
    mb_init(b->mb, b->perm, BUILD_VERTS, BUILD_INDICES);
    for (u32 i = from; i < to; i++) {
        if (o->group[i] != g || ((o->flags[i] & OBJF_NO_SHADOW) != 0) != ns) continue;
        if (b->mb->vcount > BUILD_VERTS - BATCH_SPARE && !flush(b, ns)) return false;
        shape_build(b->mb, o, i);
    }
    bool ok = flush(b, ns);
    b->perm->used = b->mark;
    return ok;
}

static bool batches_build(Scene *s, MeshBuilder *mb, Arena *perm, const Objects *o)
{
    if (!shapes_bind()) return false;
    Batching b = { .s = s, .mb = mb, .perm = perm, .mark = perm->used };
    for (u32 c = 0; c < OBJ_SIDE * OBJ_SIDE; c++)
        if (o->chunk[c] != o->chunk[c + 1])
            for (u32 g = 0; g < o->groups; g++)
                if (!batch_run(&b, o, o->chunk[c], o->chunk[c + 1], g, false)) return false;
    for (u32 g = 0; g < o->groups; g++)
        if (!batch_run(&b, o, 0, o->count, g, true)) return false;
    log_info("МИР: объекты — пакетов %u, треугольников %u", s->batches, b.tris);
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
    // Статика пустоши — объекты по заготовкам (зал, ринг, Остов, Скудель, горизонт); места жителей — вместе с посёлком
    s->batch = ARENA_PUSH_ARRAY(perm, GpuMesh, SCENE_BATCHES);
    s->batch_no_shadow = ARENA_PUSH_ARRAY(perm, bool, SCENE_BATCHES);
    if (!batches_build(s, mb, perm, objs)) return false;
    town_spots(town);
    log_info("МИР: Скудель — мест жителей %u", town->spot_count);
    scrap_view_init(&s->scrap, mb, perm);
    loot_view_init(&s->loot, mb, perm);
    return true;
}

u32 scene_statics(const Scene *s, DrawItem *out)
{
    u32 n = 0;
    for (u32 i = 0; i < s->batches; i++) out[n++] = (DrawItem){ .mesh = &s->batch[i], .no_shadow = s->batch_no_shadow[i] };
    return n;
}
