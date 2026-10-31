#include "town_build.h"

#include "props.h"
#include "shapes.h"

// Скудель собирается из объектов (data/defs/objects.txt) по заготовке skudel (data/defs/prefabs.txt). Здесь —
// формы её частей: генератор строит один экземпляр в его осях, разброс — из зерна. Ярусы — для высоты земли

// Ярусы: нижний (улица), средний и верхний; за верхним древняя стена
#define TX0 56.0f
#define TX1 100.0f
#define TZ0 -22.0f
#define TZ1 4.0f    // фронт среднего яруса
#define TZ2 16.0f   // фронт верхнего
#define TZ3 26.0f   // подножие стены
#define TH0 0.05f
#define TH1 3.5f
#define TH2 7.0f
// Пандус на средний ярус: вырез в его плите
#define RX0 75.5f
#define RX1 80.5f
#define RZ1 11.0f
// Лестница на верхний ярус вдоль его фронта, вверх на восток, наверху площадка
#define SX0 84.0f
#define SX1 92.0f
#define SX2 94.0f
#define SZ0 14.0f

typedef enum StallKind { STALL_SCRAP, STALL_WATER, STALL_FOOD, STALL_CLOTH, STALL_RELIC, STALL_TOOLS, STALL_KINDS } StallKind;

// ---------------------------------------------------------------- части домов

// Дверь: точка — низ середины проёма на фасаде. Проём материалом 0, перемычка материалом 1
static void sh_door(const ShapeCtx *c)
{
    prop_mat(c->mb, c->mat[0]);
    prop_lbox(c->mb, c->L, v3(0, c->size.y * 0.5f, 0), c->size, 0, 0, 0);
    prop_paint(c->mb, c->r, c->mat[1], 0.2f);
    prop_lbox(c->mb, c->L, v3(0, c->size.y + 0.08f, 0.04f), v3(c->size.x + 0.25f, 0.12f, 0.1f), 0, 0, 0);
}

// Навес над дверью из листа или ткани, наклонён от стены; точка — середина
static void sh_awning(const ShapeCtx *c)
{
    Rng *r = c->r;
    mb_set(c->mb, rng_f32(r) < 0.5f ? mat_tint(r, g_mat.sheet, 0.2f) : mat_tint(r, g_mat.cloth, 0.2f), MAT_RUST, BONE_NONE);
    prop_lbox(c->mb, c->L, v3(0, 0, 0), c->size, 0.3f, 0, rng_range(r, -0.04f, 0.04f));
}

// Окно: тёмный проём или свет изнутри, материал 0 светится сам
static void sh_window(const ShapeCtx *c)
{
    mb_set(c->mb, mat_tint(c->r, c->mat[0], c->tint), MAT_EMISSIVE, BONE_NONE);
    prop_box(c->mb, c->pos, c->size, c->yaw);
}

// Заплата из листа: чуть косо
static void sh_patch(const ShapeCtx *c)
{
    Rng *r = c->r;
    mb_set(c->mb, rng_f32(r) < 0.7f ? mat_tint(r, g_mat.sheet, 0.25f) : mat_tint(r, g_mat.metal, 0.3f), MAT_RUST, BONE_NONE);
    prop_lbox(c->mb, c->L, v3(0, 0, 0), c->size, 0, 0, rng_range(r, -0.08f, 0.08f));
}

// Балкон: точка — середина края плиты у фасада, size — ширина, высота перил, вынос. Перила, бельё, ящик
static void sh_balcony(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    Rng *r = c->r;
    M4 L = c->L;
    f32 bw = c->size.x, bd = c->size.z, rh = c->size.y;
    prop_paint(mb, r, g_mat.concrete_dark, 0.1f);
    prop_lbox(mb, L, v3(0, -0.02f, bd * 0.5f), v3(bw, 0.14f, bd), 0, 0, 0);
    prop_paint(mb, r, g_mat.rust, 0.2f);
    f32 fz = bd - 0.04f, ry = 0.05f;
    for (int s = -1; s <= 1; s++) prop_lcyl(mb, L, v3((f32)s * bw * 0.48f, ry, fz), v3((f32)s * bw * 0.48f, ry + rh, fz), 0.03f, 4);
    prop_lcyl(mb, L, v3(-bw * 0.5f, ry + rh, fz), v3(bw * 0.5f, ry + rh, fz), 0.03f, 4);
    prop_lcyl(mb, L, v3(-bw * 0.48f, ry + rh, 0), v3(-bw * 0.48f, ry + rh, fz), 0.025f, 4);
    prop_lcyl(mb, L, v3(bw * 0.48f, ry + rh, 0), v3(bw * 0.48f, ry + rh, fz), 0.025f, 4);
    if (rng_f32(r) < 0.55f) {
        prop_paint(mb, r, g_mat.cloth, 0.2f);
        prop_lbox(mb, L, v3(rng_range(r, -bw * 0.25f, bw * 0.25f), ry + 0.65f, fz + 0.04f),
                  v3(rng_range(r, 0.5f, 0.9f), rng_range(r, 0.5f, 0.8f), 0.02f), 0, 0, rng_range(r, -0.05f, 0.05f));
    }
    if (rng_f32(r) < 0.5f) prop_crate(mb, r, m4_point(L, v3(-bw * 0.3f, ry, 0.35f)), c->yaw, 0.4f);
}

// Водосток: от крыши (size.y) вниз, у земли колено вперёд
static void sh_downpipe(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, g_mat.rust, 0.2f);
    prop_lcyl(c->mb, c->L, v3(0, c->size.y, 0), v3(0, 0.3f, 0), 0.06f, 6);
    prop_lcyl(c->mb, c->L, v3(0, 0.3f, 0), v3(0, 0.05f, 0.35f), 0.06f, 6);
}

// Бак на крыше: точка — на крыше
static void sh_roof_tank(const ShapeCtx *c)
{
    Rng *r = c->r;
    prop_paint(c->mb, r, g_mat.sheet, 0.15f);
    f32 rr = rng_range(r, 0.45f, 0.75f), hh = rng_range(r, 0.9f, 1.5f);
    prop_lcyl(c->mb, c->L, v3(0, 0.25f, 0), v3(0, 0.25f + hh, 0), rr, 12);
    prop_paint(c->mb, r, g_mat.rust, 0.2f);
    prop_lbox(c->mb, c->L, v3(0, 0.12f, 0), v3(rr * 1.8f, 0.25f, rr * 1.8f), 0, 0, 0);
}

static void sh_antenna(const ShapeCtx *c)
{
    Rng *r = c->r;
    prop_paint(c->mb, r, g_mat.metal, 0.2f);
    f32 hh = rng_range(r, 2.0f, 4.0f);
    V3 top = v3(rng_range(r, -0.15f, 0.15f), hh, rng_range(r, -0.15f, 0.15f));
    prop_lcyl(c->mb, c->L, v3(0, 0, 0), top, 0.03f, 4);
    for (u32 j = 0; j < 2; j++) {
        f32 yy = hh * (0.6f + 0.25f * (f32)j), half = rng_range(r, 0.3f, 0.6f);
        prop_lcyl(c->mb, c->L, v3(-half, yy, 0), v3(half, yy, 0), 0.015f, 4);
    }
}

// Брезент на крыше: куча ткани, лежит как бросили
static void sh_tarp(const ShapeCtx *c)
{
    Rng *r = c->r;
    prop_paint(c->mb, r, g_mat.cloth, 0.2f);
    prop_lbox(c->mb, c->L, v3(0, 0.35f, 0), v3(rng_range(r, 1.2f, 2.0f), 0.5f, rng_range(r, 1.0f, 1.6f)), rng_range(r, -0.1f, 0.1f),
              rng_range(r, 0, PI_F), rng_range(r, -0.1f, 0.1f));
}

// Бортик крыши по фасаду и левому боку: size — ширина и глубина крыши
static void sh_parapet(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, g_mat.concrete_dark, 0.1f);
    prop_lbox(c->mb, c->L, v3(0, 0.3f, c->size.z * 0.5f), v3(c->size.x, c->size.y, 0.15f), 0, 0, 0);
    prop_lbox(c->mb, c->L, v3(-c->size.x * 0.5f, 0.3f, 0), v3(0.15f, c->size.y, c->size.z), 0, 0, 0);
}

// ---------------------------------------------------------------- прилавки

static void goods(MeshBuilder *mb, Rng *r, M4 L, u32 kind, f32 w, f32 d, f32 h)
{
    f32 cz = -d * 0.5f;
    u32 n = 4 + rng_below(r, 5);
    for (u32 i = 0; i < n; i++) {
        f32 x = rng_range(r, -w * 0.5f + 0.18f, w * 0.5f - 0.18f), z = cz + rng_range(r, -d * 0.5f + 0.15f, d * 0.5f - 0.15f);
        f32 yaw = rng_range(r, 0, PI_F);
        switch (kind) {
        case STALL_SCRAP: {
            u32 k = rng_below(r, 3);
            mb_set(mb, k == 1 ? mat_tint(r, g_mat.metal, 0.3f) : mat_tint(r, g_mat.sheet, 0.2f), k == 1 ? MAT_METAL : MAT_RUST, BONE_NONE);
            if (k == 0) prop_lcyl(mb, L, v3(x, h, z), v3(x, h + 0.04f, z), rng_range(r, 0.07f, 0.15f), 8);
            else if (k == 1) prop_lcyl(mb, L, v3(x - 0.25f, h + 0.04f, z), v3(x + 0.25f, h + 0.04f, z + rng_range(r, -0.1f, 0.1f)), 0.035f, 6);
            else prop_lbox(mb, L, v3(x, h + 0.07f, z), v3(rng_range(r, 0.1f, 0.25f), 0.14f, rng_range(r, 0.1f, 0.2f)), 0, yaw, 0);
        } break;
        case STALL_WATER: {
            if (rng_f32(r) < 0.55f) {
                static const V3 cc[3] = { { 0.24f, 0.27f, 0.18f }, { 0.40f, 0.22f, 0.12f }, { 0.22f, 0.25f, 0.29f } };
                mb_set(mb, prop_tint(r, cc[rng_below(r, 3)], 0.15f), MAT_METAL, BONE_NONE);
                prop_lbox(mb, L, v3(x, h + 0.16f, z), v3(0.22f, 0.32f, 0.14f), 0, yaw * 0.3f, 0);
            } else {
                mb_set(mb, prop_tint(r, v3(0.32f, 0.26f, 0.20f), 0.2f), MAT_LEATHER, BONE_NONE);
                f32 hh = rng_range(r, 0.18f, 0.3f), rr = rng_range(r, 0.06f, 0.1f);
                prop_lcyl(mb, L, v3(x, h, z), v3(x, h + hh, z), rr, 8);
                prop_lcyl(mb, L, v3(x, h + hh, z), v3(x, h + hh + 0.06f, z), rr * 0.4f, 6);
            }
        } break;
        case STALL_FOOD: {
            mb_set(mb, prop_tint(r, v3(0.40f, 0.35f, 0.25f), 0.15f), MAT_CLOTH, BONE_NONE);
            f32 s = rng_range(r, 0.22f, 0.34f);
            prop_lbox(mb, L, v3(x, h + s * 0.4f, z), v3(s, s * 0.8f, s), 0, yaw, 0);
            mb_set(mb, rng_f32(r) < 0.5f ? prop_tint(r, v3(0.30f, 0.20f, 0.10f), 0.2f) : prop_tint(r, v3(0.38f, 0.33f, 0.18f), 0.2f), MAT_SAND,
                   BONE_NONE);
            prop_lbox(mb, L, v3(x, h + s * 0.8f + 0.01f, z), v3(s * 0.85f, 0.04f, s * 0.85f), 0, yaw, 0);
        } break;
        case STALL_CLOTH: {
            f32 yy = h;
            u32 layers = 2 + rng_below(r, 5);
            for (u32 j = 0; j < layers; j++) {
                prop_paint(mb, r, g_mat.cloth, 0.2f);
                prop_lbox(mb, L, v3(x, yy + 0.025f, z), v3(0.34f, 0.05f, 0.26f), 0, rng_range(r, -0.15f, 0.15f), 0);
                yy += 0.05f;
            }
        } break;
        case STALL_RELIC: {
            u32 k = rng_below(r, 3);
            if (k == 0) {
                mb_set(mb, v3(0.12f, 0.45f, 0.18f), MAT_GLYPH, BONE_NONE);
                prop_lbox(mb, L, v3(x, h + 0.015f, z), v3(0.36f, 0.03f, 0.28f), 0, yaw * 0.2f, 0);
            } else if (k == 1) {
                prop_paint(mb, r, g_mat.metal, 0.3f);
                prop_lcyl(mb, L, v3(x, h, z), v3(x, h + rng_range(r, 0.1f, 0.25f), z), rng_range(r, 0.08f, 0.14f), 10);
                prop_lbox(mb, L, v3(x, h + 0.05f, z), v3(0.3f, 0.06f, 0.08f), 0, yaw, 0);
            } else {
                mb_set(mb, prop_tint(r, v3(0.33f, 0.31f, 0.28f), 0.2f), MAT_CONCRETE, BONE_NONE);
                prop_lbox(mb, L, v3(x, h + 0.1f, z), v3(0.18f, 0.2f, 0.18f), 0.2f, yaw, 0.3f);
            }
        } break;
        default: {
            prop_paint(mb, r, g_mat.metal, 0.3f);
            if (rng_f32(r) < 0.6f) {
                prop_lbox(mb, L, v3(x, h + 0.01f, z), v3(0.05f, 0.015f, rng_range(r, 0.55f, 0.85f)), 0, rng_range(r, -0.1f, 0.1f), 0);
            } else {
                mb_set(mb, prop_tint(r, v3(0.30f, 0.22f, 0.15f), 0.2f), MAT_LEATHER, BONE_NONE);
                prop_lcyl(mb, L, v3(x, h + 0.03f, z - 0.2f), v3(x, h + 0.03f, z + 0.15f), 0.02f, 5);
                prop_paint(mb, r, g_mat.metal, 0.3f);
                prop_lbox(mb, L, v3(x, h + 0.05f, z + 0.18f), v3(0.14f, 0.07f, 0.07f), 0, 0, 0);
            }
        } break;
        }
    }
}

// Прилавок: точка — середина переднего края стола у земли, +z — к покупателям; size — ширина, высота и глубина
// стола. Стол, навес, товар (arg — StallKind), вывеска или лампа. Продавец стоит за столом (место в town_spots)
static void sh_stall(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    Rng *r = c->r;
    M4 L = c->L;
    u32 kind = c->arg;
    f32 w = c->size.x, h = c->size.y, d = c->size.z;
    f32 cz = -d * 0.5f;
    // Стол: лист на ножках, на бетонных блоках или на бочках
    bool rust = rng_f32(r) < 0.5f;
    mb_set(mb, rust ? mat_tint(r, g_mat.rust, 0.2f) : mat_tint(r, g_mat.metal, 0.25f), rust ? MAT_RUST : MAT_METAL, BONE_NONE);
    prop_lbox(mb, L, v3(0, h - 0.03f, cz), v3(w, 0.06f, d), 0, rng_range(r, -0.02f, 0.02f), rng_range(r, -0.01f, 0.01f));
    u32 table = rng_below(r, 3);
    if (table == 0) {
        for (int i = 0; i < 4; i++)
            prop_lbox(mb, L, v3((i & 1 ? 1.0f : -1.0f) * (w * 0.5f - 0.1f), (h - 0.06f) * 0.5f, cz + (i & 2 ? 1.0f : -1.0f) * (d * 0.5f - 0.08f)),
                      v3(0.05f, h - 0.06f, 0.05f), 0, 0, 0);
    } else if (table == 1) {
        prop_paint(mb, r, g_mat.concrete, 0.1f);
        for (int s = -1; s <= 1; s += 2)
            prop_lbox(mb, L, v3((f32)s * (w * 0.5f - 0.3f), (h - 0.06f) * 0.5f, cz), v3(0.4f, h - 0.06f, d * 0.85f), 0, rng_range(r, -0.05f, 0.05f), 0);
    } else {
        prop_paint(mb, r, g_mat.sheet, 0.2f);
        for (int s = -1; s <= 1; s += 2)
            prop_lcyl(mb, L, v3((f32)s * (w * 0.5f - 0.35f), 0, cz), v3((f32)s * (w * 0.5f - 0.35f), h - 0.06f, cz), 0.28f, 10);
    }
    if (rng_f32(r) < 0.45f) {
        prop_paint(mb, r, g_mat.cloth, 0.2f);
        prop_lbox(mb, L, v3(0, (h - 0.06f) * 0.5f + 0.06f, 0.015f), v3(w * 0.96f, h - 0.18f, 0.02f), rng_range(r, -0.04f, 0.04f), 0, 0);
    }
    // Навес: задние стойки за продавцом, передние не всегда
    f32 hb = rng_range(r, 2.3f, 2.7f), hf = hb - rng_range(r, 0.25f, 0.5f);
    f32 aw = w + rng_range(r, 0.2f, 0.6f), zb = -d - 0.95f, zf = 0.35f;
    prop_paint(mb, r, g_mat.rust, 0.2f);
    for (int s = -1; s <= 1; s += 2) prop_lcyl(mb, L, v3((f32)s * aw * 0.48f, 0, zb), v3((f32)s * aw * 0.48f, hb, zb), 0.04f, 5);
    if (rng_f32(r) < 0.55f)
        for (int s = -1; s <= 1; s += 2) prop_lcyl(mb, L, v3((f32)s * aw * 0.48f, 0, zf - 0.05f), v3((f32)s * aw * 0.48f, hf, zf - 0.05f), 0.035f, 5);
    else
        for (int s = -1; s <= 1; s += 2)
            prop_lcyl(mb, L, v3((f32)s * aw * 0.48f, hb * 0.6f, zb), v3((f32)s * aw * 0.48f, hf, zf - 0.05f), 0.03f, 4);
    prop_lcyl(mb, L, v3(-aw * 0.5f, hf, zf - 0.05f), v3(aw * 0.5f, hf, zf - 0.05f), 0.03f, 5);
    f32 len = sqrtf((zf - zb) * (zf - zb) + (hb - hf) * (hb - hf)), ang = atan2f(hb - hf, zf - zb);
    bool cloth = rng_f32(r) < 0.7f;
    if (cloth && rng_f32(r) < 0.4f) {
        // Полосами двух цветов
        V3 c0 = mat_tint(r, g_mat.cloth, 0.15f), c1 = mat_tint(r, g_mat.cloth, 0.15f);
        u32 ns = 3 + rng_below(r, 3);
        for (u32 i = 0; i < ns; i++) {
            mb_set(mb, i & 1 ? c1 : c0, MAT_CLOTH, BONE_NONE);
            f32 sx = -aw * 0.5f + ((f32)i + 0.5f) * aw / (f32)ns;
            prop_lbox(mb, L, v3(sx, (hb + hf) * 0.5f + 0.04f, (zb + zf) * 0.5f), v3(aw / (f32)ns, 0.03f, len), ang, 0, 0);
        }
    } else {
        mb_set(mb, cloth ? mat_tint(r, g_mat.cloth, 0.15f) : mat_tint(r, g_mat.sheet, 0.15f), cloth ? MAT_CLOTH : MAT_RUST, BONE_NONE);
        prop_lbox(mb, L, v3(0, (hb + hf) * 0.5f + 0.04f, (zb + zf) * 0.5f), v3(aw, 0.03f, len), ang, 0, rng_range(r, -0.03f, 0.03f));
    }
    if (cloth && rng_f32(r) < 0.45f) {
        // Оборванный край свисает
        prop_paint(mb, r, g_mat.cloth, 0.2f);
        prop_lbox(mb, L, v3(rng_range(r, -aw * 0.3f, aw * 0.3f), hf - 0.2f, zf + 0.02f), v3(aw * rng_range(r, 0.2f, 0.4f), 0.4f, 0.02f), 0, 0, 0);
    }
    goods(mb, r, L, kind, w, d, h);
    // Висящий товар с переднего края навеса
    if (kind == STALL_FOOD || kind == STALL_CLOTH || kind == STALL_TOOLS) {
        u32 nh = 2 + rng_below(r, 4);
        for (u32 i = 0; i < nh; i++) {
            f32 x = rng_range(r, -aw * 0.42f, aw * 0.42f), hh = rng_range(r, 0.3f, 0.7f);
            if (kind == STALL_FOOD) mb_set(mb, prop_tint(r, v3(0.26f, 0.13f, 0.08f), 0.2f), MAT_LEATHER, BONE_NONE);
            else if (kind == STALL_CLOTH) prop_paint(mb, r, g_mat.cloth, 0.2f);
            else prop_paint(mb, r, g_mat.metal, 0.3f);
            f32 ww = kind == STALL_CLOTH ? rng_range(r, 0.3f, 0.5f) : 0.08f;
            prop_lbox(mb, L, v3(x, hf - hh * 0.5f - 0.03f, zf - 0.05f), v3(ww, hh, 0.03f), 0, rng_range(r, -0.3f, 0.3f), 0);
        }
    }
    // Вывеска знаками над навесом или лампа под ним
    if (rng_f32(r) < 0.45f) {
        f32 sw = rng_range(r, 1.2f, 1.8f);
        prop_paint(mb, r, g_mat.rust, 0.2f);
        prop_lcyl(mb, L, v3(-sw * 0.3f, hb, zb + 0.1f), v3(-sw * 0.3f, hb + 0.35f, zb + 0.1f), 0.025f, 4);
        prop_lcyl(mb, L, v3(sw * 0.3f, hb, zb + 0.1f), v3(sw * 0.3f, hb + 0.35f, zb + 0.1f), 0.025f, 4);
        prop_paint(mb, r, g_mat.sign_paint, 0.25f);
        prop_lbox(mb, L, v3(rng_range(r, -0.15f, 0.15f), hb + 0.75f, zb + 0.1f), v3(sw, 0.85f, 0.04f), 0, 0, rng_range(r, -0.04f, 0.04f));
    }
    if (rng_f32(r) < 0.6f) {
        f32 x = rng_range(r, -aw * 0.3f, aw * 0.3f), z = rng_range(r, -d * 0.6f, -d * 0.2f);
        f32 yy = hf + (hb - hf) * (zf - z) / (zf - zb);
        mb_set(mb, v3(0.06f, 0.055f, 0.05f), MAT_METAL, BONE_NONE);
        prop_lcyl(mb, L, v3(x, yy, z), v3(x, yy - 0.3f, z), 0.01f, 4);
        prop_paint(mb, r, g_mat.lamp, 0.2f);
        prop_lbox(mb, L, v3(x, yy - 0.36f, z), v3(0.12f, 0.12f, 0.12f), 0, 0, 0);
    }
}

// ---------------------------------------------------------------- ярусы, стена, улицы

// Пандус: точка — середина наклонной плиты; size — ширина, подъём, длина по земле
static void sh_ramp(const ShapeCtx *c)
{
    f32 run = c->size.z, rise = c->size.y, len = sqrtf(run * run + rise * rise), a = atan2f(rise, run);
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    prop_lbox(c->mb, c->L, v3(0, -0.3f * cosf(a), 0.3f * sinf(a)), v3(c->size.x, 0.6f, len), -a, 0, 0);
}

// Контрфорс у фронта яруса: брус, наклонённый к стене; точка — середина
static void sh_buttress(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, c->mat[0], c->tint);
    mb_box(c->mb, m4_trs(c->pos, c->size, 0.22f, c->yaw, 0));
}

// Обломок плиты: лежит косо как упал
static void sh_rubble(const ShapeCtx *c)
{
    Rng *r = c->r;
    prop_paint(c->mb, r, c->mat[0], c->tint);
    mb_box(c->mb, m4_trs(c->pos, c->size, rng_range(r, -0.5f, 0.5f), c->yaw, rng_range(r, -0.5f, 0.5f)));
}

// Нанос песка у стены: наклонён к ней
static void sh_sand(const ShapeCtx *c)
{
    prop_mat(c->mb, c->mat[0]);
    mb_box(c->mb, m4_trs(c->pos, c->size, rng_range(c->r, 0.15f, 0.3f), c->yaw, 0));
}

// Плита мостовой: чуть косо и неровно
static void sh_paving(const ShapeCtx *c)
{
    Rng *r = c->r;
    prop_paint(c->mb, r, c->mat[0], c->tint);
    mb_box(c->mb, m4_trs(c->pos, c->size, rng_range(r, -0.015f, 0.015f), c->yaw, rng_range(r, -0.015f, 0.015f)));
}

// Полоса знаков: плоскость size.x × size.y лицом к +z
static void sh_glyphs(const ShapeCtx *c)
{
    prop_mat(c->mb, c->mat[0]);
    mb_quad(c->mb, c->pos, m4_point(m4_rot_y(c->yaw), v3(-c->size.x, 0, 0)), v3(0, c->size.y, 0));
}

// Лачуга на выступе стены: короб из листа с проёмом; точка — низ середины, проём на −z
static void sh_shack(const ShapeCtx *c)
{
    prop_paint(c->mb, c->r, g_mat.sheet, 0.15f);
    prop_lbox(c->mb, c->L, v3(0, c->size.y * 0.5f, 0), c->size, 0, 0, 0);
    prop_mat(c->mb, g_mat.hollow);
    prop_lbox(c->mb, c->L, v3(0, 0.9f, -c->size.z * 0.5f - 0.02f), v3(0.8f, 1.7f, 0.05f), 0, 0, 0);
}

// Мачта с красным огнём у пролома стены: растяжки, огонь на верху
static void sh_signal_mast(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    V3 m = c->pos;
    prop_paint(mb, c->r, g_mat.metal, 0.1f);
    mb_cylinder(mb, m, v3(m.x + 0.15f, m.y + 16.0f, m.z), 0.12f, 6);
    for (u32 i = 0; i < 4; i++) {
        f32 a = (f32)i * (PI_F * 0.5f) + 0.5f, y = m.y + 6.0f + (f32)i * 2.5f;
        mb_cylinder(mb, v3(m.x + 0.1f, y, m.z), v3(m.x + cosf(a) * 2.2f, m.y, m.z + sinf(a) * 1.2f), 0.02f, 4);
    }
    mb_set(mb, v3(0.9f, 0.12f, 0.06f), MAT_EMISSIVE, BONE_NONE);
    prop_box(mb, v3(m.x + 0.15f, m.y + 16.1f, m.z), v3(0.25f, 0.25f, 0.25f), 0);
}

// Ворота: точка — середина прохода у земли, проход вдоль локальной z шириной size.z. Столбы, балка, знак, лоскуты, лампы
static void sh_gate(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    Rng *r = c->r;
    M4 L = c->L;
    f32 h = c->size.z * 0.5f;
    prop_paint(mb, r, g_mat.concrete, 0.08f);
    prop_lbox(mb, L, v3(0, 2.6f, -h - 0.5f), v3(1.0f, 5.2f, 1.0f), 0, 0.04f, 0);
    prop_lbox(mb, L, v3(0, 2.4f, h + 0.5f), v3(1.0f, 4.8f, 1.0f), 0, -0.03f, 0);
    prop_paint(mb, r, g_mat.rust, 0.1f);
    prop_lbox(mb, L, v3(0, 4.95f, 0), v3(0.4f, 0.5f, c->size.z + 2.4f), 0.02f, 0, 0);
    prop_mat(mb, g_mat.glyph_green);
    mb_quad(mb, m4_point(L, v3(-0.25f, 4.0f, 0)), m4_point(m4_rot_y(c->yaw), v3(0, 0, 1.5f)), v3(0, 0.45f, 0));
    for (u32 i = 0; i < 6; i++) {
        prop_paint(mb, r, g_mat.cloth, 0.2f);
        f32 z = lerpf(-h + 0.3f, h - 0.3f, (f32)i / 5.0f) + rng_range(r, -0.2f, 0.2f), hh = rng_range(r, 0.4f, 1.1f);
        prop_lbox(mb, L, v3(0, 4.7f - hh * 0.5f, z), v3(0.02f, hh, rng_range(r, 0.15f, 0.3f)), 0, rng_range(r, -0.2f, 0.2f), 0);
    }
    prop_mat(mb, g_mat.lamp);
    prop_lbox(mb, L, v3(-0.55f, 3.6f, -h - 0.5f), v3(0.12f, 0.2f, 0.2f), 0, 0, 0);
    prop_lbox(mb, L, v3(-0.55f, 3.6f, h + 0.5f), v3(0.12f, 0.2f, 0.2f), 0, 0, 0);
}

const Shape g_town_shapes[] = {
    { "door", sh_door, 0 },
    { "awning", sh_awning, 0 },
    { "window", sh_window, 0 },
    { "patch", sh_patch, 0 },
    { "balcony", sh_balcony, 0 },
    { "downpipe", sh_downpipe, 0 },
    { "roof_tank", sh_roof_tank, 0 },
    { "antenna", sh_antenna, 0 },
    { "tarp", sh_tarp, 0 },
    { "parapet", sh_parapet, 0 },
    { "stall_scrap", sh_stall, STALL_SCRAP },
    { "stall_water", sh_stall, STALL_WATER },
    { "stall_food", sh_stall, STALL_FOOD },
    { "stall_cloth", sh_stall, STALL_CLOTH },
    { "stall_relic", sh_stall, STALL_RELIC },
    { "stall_tools", sh_stall, STALL_TOOLS },
    { "ramp", sh_ramp, 0 },
    { "buttress", sh_buttress, 0 },
    { "rubble", sh_rubble, 0 },
    { "sand", sh_sand, 0 },
    { "paving", sh_paving, 0 },
    { "glyphs", sh_glyphs, 0 },
    { "shack", sh_shack, 0 },
    { "signal_mast", sh_signal_mast, 0 },
    { "gate", sh_gate, 0 },
};
const u32 g_town_shape_count = ARRAY_COUNT(g_town_shapes);

// ---------------------------------------------------------------- места жителей

// Где стоят жители: за прилавками, на балконах, у ворот, у кучи лома, у перил. Перенесены из прежней раскладки
// вместе с ней (data/defs/prefabs.txt, skudel); точки действия объектов (act) их пока не заменяют
static const TownSpot g_spots[] = {
    { { 56.20f, 0.05f, -11.20f }, -1.571f, SPOT_GUARD },
    { { 56.20f, 0.05f, -4.80f }, -1.571f, SPOT_GUARD },
    { { 65.36f, 2.96f, -2.34f }, 3.142f, SPOT_IDLE },
    { { 71.30f, 3.05f, -13.84f }, 0.000f, SPOT_IDLE },
    { { 85.69f, 2.97f, -2.72f }, 3.142f, SPOT_IDLE },
    { { 96.33f, 3.07f, -2.35f }, 3.142f, SPOT_IDLE },
    { { 97.66f, 6.29f, -1.98f }, 3.142f, SPOT_IDLE },
    { { 64.35f, 13.41f, 19.93f }, 3.142f, SPOT_IDLE },
    { { 60.53f, 0.05f, -12.19f }, 0.027f, SPOT_TRADER },
    { { 59.82f, 0.05f, -3.83f }, 3.108f, SPOT_TRADER },
    { { 63.94f, 0.05f, -12.23f }, 0.005f, SPOT_TRADER },
    { { 65.96f, 0.05f, -3.72f }, 3.120f, SPOT_TRADER },
    { { 71.49f, 0.05f, -12.15f }, 0.031f, SPOT_TRADER },
    { { 72.42f, 0.05f, -3.82f }, 3.176f, SPOT_TRADER },
    { { 84.13f, 0.05f, -12.30f }, -0.002f, SPOT_TRADER },
    { { 84.49f, 0.05f, -3.90f }, 3.179f, SPOT_TRADER },
    { { 90.28f, 0.05f, -12.12f }, -0.043f, SPOT_TRADER },
    { { 89.44f, 0.05f, -3.73f }, 3.153f, SPOT_TRADER },
    { { 96.34f, 0.05f, -12.12f }, 0.029f, SPOT_TRADER },
    { { 95.40f, 0.05f, -3.83f }, 3.146f, SPOT_TRADER },
    { { 74.28f, 0.05f, -0.95f }, 1.571f, SPOT_TRADER },
    { { 74.27f, 0.05f, -16.14f }, 1.571f, SPOT_TRADER },
    { { 81.60f, 0.05f, -18.43f }, -1.571f, SPOT_TRADER },
    { { 62.74f, 3.50f, 9.36f }, 0.000f, SPOT_TRADER },
    { { 68.78f, 3.50f, 9.48f }, 0.000f, SPOT_TRADER },
    { { 88.23f, 3.50f, 9.48f }, 0.000f, SPOT_TRADER },
    { { 94.54f, 3.50f, 9.46f }, 0.000f, SPOT_TRADER },
    { { 82.37f, 7.00f, 20.82f }, 3.142f, SPOT_TRADER },
    { { 67.20f, 0.05f, -12.00f }, 2.400f, SPOT_WORKER },
    { { 68.80f, 0.05f, -13.20f }, -0.600f, SPOT_WORKER },
    { { 92.00f, 0.05f, -11.80f }, 2.800f, SPOT_WORKER },
    { { 66.00f, 7.00f, 16.55f }, 3.142f, SPOT_IDLE },
    { { 74.50f, 7.00f, 16.60f }, 3.442f, SPOT_IDLE },
    { { 96.50f, 7.00f, 16.55f }, 2.942f, SPOT_IDLE },
    { { 74.30f, 3.50f, 4.60f }, 3.142f, SPOT_IDLE },
};

void town_spots(Town *t)
{
    *t = (Town){ 0 };
    for (u32 i = 0; i < ARRAY_COUNT(g_spots) && i < TOWN_MAX_SPOTS; i++) t->spot[t->spot_count++] = g_spots[i];
}

// ---------------------------------------------------------------- высота земли

f32 town_ground(f32 x, f32 z, f32 base)
{
    if (x < TX0 || x > TX1 || z < TZ0 || z > TZ3) return base;
    if (z < TZ1) return TH0;
    if (x >= RX0 && x <= RX1 && z < RZ1) return lerpf(TH0, TH1, (z - TZ1) / (RZ1 - TZ1));
    if (x >= SX0 && x <= SX1 && z >= SZ0 && z < TZ2) return lerpf(TH1, TH2, (x - SX0) / (SX1 - SX0));
    if (x > SX1 && x <= SX2 && z >= SZ0 && z < TZ2) return TH2;
    if (z < TZ2) return TH1;
    return TH2;
}
