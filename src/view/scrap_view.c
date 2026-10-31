#include "scrap_view.h"
#include "../content/props.h"

static void box_at(MeshBuilder *mb, V3 c, V3 h) { mb_box(mb, m4_trs(c, v3_mul(h, 2.0f), 0, 0, 0)); }

static void build_kind(MeshBuilder *mb, u32 k)
{
    switch (k) {
    case SCRAP_PIPE:
        mb_set(mb, mat_c(g_mat.rust_deep), MAT_RUST, 0);
        mb_cylinder(mb, v3(0, -0.59f, 0), v3(0, 0.59f, 0), 0.045f, 10);
        break;
    case SCRAP_REBAR:
        mb_set(mb, mat_c(g_mat.rust_deep), MAT_RUST, 0);
        mb_cylinder(mb, v3(0, -0.46f, 0), v3(0, 0.46f, 0), 0.012f, 6);
        break;
    case SCRAP_SHEET:
        mb_set(mb, mat_c(g_mat.iron), MAT_METAL, 0);
        box_at(mb, v3(0, 0, 0), g_scrap_phys[k].part[0].b);
        break;
    case SCRAP_CHUNK:
        mb_set(mb, mat_c(g_mat.concrete_chip), MAT_CONCRETE, 0);
        box_at(mb, v3(0, 0, 0), g_scrap_phys[k].part[0].b);
        mb_set(mb, mat_c(g_mat.rust_deep), MAT_RUST, 0);
        mb_cylinder(mb, v3(-0.05f, 0.0f, 0.04f), v3(-0.05f, 0.24f, 0.07f), 0.008f, 5);
        break;
    case SCRAP_CHANNEL:
        mb_set(mb, mat_c(g_mat.rust_deep), MAT_RUST, 0);
        for (u32 i = 0; i < 3; i++) box_at(mb, g_scrap_phys[k].part[i].a, g_scrap_phys[k].part[i].b);
        break;
    case SCRAP_KEG:
        mb_set(mb, mat_c(g_mat.rust_deep), MAT_RUST, 0);
        mb_cylinder(mb, v3(0, -0.28f, 0), v3(0, 0.28f, 0), 0.16f, 12);
        mb_set(mb, mat_c(g_mat.iron), MAT_METAL, 0);
        mb_cylinder(mb, v3(0, -0.12f, 0), v3(0, -0.10f, 0), 0.165f, 12);
        mb_cylinder(mb, v3(0, 0.10f, 0), v3(0, 0.12f, 0), 0.165f, 12);
        break;
    }
}

void scrap_view_init(ScrapView *v, MeshBuilder *mb, Arena *perm)
{
    for (u32 k = 0; k < SCRAP_COUNT; k++) {
        size_t mark = perm->used;
        mb_init(mb, perm, 4096, 12288);
        build_kind(mb, k);
        gpu_mesh_upload(&v->mesh[k], mb);
        perm->used = mark;
    }
    v->bones = ARENA_PUSH_ARRAY(perm, M4, SCRAP_DRAW_MAX + RENDER_MAX_BONES);
}

u32 scrap_draw(ScrapView *v, const Phys *p, f32 alpha, V3 eye, DrawItem *out, u32 cap)
{
    u32 n = 0;
    for (u32 h = 0; h < p->count && n < cap && n < SCRAP_DRAW_MAX; h++) {
        if (p->state[h] == PS_FREE || p->user[h] < SCRAP_USER || p->user[h] >= SCRAP_USER + SCRAP_COUNT) continue;
        V3 d = v3_sub(p->x[h], eye);
        if (v3_dot(d, d) > 200.0f * 200.0f) continue;
        v->bones[n] = phys_pose(p, h, alpha);
        out[n] = (DrawItem){ .mesh = &v->mesh[p->user[h] - SCRAP_USER], .bones = &v->bones[n] };
        n++;
    }
    return n;
}
