#include "shapes.h"

#include <string.h>

#include "../core/log.h"
#include "props.h"

// ---------------------------------------------------------------- общие формы: коробка и предметы props.c

void shape_to_world(MeshBuilder *mb, u32 v0, M4 L)
{
    for (u32 i = v0; i < mb->vcount; i++) {
        Vertex *v = &mb->v[i];
        V3 p = m4_point(L, v3(v->pos[0], v->pos[1], v->pos[2])), n = m4_dir(L, v3(v->nrm[0], v->nrm[1], v->nrm[2]));
        v->pos[0] = p.x, v->pos[1] = p.y, v->pos[2] = p.z;
        v->nrm[0] = n.x, v->nrm[1] = n.y, v->nrm[2] = n.z;
    }
}

// Коробка материалом 0 с разбросом tint; точка экземпляра — середина. var — наклоны вокруг x и z, градусы
static void sh_box(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    if (c->var[0] || c->var[1]) mb_box(c->mb, m4_trs(c->pos, c->size, shape_deg(c->var[0]), c->yaw, shape_deg(c->var[1])));
    else prop_box(c->mb, c->pos, c->size, c->yaw);
}

// Знак на стене: плоскость лицом вдоль +z, точка — середина, size.x на size.y
static void sh_panel(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    mb_quad(c->mb, c->pos, m4_dir(c->L, v3(c->size.x * 0.5f, 0, 0)), v3(0, c->size.y * 0.5f, 0));
}

// Знак на полу: плоскость лицом вверх, size.x поперёк, size.z вдоль +z
static void sh_decal(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    mb_quad(c->mb, c->pos, m4_dir(c->L, v3(-c->size.x * 0.5f, 0, 0)), m4_dir(c->L, v3(0, 0, c->size.z * 0.5f)));
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

// Протяжённые: от точки экземпляра вдоль +x на size.x (меньше нуля — назад), с подъёмом size.y к концу
static V3 far_end(const ShapeCtx *c) { return m4_point(c->L, v3(c->size.x, c->size.y, 0)); }
static void sh_fence(const ShapeCtx *c) { prop_fence(c->mb, c->r, c->pos, far_end(c)); }
static void sh_railing(const ShapeCtx *c) { prop_railing(c->mb, c->r, c->pos, far_end(c)); }
// Провод: size.z — провис, arg — что висит (0 голый, 1 тряпки, 2 лампа)
static void sh_cable(const ShapeCtx *c) { prop_cable(c->mb, c->r, c->pos, far_end(c), c->size.z, c->arg); }
// Труба: size.z — радиус
static void sh_pipe(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    mb_cylinder(c->mb, c->pos, far_end(c), c->size.z, c->size.z >= 1.0f ? 24 : c->size.z >= 0.4f ? 12 : 10);
}
// Провод без подвесок: size.z — провис, радиус по var[0] в мм (0 — 35 мм)
static void sh_wire(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], 0.0f);
    prop_wire(c->mb, c->pos, far_end(c), c->size.z, c->var[0] ? (f32)c->var[0] * 0.001f : 0.035f, 14, 5);
}
// Участок трубы по стене (стена позади, −z): хомуты к стене, муфты, материал 1 — железо хомутов.
// var[0] = 1 — без хомутов (труба уходит в стену), var[1] = 1 — колено в конце
static void sh_wall_pipe(const ShapeCtx *c)
{
    V3 a = c->pos, b = far_end(c), wall = m4_dir(c->L, v3(0, 0, 1));
    f32 r = c->size.z;
    mb_set(c->mb, prop_shade(c->r, mat_c(c->mat[0]), 0.12f), MAT_RUST, BONE_NONE);
    mb_cylinder(c->mb, a, b, r, 12);
    V3 d = v3_sub(b, a);
    f32 len = v3_len(d);
    V3 dn = v3_mul(d, 1.0f / len);
    for (f32 t = 1.2f; t < len - 0.6f; t += 3.0f) {
        V3 p = v3_add(a, v3_mul(dn, t));
        mb_cylinder(c->mb, v3_sub(p, v3_mul(dn, 0.05f)), v3_add(p, v3_mul(dn, 0.05f)), r * 1.4f, 12);
    }
    V3 rust = c->mb->color;
    for (f32 t = 0.8f; t < len - 0.3f && !c->var[0]; t += 2.1f) {
        V3 p = v3_add(a, v3_mul(dn, t));
        mb_set(c->mb, mat_c(c->mat[1]), MAT_METAL, BONE_NONE);
        mb_cylinder(c->mb, v3_sub(p, v3_mul(dn, 0.04f)), v3_add(p, v3_mul(dn, 0.04f)), r * 1.15f, 12);
        mb_cylinder(c->mb, p, v3_sub(p, v3_mul(wall, r + 0.25f)), 0.03f, 6);
        mb_set(c->mb, rust, MAT_RUST, BONE_NONE);
    }
    if (c->var[1]) mb_capsule(c->mb, b, v3_add(b, v3(0, 0.001f, 0)), r * 1.12f, r * 1.12f, 12, 1.0f, 1.0f);
}
// Стояк: вверх на size.y, радиус size.x
static void sh_pipe_up(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    mb_cylinder(c->mb, c->pos, v3_add(c->pos, v3(0, c->size.y, 0)), c->size.x, c->size.x >= 0.4f ? 12 : c->size.x >= 0.2f ? 10 : 5);
}

static const Shape g_common[] = {
    { "box", sh_box, 0 },
    { "panel", sh_panel, 0 },
    { "decal", sh_decal, 0 },
    { "wire", sh_wire, 0 },
    { "wall_pipe", sh_wall_pipe, 0 },
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
    const struct {
        const Shape *s;
        u32 n;
    } tab[] = { { g_common, ARRAY_COUNT(g_common) }, { g_town_shapes, g_town_shape_count }, { g_hall_shapes, g_hall_shape_count },
                { g_ring_shapes, g_ring_shape_count }, { g_ostov_shapes, g_ostov_shape_count } };
    for (u32 t = 0; t < ARRAY_COUNT(tab); t++)
        for (u32 i = 0; i < tab[t].n; i++)
            if (!strcmp(tab[t].s[i].name, name)) return &tab[t].s[i];
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
                   .size = obj_size(o, i), .tint = d->tint, .arg = s->arg, .var = { o->var[i][0], o->var[i][1] } };
    for (u32 k = 0; k < OBJ_MATS; k++) c.mat[k] = obj_mat(o, i, k);
    // Без материала в описании — бетон: генератор, который его не берёт, красит сам
    for (u32 k = 0; k < OBJ_MATS; k++)
        if (!c.mat[k]) c.mat[k] = g_mat.concrete;
    s->fn(&c);
}
