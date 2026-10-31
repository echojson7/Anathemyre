#include "scrap.h"

// Формы в осях вещи; масса — сталь 7800 кг/м³, бетон 2400
const PhysDesc g_scrap_phys[SCRAP_COUNT] = {
    [SCRAP_PIPE] = { .part = { { PP_CAPSULE, { 0, -0.55f, 0 }, { 0, 0.55f, 0 }, 0.045f } }, .parts = 1, .mass = 6.0f },
    [SCRAP_REBAR] = { .part = { { PP_CAPSULE, { 0, -0.45f, 0 }, { 0, 0.45f, 0 }, 0.012f } }, .parts = 1, .mass = 0.8f },
    [SCRAP_SHEET] = { .part = { { PP_BOX, { 0, 0, 0 }, { 0.30f, 0.004f, 0.22f }, 0 } }, .parts = 1, .mass = 4.0f },
    [SCRAP_CHUNK] = { .part = { { PP_BOX, { 0, 0, 0 }, { 0.16f, 0.11f, 0.13f }, 0 } }, .parts = 1, .mass = 18.0f },
    // Швеллер: стенка и две полки, три коробки
    [SCRAP_CHANNEL] = { .part = { { PP_BOX, { 0, 0, 0 }, { 0.05f, 0.6f, 0.004f }, 0 },
                                  { PP_BOX, { -0.046f, 0, 0.022f }, { 0.004f, 0.6f, 0.022f }, 0 },
                                  { PP_BOX, { 0.046f, 0, 0.022f }, { 0.004f, 0.6f, 0.022f }, 0 } },
                        .parts = 3, .mass = 10.0f },
    [SCRAP_KEG] = { .part = { { PP_CAPSULE, { 0, -0.12f, 0 }, { 0, 0.12f, 0 }, 0.16f } }, .parts = 1, .mass = 9.0f },
};

const MatDef *scrap_mat(u32 kind)
{
    switch (kind) {
    case SCRAP_SHEET: return g_mat.iron;
    case SCRAP_CHUNK: return g_mat.concrete_chip;
    default:          return g_mat.rust_deep;
    }
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
