#include "scrap.h"
#include "props.h"

// Формы в осях вещи; масса — сталь 7800 кг/м³, бетон 2400
const PhysDesc g_scrap_phys[SCRAP_COUNT] = {
    [SCRAP_PIPE] = { .part = { { PP_CAPSULE, { 0, -0.55f, 0 }, { 0, 0.55f, 0 }, 0.045f } }, .parts = 1, .mass = 6.0f,
                     .friction = 0.5f, .bounce = 0.25f },
    [SCRAP_REBAR] = { .part = { { PP_CAPSULE, { 0, -0.45f, 0 }, { 0, 0.45f, 0 }, 0.012f } }, .parts = 1, .mass = 0.8f,
                      .friction = 0.6f, .bounce = 0.2f },
    [SCRAP_SHEET] = { .part = { { PP_BOX, { 0, 0, 0 }, { 0.30f, 0.004f, 0.22f }, 0 } }, .parts = 1, .mass = 4.0f,
                      .friction = 0.5f, .bounce = 0.1f },
    [SCRAP_CHUNK] = { .part = { { PP_BOX, { 0, 0, 0 }, { 0.16f, 0.11f, 0.13f }, 0 } }, .parts = 1, .mass = 18.0f,
                      .friction = 0.8f, .bounce = 0.05f },
    // Швеллер: стенка и две полки, три коробки
    [SCRAP_CHANNEL] = { .part = { { PP_BOX, { 0, 0, 0 }, { 0.05f, 0.6f, 0.004f }, 0 },
                                  { PP_BOX, { -0.046f, 0, 0.022f }, { 0.004f, 0.6f, 0.022f }, 0 },
                                  { PP_BOX, { 0.046f, 0, 0.022f }, { 0.004f, 0.6f, 0.022f }, 0 } },
                        .parts = 3, .mass = 10.0f, .friction = 0.5f, .bounce = 0.15f },
    [SCRAP_KEG] = { .part = { { PP_CAPSULE, { 0, -0.12f, 0 }, { 0, 0.12f, 0 }, 0.16f } }, .parts = 1, .mass = 9.0f,
                    .friction = 0.5f, .bounce = 0.3f },
};

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

static M4 random_xf(Rng *rng, V3 at)
{
    return m4_trs(at, v3(1, 1, 1), rng_range(rng, 0, 2 * PI_F), rng_range(rng, 0, 2 * PI_F), rng_range(rng, 0, 2 * PI_F));
}

void scrap_drop(Phys *p, Rng *rng, V3 at, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        u32 k = rng_below(rng, SCRAP_COUNT);
        V3 c = v3_add(at, v3(rng_range(rng, -1.2f, 1.2f), 1.5f + 0.25f * (f32)i, rng_range(rng, -1.2f, 1.2f)));
        // Чуть закрутить: брошенное летит не ровно
        V3 w = v3(rng_range(rng, -2, 2), rng_range(rng, -2, 2), rng_range(rng, -2, 2));
        phys_spawn(p, &g_scrap_phys[k], random_xf(rng, c), v3(0, -0.5f, 0), w, PHYS_SETTLE, (u16)(SCRAP_USER + k));
    }
}

void scrap_place(Phys *p, Rng *rng, V3 at, u32 n, f32 (*ground)(f32, f32))
{
    for (u32 i = 0; i < n; i++) {
        u32 k = i % SCRAP_COUNT;
        f32 x = at.x + rng_range(rng, -3.0f, 3.0f), z = at.z + rng_range(rng, -3.0f, 3.0f);
        // Последние две висят на месте, пока их не тронут
        u8 mode = i + 2 >= n ? PHYS_HANG : PHYS_SETTLE;
        V3 c = v3(x, ground(x, z) + rng_range(rng, 0.4f, 1.2f) + (mode == PHYS_HANG ? 0.8f : 0.0f), z);
        phys_spawn(p, &g_scrap_phys[k], random_xf(rng, c), v3(0, 0, 0), v3(0, 0, 0), mode, (u16)(SCRAP_USER + k));
    }
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
