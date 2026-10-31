#include "town_build.h"
#include "../defs/defs.h"

#include "../sim/look.h"
#include "../sim/names.h"
#include "props.h"
#include "../sim/squad.h"

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

// Цвета — материалы data/defs/materials.txt; оттенки одной вещи остаются у неё

typedef enum StallKind { STALL_SCRAP, STALL_WATER, STALL_FOOD, STALL_CLOTH, STALL_RELIC, STALL_TOOLS, STALL_KINDS } StallKind;


static void add_spot(Town *t, V3 p, f32 heading, u8 kind)
{
    if (t->spot_count < TOWN_MAX_SPOTS) t->spot[t->spot_count++] = (TownSpot){ p, heading, kind };
}

// ---------------------------------------------------------------- дома

// Дом: base — середина основания, фасад смотрит по yaw. Этажи из бетона и ржавых листов,
// каждый чуть смещён; окна, двери, балконы, заплаты, лестницы, хлам на крыше. Возвращает высоту крыши
static f32 house(MeshBuilder *mb, Rng *r, Town *t, V3 base, f32 yaw, f32 w, f32 d, u32 floors)
{
    M4 L = prop_frame(base, yaw);
    f32 y = 0.0f, ox = 0.0f, oz = 0.0f, pw = w, pd = d;
    t->houses++;
    for (u32 f = 0; f < floors; f++) {
        f32 h = rng_range(r, 2.7f, 3.2f);
        f32 fw = f == 0 ? w : clampf(pw * rng_range(r, 0.8f, 1.06f), 2.4f, w * 1.08f);
        f32 fd = f == 0 ? d : clampf(pd * rng_range(r, 0.8f, 1.04f), 2.4f, d * 1.05f);
        if (f > 0) {
            f32 lim = (w - fw) * 0.5f + 0.3f;
            ox = clampf(ox + rng_range(r, -0.5f, 0.5f), -lim, lim);
            oz = clampf(oz + rng_range(r, -0.3f, 0.45f), -0.4f, 0.7f);  // верх нависает над улицей
        }
        bool concrete = f == 0 || rng_f32(r) < 0.35f;
        V3 wall = concrete ? prop_tint(r, rng_f32(r) < 0.5f ? mat_c(g_mat.concrete) : mat_c(g_mat.concrete_dark), 0.08f) : mat_tint(r, g_mat.sheet, 0.15f);
        mb_set(mb, wall, concrete ? MAT_CONCRETE : MAT_RUST, BONE_NONE);
        prop_lbox(mb, L, v3(ox, y + h * 0.5f, oz), v3(fw, h, fd), 0, 0, 0);
        // Плита перекрытия или крыши
        prop_paint(mb, r, g_mat.concrete_dark, 0.1f);
        prop_lbox(mb, L, v3(ox, y + h + 0.07f, oz), v3(fw + 0.25f, 0.14f, fd + 0.25f), 0, 0, 0);
        f32 front = oz + fd * 0.5f;
        if (f == 0) {
            f32 dx = ox + rng_range(r, -fw * 0.25f, fw * 0.25f);
            prop_mat(mb, g_mat.hollow);
            prop_lbox(mb, L, v3(dx, 1.0f, front), v3(0.95f, 2.0f, 0.06f), 0, 0, 0);
            prop_paint(mb, r, g_mat.rust, 0.2f);
            prop_lbox(mb, L, v3(dx, 2.08f, front + 0.04f), v3(1.2f, 0.12f, 0.1f), 0, 0, 0);
            if (rng_f32(r) < 0.5f) {
                // Навес над дверью
                mb_set(mb, rng_f32(r) < 0.5f ? mat_tint(r, g_mat.sheet, 0.2f) : mat_tint(r, g_mat.cloth, 0.2f), MAT_RUST, BONE_NONE);
                prop_lbox(mb, L, v3(dx, 2.45f, front + 0.5f), v3(1.6f, 0.03f, 1.1f), 0.3f, 0, rng_range(r, -0.04f, 0.04f));
            }
            // Окно рядом с дверью
            if (fw > 3.6f && rng_f32(r) < 0.6f) {
                f32 wx = dx > ox ? dx - 1.5f : dx + 1.5f;
                mb_set(mb, rng_f32(r) < 0.2f ? mat_c(g_mat.window) : mat_c(g_mat.hollow), MAT_EMISSIVE, BONE_NONE);
                prop_lbox(mb, L, v3(wx, 1.55f, front), v3(0.7f, 0.55f, 0.06f), 0, 0, 0);
            }
        } else {
            u32 nw = 1 + rng_below(r, fw > 3.8f ? 3 : 2);
            for (u32 i = 0; i < nw; i++) {
                f32 wx = ox + ((f32)i + 0.5f) / (f32)nw * fw - fw * 0.5f + rng_range(r, -0.2f, 0.2f);
                f32 ww = rng_range(r, 0.5f, 0.85f), wh = rng_range(r, 0.45f, 0.75f);
                bool lit = rng_f32(r) < 0.22f;
                mb_set(mb, lit ? mat_tint(r, g_mat.window, 0.3f) : mat_c(g_mat.hollow), MAT_EMISSIVE, BONE_NONE);
                prop_lbox(mb, L, v3(wx, y + 1.5f, front), v3(ww, wh, 0.06f), 0, 0, 0);
                if (!lit && rng_f32(r) < 0.35f) {
                    prop_paint(mb, r, g_mat.sheet, 0.2f);
                    prop_lbox(mb, L, v3(wx + ww * 0.6f, y + 1.5f, front + 0.12f), v3(ww * 0.5f, wh * 1.1f, 0.03f), 0, 0.6f, 0);
                }
            }
            // Окно сбоку
            if (rng_f32(r) < 0.5f) {
                f32 sx = ox + (rng_f32(r) < 0.5f ? -1.0f : 1.0f) * fw * 0.5f;
                prop_mat(mb, g_mat.hollow);
                prop_lbox(mb, L, v3(sx, y + 1.5f, oz + rng_range(r, -fd * 0.2f, fd * 0.2f)), v3(0.06f, 0.55f, 0.7f), 0, 0, 0);
            }
            // Балкон
            if (rng_f32(r) < 0.55f) {
                f32 bd = rng_range(r, 0.9f, 1.3f), bw = fw * rng_range(r, 0.45f, 0.85f);
                f32 bx = ox + rng_range(r, -(fw - bw) * 0.5f, (fw - bw) * 0.5f);
                prop_paint(mb, r, g_mat.concrete_dark, 0.1f);
                prop_lbox(mb, L, v3(bx, y - 0.02f, front + bd * 0.5f), v3(bw, 0.14f, bd), 0, 0, 0);
                prop_paint(mb, r, g_mat.rust, 0.2f);
                f32 fz = front + bd - 0.04f, ry = y + 0.05f;
                for (int s = -1; s <= 1; s++)
                    prop_lcyl(mb, L, v3(bx + (f32)s * bw * 0.48f, ry, fz), v3(bx + (f32)s * bw * 0.48f, ry + 1.0f, fz), 0.03f, 4);
                prop_lcyl(mb, L, v3(bx - bw * 0.5f, ry + 1.0f, fz), v3(bx + bw * 0.5f, ry + 1.0f, fz), 0.03f, 4);
                prop_lcyl(mb, L, v3(bx - bw * 0.48f, ry + 1.0f, front), v3(bx - bw * 0.48f, ry + 1.0f, fz), 0.025f, 4);
                prop_lcyl(mb, L, v3(bx + bw * 0.48f, ry + 1.0f, front), v3(bx + bw * 0.48f, ry + 1.0f, fz), 0.025f, 4);
                if (rng_f32(r) < 0.55f) {
                    prop_paint(mb, r, g_mat.cloth, 0.2f);
                    prop_lbox(mb, L, v3(bx + rng_range(r, -bw * 0.25f, bw * 0.25f), ry + 0.65f, fz + 0.04f),
                         v3(rng_range(r, 0.5f, 0.9f), rng_range(r, 0.5f, 0.8f), 0.02f), 0, 0, rng_range(r, -0.05f, 0.05f));
                }
                if (rng_f32(r) < 0.5f) prop_crate(mb, r, m4_point(L, v3(bx - bw * 0.3f, ry, front + 0.35f)), yaw, 0.4f);
                if (rng_f32(r) < 0.6f)
                    add_spot(t, m4_point(L, v3(bx + rng_range(r, -bw * 0.2f, bw * 0.2f), y + 0.05f, front + bd * 0.45f)), yaw, SPOT_IDLE);
            }
        }
        // Заплаты из листа
        u32 np = rng_below(r, 3);
        for (u32 i = 0; i < np; i++) {
            mb_set(mb, rng_f32(r) < 0.7f ? mat_tint(r, g_mat.sheet, 0.25f) : mat_tint(r, g_mat.metal, 0.3f), MAT_RUST, BONE_NONE);
            f32 pw2 = rng_range(r, 0.6f, 1.6f), ph = rng_range(r, 0.5f, 1.4f);
            f32 py = y + rng_range(r, ph * 0.5f + 0.1f, h - ph * 0.5f - 0.1f);
            if (rng_f32(r) < 0.6f)
                prop_lbox(mb, L, v3(ox + rng_range(r, -fw * 0.35f, fw * 0.35f), py, front + 0.03f), v3(pw2, ph, 0.03f), 0, 0,
                     rng_range(r, -0.08f, 0.08f));
            else {
                f32 sx = ox + (rng_f32(r) < 0.5f ? -1.0f : 1.0f) * (fw * 0.5f + 0.03f);
                prop_lbox(mb, L, v3(sx, py, oz + rng_range(r, -fd * 0.3f, fd * 0.3f)), v3(0.03f, ph, pw2), rng_range(r, -0.08f, 0.08f), 0, 0);
            }
        }
        y += h + 0.14f;
        pw = fw;
        pd = fd;
    }
    // Лестница сбоку на второй этаж и выше
    if (floors > 1 && rng_f32(r) < 0.45f) {
        f32 sx = (rng_f32(r) < 0.5f ? -1.0f : 1.0f) * (w * 0.5f + 0.06f);
        prop_ladder(mb, L, v3(sx, 0, rng_range(r, -d * 0.2f, d * 0.3f)), y - 0.1f);
    }
    // Водосток
    if (rng_f32(r) < 0.5f) {
        prop_paint(mb, r, g_mat.rust, 0.2f);
        f32 sx = (rng_f32(r) < 0.5f ? -1.0f : 1.0f) * (w * 0.5f + 0.08f), sz = d * 0.5f - 0.15f;
        prop_lcyl(mb, L, v3(sx, y, sz), v3(sx, 0.3f, sz), 0.06f, 6);
        prop_lcyl(mb, L, v3(sx, 0.3f, sz), v3(sx, 0.05f, sz + 0.35f), 0.06f, 6);
    }
    // Крыша: бак, антенна, брезент, ящики, бортик
    u32 nr = 1 + rng_below(r, 3);
    for (u32 i = 0; i < nr; i++) {
        V3 q = v3(ox + rng_range(r, -pw * 0.3f, pw * 0.3f), y, oz + rng_range(r, -pd * 0.3f, pd * 0.3f));
        u32 k = rng_below(r, 5);
        if (k == 0) {
            prop_paint(mb, r, g_mat.sheet, 0.15f);
            f32 rr = rng_range(r, 0.45f, 0.75f), hh = rng_range(r, 0.9f, 1.5f);
            prop_lcyl(mb, L, v3(q.x, q.y + 0.25f, q.z), v3(q.x, q.y + 0.25f + hh, q.z), rr, 12);
            prop_paint(mb, r, g_mat.rust, 0.2f);
            prop_lbox(mb, L, v3(q.x, q.y + 0.12f, q.z), v3(rr * 1.8f, 0.25f, rr * 1.8f), 0, 0, 0);
        } else if (k == 1) {
            prop_paint(mb, r, g_mat.metal, 0.2f);
            f32 hh = rng_range(r, 2.0f, 4.0f);
            V3 top = v3(q.x + rng_range(r, -0.15f, 0.15f), q.y + hh, q.z + rng_range(r, -0.15f, 0.15f));
            prop_lcyl(mb, L, q, top, 0.03f, 4);
            for (u32 j = 0; j < 2; j++) {
                f32 yy = q.y + hh * (0.6f + 0.25f * (f32)j), half = rng_range(r, 0.3f, 0.6f);
                prop_lcyl(mb, L, v3(q.x - half, yy, q.z), v3(q.x + half, yy, q.z), 0.015f, 4);
            }
        } else if (k == 2) {
            prop_paint(mb, r, g_mat.cloth, 0.2f);
            prop_lbox(mb, L, v3(q.x, q.y + 0.35f, q.z), v3(rng_range(r, 1.2f, 2.0f), 0.5f, rng_range(r, 1.0f, 1.6f)),
                 rng_range(r, -0.1f, 0.1f), rng_range(r, 0, PI_F), rng_range(r, -0.1f, 0.1f));
        } else if (k == 3) {
            prop_crate_stack(mb, r, m4_point(L, q), yaw + rng_range(r, 0, PI_F));
        } else {
            prop_paint(mb, r, g_mat.concrete_dark, 0.1f);
            prop_lbox(mb, L, v3(ox, y + 0.3f, oz + pd * 0.5f), v3(pw, 0.6f, 0.15f), 0, 0, 0);
            prop_lbox(mb, L, v3(ox - pw * 0.5f, y + 0.3f, oz), v3(0.15f, 0.6f, pd), 0, 0, 0);
        }
    }
    // Хлам у фасада по краям
    u32 nc = rng_below(r, 3);
    for (u32 i = 0; i < nc; i++) {
        f32 sx = (i & 1 ? 1.0f : -1.0f) * rng_range(r, w * 0.3f, w * 0.45f);
        prop_clutter(mb, r, m4_point(L, v3(sx, 0, d * 0.5f + 0.35f)), yaw + rng_range(r, -0.3f, 0.3f));
    }
    return y;
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

// Прилавок: p — середина переднего края стола у земли, yaw — к покупателям. Продавец за столом
static void stall(MeshBuilder *mb, Rng *r, Town *t, V3 p, f32 yaw, u32 kind)
{
    M4 L = prop_frame(p, yaw);
    f32 w = rng_range(r, 1.7f, 2.6f), d = rng_range(r, 0.75f, 1.0f), h = rng_range(r, 0.82f, 0.95f);
    f32 cz = -d * 0.5f;
    t->stalls++;
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
    for (int s = -1; s <= 1; s += 2) {
        prop_lcyl(mb, L, v3((f32)s * aw * 0.48f, 0, zb), v3((f32)s * aw * 0.48f, hb, zb), 0.04f, 5);
    }
    bool front_posts = rng_f32(r) < 0.55f;
    if (front_posts)
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
    // Рядом ящики, мешки, бочки; у водоноса всегда бочка
    u32 nc = 1 + rng_below(r, 3);
    for (u32 i = 0; i < nc; i++) {
        f32 s = i & 1 ? 1.0f : -1.0f;
        V3 q = m4_point(L, v3(s * (w * 0.5f + rng_range(r, 0.35f, 0.7f)), 0, rng_range(r, -d - 0.5f, -0.2f)));
        if (kind == STALL_WATER && i == 0) prop_barrel(mb, r, q, false);
        else prop_clutter(mb, r, q, yaw + rng_range(r, -0.4f, 0.4f));
    }
    add_spot(t, m4_point(L, v3(rng_range(r, -w * 0.25f, w * 0.25f), 0, -d - 0.45f)), yaw, SPOT_TRADER);
}

// ---------------------------------------------------------------- ярусы, стена, улицы

static void slab(MeshBuilder *mb, f32 x0, f32 x1, f32 z0, f32 z1, f32 y0, f32 y1)
{
    prop_box(mb, v3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f), v3(x1 - x0, y1 - y0, z1 - z0), 0);
}

static void terraces(MeshBuilder *mb, Rng *r)
{
    prop_paint(mb, r, g_mat.concrete, 0.05f);
    // Средний ярус с вырезом под пандус
    slab(mb, TX0, RX0, TZ1, TZ2, -1.0f, TH1);
    slab(mb, RX1, TX1, TZ1, TZ2, -1.0f, TH1);
    slab(mb, RX0, RX1, RZ1, TZ2, -1.0f, TH1);
    // Верхний ярус
    prop_paint(mb, r, g_mat.concrete_dark, 0.05f);
    slab(mb, TX0, TX1, TZ2, TZ3, -1.0f, TH2);
    // Пандус: наклонная плита с бортиками
    f32 run = RZ1 - TZ1, rise = TH1 - TH0, len = sqrtf(run * run + rise * rise), a = atan2f(rise, run);
    V3 n = v3(0, cosf(a), -sinf(a));
    V3 top = v3((RX0 + RX1) * 0.5f, (TH0 + TH1) * 0.5f, (TZ1 + RZ1) * 0.5f);
    prop_paint(mb, r, g_mat.concrete, 0.08f);
    mb_box(mb, m4_trs(v3_sub(top, v3_mul(n, 0.3f)), v3(RX1 - RX0, 0.6f, len), -a, 0, 0));
    // Лестница на верхний ярус: ступени по 0.29 м, внизу сплошная кладка
    u32 steps = 12;
    for (u32 k = 0; k < steps; k++) {
        f32 x0 = SX0 + (SX1 - SX0) * (f32)k / (f32)steps;
        slab(mb, x0, SX1, SZ0, TZ2, TH1 - 0.1f, TH1 + (TH2 - TH1) * (f32)(k + 1) / (f32)steps);
    }
    slab(mb, SX1, SX2, SZ0, TZ2, TH1 - 0.1f, TH2);
    prop_railing(mb, r, v3(SX0 + 1.0f, TH1 + 0.35f, SZ0 + 0.05f), v3(SX1, TH2, SZ0 + 0.05f));
    prop_railing(mb, r, v3(SX1, TH2, SZ0 + 0.05f), v3(SX2, TH2, SZ0 + 0.05f));
    // Перила по краю верхнего яруса и у пандуса
    prop_railing(mb, r, v3(TX0 + 0.3f, TH2, TZ2 + 0.1f), v3(SX1, TH2, TZ2 + 0.1f));
    prop_railing(mb, r, v3(SX2 + 0.1f, TH2, TZ2 + 0.1f), v3(TX1 - 0.3f, TH2, TZ2 + 0.1f));
    prop_railing(mb, r, v3(RX0 - 0.1f, TH1, RZ1 - 0.2f), v3(RX0 - 0.1f, TH1, TZ1 + 0.2f));
    prop_railing(mb, r, v3(RX1 + 0.1f, TH1, TZ1 + 0.2f), v3(RX1 + 0.1f, TH1, RZ1 - 0.2f));
    prop_railing(mb, r, v3(RX0 - 2.0f, TH1, TZ1 + 0.1f), v3(RX0 - 0.1f, TH1, TZ1 + 0.1f));
    prop_railing(mb, r, v3(RX1 + 0.1f, TH1, TZ1 + 0.1f), v3(RX1 + 2.0f, TH1, TZ1 + 0.1f));
    // Контрфорсы, трубы и водостоки на фронте верхнего яруса
    for (f32 x = TX0 + 2.0f; x < TX1 - 1.0f; x += rng_range(r, 4.5f, 7.0f)) {
        if (x > SX0 - 1.0f && x < SX2 + 0.5f) continue;
        prop_paint(mb, r, g_mat.concrete_dark, 0.08f);
        mb_box(mb, m4_trs(v3(x, TH1 + 1.5f, TZ2 - 0.5f), v3(0.9f, 3.2f, 1.4f), 0.22f, 0, 0));
        if (rng_f32(r) < 0.5f) {
            prop_mat(mb, g_mat.hollow);
            prop_box(mb, v3(x + 1.6f, TH1 + 2.4f, TZ2), v3(0.5f, 0.5f, 0.08f), 0);
        }
    }
    prop_paint(mb, r, g_mat.rust, 0.15f);
    mb_cylinder(mb, v3(TX0 + 1.0f, TH1 + 2.6f, TZ2 + 0.35f), v3(SX0 - 1.0f, TH1 + 2.8f, TZ2 + 0.35f), 0.22f, 10);
    // Обломки у западных торцов ярусов: сюда обрушилась часть плит
    for (u32 i = 0; i < 26; i++) {
        f32 z = rng_range(r, TZ1 - 1.0f, TZ3), x = TX0 - rng_range(r, 0.3f, 4.5f);
        f32 top_h = z < TZ2 ? TH1 : TH2;
        f32 s = rng_range(r, 0.6f, 2.2f), yy = rng_range(r, 0.0f, top_h * (1.0f - (TX0 - x) / 5.0f));
        mb_set(mb, prop_tint(r, rng_f32(r) < 0.5f ? mat_c(g_mat.concrete) : mat_c(g_mat.concrete_dark), 0.1f), MAT_CONCRETE, BONE_NONE);
        mb_box(mb, m4_trs(v3(x, yy, z), v3(s * rng_range(r, 0.8f, 1.8f), s, s * rng_range(r, 0.7f, 1.4f)), rng_range(r, -0.5f, 0.5f),
                      rng_range(r, 0, PI_F), rng_range(r, -0.5f, 0.5f)));
    }
    // Песок намело к стенам и в углы
    prop_mat(mb, g_mat.sand);
    for (u32 i = 0; i < 14; i++) {
        f32 x = rng_range(r, TX0 + 1.0f, TX1 - 1.0f);
        u32 k = rng_below(r, 3);
        f32 z = k == 0 ? TZ1 + 0.4f : k == 1 ? TZ2 + 0.4f : TZ3 - 0.6f;
        f32 yy = k == 0 ? TH1 : TH2;
        if (k == 0 && x > RX0 - 1.0f && x < RX1 + 1.0f) continue;
        if (k == 1 && x > SX0 - 1.0f && x < SX2 + 1.0f) continue;
        mb_box(mb, m4_trs(v3(x, yy - 0.2f, z), v3(rng_range(r, 2.0f, 5.0f), 0.6f, rng_range(r, 1.2f, 2.0f)), rng_range(r, 0.15f, 0.3f) * (k == 2 ? 1.0f : -1.0f),
                      rng_range(r, -0.1f, 0.1f), 0));
    }
}

// Древняя стена за посёлком: выше всего вокруг, с непонятным проёмом и знаками
static void ancient_wall(MeshBuilder *mb, Rng *r)
{
    f32 z0 = TZ3, z1 = TZ3 + 9.0f;
    for (f32 x = 34.0f; x < 124.0f;) {
        f32 w = rng_range(r, 5.0f, 9.0f);
        // Восточнее посёлка стена проломлена
        f32 h = x > 102.0f && x < 114.0f ? rng_range(r, 12.0f, 18.0f) : rng_range(r, 30.0f, 38.0f);
        mb_set(mb, prop_tint(r, v3(0.34f, 0.33f, 0.31f), 0.06f), MAT_CONCRETE, BONE_NONE);
        slab(mb, x, x + w, z0, z1, -1.0f, h);
        // Пилястры
        if (rng_f32(r) < 0.7f) {
            prop_paint(mb, r, g_mat.concrete_dark, 0.06f);
            f32 px = x + w * 0.5f;
            slab(mb, px - 1.0f, px + 1.0f, z0 - 1.2f, z0, px > TX0 && px < TX1 ? TH2 - 0.2f : -1.0f, h - rng_range(r, 0.0f, 6.0f));
        }
        x += w;
    }
    // Проём неизвестного назначения и полоса знаков над ним
    f32 gx = 82.0f;
    prop_mat(mb, g_mat.hollow);
    prop_box(mb, v3(gx, TH2 + 5.5f, z0 - 0.02f), v3(6.0f, 11.0f, 0.1f), 0);
    prop_paint(mb, r, g_mat.concrete_dark, 0.05f);
    slab(mb, gx - 4.2f, gx + 4.2f, z0 - 1.6f, z0, TH2 + 11.0f, TH2 + 12.6f);
    slab(mb, gx - 4.2f, gx - 3.0f, z0 - 1.6f, z0, TH2, TH2 + 11.0f);
    slab(mb, gx + 3.0f, gx + 4.2f, z0 - 1.6f, z0, TH2, TH2 + 11.0f);
    prop_mat(mb, g_mat.glyph_green);
    mb_quad(mb, v3(gx, TH2 + 15.0f, z0 - 0.04f), v3(-6.0f, 0, 0), v3(0, 0.9f, 0));
    // Выступ на высоте с двумя лачугами и лестницей к нему: живут и там
    prop_paint(mb, r, g_mat.concrete_dark, 0.05f);
    slab(mb, 66.5f, 77.0f, z0 - 2.2f, z0, TH2 + 7.6f, TH2 + 8.0f);
    for (int i = 0; i < 2; i++) {
        prop_paint(mb, r, g_mat.sheet, 0.15f);
        f32 x = 68.8f + (f32)i * 4.6f + rng_range(r, -0.3f, 0.3f), hh = rng_range(r, 2.0f, 2.6f);
        mb_box(mb, m4_trs(v3(x, TH2 + 8.0f + hh * 0.5f, z0 - 1.0f), v3(rng_range(r, 2.4f, 3.2f), hh, 1.8f), 0, rng_range(r, -0.05f, 0.05f), 0));
        prop_mat(mb, g_mat.hollow);
        prop_box(mb, v3(x, TH2 + 8.9f, z0 - 1.92f), v3(0.8f, 1.7f, 0.05f), 0);
    }
    prop_ladder(mb, prop_frame(v3(77.4f, TH2, z0 - 0.3f), PI_F * 0.5f), v3(0, 0, 0), 8.0f);
    prop_railing(mb, r, v3(66.7f, TH2 + 8.0f, z0 - 2.1f), v3(76.8f, TH2 + 8.0f, z0 - 2.1f));
    // Ржавые трубы по стене и красный огонь на мачте у пролома: единственный ориентир издалека
    prop_paint(mb, r, g_mat.rust, 0.1f);
    mb_cylinder(mb, v3(48.0f, -0.2f, z0 - 0.5f), v3(48.0f, 33.0f, z0 - 0.5f), 0.5f, 12);
    mb_cylinder(mb, v3(95.0f, TH2, z0 - 0.4f), v3(95.0f, 24.0f, z0 - 0.4f), 0.3f, 10);
    mb_cylinder(mb, v3(95.0f, 24.0f, z0 - 0.4f), v3(101.0f, 24.0f, z0 - 0.4f), 0.3f, 10);
    prop_paint(mb, r, g_mat.metal, 0.1f);
    V3 mast = v3(75.8f, TH2, 23.2f);
    mb_cylinder(mb, mast, v3(mast.x + 0.15f, TH2 + 16.0f, mast.z), 0.12f, 6);
    for (u32 i = 0; i < 4; i++) {
        f32 a = (f32)i * (PI_F * 0.5f) + 0.5f, y = TH2 + 6.0f + (f32)i * 2.5f;
        mb_cylinder(mb, v3(mast.x + 0.1f, y, mast.z), v3(mast.x + cosf(a) * 2.2f, TH2, mast.z + sinf(a) * 1.2f), 0.02f, 4);
    }
    mb_set(mb, v3(0.9f, 0.12f, 0.06f), MAT_EMISSIVE, BONE_NONE);
    prop_box(mb, v3(mast.x + 0.15f, TH2 + 16.1f, mast.z), v3(0.25f, 0.25f, 0.25f), 0);
    // Груда обломков у пролома
    for (u32 i = 0; i < 18; i++) {
        f32 s = rng_range(r, 1.0f, 3.5f);
        prop_paint(mb, r, g_mat.concrete_dark, 0.1f);
        mb_box(mb, m4_trs(v3(rng_range(r, 101.0f, 116.0f), rng_range(r, 0.0f, 4.0f), rng_range(r, TZ3 - 9.0f, TZ3 - 1.0f)),
                      v3(s * 1.6f, s, s * 1.2f), rng_range(r, -0.6f, 0.6f), rng_range(r, 0, PI_F), rng_range(r, -0.6f, 0.6f)));
    }
}

// Плиты мостовой нижнего яруса, между ними песок
static void pavement(MeshBuilder *mb, Rng *r)
{
    for (f32 x = TX0 + 1.5f; x < TX1; x += 3.0f)
        for (f32 z = TZ0 + 1.5f; z < TZ1; z += 3.0f) {
            if (rng_f32(r) < 0.22f) continue;
            mb_set(mb, prop_tint(r, rng_f32(r) < 0.6f ? mat_c(g_mat.concrete) : mat_c(g_mat.concrete_dark), 0.08f), MAT_CONCRETE, BONE_NONE);
            f32 top = rng_range(r, 0.03f, 0.08f);
            mb_box(mb, m4_trs(v3(x + rng_range(r, -0.3f, 0.3f), top - 0.1f, z + rng_range(r, -0.3f, 0.3f)),
                          v3(rng_range(r, 2.2f, 2.9f), 0.2f, rng_range(r, 2.2f, 2.9f)), rng_range(r, -0.015f, 0.015f), rng_range(r, -0.06f, 0.06f),
                          rng_range(r, -0.015f, 0.015f)));
        }
}

// Ворота с запада: столбы, балка, знак, лампы, лоскуты
static void gate(MeshBuilder *mb, Rng *r, Town *t, f32 x, f32 z0, f32 z1)
{
    prop_paint(mb, r, g_mat.concrete, 0.08f);
    prop_box(mb, v3(x, 2.6f, z0 - 0.5f), v3(1.0f, 5.2f, 1.0f), 0.04f);
    prop_box(mb, v3(x, 2.4f, z1 + 0.5f), v3(1.0f, 4.8f, 1.0f), -0.03f);
    prop_paint(mb, r, g_mat.rust, 0.1f);
    mb_box(mb, m4_trs(v3(x, 4.95f, (z0 + z1) * 0.5f), v3(0.4f, 0.5f, z1 - z0 + 2.4f), 0.02f, 0, 0));
    prop_mat(mb, g_mat.glyph_green);
    mb_quad(mb, v3(x - 0.25f, 4.0f, (z0 + z1) * 0.5f), v3(0, 0, 1.5f), v3(0, 0.45f, 0));
    for (u32 i = 0; i < 6; i++) {
        prop_paint(mb, r, g_mat.cloth, 0.2f);
        f32 z = lerpf(z0 + 0.3f, z1 - 0.3f, (f32)i / 5.0f) + rng_range(r, -0.2f, 0.2f), h = rng_range(r, 0.4f, 1.1f);
        prop_box(mb, v3(x, 4.7f - h * 0.5f, z), v3(0.02f, h, rng_range(r, 0.15f, 0.3f)), rng_range(r, -0.2f, 0.2f));
    }
    prop_mat(mb, g_mat.lamp);
    prop_box(mb, v3(x - 0.55f, 3.6f, z0 - 0.5f), v3(0.12f, 0.2f, 0.2f), 0);
    prop_box(mb, v3(x - 0.55f, 3.6f, z1 + 0.5f), v3(0.12f, 0.2f, 0.2f), 0);
    add_spot(t, v3(x + 0.6f, TH0, z0 - 1.2f), -PI_F * 0.5f, SPOT_GUARD);
    add_spot(t, v3(x + 0.6f, TH0, z1 + 1.2f), -PI_F * 0.5f, SPOT_GUARD);
}

void town_build(MeshBuilder *mb, Rng *r, Town *t)
{
    *t = (Town){ 0 };
    terraces(mb, r);
    ancient_wall(mb, r);
    pavement(mb, r);

    // Ограда нижнего яруса, ворота на западе и проход на восток
    f32 mz0 = -10.0f, mz1 = -6.0f;  // главная улица
    gate(mb, r, t, TX0 - 0.4f, mz0, mz1);
    prop_fence(mb, r, v3(TX0 - 0.6f, 0, TZ0), v3(TX0 - 0.6f, 0, mz0 - 1.0f));
    prop_fence(mb, r, v3(TX0 - 0.6f, 0, mz1 + 1.0f), v3(TX0 - 0.6f, 0, TZ1));
    prop_fence(mb, r, v3(TX0, 0, TZ0 - 0.4f), v3(RX0, 0, TZ0 - 0.4f));
    prop_fence(mb, r, v3(RX1, 0, TZ0 - 0.4f), v3(TX1, 0, TZ0 - 0.4f));
    prop_fence(mb, r, v3(TX1 + 0.4f, 0, TZ0), v3(TX1 + 0.4f, 0, mz0 - 0.5f));
    prop_fence(mb, r, v3(TX1 + 0.4f, 0, mz1 + 0.5f), v3(TX1 + 0.4f, 0, TZ1));

    // Дома нижнего яруса: северный ряд прислонён к среднему ярусу, южный у ограды
    static const f32 north_x[6] = { 58.7f, 64.3f, 70.3f, 85.6f, 91.4f, 97.1f };
    static const f32 south_x[6] = { 58.9f, 64.6f, 70.3f, 85.6f, 91.3f, 97.0f };
    f32 north_roof[6], south_roof[6];
    for (u32 i = 0; i < 6; i++) {
        north_roof[i] = house(mb, r, t, v3(north_x[i], TH0, 0.6f), PI_F, rng_range(r, 4.6f, 5.4f), 6.2f, 1 + rng_below(r, 3));
        south_roof[i] = house(mb, r, t, v3(south_x[i], TH0, -16.7f), 0.0f, rng_range(r, 4.6f, 5.3f), 5.8f, 1 + rng_below(r, 3));
    }
    // Средний ярус: дома спиной к обрыву, фасадом на верхнюю улицу
    static const f32 mid_x[6] = { 59.5f, 65.2f, 71.0f, 85.2f, 91.0f, 96.7f };
    for (u32 i = 0; i < 6; i++)
        house(mb, r, t, v3(mid_x[i], TH1, 6.5f), 0.0f, rng_range(r, 4.2f, 5.0f), 4.0f, 1 + rng_below(r, 2));
    // Верхний ярус: башня в четыре этажа и дома у стены
    house(mb, r, t, v3(63.0f, TH2, 22.5f), PI_F, 5.6f, 4.0f, 4);
    house(mb, r, t, v3(71.5f, TH2, 22.5f), PI_F, 4.8f, 3.8f, 2);
    house(mb, r, t, v3(89.5f, TH2, 22.5f), PI_F, 5.6f, 3.8f, 3);
    house(mb, r, t, v3(96.8f, TH2, 22.3f), PI_F, 4.2f, 3.6f, 1 + rng_below(r, 2));

    // Прилавки главной улицы: южные смотрят на север, северные на юг
    static const f32 ss_x[6] = { 60.0f, 64.6f, 71.4f, 84.6f, 90.1f, 96.2f };
    static const f32 ns_x[6] = { 59.6f, 66.0f, 72.0f, 84.2f, 89.6f, 95.8f };
    static const u8 ss_k[6] = { STALL_WATER, STALL_SCRAP, STALL_FOOD, STALL_TOOLS, STALL_CLOTH, STALL_SCRAP };
    static const u8 ns_k[6] = { STALL_FOOD, STALL_CLOTH, STALL_SCRAP, STALL_WATER, STALL_RELIC, STALL_FOOD };
    for (u32 i = 0; i < 6; i++) {
        stall(mb, r, t, v3(ss_x[i] + rng_range(r, -0.3f, 0.3f), TH0, mz0 - 0.9f), rng_range(r, -0.05f, 0.05f), ss_k[i]);
        stall(mb, r, t, v3(ns_x[i] + rng_range(r, -0.3f, 0.3f), TH0, mz1 + 0.9f), PI_F + rng_range(r, -0.05f, 0.05f), ns_k[i]);
    }
    // Переулок к пандусу и на юг
    stall(mb, r, t, v3(RX0 + 0.2f, TH0, -0.4f), PI_F * 0.5f, STALL_TOOLS);
    stall(mb, r, t, v3(RX0 + 0.2f, TH0, -16.0f), PI_F * 0.5f, STALL_WATER);
    stall(mb, r, t, v3(RX1 - 0.2f, TH0, -18.6f), -PI_F * 0.5f, STALL_SCRAP);
    // Верхняя улица среднего яруса и лоток со знаками у проёма в стене
    stall(mb, r, t, v3(62.4f, TH1, 10.7f), 0.0f, STALL_FOOD);
    stall(mb, r, t, v3(68.3f, TH1, 10.7f), 0.0f, STALL_RELIC);
    stall(mb, r, t, v3(88.2f, TH1, 10.7f), 0.0f, STALL_CLOTH);
    stall(mb, r, t, v3(94.3f, TH1, 10.7f), 0.0f, STALL_WATER);
    stall(mb, r, t, v3(82.0f, TH2, 19.6f), PI_F, STALL_RELIC);

    // Очаг с ковром вместо прилавка, куча лома, где разбирают железо
    prop_fire_pit(mb, r, v3(68.0f, TH0, -12.6f));
    prop_rug(mb, r, v3(66.4f, TH0, -12.9f), 0.2f);
    add_spot(t, v3(67.2f, TH0, -12.0f), 2.4f, SPOT_WORKER);
    add_spot(t, v3(68.8f, TH0, -13.2f), -0.6f, SPOT_WORKER);
    prop_scrap_heap(mb, r, v3(93.0f, TH0, -13.0f), 1.5f);
    prop_scrap_heap(mb, r, v3(RX1 + 1.0f, TH0, 2.0f), 1.0f);
    add_spot(t, v3(92.0f, TH0, -11.8f), 2.8f, SPOT_WORKER);
    prop_rug(mb, r, v3(80.0f, TH1, 15.0f), 0.0f);
    prop_water_tower(mb, r, v3(60.0f, TH1, 14.4f), 3.0f);
    prop_scrap_heap(mb, r, v3(72.0f, TH1, 15.0f), 0.9f);
    prop_crate_stack(mb, r, v3(66.0f, TH1, 15.3f), 0.1f);
    prop_crate_stack(mb, r, v3(97.0f, TH1, 15.2f), -0.2f);
    prop_barrel(mb, r, v3(81.5f, TH1, 15.4f), false);
    prop_barrel(mb, r, v3(82.2f, TH1, 15.5f), true);
    for (u32 i = 0; i < 5; i++) prop_clutter(mb, r, v3(rng_range(r, 58.0f, 98.0f), TH2, rng_range(r, 19.2f, 20.0f)), rng_range(r, 0, PI_F));
    // Зеваки у перил верхнего яруса смотрят вниз
    add_spot(t, v3(66.0f, TH2, TZ2 + 0.55f), PI_F, SPOT_IDLE);
    add_spot(t, v3(74.5f, TH2, TZ2 + 0.6f), PI_F + 0.3f, SPOT_IDLE);
    add_spot(t, v3(96.5f, TH2, TZ2 + 0.55f), PI_F - 0.2f, SPOT_IDLE);
    add_spot(t, v3(RX0 - 1.2f, TH1, TZ1 + 0.6f), PI_F, SPOT_IDLE);

    // Фонари вдоль улиц
    static const f32 lamp_x[6] = { 57.5f, 68.5f, 81.6f, 87.0f, 93.5f, 99.0f };
    for (u32 i = 0; i < 6; i++) {
        bool south = i & 1;
        prop_lamp_post(mb, r, v3(lamp_x[i], TH0, south ? mz0 - 0.55f : mz1 + 0.55f), south ? 0.0f : PI_F);
    }
    prop_lamp_post(mb, r, v3(RX0 - 0.6f, TH1, 12.0f), PI_F * 0.5f);
    prop_lamp_post(mb, r, v3(95.6f, TH2, 16.6f), PI_F);
    prop_lamp_post(mb, r, v3(76.0f, TH0, -20.5f), 0.5f);

    // Провода над главной улицей от крыши к крыше, тряпки и лампы на них
    for (u32 i = 0; i < 9; i++) {
        u32 a = rng_below(r, 6), b = a + (rng_f32(r) < 0.5f && a < 5 ? 1 : 0);
        f32 ya = fminf(north_roof[a], 6.0f) - rng_range(r, 0.2f, 0.6f), yb = fminf(south_roof[b], 6.0f) - rng_range(r, 0.2f, 0.6f);
        f32 xa = north_x[a] + rng_range(r, -2.0f, 2.0f), xb = south_x[b] + rng_range(r, -2.0f, 2.0f);
        f32 sag = rng_range(r, 0.3f, 0.8f);
        if (fminf(ya, yb) - sag < 2.6f) sag = fmaxf(0.1f, fminf(ya, yb) - 2.6f);
        if (fminf(ya, yb) < 2.7f) continue;
        prop_cable(mb, r, v3(xa, ya, -2.5f), v3(xb, yb, -13.7f), sag, rng_below(r, 3));
    }
    // С верхнего яруса вниз, к домам среднего
    for (u32 i = 0; i < 4; i++) {
        f32 x = rng_range(r, 58.0f, 98.0f);
        if (x > SX0 - 1.0f && x < SX2 + 1.0f) continue;
        f32 xb = mid_x[rng_below(r, 6)] + rng_range(r, -1.5f, 1.5f);
        prop_paint(mb, r, g_mat.rust, 0.2f);
        mb_cylinder(mb, v3(x, TH2, TZ2 + 0.35f), v3(x, TH2 + 3.2f, TZ2 + 0.35f), 0.05f, 5);
        prop_cable(mb, r, v3(x, TH2 + 3.1f, TZ2 + 0.35f), v3(xb, TH1 + 2.6f, 8.6f), 0.5f, rng_below(r, 2));
    }
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

// ---------------------------------------------------------------- жители

// Узлы улиц, по которым ходят прохожие; market — у прилавков, там задерживаются
typedef struct Node {
    f32 x, z, jitter;
    bool market;
} Node;

static const Node k_nodes[] = {
    { 50.0f, -8.0f, 1.0f, false },  // 0 у ворот снаружи
    { 58.5f, -8.0f, 0.8f, true },   // 1
    { 66.0f, -8.0f, 1.0f, true },   // 2
    { 78.0f, -8.0f, 0.8f, false },  // 3 перекрёсток
    { 88.0f, -8.0f, 1.0f, true },   // 4
    { 98.0f, -8.0f, 0.8f, true },   // 5
    { 78.0f, -19.0f, 1.0f, true },  // 6 переулок на юг
    { 78.0f, 1.5f, 0.8f, true },    // 7 под пандусом
    { 78.0f, 12.5f, 0.8f, false },  // 8 верх пандуса
    { 64.0f, 12.5f, 1.0f, true },   // 9
    { 84.5f, 12.5f, 0.6f, true },   // 10
    { 83.0f, 15.0f, 0.3f, false },  // 11 низ лестницы
    { 93.0f, 15.0f, 0.3f, false },  // 12 площадка наверху
    { 94.0f, 18.0f, 0.6f, false },  // 13
    { 82.0f, 18.0f, 1.0f, true },   // 14 у лотка со знаками
    { 68.0f, 17.8f, 0.8f, true },   // 15 у башни
};
#define NODE_COUNT ((u32)ARRAY_COUNT(k_nodes))

static const u8 k_edges[][2] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 3, 6 }, { 3, 7 }, { 7, 8 },
    { 8, 9 }, { 8, 10 }, { 10, 11 }, { 11, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 },
};

static u32 next_node(Rng *r, u32 at, u32 prev)
{
    u32 opts[8], n = 0;
    for (u32 i = 0; i < ARRAY_COUNT(k_edges); i++) {
        u32 o = k_edges[i][0] == at ? k_edges[i][1] : k_edges[i][1] == at ? k_edges[i][0] : 255u;
        if (o != 255u && o != prev && n < 8) opts[n++] = o;
    }
    return n ? opts[rng_below(r, n)] : prev;
}

static void walk_to(People *p, Rng *r, u32 i, u32 node)
{
    const Node *nd = &k_nodes[node];
    p->prev[i] = p->node[i];
    p->node[i] = (u8)node;
    // Порядок случайных чисел задан явно: сначала z, потом x
    f32 jz = rng_range(r, -nd->jitter, nd->jitter), jx = rng_range(r, -nd->jitter, nd->jitter);
    player_order_move(&p->body[i], v3(nd->x + jx, 0, nd->z + jz));
    p->body[i].marker = 0.0f;
}

void town_folk_add(People *p, const Town *t, Rng *r, u32 walkers, u32 variants, u8 team)
{
    u32 first = p->count;
    // Сначала продавцы, сторожа и работники, потом прохожие, остаток мест — зеваки.
    // Один посёлок: за своих встают сторожа
    for (u32 pass = 0; pass < 3; pass++) {
        if (pass == 1) {
            for (u32 k = 0; k < walkers && p->count - first < TOWN_MAX_FOLK; k++) {
                u32 node = 1 + rng_below(r, NODE_COUNT - 1);
                const Node *nd = &k_nodes[node];
                f32 h = rng_range(r, 0, 2 * PI_F);
                f32 jz = rng_range(r, -nd->jitter, nd->jitter), jx = rng_range(r, -nd->jitter, nd->jitter);
                u32 i = people_add(p, ROLE_TOWN, v3(nd->x + jx, 0, nd->z + jz), h, team, 0);
                p->node[i] = p->prev[i] = (u8)node;
                p->wait[i] = rng_range(r, 0.0f, 4.0f);
            }
        }
        for (u32 s = 0; s < t->spot_count && p->count - first < TOWN_MAX_FOLK; s++) {
            const TownSpot *sp = &t->spot[s];
            bool want = pass == 0 ? sp->kind != SPOT_IDLE : pass == 2 && sp->kind == SPOT_IDLE;
            if (!want) continue;
            bool guard = sp->kind == SPOT_GUARD;
            u32 i = people_add(p, ROLE_TOWN, sp->pos, sp->heading, team, guard ? FF_ASSIST : 0);
            p->stand[i] = true;
            p->sheet[i].role = sp->kind == SPOT_TRADER ? "ТОРГОВЕЦ" : guard ? "СТОРОЖ СКУДЕЛИ"
                             : sp->kind == SPOT_WORKER ? "СБОРЩИК ЛОМА" : "ЖИТЕЛЬ СКУДЕЛИ";
        }
    }
    for (u32 i = first; i < p->count; i++) {
        p->seed[i] = rng_range(r, 0.0f, 100.0f);
        p->variant[i] = (u8)rng_below(r, variants);
        p->pace[i] = rng_range(r, 0.95f, 1.4f);
        p->body[i].phase = rng_f32(r);
        p->body[i].gait = GAIT_WALK;
        p->home[i] = p->body[i].pos;
        p->home_h[i] = p->body[i].heading;
    }
    // Листы жителей: свой поток случайных чисел, чтобы посёлок не зависел от их числа.
    // Сторожа бывалые, с ломом в руке (как рисуется), и у всех сторожей и у каждого пятого прохожего — факел
    for (u32 i = first; i < p->count; i++) {
        u32 k = i - first;
        bool armed = p->fflags[i] & FF_ASSIST;
        CharSheet *c = &p->sheet[i];
        const char *role = c->role ? c->role : "ПРОХОЖИЙ";
        Rng sr;
        rng_seed(&sr, 0x70C0u + k, 93);
        u8 weapon = def_weapon(p->variant[i] % 2 ? "sekach" : "ressora");
        sheet_random(c, &sr, name_next(p->variant[i] % BODY_KIND_COUNT == BODY_FEMALE), armed ? 1 : 0, weapon);
        if (!armed) {
            Item old;
            sheet_equip(c, SLOT_WEAPON, (Item){ 0 }, &old);
        }
        c->role = role;
        p->torch[i] = armed || (!p->stand[i] && k % 5 == 0);
        people_look_random(p, i, k * 6151u + 5003u, k * 977u + 31u);
    }
}

void town_folk_step(People *pp, u32 i, Rng *r, f32 dt, GroundFn ground)
{
    Player *p = &pp->body[i];
    if (p->ctrl != CTRL_FREE) {
        // Бой или погоня: телом правит поле боя; после — постоит и пойдёт дальше
        pp->wait[i] = rng_range(r, 3.0f, 8.0f);
        player_update(p, dt, ground);
        return;
    }
    if (pp->stand[i] && (p->moving || v3_len(v3_sub(pp->home[i], p->pos)) > 0.5f)) {
        // Стоящий после боя возвращается на место и встаёт как стоял
        if (!p->moving && (pp->wait[i] -= dt) <= 0.0f) {
            player_order_move(p, pp->home[i]);
            p->marker = 0.0f;
        }
        p->vmax = pp->pace[i];
        player_update(p, dt, ground);
        if (!p->moving) p->heading = pp->home_h[i];
        return;
    }
    if (pp->stand[i]) {
        // Стоящие следят головой за ближайшим прохожим
        f32 best = 36.0f, goal = 0.0f;
        for (u32 j = 0; j < pp->count; j++) {
            if (pp->role[j] != ROLE_TOWN || pp->stand[j]) continue;
            const Player *o = &pp->body[j];
            f32 dx = o->pos.x - p->pos.x, dz = o->pos.z - p->pos.z, d2 = dx * dx + dz * dz;
            if (d2 < best && fabsf(o->pos.y - p->pos.y) < 2.0f) {
                f32 a = atan2f(dx, dz) - p->heading;
                while (a > PI_F) a -= 2 * PI_F;
                while (a < -PI_F) a += 2 * PI_F;
                if (fabsf(a) < 1.6f) {
                    best = d2;
                    goal = clampf(a, -1.0f, 1.0f);
                }
            }
        }
        p->look += (goal - p->look) * fminf(1.0f, dt * 2.5f);
        return;
    }
    bool was = p->moving;
    if (!p->moving && (pp->wait[i] -= dt) <= 0.0f) walk_to(pp, r, i, next_node(r, pp->node[i], pp->prev[i]));
    p->vmax = pp->pace[i];
    player_update(p, dt, ground);
    if (was && !p->moving) pp->wait[i] = k_nodes[pp->node[i]].market ? rng_range(r, 1.5f, 8.0f) : rng_range(r, 0.0f, 1.2f);
}
