#include "shapes.h"

#include <string.h>

#include "../core/log.h"
#include "props.h"

// ---------------------------------------------------------------- общие формы: коробка и предметы props.c

// Коробка материалом 0 с разбросом tint; точка экземпляра — середина
static void sh_box(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    prop_box(c->mb, c->pos, c->size, c->yaw);
}

static void sh_crate_stack(const ShapeCtx *c) { prop_crate_stack(c->mb, c->r, c->pos, c->yaw); }
static void sh_barrel(const ShapeCtx *c) { prop_barrel(c->mb, c->r, c->pos, c->arg != 0); }
static void sh_clutter(const ShapeCtx *c) { prop_clutter(c->mb, c->r, c->pos, c->yaw); }
static void sh_lamp_post(const ShapeCtx *c) { prop_lamp_post(c->mb, c->r, c->pos, c->yaw); }
static void sh_rug(const ShapeCtx *c) { prop_rug(c->mb, c->r, c->pos, c->yaw); }
static void sh_fire_pit(const ShapeCtx *c) { prop_fire_pit(c->mb, c->r, c->pos); }
static void sh_scrap_heap(const ShapeCtx *c) { prop_scrap_heap(c->mb, c->r, c->pos, c->size.x); }
static void sh_water_tower(const ShapeCtx *c) { prop_water_tower(c->mb, c->r, c->pos, c->size.y); }
static void sh_ladder(const ShapeCtx *c) { prop_ladder(c->mb, c->L, v3(0, 0, 0), c->size.y); }

// Протяжённые: от точки экземпляра вдоль +x на size.x, с подъёмом size.y к концу
static V3 far_end(const ShapeCtx *c) { return m4_point(c->L, v3(c->size.x, c->size.y, 0)); }
static void sh_fence(const ShapeCtx *c) { prop_fence(c->mb, c->r, c->pos, far_end(c)); }
static void sh_railing(const ShapeCtx *c) { prop_railing(c->mb, c->r, c->pos, far_end(c)); }
// Провод: size.z — провис, arg — что висит (0 голый, 1 тряпки, 2 лампа)
static void sh_cable(const ShapeCtx *c) { prop_cable(c->mb, c->r, c->pos, far_end(c), c->size.z, c->arg); }
// Труба: size.z — радиус
static void sh_pipe(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    mb_cylinder(c->mb, c->pos, far_end(c), c->size.z, c->size.z >= 0.4f ? 12 : 10);
}
// Стояк: вверх на size.y, радиус size.x
static void sh_pipe_up(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    mb_cylinder(c->mb, c->pos, v3_add(c->pos, v3(0, c->size.y, 0)), c->size.x, c->size.x >= 0.4f ? 12 : c->size.x >= 0.2f ? 10 : 5);
}

static const Shape g_common[] = {
    { "box", sh_box, 0 },
    { "crate_stack", sh_crate_stack, 0 },
    { "barrel", sh_barrel, 0 },
    { "barrel_tipped", sh_barrel, 1 },
    { "clutter", sh_clutter, 0 },
    { "lamp_post", sh_lamp_post, 0 },
    { "rug", sh_rug, 0 },
    { "fire_pit", sh_fire_pit, 0 },
    { "scrap_heap", sh_scrap_heap, 0 },
    { "water_tower", sh_water_tower, 0 },
    { "ladder", sh_ladder, 0 },
    { "fence", sh_fence, 0 },
    { "railing", sh_railing, 0 },
    { "cable", sh_cable, 0 },
    { "cable_rags", sh_cable, 1 },
    { "cable_lamp", sh_cable, 2 },
    { "pipe", sh_pipe, 0 },
    { "pipe_up", sh_pipe_up, 0 },
};

// ---------------------------------------------------------------- поиск по имени

static const Shape *s_shape_of[OBJDEF_CAP];

static const Shape *find(const char *name)
{
    for (u32 i = 0; i < ARRAY_COUNT(g_common); i++)
        if (!strcmp(g_common[i].name, name)) return &g_common[i];
    for (u32 i = 0; i < g_town_shape_count; i++)
        if (!strcmp(g_town_shapes[i].name, name)) return &g_town_shapes[i];
    return NULL;
}

bool shapes_bind(void)
{
    bool ok = true;
    for (u32 i = 0; i < g_object_count; i++) {
        const ObjectDef *d = &g_objects[i];
        s_shape_of[i] = d->shape ? find(d->shape) : NULL;
        if (d->shape && !s_shape_of[i]) {
            log_error("ОБЪЕКТЫ: у объекта %s форма %s, а такого генератора нет (content/shapes.c)", d->id, d->shape);
            ok = false;
        }
    }
    return ok;
}

void shape_build(MeshBuilder *mb, const Objects *o, u32 i)
{
    const Shape *s = s_shape_of[o->def[i]];
    if (!s) return;
    const ObjectDef *d = &g_objects[o->def[i]];
    Rng r;
    rng_seed(&r, o->seed[i], 3);
    ShapeCtx c = { .mb = mb, .r = &r, .L = obj_frame(o, i), .pos = o->pos[i], .yaw = obj_yaw(o, i),
                   .size = obj_size(o, i), .tint = d->tint, .arg = s->arg };
    for (u32 k = 0; k < OBJ_MATS; k++) c.mat[k] = obj_mat(o, i, k);
    // Без материала в описании — бетон: генератор, который его не берёт, красит сам
    for (u32 k = 0; k < OBJ_MATS; k++)
        if (!c.mat[k]) c.mat[k] = g_mat.concrete;
    s->fn(&c);
}
