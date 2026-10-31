#include "props.h"

// Ткани: навесы, тряпки, мешки, ковры; листы: обшивка, заборы, лом
const V3 g_pal_cloth[PAL_CLOTH_N] = {
    { 0.45f, 0.32f, 0.15f }, { 0.40f, 0.14f, 0.10f }, { 0.50f, 0.47f, 0.40f }, { 0.28f, 0.30f, 0.18f },
    { 0.30f, 0.22f, 0.15f }, { 0.25f, 0.27f, 0.30f }, { 0.36f, 0.30f, 0.22f },
};
const V3 g_pal_sheet[PAL_SHEET_N] = {
    { 0.34f, 0.21f, 0.13f }, { 0.27f, 0.18f, 0.12f }, { 0.23f, 0.24f, 0.20f }, { 0.21f, 0.22f, 0.24f }, { 0.31f, 0.28f, 0.23f },
};

// Ящик из листа с рёбрами; возвращает высоту
f32 prop_crate(MeshBuilder *mb, Rng *r, V3 p, f32 yaw, f32 s)
{
    V3 size = v3(s * rng_range(r, 0.9f, 1.5f), s * rng_range(r, 0.7f, 1.0f), s * rng_range(r, 0.8f, 1.1f));
    u32 k = rng_below(r, 3);
    V3 col = k == 0 ? prop_tint(r, PAL_RUST, 0.2f) : k == 1 ? prop_tint(r, v3(0.24f, 0.26f, 0.19f), 0.2f) : prop_tint(r, PAL_METAL, 0.2f);
    M4 L = prop_frame(p, yaw);
    mb_set(mb, col, k == 0 ? MAT_RUST : MAT_METAL, BONE_NONE);
    prop_lbox(mb, L, v3(0, size.y * 0.5f, 0), size, 0, 0, 0);
    mb_set(mb, v3_mul(col, 0.65f), MAT_RUST, BONE_NONE);
    prop_lbox(mb, L, v3(0, size.y * 0.82f, 0), v3(size.x + 0.03f, size.y * 0.1f, size.z + 0.03f), 0, 0, 0);
    if (rng_f32(r) < 0.5f) prop_lbox(mb, L, v3(0, size.y * 0.2f, 0), v3(size.x + 0.03f, size.y * 0.1f, size.z + 0.03f), 0, 0, 0);
    return size.y;
}

// Несколько ящиков: внизу рядом, сверху со сдвигом и поворотом
void prop_crate_stack(MeshBuilder *mb, Rng *r, V3 p, f32 yaw)
{
    f32 s = rng_range(r, 0.45f, 0.7f);
    f32 h = prop_crate(mb, r, p, yaw + rng_range(r, -0.15f, 0.15f), s);
    if (rng_f32(r) < 0.6f) {
        V3 side = v3(cosf(yaw), 0, -sinf(yaw));
        prop_crate(mb, r, v3_add(p, v3_mul(side, s * rng_range(r, 1.1f, 1.5f))), yaw + rng_range(r, -0.3f, 0.3f), s * rng_range(r, 0.8f, 1.1f));
    }
    if (rng_f32(r) < 0.55f) prop_crate(mb, r, v3(p.x + rng_range(r, -0.1f, 0.1f), p.y + h, p.z + rng_range(r, -0.1f, 0.1f)),
                                  yaw + rng_range(r, -0.5f, 0.5f), s * rng_range(r, 0.6f, 0.9f));
}

void prop_barrel(MeshBuilder *mb, Rng *r, V3 p, bool tipped)
{
    static const V3 bc[4] = { { 0.40f, 0.22f, 0.12f }, { 0.24f, 0.26f, 0.18f }, { 0.22f, 0.25f, 0.28f }, { 0.36f, 0.14f, 0.10f } };
    f32 rad = rng_range(r, 0.26f, 0.32f), h = rng_range(r, 0.8f, 0.95f);
    u32 k = rng_below(r, 4);
    V3 col = prop_tint(r, bc[k], 0.15f);
    V3 a = p, b;
    if (tipped) {
        f32 yaw = rng_range(r, 0, 2 * PI_F);
        a = v3(p.x, p.y + rad, p.z);
        b = v3_add(a, v3(sinf(yaw) * h, 0, cosf(yaw) * h));
    } else {
        b = v3(p.x + rng_range(r, -0.04f, 0.04f), p.y + h, p.z + rng_range(r, -0.04f, 0.04f));
    }
    mb_set(mb, col, k ? MAT_METAL : MAT_RUST, BONE_NONE);
    mb_cylinder(mb, a, b, rad, 10);
    mb_set(mb, v3_mul(col, 0.6f), MAT_RUST, BONE_NONE);
    V3 d = v3_mul(v3_norm(v3_sub(b, a)), 0.025f);
    for (int i = 0; i < 2; i++) {
        V3 c = v3_lerp(a, b, i ? 0.78f : 0.22f);
        mb_cylinder(mb, v3_sub(c, d), v3_add(c, d), rad + 0.015f, 10);
    }
}

void prop_sack(MeshBuilder *mb, Rng *r, V3 p, f32 yaw)
{
    f32 w = rng_range(r, 0.4f, 0.6f);
    mb_set(mb, prop_tint(r, v3(0.40f, 0.35f, 0.25f), 0.18f), MAT_CLOTH, BONE_NONE);
    mb_box(mb, m4_trs(v3(p.x, p.y + 0.13f, p.z), v3(w, 0.26f, w * 0.65f), rng_range(r, -0.12f, 0.12f), yaw, rng_range(r, -0.12f, 0.12f)));
    mb_box(mb, m4_trs(v3(p.x, p.y + 0.29f, p.z), v3(0.13f, 0.09f, 0.11f), 0, yaw, rng_range(r, -0.3f, 0.3f)));
}

void prop_sack_pile(MeshBuilder *mb, Rng *r, V3 p, f32 yaw)
{
    u32 n = 2 + rng_below(r, 4);
    V3 side = v3(cosf(yaw), 0, -sinf(yaw));
    for (u32 i = 0; i < n; i++) {
        f32 o = ((f32)(i % 3) - 1.0f) * 0.5f;
        V3 q = v3_add(p, v3_mul(side, o + rng_range(r, -0.08f, 0.08f)));
        if (i >= 3) q = v3(q.x + side.x * 0.25f, p.y + 0.24f, q.z + side.z * 0.25f);
        prop_sack(mb, r, q, yaw + rng_range(r, -0.4f, 0.4f));
    }
}

// Куча лома: листы, трубы, обломки, колёса; выше к центру
void prop_scrap_heap(MeshBuilder *mb, Rng *r, V3 c, f32 rad)
{
    u32 n = 9 + rng_below(r, 9);
    for (u32 i = 0; i < n; i++) {
        f32 a = rng_range(r, 0, 2 * PI_F), d = rad * sqrtf(rng_f32(r));
        f32 h = (1.0f - d / rad) * rad * 0.55f * rng_range(r, 0.4f, 1.0f);
        V3 p = v3(c.x + cosf(a) * d, c.y + h, c.z + sinf(a) * d);
        u32 k = rng_below(r, 4);
        mb_set(mb, k == 2 ? prop_tint(r, PAL_METAL, 0.3f) : prop_pick(r, g_pal_sheet, 5, 0.2f), k == 2 ? MAT_METAL : MAT_RUST, BONE_NONE);
        if (k == 0) {
            mb_box(mb, m4_trs(p, v3(rng_range(r, 0.6f, 1.3f), 0.04f, rng_range(r, 0.4f, 0.9f)), rng_range(r, -0.7f, 0.7f),
                          rng_range(r, 0, PI_F), rng_range(r, -0.7f, 0.7f)));
        } else if (k == 1) {
            f32 yaw = rng_range(r, 0, PI_F), len = rng_range(r, 0.6f, 1.7f);
            V3 dd = v3(sinf(yaw) * len * 0.5f, rng_range(r, -0.3f, 0.3f), cosf(yaw) * len * 0.5f);
            mb_cylinder(mb, v3_sub(p, dd), v3_add(p, dd), rng_range(r, 0.04f, 0.1f), 6);
        } else if (k == 2) {
            f32 s = rng_range(r, 0.2f, 0.5f);
            mb_box(mb, m4_trs(p, v3(s, s * rng_range(r, 0.5f, 1.0f), s * rng_range(r, 0.6f, 1.2f)), rng_range(r, -0.5f, 0.5f),
                          rng_range(r, 0, PI_F), rng_range(r, -0.5f, 0.5f)));
        } else {
            f32 yaw = rng_range(r, 0, PI_F), tilt = rng_range(r, 0.3f, 1.4f);
            V3 ax = v3(sinf(yaw) * sinf(tilt) * 0.04f, cosf(tilt) * 0.04f, cosf(yaw) * sinf(tilt) * 0.04f);
            mb_cylinder(mb, v3_sub(p, ax), v3_add(p, ax), rng_range(r, 0.2f, 0.45f), 10);
        }
    }
}

// Провод или кабель с провисом по параболе: segs кусков, sides граней. Цвет и материал — у вызывающего
void prop_wire(MeshBuilder *mb, V3 a, V3 b, f32 sag, f32 r, u32 segs, u32 sides)
{
    V3 prev = a;
    for (u32 i = 1; i <= segs; i++) {
        f32 t = (f32)i / (f32)segs;
        V3 p = v3_lerp(a, b, t);
        p.y -= sag * 4.0f * t * (1.0f - t);
        mb_cylinder(mb, prev, p, r, sides);
        prev = p;
    }
}

// Провод с провисом; hang: 0 голый, 1 тряпки, 2 лампа
void prop_cable(MeshBuilder *mb, Rng *r, V3 a, V3 b, f32 sag, u32 hang)
{
    mb_set(mb, v3(0.06f, 0.055f, 0.05f), MAT_METAL, BONE_NONE);
    prop_wire(mb, a, b, sag, 0.018f, 8, 4);
    f32 yaw = atan2f(-(b.z - a.z), b.x - a.x);
    if (hang == 1) {
        u32 n = 3 + rng_below(r, 5);
        for (u32 i = 0; i < n; i++) {
            f32 t = rng_range(r, 0.12f, 0.88f), w = rng_range(r, 0.3f, 0.7f), h = rng_range(r, 0.35f, 1.0f);
            V3 p = v3_lerp(a, b, t);
            p.y -= sag * 4.0f * t * (1.0f - t) + h * 0.5f;
            mb_set(mb, prop_pick(r, g_pal_cloth, 7, 0.2f), MAT_CLOTH, BONE_NONE);
            mb_box(mb, m4_trs(p, v3(w, h, 0.02f), rng_range(r, -0.15f, 0.15f), yaw, rng_range(r, -0.05f, 0.05f)));
        }
    } else if (hang == 2) {
        V3 p = v3_lerp(a, b, 0.5f);
        p.y -= sag;
        mb_set(mb, v3(0.06f, 0.055f, 0.05f), MAT_METAL, BONE_NONE);
        mb_cylinder(mb, p, v3(p.x, p.y - 0.3f, p.z), 0.012f, 4);
        mb_set(mb, PAL_METAL, MAT_METAL, BONE_NONE);
        prop_box(mb, v3(p.x, p.y - 0.34f, p.z), v3(0.26f, 0.08f, 0.26f), yaw);
        mb_set(mb, prop_tint(r, PAL_LAMP, 0.15f), MAT_EMISSIVE, BONE_NONE);
        prop_box(mb, v3(p.x, p.y - 0.42f, p.z), v3(0.13f, 0.08f, 0.13f), yaw);
    }
}

void prop_lamp_post(MeshBuilder *mb, Rng *r, V3 p, f32 yaw)
{
    f32 h = rng_range(r, 3.0f, 4.0f);
    V3 top = v3(p.x + rng_range(r, -0.08f, 0.08f), p.y + h, p.z + rng_range(r, -0.08f, 0.08f));
    mb_set(mb, prop_tint(r, PAL_CONCRETE_DARK, 0.1f), MAT_CONCRETE, BONE_NONE);
    prop_box(mb, v3(p.x, p.y + 0.2f, p.z), v3(0.45f, 0.4f, 0.45f), rng_range(r, 0, PI_F));
    mb_set(mb, prop_tint(r, PAL_RUST, 0.2f), MAT_RUST, BONE_NONE);
    mb_cylinder(mb, p, top, 0.06f, 6);
    V3 arm = v3_add(top, v3(sinf(yaw) * 0.7f, -0.05f, cosf(yaw) * 0.7f));
    mb_cylinder(mb, top, arm, 0.035f, 5);
    mb_set(mb, PAL_METAL, MAT_METAL, BONE_NONE);
    prop_box(mb, v3(arm.x, arm.y - 0.08f, arm.z), v3(0.32f, 0.1f, 0.32f), yaw);
    mb_set(mb, prop_tint(r, PAL_LAMP, 0.15f), MAT_EMISSIVE, BONE_NONE);
    prop_box(mb, v3(arm.x, arm.y - 0.16f, arm.z), v3(0.16f, 0.07f, 0.16f), yaw);
}

void prop_rug(MeshBuilder *mb, Rng *r, V3 p, f32 yaw)
{
    f32 w = rng_range(r, 1.2f, 2.3f), d = rng_range(r, 0.8f, 1.5f);
    mb_set(mb, prop_pick(r, g_pal_cloth, 7, 0.15f), MAT_CLOTH, BONE_NONE);
    prop_box(mb, v3(p.x, p.y + 0.01f, p.z), v3(w, 0.02f, d), yaw);
    mb_set(mb, prop_pick(r, g_pal_cloth, 7, 0.15f), MAT_CLOTH, BONE_NONE);
    prop_box(mb, v3(p.x, p.y + 0.012f, p.z), v3(w * 0.85f, 0.02f, d * 0.18f), yaw);
}

// Очаг: камни кругом, угли, тренога с котлом
void prop_fire_pit(MeshBuilder *mb, Rng *r, V3 p)
{
    for (u32 i = 0; i < 8; i++) {
        f32 a = (f32)i * (2 * PI_F / 8) + rng_range(r, -0.2f, 0.2f);
        f32 s = rng_range(r, 0.18f, 0.28f);
        mb_set(mb, prop_tint(r, PAL_CONCRETE_DARK, 0.2f), MAT_CONCRETE, BONE_NONE);
        mb_box(mb, m4_trs(v3(p.x + cosf(a) * 0.5f, p.y + s * 0.4f, p.z + sinf(a) * 0.5f), v3(s, s * 0.8f, s * 1.2f), 0, a, 0));
    }
    mb_set(mb, v3(0.05f, 0.045f, 0.04f), MAT_CONCRETE, BONE_NONE);
    for (u32 i = 0; i < 4; i++)
        mb_box(mb, m4_trs(v3(p.x, p.y + 0.06f, p.z), v3(0.6f, 0.06f, 0.07f), 0, rng_range(r, 0, PI_F), rng_range(r, -0.1f, 0.1f)));
    mb_set(mb, v3(0.9f, 0.32f, 0.06f), MAT_EMISSIVE, BONE_NONE);
    for (u32 i = 0; i < 3; i++)
        prop_box(mb, v3(p.x + rng_range(r, -0.15f, 0.15f), p.y + 0.08f, p.z + rng_range(r, -0.15f, 0.15f)), v3(0.12f, 0.05f, 0.1f),
            rng_range(r, 0, PI_F));
    mb_set(mb, prop_tint(r, PAL_RUST, 0.2f), MAT_RUST, BONE_NONE);
    V3 top = v3(p.x, p.y + 1.25f, p.z);
    for (u32 i = 0; i < 3; i++) {
        f32 a = (f32)i * (2 * PI_F / 3) + 0.4f;
        mb_cylinder(mb, v3(p.x + cosf(a) * 0.7f, p.y, p.z + sinf(a) * 0.7f), top, 0.025f, 4);
    }
    mb_cylinder(mb, top, v3(p.x, p.y + 0.6f, p.z), 0.01f, 4);
    mb_set(mb, v3(0.08f, 0.075f, 0.07f), MAT_METAL, BONE_NONE);
    mb_cylinder(mb, v3(p.x, p.y + 0.38f, p.z), v3(p.x, p.y + 0.62f, p.z), 0.2f, 10);
}

// Бак на ногах с лестницей
void prop_water_tower(MeshBuilder *mb, Rng *r, V3 p, f32 h)
{
    f32 rad = rng_range(r, 1.0f, 1.4f), th = rng_range(r, 1.8f, 2.6f);
    mb_set(mb, prop_tint(r, PAL_RUST, 0.15f), MAT_RUST, BONE_NONE);
    for (u32 i = 0; i < 4; i++) {
        f32 a = (f32)i * (PI_F / 2) + PI_F / 4;
        V3 foot = v3(p.x + cosf(a) * rad * 1.1f, p.y, p.z + sinf(a) * rad * 1.1f);
        mb_cylinder(mb, foot, v3(p.x + cosf(a) * rad * 0.8f, p.y + h, p.z + sinf(a) * rad * 0.8f), 0.08f, 6);
    }
    mb_cylinder(mb, v3(p.x - rad, p.y + h * 0.5f, p.z), v3(p.x + rad, p.y + h * 0.5f, p.z), 0.04f, 4);
    mb_cylinder(mb, v3(p.x, p.y + h * 0.5f, p.z - rad), v3(p.x, p.y + h * 0.5f, p.z + rad), 0.04f, 4);
    mb_set(mb, prop_tint(r, v3(0.26f, 0.19f, 0.14f), 0.1f), MAT_RUST, BONE_NONE);
    mb_cylinder(mb, v3(p.x, p.y + h, p.z), v3(p.x, p.y + h + th, p.z), rad, 16);
    mb_set(mb, prop_tint(r, PAL_METAL, 0.2f), MAT_METAL, BONE_NONE);
    mb_cylinder(mb, v3(p.x, p.y + h + th, p.z), v3(p.x, p.y + h + th + 0.25f, p.z), rad * 0.6f, 12);
    // Труба вниз с краном
    mb_set(mb, prop_tint(r, PAL_RUST, 0.2f), MAT_RUST, BONE_NONE);
    mb_cylinder(mb, v3(p.x + rad * 0.5f, p.y + h, p.z), v3(p.x + rad * 0.5f, p.y + 0.9f, p.z), 0.06f, 6);
    mb_cylinder(mb, v3(p.x + rad * 0.5f, p.y + 0.9f, p.z), v3(p.x + rad * 0.5f + 0.3f, p.y + 0.9f, p.z), 0.05f, 6);
}

// Забор из листов и блоков от a до b; дыры и разная высота
void prop_fence(MeshBuilder *mb, Rng *r, V3 a, V3 b)
{
    V3 d = v3_sub(b, a);
    f32 len = sqrtf(d.x * d.x + d.z * d.z);
    f32 yaw = atan2f(-d.z, d.x);
    f32 t = 0.0f;
    while (t < len) {
        f32 w = rng_range(r, 0.9f, 1.5f);
        f32 c = fminf(t + w * 0.5f, len);
        V3 p = v3_add(a, v3_mul(d, c / len));
        f32 k = rng_f32(r);
        if (k < 0.08f) {
            // дыра
        } else if (k < 0.3f) {
            mb_set(mb, prop_tint(r, PAL_CONCRETE, 0.15f), MAT_CONCRETE, BONE_NONE);
            f32 h = rng_range(r, 0.8f, 1.4f);
            mb_box(mb, m4_trs(v3(p.x, p.y + h * 0.5f, p.z), v3(w * 0.95f, h, 0.6f), 0, yaw + rng_range(r, -0.08f, 0.08f), 0));
        } else {
            mb_set(mb, prop_pick(r, g_pal_sheet, 5, 0.2f), MAT_RUST, BONE_NONE);
            f32 h = rng_range(r, 1.6f, 2.5f);
            mb_box(mb, m4_trs(v3(p.x, p.y + h * 0.5f - 0.1f, p.z), v3(w * 1.05f, h, 0.04f), rng_range(r, -0.08f, 0.08f),
                          yaw + rng_range(r, -0.05f, 0.05f), rng_range(r, -0.06f, 0.06f)));
            if (rng_f32(r) < 0.4f) {
                mb_set(mb, prop_tint(r, PAL_RUST, 0.2f), MAT_RUST, BONE_NONE);
                mb_cylinder(mb, v3(p.x, p.y, p.z), v3(p.x, p.y + h + 0.2f, p.z), 0.05f, 5);
            }
        }
        t += w;
    }
}

// Перила из труб вдоль a→b; часть пролётов выломана
void prop_railing(MeshBuilder *mb, Rng *r, V3 a, V3 b)
{
    V3 d = v3_sub(b, a);
    f32 len = sqrtf(d.x * d.x + d.z * d.z);
    u32 n = (u32)(len / 1.8f) + 1;
    mb_set(mb, prop_tint(r, PAL_RUST, 0.2f), MAT_RUST, BONE_NONE);
    for (u32 i = 0; i < n; i++) {
        V3 p0 = v3_add(a, v3_mul(d, (f32)i / n)), p1 = v3_add(a, v3_mul(d, (f32)(i + 1) / n));
        if (rng_f32(r) < 0.12f) continue;
        V3 lean = v3(rng_range(r, -0.08f, 0.08f), 0, rng_range(r, -0.08f, 0.08f));
        mb_cylinder(mb, p0, v3_add(v3(p0.x, p0.y + 1.0f, p0.z), lean), 0.035f, 5);
        mb_cylinder(mb, v3(p0.x, p0.y + 1.0f, p0.z), v3(p1.x, p1.y + 1.0f + rng_range(r, -0.05f, 0.05f), p1.z), 0.03f, 5);
        if (rng_f32(r) < 0.7f) mb_cylinder(mb, v3(p0.x, p0.y + 0.5f, p0.z), v3(p1.x, p1.y + 0.5f, p1.z), 0.025f, 4);
        if (i + 1 == n) mb_cylinder(mb, p1, v3(p1.x, p1.y + 1.0f, p1.z), 0.035f, 5);
    }
}

void prop_ladder(MeshBuilder *mb, M4 L, V3 base, f32 h)
{
    mb_set(mb, v3(0.30f, 0.18f, 0.11f), MAT_RUST, BONE_NONE);
    prop_lcyl(mb, L, v3(base.x, base.y, base.z - 0.22f), v3(base.x, base.y + h, base.z - 0.22f), 0.025f, 4);
    prop_lcyl(mb, L, v3(base.x, base.y, base.z + 0.22f), v3(base.x, base.y + h, base.z + 0.22f), 0.025f, 4);
    for (f32 y = 0.3f; y < h; y += 0.32f) prop_lbox(mb, L, v3(base.x, base.y + y, base.z), v3(0.03f, 0.03f, 0.44f), 0, 0, 0);
}

// Мелочь у стены дома или прилавка
void prop_clutter(MeshBuilder *mb, Rng *r, V3 p, f32 yaw)
{
    u32 k = rng_below(r, 6);
    if (k == 0) prop_crate_stack(mb, r, p, yaw);
    else if (k == 1) prop_barrel(mb, r, p, rng_f32(r) < 0.2f);
    else if (k == 2) prop_sack_pile(mb, r, p, yaw);
    else if (k == 3) {
        prop_barrel(mb, r, p, false);
        prop_barrel(mb, r, v3(p.x + cosf(yaw) * 0.62f, p.y, p.z - sinf(yaw) * 0.62f), false);
    } else if (k == 4) prop_scrap_heap(mb, r, p, rng_range(r, 0.5f, 0.9f));
    else prop_crate(mb, r, p, yaw + rng_range(r, -0.3f, 0.3f), rng_range(r, 0.4f, 0.6f));
}

// Труба по точкам вдоль стены: колена, фланцы через ~3 м, хомуты к стене (wall — наружу от стены).
// rust — цвет трубы, iron — хомутов
void prop_pipe_run(MeshBuilder *mb, const V3 *pts, u32 n, f32 r, V3 wall, V3 rust, V3 iron, Rng *rng)
{
    mb_set(mb, prop_shade(rng, rust, 0.12f), MAT_RUST, BONE_NONE);
    for (u32 i = 0; i + 1 < n; i++) {
        V3 a = pts[i], b = pts[i + 1];
        mb_cylinder(mb, a, b, r, 12);
        V3 d = v3_sub(b, a);
        f32 len = v3_len(d);
        V3 dn = v3_mul(d, 1.0f / len);
        for (f32 t = 1.2f; t < len - 0.6f; t += 3.0f) {
            V3 p = v3_add(a, v3_mul(dn, t));
            mb_cylinder(mb, v3_sub(p, v3_mul(dn, 0.05f)), v3_add(p, v3_mul(dn, 0.05f)), r * 1.4f, 12);
        }
        for (f32 t = 0.8f; t < len - 0.3f; t += 2.1f) {
            V3 p = v3_add(a, v3_mul(dn, t));
            V3 back = v3_sub(p, v3_mul(wall, r + 0.25f));
            mb_set(mb, iron, MAT_METAL, BONE_NONE);
            mb_cylinder(mb, v3_sub(p, v3_mul(dn, 0.04f)), v3_add(p, v3_mul(dn, 0.04f)), r * 1.15f, 12);
            mb_cylinder(mb, p, back, 0.03f, 6);
            mb_set(mb, prop_shade(rng, rust, 0.12f), MAT_RUST, BONE_NONE);
        }
    }
    // Колена: утолщение на стыке
    for (u32 i = 1; i + 1 < n; i++) mb_capsule(mb, pts[i], v3_add(pts[i], v3(0, 0.001f, 0)), r * 1.12f, r * 1.12f, 12, 1.0f, 1.0f);
}

void prop_ground(MeshBuilder *mb, PropKind kind, u32 seed)
{
    Rng r;
    rng_seed(&r, seed, 71 + (u64)kind);
    switch (kind) {
    case PROP_LOG: {
        // Поваленный ствол с обломанными сучьями
        f32 len = rng_range(&r, 3.0f, 7.0f), rad = rng_range(&r, 0.18f, 0.38f);
        mb_set(mb, v3_mul(v3(0.16f, 0.13f, 0.10f), rng_range(&r, 0.8f, 1.2f)), MAT_BARK, BONE_NONE);
        V2 prof[5] = { { 0, rad * 0.6f }, { 0.08f, rad * 1.05f }, { len * 0.5f, rad }, { len - 0.1f, rad * 0.8f }, { len, rad * 0.45f } };
        mb_lathe(mb, v3(-len * 0.5f, rad * 0.75f, 0), v3(len * 0.5f, rad * 0.65f, 0), prof, 5, 7, 1, 1);
        u32 stubs = 1 + rng_below(&r, 3);
        for (u32 i = 0; i < stubs; i++) {
            f32 x = rng_range(&r, -len * 0.4f, len * 0.4f);
            V3 a = v3(x, rad * 0.75f, 0), d = v3_norm(v3(rng_range(&r, -0.4f, 0.4f), rng_range(&r, 0.2f, 1.0f), rng_range(&r, -1, 1)));
            mb_capsule(mb, a, v3_add(a, v3_mul(d, rad + rng_range(&r, 0.3f, 0.8f))), rad * 0.35f, rad * 0.15f, 5, 1, 1);
        }
    } break;
    case PROP_STUMP: {
        f32 rad = rng_range(&r, 0.22f, 0.45f), h = rng_range(&r, 0.3f, 0.9f);
        mb_set(mb, v3_mul(v3(0.16f, 0.12f, 0.09f), rng_range(&r, 0.8f, 1.2f)), MAT_BARK, BONE_NONE);
        mb_cylinder(mb, v3(0, -0.1f, 0), v3(0, h, 0), rad, 9);
        u32 roots = 4 + rng_below(&r, 3);
        for (u32 i = 0; i < roots; i++) {
            f32 a = 2 * PI_F * (f32)i / (f32)roots + rng_range(&r, -0.3f, 0.3f);
            mb_capsule(mb, v3(0, h * 0.3f, 0), v3(sinf(a) * rad * 2.6f, -0.1f, cosf(a) * rad * 2.6f), rad * 0.4f, rad * 0.15f, 5, 1, 1);
        }
    } break;
    case PROP_PIPE: {
        // Ржавая труба древних машин, наполовину в земле
        f32 len = rng_range(&r, 2.0f, 5.0f), rad = rng_range(&r, 0.25f, 0.55f);
        mb_set(mb, v3_mul(v3(0.30f, 0.17f, 0.10f), rng_range(&r, 0.8f, 1.1f)), MAT_RUST, BONE_NONE);
        mb_cylinder(mb, v3(-len * 0.5f, rad * 0.4f, 0), v3(len * 0.5f, rad * 0.4f, 0), rad, 12);
        for (int s = -1; s <= 1; s += 2)
            mb_cylinder(mb, v3((f32)s * len * 0.5f - 0.06f, rad * 0.4f, 0), v3((f32)s * len * 0.5f + 0.06f, rad * 0.4f, 0), rad * 1.25f, 12);
    } break;
    case PROP_PLATE: {
        mb_set(mb, v3_mul(v3(0.28f, 0.16f, 0.09f), rng_range(&r, 0.8f, 1.1f)), MAT_RUST, BONE_NONE);
        f32 w = rng_range(&r, 1.0f, 2.2f), d = rng_range(&r, 0.8f, 1.6f);
        f32 bend = rng_range(&r, 0.2f, 0.7f);
        mb_box(mb, m4_mul(m4_translate(v3(0, 0.05f, 0)), m4_scale(v3(w, 0.05f, d))));
        mb_box(mb, m4_mul(m4_translate(v3(w * 0.5f + sinf(PI_F * 0.5f - bend) * 0.3f, sinf(bend) * 0.3f, 0)),
                          m4_mul(m4_rot_z(bend), m4_scale(v3(0.6f, 0.05f, d)))));
        // Рёбра жёсткости
        for (int k = -1; k <= 1; k++) mb_box(mb, m4_mul(m4_translate(v3(0, 0.1f, (f32)k * d * 0.3f)), m4_scale(v3(w, 0.08f, 0.06f))));
    } break;
    case PROP_GEAR: {
        // Шестерня неизвестной машины
        f32 rad = rng_range(&r, 0.7f, 1.5f);
        mb_set(mb, v3_mul(v3(0.26f, 0.17f, 0.11f), rng_range(&r, 0.8f, 1.1f)), MAT_RUST, BONE_NONE);
        mb_cylinder(mb, v3(0, rad * 0.8f, -0.1f), v3(0, rad * 0.8f, 0.1f), rad, 20);
        mb_cylinder(mb, v3(0, rad * 0.8f, -0.25f), v3(0, rad * 0.8f, 0.25f), rad * 0.25f, 10);
        u32 teeth = 12 + rng_below(&r, 8);
        for (u32 i = 0; i < teeth; i++) {
            f32 a = 2 * PI_F * (f32)i / (f32)teeth;
            mb_box(mb, m4_mul(m4_translate(v3(sinf(a) * rad * 1.05f, rad * 0.8f + cosf(a) * rad * 1.05f, 0)),
                              m4_mul(m4_rot_z(-a), m4_scale(v3(rad * 0.18f, rad * 0.18f, 0.18f)))));
        }
    } break;
    case PROP_SLAB: {
        // Обломок бетона с арматурой, во мху
        mb_set(mb, v3_mul(v3(0.40f, 0.38f, 0.35f), rng_range(&r, 0.8f, 1.1f)), MAT_CONCRETE, BONE_NONE);
        mb->param = rng_range(&r, 0.2f, 0.5f);
        f32 w = rng_range(&r, 1.0f, 2.6f), h = rng_range(&r, 0.3f, 0.9f), d = rng_range(&r, 0.8f, 2.0f);
        mb_box(mb, m4_mul(m4_translate(v3(0, h * 0.4f, 0)), m4_mul(m4_rot_z(rng_range(&r, -0.15f, 0.15f)), m4_scale(v3(w, h, d)))));
        if (rng_f32(&r) < 0.6f)
            mb_box(mb, m4_mul(m4_translate(v3(w * 0.3f, h * 0.9f, d * 0.1f)), m4_mul(m4_rot_y(rng_range(&r, 0, 1)), m4_scale(v3(w * 0.5f, h * 0.6f, d * 0.6f)))));
        mb_set(mb, v3(0.30f, 0.15f, 0.08f), MAT_RUST, BONE_NONE);
        u32 bars = 2 + rng_below(&r, 4);
        for (u32 i = 0; i < bars; i++) {
            V3 a = v3(rng_range(&r, -w * 0.4f, w * 0.4f), h * 0.6f, rng_range(&r, -d * 0.4f, d * 0.4f));
            V3 b = v3_add(a, v3(rng_range(&r, -0.5f, 0.5f), rng_range(&r, 0.4f, 1.2f), rng_range(&r, -0.5f, 0.5f)));
            mb_cylinder(mb, a, b, 0.02f, 5);
        }
    } break;
    default: break;
    }
}
