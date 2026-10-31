#include "valley_view.h"

#include <string.h>

#include "../core/log.h"
#include "../core/rng.h"
#include "../world/valley.h"

// Пороги детализации: диаметр сферы на экране, пикселей
static const f32 g_tree_lod_px[FLORA_LODS - 1] = { 200.0f, 80.0f, 40.0f };
#define ROCK_LOD_PX 80.0f
#define GRASS_LOD_PX 1100.0f

#define GRID_CELL 32.0f          // участок групп экземпляров, м
#define GRID_N 20                // участков на сторону
#define INST_CAP 900000u         // экземпляров в долине не больше

// Сетки экземпляров подряд: деревья, трава, камни, мелкие камни, мелочи
enum {
    SLOT_TREE = 0,
    SLOT_GRASS = SLOT_TREE + TREE_KINDS * VV_TREE_VARIANTS,
    SLOT_ROCK = SLOT_GRASS + GRASS_KINDS * VV_GRASS_VARIANTS,
    SLOT_PEBBLE = SLOT_ROCK + VV_ROCKS,
    SLOT_PROP = SLOT_PEBBLE + VV_ROCKS,
    SLOT_COUNT = SLOT_PROP + PROP_KINDS * VV_PROP_VARIANTS,
};
#define KEY_COUNT (SLOT_COUNT * GRID_N * GRID_N)

static const GpuMesh *slot_mesh(const ValleyView *v, u32 s)
{
    if (s < SLOT_GRASS) return &v->tree[(s - SLOT_TREE) / VV_TREE_VARIANTS][(s - SLOT_TREE) % VV_TREE_VARIANTS][0];
    if (s < SLOT_ROCK) return &v->grass[(s - SLOT_GRASS) / VV_GRASS_VARIANTS][(s - SLOT_GRASS) % VV_GRASS_VARIANTS][0];
    if (s < SLOT_PEBBLE) return &v->rock[s - SLOT_ROCK][0];
    if (s < SLOT_PROP) return &v->pebble[s - SLOT_PEBBLE];
    return &v->prop[(s - SLOT_PROP) / VV_PROP_VARIANTS][(s - SLOT_PROP) % VV_PROP_VARIANTS];
}

static f32 bare_at(f32 x, f32 z)
{
    return smoothstepf(0.68f, 0.8f, valley_fbm(x * 0.05f + 9.0f, z * 0.05f - 4.0f, 3));
}

// Вода: полоса вдоль русла, цвет вершин — толща (мелко светлее)
static void water(MeshBuilder *mb)
{
    const f32 z0 = -VALLEY_HALF - 10.0f, dz = 2.0f;
    const u32 rows = (u32)((2.0f * VALLEY_HALF + 20.0f) / dz) + 1, cols = 11;
    if (!mb_reserve(mb, rows * cols, (rows - 1) * (cols - 1) * 6)) return;
    mb_set(mb, v3(0, 0, 0), MAT_WATER, BONE_NONE);
    u32 base = mb->vcount;
    for (u32 i = 0; i < rows; i++) {
        f32 z = z0 + (f32)i * dz;
        f32 rx = valley_river_x(z), w = valley_river_w(z) + 5.0f, wy = valley_water_y(z);
        for (u32 j = 0; j < cols; j++) {
            f32 x = rx - w + 2.0f * w * (f32)j / (f32)(cols - 1);
            f32 depth = clampf((wy - valley_height(x, z)) / 1.3f, 0.0f, 1.0f);
            mb->color = v3_lerp(v3(0.13f, 0.12f, 0.085f), v3(0.022f, 0.036f, 0.032f), sqrtf(depth));
            mb_vertex(mb, v3(x, wy, z), v3(0, 1, 0));
        }
    }
    for (u32 i = 0; i + 1 < rows; i++)
        for (u32 j = 0; j + 1 < cols; j++) {
            u32 p00 = base + i * cols + j, p01 = p00 + 1, p10 = p00 + cols, p11 = p10 + 1;
            mb_tri(mb, p00, p10, p11);
            mb_tri(mb, p00, p11, p01);
        }
}

static void box_at(MeshBuilder *mb, V3 c, V3 size, f32 rx, f32 ry, f32 rz)
{
    mb_box(mb, m4_trs(c, size, rx, ry, rz));
}

static void glyph(MeshBuilder *mb, V3 c, V3 normal, f32 hw, f32 hh)
{
    mb_set(mb, v3(0.22f, 0.75f, 0.32f), MAT_GLYPH, BONE_NONE);
    V3 right = v3_norm(v3_cross(v3(0, 1, 0), normal));
    mb_quad(mb, v3_add(c, v3_mul(normal, 0.03f)), v3_mul(right, hw), v3(0, hh, 0));
}

#define AQ_Z (-40.0f)

// Руины, заросшие мхом: акведук через реку, столбы на террасах, кольцо в склоне, труба из горы
static void ruins(MeshBuilder *mb, Rng *rng)
{
    V3 conc = v3(0.40f, 0.385f, 0.36f);
    // Акведук поперёк реки: опоры через 18 м, настил; два пролёта рухнули
    f32 rx = valley_river_x(AQ_Z), top = valley_water_y(AQ_Z) + 21.0f;
    for (int k = -7; k <= 7; k++) {
        f32 x = rx + (f32)k * 18.0f + 9.0f;
        f32 g = valley_height(x, AQ_Z);
        mb_set(mb, conc, MAT_CONCRETE, BONE_NONE);
        mb->param = 0.35f;
        if (g < top - 3.0f) {
            f32 h = top - 2.5f - g + 3.0f;
            box_at(mb, v3(x, g - 3.0f + h * 0.5f, AQ_Z), v3(3.2f, h, 3.6f), 0, 0, 0);
            box_at(mb, v3(x, g + 0.8f, AQ_Z), v3(4.4f, 1.6f, 4.8f), 0, 0, 0);
            if (k == 2) glyph(mb, v3(x, g + 6.0f, AQ_Z + 1.8f), v3(0, 0, 1), 0.7f, 2.0f);
        }
        if (k == 7) break;
        // Настил до следующей опоры: вход в склон обрезает гора
        bool broken = k == 0 || k == -4;
        mb_set(mb, conc, MAT_CONCRETE, BONE_NONE);
        mb->param = 0.3f;
        if (!broken) {
            box_at(mb, v3(x + 9.0f, top - 1.2f, AQ_Z), v3(18.0f, 2.4f, 5.2f), 0, 0, 0);
            box_at(mb, v3(x + 9.0f, top + 0.4f, AQ_Z - 2.3f), v3(18.0f, 0.8f, 0.5f), 0, 0, 0);
            box_at(mb, v3(x + 9.0f, top + 0.4f, AQ_Z + 2.3f), v3(18.0f, 0.8f, 0.5f), 0, 0, 0);
            // Арка под настилом ступенями
            for (int s = 1; s <= 3; s++)
                box_at(mb, v3(x + 9.0f, top - 2.4f - (f32)s * 0.7f, AQ_Z), v3(18.0f - (f32)s * 4.5f, 0.75f, 4.6f), 0, 0, 0);
        } else {
            // Обломок пролёта: один край на опоре, другой в земле или в реке
            f32 gx = valley_height(x + 12.0f, AQ_Z + 3.0f);
            V3 a = v3(x + 2.0f, top - 3.0f, AQ_Z), b = v3(x + 15.0f, gx + 0.5f, AQ_Z + 3.0f);
            V3 c = v3_mul(v3_add(a, b), 0.5f);
            f32 len = v3_len(v3_sub(b, a));
            f32 pitch = atan2f(b.y - a.y, b.x - a.x);
            box_at(mb, c, v3(len, 2.4f, 5.2f), 0, -0.22f, pitch);
            box_at(mb, v3(x + 5.0f, valley_height(x + 5.0f, AQ_Z - 4.0f) + 0.6f, AQ_Z - 4.0f), v3(3.0f, 1.8f, 2.4f), 0.2f, 0.5f, 0.3f);
        }
    }
    // Столбы и плиты на террасах: часть сломана и наклонена
    for (int i = 0; i < 14; i++) {
        f32 x = 0, z = 0;
        for (int tries = 0; tries < 30; tries++) {
            x = rng_range(rng, -260.0f, 260.0f);
            z = rng_range(rng, -260.0f, 260.0f);
            if (valley_river_dist(x, z) > 45.0f && valley_normal(x, z).y > 0.93f) break;
        }
        f32 g = valley_height(x, z);
        f32 h = rng_range(rng, 4.0f, 22.0f), w = rng_range(rng, 1.6f, 3.4f);
        f32 lean = rng_f32(rng) < 0.4f ? rng_range(rng, 0.08f, 0.3f) : 0.0f;
        f32 yaw = rng_range(rng, 0, 2 * PI_F), depth = w * rng_range(rng, 0.8f, 1.6f);
        mb_set(mb, v3_mul(conc, rng_range(rng, 0.85f, 1.05f)), MAT_CONCRETE, BONE_NONE);
        mb->param = rng_range(rng, 0.15f, 0.6f);
        box_at(mb, v3(x + sinf(lean) * h * 0.5f * cosf(yaw), g - 1.5f + h * 0.5f, z - sinf(lean) * h * 0.5f * sinf(yaw)),
               v3(w, h + 3.0f, depth), 0, yaw, -lean);
        if (rng_f32(rng) < 0.5f) box_at(mb, v3(x, g + 0.4f, z), v3(w * 2.4f, 1.4f, w * 2.4f), 0, yaw, 0);
        // Знаки на грани у основания: у неналонённых
        if (i % 3 == 0 && lean == 0.0f)
            glyph(mb, v3(x + sinf(yaw) * depth * 0.5f, g + 2.6f, z + cosf(yaw) * depth * 0.5f), v3(sinf(yaw), 0, cosf(yaw)), w * 0.32f, 1.3f);
        if (i % 3 == 0) {
            // Упавшая капитель рядом
            mb_set(mb, conc, MAT_CONCRETE, BONE_NONE);
            mb->param = 0.3f;
            f32 a = rng_range(rng, 0, 2 * PI_F), r = rng_range(rng, 3.0f, 6.0f);
            f32 px = x + sinf(a) * r, pz = z + cosf(a) * r;
            box_at(mb, v3(px, valley_height(px, pz) + 0.3f, pz), v3(w * 1.5f, w * 0.7f, w * 1.2f), rng_range(rng, -0.3f, 0.3f), a, rng_range(rng, -0.3f, 0.3f));
        }
    }
    // Кольцо неясного назначения, наполовину в склоне на западе
    {
        f32 cz = 110.0f, cxp = valley_river_x(cz) - 120.0f;
        f32 g = valley_height(cxp, cz);
        mb_set(mb, v3(0.33f, 0.32f, 0.30f), MAT_CONCRETE, BONE_NONE);
        mb->param = 0.35f;
        const int segs = 40;
        const f32 R = 34.0f;
        for (int s = 0; s < segs; s++) {
            f32 a = 2 * PI_F * ((f32)s + 0.5f) / (f32)segs;
            V3 c = v3(cxp, g + 4.0f + sinf(a) * R, cz + cosf(a) * R);
            if (c.y < valley_height(c.x, c.z) - 6.0f) continue;
            box_at(mb, c, v3(6.0f, 5.6f, 2 * PI_F * R / (f32)segs + 0.4f), a + 0.5f * PI_F, 0, 0);
        }
        glyph(mb, v3(cxp + 3.2f, g + 7.0f, cz - R + 1.0f), v3(1, 0, 0), 1.6f, 0.6f);
    }
    // Ржавая труба выходит из склона на востоке, конец обломан
    {
        f32 z = -150.0f, x = valley_river_x(z) + 95.0f;
        f32 g = valley_height(x, z);
        mb_set(mb, v3(0.28f, 0.16f, 0.10f), MAT_RUST, BONE_NONE);
        V3 a = v3(x + 40.0f, g - 4.0f, z - 10.0f), b = v3(x, g + 2.0f, z);
        mb_cylinder(mb, a, b, 3.2f, 20);
        for (int k = 1; k <= 4; k++) {
            V3 c = v3_lerp(a, b, (f32)k / 4.6f);
            V3 d = v3_mul(v3_norm(v3_sub(b, a)), 0.3f);
            mb_cylinder(mb, v3_sub(c, d), v3_add(c, d), 3.6f, 20);
        }
    }
}

// Мегаструктура за горами вниз по течению: проступает сквозь дымку в разрыве гряд
static void megastructure(MeshBuilder *mb)
{
    mb_set(mb, v3(0.30f, 0.29f, 0.27f), MAT_CONCRETE, BONE_NONE);
    f32 z = 2100.0f;
    box_at(mb, v3(0, 380.0f, z), v3(4200.0f, 900.0f, 90.0f), 0, 0, 0);
    for (int k = -12; k <= 12; k++) {
        f32 x = (f32)k * 170.0f, h = 980.0f + 140.0f * sinf((f32)k * 1.7f);
        box_at(mb, v3(x, h * 0.5f - 40.0f, z - 60.0f), v3(56.0f, h, 50.0f), 0, 0, 0);
    }
    box_at(mb, v3(-420.0f, 700.0f, z - 300.0f), v3(120.0f, 1600.0f, 120.0f), 0, 0, 0.06f);
    box_at(mb, v3(380.0f, 520.0f, z - 380.0f), v3(70.0f, 1200.0f, 70.0f), 0.0f, 0.4f, -0.22f);
}

// Экземпляр перед сортировкой: ключ группы (сетка, ячейка)
typedef struct Placed {
    RInst r;
    u32   key;
} Placed;

typedef struct Placer {
    Placed *p;
    u32     n, cap;
    Rng    *rng;
} Placer;

static u32 pack_tint(V3 t, u32 rank)
{
    u32 r = (u32)clampf(t.x * 128.0f, 0.0f, 255.0f), g = (u32)clampf(t.y * 128.0f, 0.0f, 255.0f), b = (u32)clampf(t.z * 128.0f, 0.0f, 255.0f);
    return r | g << 8 | b << 16 | (rank & 255u) << 24;
}

// pos — в координатах долины; экземпляр — на карте мира
static void place(Placer *pl, u32 slot, V3 pos, f32 scale, f32 hscale, f32 lean, V3 tint)
{
    if (pl->n >= pl->cap) return;
    u32 cx = (u32)CLAMP((i32)((pos.x + VALLEY_HALF) / GRID_CELL), 0, GRID_N - 1);
    u32 cz = (u32)CLAMP((i32)((pos.z + VALLEY_HALF) / GRID_CELL), 0, GRID_N - 1);
    Placed *p = &pl->p[pl->n++];
    V3 w = v3_add(pos, VALLEY_ORIGIN);
    p->r = (RInst){ { w.x, w.y, w.z }, rng_range(pl->rng, 0, 2 * PI_F), scale, hscale, lean, pack_tint(tint, rng_u32(pl->rng)) };
    p->key = (cz * GRID_N + cx) * SLOT_COUNT + slot;
}

static V3 jitter_tint(Rng *r, f32 a)
{
    f32 k = rng_range(r, 1.0f - a, 1.0f + a);
    return v3(k * rng_range(r, 0.95f, 1.05f), k, k * rng_range(r, 0.9f, 1.1f));
}

static void place_trees(Placer *pl)
{
    Rng *r = pl->rng;
    const f32 step = 3.2f;
    for (f32 z = -VALLEY_HALF + 2; z < VALLEY_HALF - 2; z += step)
        for (f32 x = -VALLEY_HALF + 2; x < VALLEY_HALF - 2; x += step) {
            f32 px = x + rng_range(r, -1.4f, 1.4f), pz = z + rng_range(r, -1.4f, 1.4f);
            f32 u = rng_f32(r), pick = rng_f32(r);
            f32 h = valley_height(px, pz), wy = valley_water_y(pz);
            f32 d = valley_river_dist(px, pz), w = valley_river_w(pz);
            if (h < wy + 0.35f || d < w + 1.5f) continue;
            V3 n = valley_normal(px, pz);
            f32 f = valley_forest(px, pz);
            f32 prob = n.y < 0.8f ? 0.015f : 0.01f + f * 0.42f;
            if (u >= prob) continue;
            f32 e = h - wy;
            TreeKind k;
            if (n.y < 0.8f) k = TREE_DEAD;
            else if (d < w + 20.0f) k = pick < 0.45f ? TREE_BROAD : (pick < 0.8f ? TREE_BIRCH : (pick < 0.94f ? TREE_PINE : TREE_DEAD));
            else if (e > 22.0f) k = pick < 0.62f ? TREE_PINE : (pick < 0.78f ? TREE_BROAD : (pick < 0.88f ? TREE_BIRCH : TREE_DEAD));
            else k = pick < 0.4f ? TREE_BROAD : (pick < 0.65f ? TREE_PINE : (pick < 0.88f ? TREE_BIRCH : TREE_DEAD));
            f32 s = rng_range(r, 0.75f, 1.3f);
            f32 lean = k == TREE_DEAD ? rng_range(r, -0.18f, 0.18f) : rng_range(r, -0.06f, 0.06f);
            // Сухие места — желтее и бледнее листва
            f32 dry = valley_dry(px, pz);
            V3 t = v3_hadamard(jitter_tint(r, 0.12f), v3(1.0f + dry * 0.25f, 1.0f + dry * 0.05f, 1.0f - dry * 0.15f));
            place(pl, SLOT_TREE + (u32)k * VV_TREE_VARIANTS + rng_below(r, VV_TREE_VARIANTS), v3(px, h - 0.1f, pz), s,
                  s * rng_range(r, 0.85f, 1.15f), lean, t);
        }
    // Кусты: по опушкам и берегам
    const f32 bstep = 2.6f;
    for (f32 z = -VALLEY_HALF + 1; z < VALLEY_HALF - 1; z += bstep)
        for (f32 x = -VALLEY_HALF + 1; x < VALLEY_HALF - 1; x += bstep) {
            f32 px = x + rng_range(r, -1.1f, 1.1f), pz = z + rng_range(r, -1.1f, 1.1f);
            f32 u = rng_f32(r);
            f32 h = valley_height(px, pz), wy = valley_water_y(pz);
            f32 d = valley_river_dist(px, pz), w = valley_river_w(pz);
            if (h < wy + 0.25f) continue;
            if (valley_normal(px, pz).y < 0.72f) continue;
            f32 f = valley_forest(px, pz);
            f32 edge = smoothstepf(0.1f, 0.35f, f) * smoothstepf(0.75f, 0.45f, f);
            f32 bank = smoothstepf(w + 1.0f, w + 2.5f, d) * smoothstepf(w + 10.0f, w + 5.0f, d);
            if (u >= 0.015f + edge * 0.3f + bank * 0.22f + f * 0.08f) continue;
            f32 s = rng_range(r, 0.65f, 1.45f);
            place(pl, SLOT_TREE + (u32)TREE_BUSH * VV_TREE_VARIANTS + rng_below(r, VV_TREE_VARIANTS), v3(px, h - 0.05f, pz), s,
                  s * rng_range(r, 0.75f, 1.2f), rng_range(r, -0.08f, 0.08f), jitter_tint(r, 0.15f));
        }
}

static void place_rock(Placer *pl, V3 p, f32 size)
{
    Rng *r = pl->rng;
    u32 slot = size < 0.55f ? SLOT_PEBBLE : SLOT_ROCK;
    f32 h = valley_height(p.x, p.z);
    place(pl, slot + rng_below(r, VV_ROCKS), v3(p.x, h - size * rng_range(r, 0.15f, 0.4f), p.z), size * rng_range(r, 0.8f, 1.2f),
          size * rng_range(r, 0.6f, 1.15f), rng_range(r, -0.3f, 0.3f), jitter_tint(r, 0.1f));
}

static void place_rocks(Placer *pl)
{
    Rng *r = pl->rng;
    // Россыпи: чаще на склонах и у подножий уступов
    for (u32 c = 0; c < 700; c++) {
        f32 cx = rng_range(r, -VALLEY_HALF + 8, VALLEY_HALF - 8), cz = rng_range(r, -VALLEY_HALF + 8, VALLEY_HALF - 8);
        V3 n = valley_normal(cx, cz);
        f32 steep = 1.0f - n.y;
        if (rng_f32(r) > 0.1f + steep * 3.0f) continue;
        u32 k = 3 + rng_below(r, 14);
        f32 rad = rng_range(r, 3.0f, 12.0f);
        for (u32 i = 0; i < k; i++) {
            f32 a = rng_range(r, 0, 2 * PI_F), d = rad * sqrtf(rng_f32(r));
            f32 u = rng_f32(r);
            place_rock(pl, v3(cx + sinf(a) * d, 0, cz + cosf(a) * d), 0.25f + 3.0f * u * u * u);
        }
    }
    // Одиночные камни на равнинах
    for (u32 i = 0; i < 250; i++) {
        V3 p = v3(rng_range(r, -VALLEY_HALF, VALLEY_HALF), 0, rng_range(r, -VALLEY_HALF, VALLEY_HALF));
        place_rock(pl, p, rng_range(r, 0.2f, 0.9f));
    }
    // Камни в русле и на перекатах
    for (u32 i = 0; i < 500; i++) {
        f32 z = rng_range(r, -VALLEY_HALF, VALLEY_HALF);
        f32 x = valley_river_x(z) + valley_river_w(z) * rng_range(r, -1.2f, 1.2f);
        f32 u = rng_f32(r);
        place_rock(pl, v3(x, 0, z), 0.25f + 1.2f * u * u);
    }
}

static void place_props(Placer *pl)
{
    Rng *r = pl->rng;
    // Валежник и пни в рощах
    for (f32 z = -VALLEY_HALF + 4; z < VALLEY_HALF - 4; z += 8.0f)
        for (f32 x = -VALLEY_HALF + 4; x < VALLEY_HALF - 4; x += 8.0f) {
            f32 px = x + rng_range(r, -3.5f, 3.5f), pz = z + rng_range(r, -3.5f, 3.5f);
            f32 u = rng_f32(r), pick = rng_f32(r);
            f32 f = valley_forest(px, pz);
            f32 h = valley_height(px, pz);
            if (h < valley_water_y(pz) + 0.3f || valley_normal(px, pz).y < 0.85f) continue;
            if (u >= 0.01f + f * 0.2f) continue;
            PropKind k = pick < 0.6f ? PROP_LOG : PROP_STUMP;
            place(pl, SLOT_PROP + (u32)k * VV_PROP_VARIANTS + rng_below(r, VV_PROP_VARIANTS), v3(px, h - 0.05f, pz),
                  rng_range(r, 0.8f, 1.2f), rng_range(r, 0.85f, 1.15f), 0.0f, jitter_tint(r, 0.12f));
        }
    // Металлолом и обломки: у акведука гуще, остальное по всей долине
    for (u32 i = 0; i < 260; i++) {
        V3 p;
        if (i < 110) {
            f32 x = valley_river_x(AQ_Z) + rng_range(r, -110.0f, 110.0f);
            p = v3(x, 0, AQ_Z + rng_range(r, -25.0f, 25.0f));
        } else {
            p = v3(rng_range(r, -VALLEY_HALF + 5, VALLEY_HALF - 5), 0, rng_range(r, -VALLEY_HALF + 5, VALLEY_HALF - 5));
        }
        f32 h = valley_height(p.x, p.z);
        if (h < valley_water_y(p.z) - 0.6f) continue;
        static const PropKind kinds[] = { PROP_SLAB, PROP_SLAB, PROP_SLAB, PROP_PIPE, PROP_PLATE, PROP_GEAR };
        PropKind k = kinds[rng_below(r, ARRAY_COUNT(kinds))];
        f32 lean = k == PROP_GEAR ? rng_range(r, 0.3f, 1.2f) : rng_range(r, -0.12f, 0.12f);
        f32 sink = k == PROP_GEAR ? 0.6f : 0.12f;
        place(pl, SLOT_PROP + (u32)k * VV_PROP_VARIANTS + rng_below(r, VV_PROP_VARIANTS), v3(p.x, h - sink, p.z),
              rng_range(r, 0.8f, 1.25f), rng_range(r, 0.8f, 1.2f), lean, jitter_tint(r, 0.1f));
    }
}

static void place_grass(Placer *pl)
{
    Rng *r = pl->rng;
    const f32 step = 0.65f;
    for (f32 z = -VALLEY_HALF + 0.5f; z < VALLEY_HALF - 0.5f; z += step)
        for (f32 x = -VALLEY_HALF + 0.5f; x < VALLEY_HALF - 0.5f; x += step) {
            f32 px = x + rng_range(r, -0.35f, 0.35f), pz = z + rng_range(r, -0.35f, 0.35f);
            f32 u = rng_f32(r), pick = rng_f32(r);
            f32 h = valley_height(px, pz), wy = valley_water_y(pz);
            f32 d = valley_river_dist(px, pz), w = valley_river_w(pz);
            GrassKind k;
            f32 dens;
            if (d < w + 2.5f || (h < wy + 0.3f && d < w + 12.0f)) {
                // Тростник по урезу воды, на глубине ничего
                if (h < wy - 0.45f) continue;
                k = GRASS_REEDS;
                dens = 0.45f * valley_fbm(px * 0.2f, pz * 0.2f, 2);
            } else {
                V3 n = valley_normal(px, pz);
                if (n.y < 0.8f) continue;
                f32 f = valley_forest(px, pz), dry = valley_dry(px, pz);
                f32 patch = smoothstepf(0.22f, 0.55f, valley_fbm(px * 0.15f + 3.0f, pz * 0.15f - 7.0f, 2));
                dens = (0.25f + 0.75f * patch) * (1.0f - bare_at(px, pz) * 0.85f) * (1.0f - f * 0.6f) * smoothstepf(0.8f, 0.9f, n.y);
                if (pick < f * 0.35f) k = GRASS_FERN;
                else if (pick < 0.08f + 0.35f * dry) k = GRASS_TALL;
                else if (pick > 0.97f - 0.04f * (1.0f - dry)) k = GRASS_FLOWERS;
                else k = GRASS_MEADOW;
            }
            if (u >= dens) continue;
            // Оттенок травы — как земля здесь: на сухих местах желтее
            V3 t = v3_lerp(v3(1.0f, 1.0f, 1.0f), v3(1.6f, 1.25f, 0.9f), valley_ground_dry(px, pz, h));
            t = v3_hadamard(t, jitter_tint(r, 0.12f));
            f32 s = rng_range(r, 0.8f, 1.25f);
            place(pl, SLOT_GRASS + (u32)k * VV_GRASS_VARIANTS + rng_below(r, VV_GRASS_VARIANTS), v3(px, h, pz), s,
                  s * rng_range(r, 0.75f, 1.3f), rng_range(r, -0.12f, 0.12f), t);
        }
}

// Экземпляры по участкам и группам (участок, сетка), внутри группы — по возрастанию порядка прореживания
static void build_groups(ValleyView *v, const Placed *src, u32 n, u32 *idx, u32 *cnt)
{
    // Сначала по порядку прореживания, затем устойчиво по ключу группы
    memset(cnt, 0, sizeof(u32) * 257);
    for (u32 i = 0; i < n; i++) cnt[(src[i].r.tint >> 24) + 1]++;
    for (u32 b = 1; b <= 256; b++) cnt[b] += cnt[b - 1];
    u32 *by_rank = idx + n;
    for (u32 i = 0; i < n; i++) by_rank[cnt[src[i].r.tint >> 24]++] = i;
    memset(cnt, 0, sizeof(u32) * (KEY_COUNT + 1));
    for (u32 i = 0; i < n; i++) cnt[src[i].key + 1]++;
    for (u32 k = 1; k <= KEY_COUNT; k++) cnt[k] += cnt[k - 1];
    u32 *start = idx + 2 * n;
    memcpy(start, cnt, sizeof(u32) * (KEY_COUNT + 1));
    for (u32 i = 0; i < n; i++) {
        const Placed *p = &src[by_rank[i]];
        v->inst[cnt[p->key]++] = p->r;
    }
    v->inst_count = n;
    v->group_count = v->cluster_count = 0;
    for (u32 k = 0; k < KEY_COUNT; k++) {
        u32 a = start[k], b = start[k + 1];
        if (k % SLOT_COUNT == 0) v->clusters[v->cluster_count++] = (InstCluster){ .first = v->group_count };
        if (a == b) continue;
        u32 slot = k % SLOT_COUNT;
        const GpuMesh *m = slot_mesh(v, slot);
        V3 lo = v3(1e9f, 1e9f, 1e9f), hi = v3(-1e9f, -1e9f, -1e9f);
        f32 smax = 0.0f;
        for (u32 i = a; i < b; i++) {
            const RInst *ri = &v->inst[i];
            lo = v3(fminf(lo.x, ri->pos[0]), fminf(lo.y, ri->pos[1]), fminf(lo.z, ri->pos[2]));
            hi = v3(fmaxf(hi.x, ri->pos[0]), fmaxf(hi.y, ri->pos[1]), fmaxf(hi.z, ri->pos[2]));
            smax = fmaxf(smax, fmaxf(ri->scale, ri->hscale));
        }
        V3 c = v3_mul(v3_add(lo, hi), 0.5f);
        f32 mr = (m->bound.w + v3_len(v3(m->bound.x, m->bound.y, m->bound.z))) * smax;
        bool grass = slot >= SLOT_GRASS && slot < SLOT_ROCK;
        v->groups[v->group_count++] = (InstGroup){
            .mesh = m, .first = a, .count = b - a,
            .bound = { c.x, c.y, c.z, v3_len(v3_sub(hi, c)) + mr },
            .bulk = grass, .no_shadow = grass || (slot >= SLOT_PEBBLE && slot < SLOT_PROP),
        };
    }
    // Сферы участков вокруг их групп
    for (u32 c = 0; c < v->cluster_count; c++) {
        InstCluster *cl = &v->clusters[c];
        cl->count = (c + 1 < v->cluster_count ? v->clusters[c + 1].first : v->group_count) - cl->first;
        V3 lo = v3(1e9f, 1e9f, 1e9f), hi = v3(-1e9f, -1e9f, -1e9f);
        for (u32 g = cl->first; g < cl->first + cl->count; g++) {
            V4 b = v->groups[g].bound;
            lo = v3(fminf(lo.x, b.x - b.w), fminf(lo.y, b.y - b.w), fminf(lo.z, b.z - b.w));
            hi = v3(fmaxf(hi.x, b.x + b.w), fmaxf(hi.y, b.y + b.w), fmaxf(hi.z, b.z + b.w));
        }
        V3 c3 = v3_mul(v3_add(lo, hi), 0.5f);
        f32 r = 0.0f;
        for (u32 g = cl->first; g < cl->first + cl->count; g++) {
            V4 b = v->groups[g].bound;
            r = fmaxf(r, v3_len(v3_sub(v3(b.x, b.y, b.z), c3)) + b.w);
        }
        cl->bound = (V4){ c3.x, c3.y, c3.z, r };
    }
}

// Пустой построитель сетки во временной части арены
static void fresh(MeshBuilder *mb, Arena *perm, size_t mark, u32 vcap, u32 icap)
{
    perm->used = mark;
    mb_init(mb, perm, vcap, icap);
}

// Сетка строилась в координатах долины: перенести на карту мира
static void upload_placed(GpuMesh *gm, MeshBuilder *mb)
{
    V3 o = VALLEY_ORIGIN;
    for (u32 i = 0; i < mb->vcount; i++) {
        mb->v[i].pos[0] += o.x;
        mb->v[i].pos[1] += o.y;
        mb->v[i].pos[2] += o.z;
    }
    gpu_mesh_upload(gm, mb);
}

bool valley_view_build(ValleyView *v, Arena *perm, u64 seed)
{
    if (v->built) return true;
    valley_init(perm);
    Rng rng;
    rng_seed(&rng, seed, 401);
    // Постоянное: экземпляры и группы
    v->inst = ARENA_PUSH_ARRAY(perm, RInst, INST_CAP);
    v->groups = ARENA_PUSH_ARRAY(perm, InstGroup, KEY_COUNT);
    if (!v->inst || !v->groups) return false;
    size_t mark = perm->used;
    MeshBuilder mb;

    // Рельеф долины — часть карты мира (map.c); здесь вода, руины и мегаструктура
    fresh(&mb, perm, mark, 8000, 40000);
    water(&mb);
    upload_placed(&v->water, &mb);
    fresh(&mb, perm, mark, 60000, 120000);
    ruins(&mb, &rng);
    upload_placed(&v->ruins, &mb);
    fresh(&mb, perm, mark, 2000, 6000);
    megastructure(&mb);
    upload_placed(&v->mega, &mb);

    // Растения и камни: у каждой сетки своё зерно
    u32 tree_tris[TREE_KINDS] = { 0 };
    for (u32 k = 0; k < TREE_KINDS; k++)
        for (u32 var = 0; var < VV_TREE_VARIANTS; var++)
            for (u32 l = 0; l < FLORA_LODS; l++) {
                fresh(&mb, perm, mark, 30000, 90000);
                flora_tree(&mb, (TreeKind)k, (u32)seed * 977u + k * 131u + var * 17u + 5u, l);
                if (!l) tree_tris[k] = MAX(tree_tris[k], mb.icount / 3);
                GpuMesh *gm = &v->tree[k][var][l];
                gpu_mesh_upload(gm, &mb);
                if (l) gpu_mesh_set_lod(&v->tree[k][var][l - 1], gm, g_tree_lod_px[l - 1]);
            }
    // Трава редеет с дальности: начало и конец, м
    static const f32 grass_fade[GRASS_KINDS][2] = { { 18, 55 }, { 22, 65 }, { 35, 100 }, { 18, 55 }, { 15, 45 } };
    for (u32 k = 0; k < GRASS_KINDS; k++)
        for (u32 var = 0; var < VV_GRASS_VARIANTS; var++)
            for (u32 l = 0; l < GRASS_LODS; l++) {
                fresh(&mb, perm, mark, 4000, 12000);
                flora_grass(&mb, (GrassKind)k, (u32)seed * 31u + k * 7u + var * 101u, l);
                GpuMesh *gm = &v->grass[k][var][l];
                gpu_mesh_upload(gm, &mb);
                gm->fade[0] = grass_fade[k][0];
                gm->fade[1] = grass_fade[k][1];
                gm->two_sided = true;
                // Участок травы дальше ~40 м (1080p) — редкие широкие травинки
                if (l) gpu_mesh_set_lod(&v->grass[k][var][0], gm, GRASS_LOD_PX);
            }
    for (u32 i = 0; i < VV_ROCKS; i++) {
        for (u32 l = 0; l < 2; l++) {
            fresh(&mb, perm, mark, 4000, 4000);
            flora_rock(&mb, (u32)seed * 13u + i * 71u, l);
            gpu_mesh_upload(&v->rock[i][l], &mb);
        }
        gpu_mesh_set_lod(&v->rock[i][0], &v->rock[i][1], ROCK_LOD_PX);
        v->pebble[i] = v->rock[i][1];
        v->pebble[i].fade[0] = 25.0f;
        v->pebble[i].fade[1] = 60.0f;
    }
    for (u32 k = 0; k < PROP_KINDS; k++)
        for (u32 var = 0; var < VV_PROP_VARIANTS; var++) {
            fresh(&mb, perm, mark, 6000, 18000);
            prop_ground(&mb, (PropKind)k, (u32)seed * 7u + k * 53u + var * 29u);
            gpu_mesh_upload(&v->prop[k][var], &mb);
        }
    perm->used = mark;

    // Расстановка: временные массивы над постоянными, после сортировки освобождаются
    Placer pl = { .p = ARENA_PUSH_ARRAY(perm, Placed, INST_CAP), .cap = INST_CAP, .rng = &rng };
    u32 *idx = ARENA_PUSH_ARRAY(perm, u32, (size_t)INST_CAP * 2 + KEY_COUNT + 1);
    u32 *cnt = ARENA_PUSH_ARRAY(perm, u32, KEY_COUNT + 1);
    if (!pl.p || !idx || !cnt) {
        perm->used = mark;
        return false;
    }
    place_trees(&pl);
    u32 n_trees = pl.n;
    place_rocks(&pl);
    place_props(&pl);
    u32 n_solid = pl.n;
    place_grass(&pl);
    if (pl.n >= pl.cap) log_error("ДОЛИНА: экземпляров больше %u, лишние отброшены", INST_CAP);
    build_groups(v, pl.p, pl.n, idx, cnt);
    perm->used = mark;
    render_upload_instances(v->inst, v->inst_count);

    // Отряд появляется на восточном берегу, ниже акведука, лицом вверх по течению
    v->spawn = v3_add(valley_spawn(), VALLEY_ORIGIN);
    v->spawn_heading = PI_F;
    v->built = true;
    log_stat("ДОЛИНА: деревьев и кустов %u, камней и мелочей %u, пучков травы %u, групп %u", n_trees, n_solid - n_trees,
             pl.n - n_solid, v->group_count);
    log_stat("ДОЛИНА: дерево LOD0 до %u треугольников (листвень %u, ель %u, берёза %u, сухое %u, куст %u)",
             MAX(MAX(tree_tris[0], tree_tris[1]), MAX(tree_tris[2], tree_tris[3])), tree_tris[0], tree_tris[1], tree_tris[2],
             tree_tris[3], tree_tris[4]);
    return true;
}

u32 valley_view_draw(const ValleyView *v, V3 eye, DrawItem *out)
{
    u32 n = 0;
    // Вода и руины издалека не видны в дымке; мегаструктура видна отовсюду
    V3 d = v3_sub(eye, VALLEY_ORIGIN);
    if (fmaxf(fabsf(d.x), fabsf(d.z)) < VALLEY_HALF + 1200.0f) {
        out[n++] = (DrawItem){ .mesh = &v->water, .no_shadow = true };
        out[n++] = (DrawItem){ .mesh = &v->ruins };
    }
    out[n++] = (DrawItem){ .mesh = &v->mega, .no_shadow = true };
    return n;
}
