#include "testroom.h"
#include "../defs/defs.h"

#include <string.h>

#include "arena.h"
#include "anim.h"
#include "look.h"
#include "names.h"
#include "ostov.h"
#include "showhall.h"
#include "squad.h"
#include "town.h"
#include "../world/map.h"
#include "../core/palette.h"

// Цвета поверхностей (альбедо, линейные)

static void box(MeshBuilder *mb, V3 c, V3 size)
{
    mb_box(mb, m4_trs(c, size, 0, 0, 0));
}

static void glyph_panel(MeshBuilder *mb, V3 c, V3 normal, f32 hw, f32 hh, V3 color)
{
    mb_set(mb, color, MAT_GLYPH, BONE_NONE);
    V3 right = v3_norm(v3_cross(v3(0, 1, 0), normal));
    mb_quad(mb, v3_add(c, v3_mul(normal, 0.03f)), v3_mul(right, hw), v3(0, hh, 0));
}

static void weapon_racks(MeshBuilder *mb);

static void hall(MeshBuilder *mb, Rng *rng)
{
    mb_set(mb, PAL_CONCRETE, MAT_CONCRETE, BONE_NONE);

    // Плита пола
    box(mb, v3(0, -0.3f, -2), v3(64, 0.8f, 52));

    // Две колоннады
    for (int row = -1; row <= 1; row += 2)
        for (int k = 0; k < 7; k++) {
            f32 x = (f32)row * 9.0f, z = -18.0f + (f32)k * 6.0f;
            bool broken = row == 1 && k == 6;
            f32 h = broken ? 5.5f : 15.0f;
            box(mb, v3(x, h * 0.5f, z), v3(1.6f, h, 1.6f));
            // Утолщение у основания
            box(mb, v3(x, 0.5f, z), v3(2.2f, 1.0f, 2.2f));
            if (broken) {
                mb_box(mb, m4_trs(v3(x + 3.5f, 0.9f, z + 2.0f), v3(1.6f, 1.6f, 7.5f), 0.08f, 0.9f, 0.1f));
                mb_box(mb, m4_trs(v3(x + 1.5f, 0.6f, z - 1.5f), v3(1.3f, 1.2f, 1.6f), 0.3f, 0.4f, 0.2f));
            }
        }

    // Балки поверх колонн и перекрытие полосами: свет проходит в щели
    for (int k = 0; k < 6; k++) {
        f32 z = -18.0f + (f32)k * 6.0f;
        box(mb, v3(0, 15.6f, z), v3(20.0f, 1.2f, 1.4f));
    }
    box(mb, v3(-9, 16.7f, -3), v3(1.8f, 1.0f, 38.0f));
    box(mb, v3(9, 16.7f, -6), v3(1.8f, 1.0f, 32.0f));
    for (int k = 0; k < 6; k++) {
        f32 z = -19.0f + (f32)k * 2.9f;
        box(mb, v3(0, 17.5f, z), v3(19.0f, 0.6f, 1.9f));
    }

    // Задняя стена с проёмами
    f32 wz = -22.0f, wh = 22.0f;
    f32 gates[] = { -14.0f, 0.0f, 14.0f };
    f32 gw = 6.0f, gh = 10.0f;
    f32 x0 = -34.0f;
    for (u32 g = 0; g <= ARRAY_COUNT(gates); g++) {
        f32 x1 = g < ARRAY_COUNT(gates) ? gates[g] - gw * 0.5f : 34.0f;
        box(mb, v3((x0 + x1) * 0.5f, wh * 0.5f, wz), v3(x1 - x0, wh, 2.4f));
        if (g < ARRAY_COUNT(gates)) {
            box(mb, v3(gates[g], (gh + wh) * 0.5f, wz), v3(gw, wh - gh, 2.4f));
            x0 = gates[g] + gw * 0.5f;
        }
    }
    // Контрфорсы
    for (int k = -3; k <= 3; k++) {
        f32 x = (f32)k * 9.5f + 4.7f;
        if (fabsf(x) > 33.0f) continue;
        mb_box(mb, m4_trs(v3(x, 7.0f, wz - 3.5f), v3(1.4f, 16.0f, 5.0f), -0.28f, 0, 0));
    }

    // Обрушенная боковая стена: ступенчатые обломки
    mb_set(mb, PAL_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
    for (int k = 0; k < 12; k++) {
        f32 z = -20.0f + (f32)k * 2.6f;
        f32 h = fmaxf(1.0f, 13.0f - (f32)k * 1.1f + rng_range(rng, -1.5f, 1.5f));
        box(mb, v3(-25.0f, h * 0.5f, z), v3(2.0f, h, 2.7f));
    }

    // Обломки и арматура
    for (int i = 0; i < 70; i++) {
        f32 ang = rng_range(rng, 0, 2 * PI_F);
        f32 r = rng_range(rng, 2.0f, 30.0f);
        f32 s = rng_range(rng, 0.25f, 1.4f);
        V3 c = v3(cosf(ang) * r, s * 0.3f, sinf(ang) * r - 4.0f);
        if (fabsf(c.x) < 6.5f && c.z > -16.0f) continue; // проход по центру
        mb_set(mb, rng_f32(rng) < 0.5f ? PAL_CONCRETE : PAL_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
        mb_box(mb, m4_trs(c, v3(s * rng_range(rng, 0.8f, 2.0f), s, s * rng_range(rng, 0.7f, 1.5f)),
                          rng_range(rng, -0.4f, 0.4f), rng_range(rng, 0, PI_F), rng_range(rng, -0.4f, 0.4f)));
        if (rng_f32(rng) < 0.35f) {
            mb_set(mb, PAL_RUST, MAT_RUST, BONE_NONE);
            V3 tip = v3_add(c, v3(rng_range(rng, -0.6f, 0.6f), rng_range(rng, 0.8f, 1.8f), rng_range(rng, -0.6f, 0.6f)));
            mb_cylinder(mb, c, tip, 0.025f, 6);
        }
    }

    // Трубы вдоль задней стены
    mb_set(mb, PAL_RUST, MAT_RUST, BONE_NONE);
    for (int i = 0; i < 2; i++) {
        f32 y = 4.0f + (f32)i * 1.3f, r = 0.45f - (f32)i * 0.1f;
        mb_cylinder(mb, v3(-33, y, wz + 1.9f), v3(33, y, wz + 1.9f), r, 14);
        for (f32 x = -32.0f; x <= 32.0f; x += 4.0f)
            mb_cylinder(mb, v3(x - 0.1f, y, wz + 1.9f), v3(x + 0.1f, y, wz + 1.9f), r + 0.12f, 14);
        for (f32 x = -26.0f; x <= 26.0f; x += 13.0f)
            box(mb, v3(x, y - 0.3f - (f32)i * 0.2f, wz + 1.5f), v3(0.3f, 0.2f, 0.9f));
    }
    mb_cylinder(mb, v3(20, 0, wz + 1.9f), v3(20, 22, wz + 1.9f), 0.6f, 14);
    mb_cylinder(mb, v3(-6, 0, wz + 1.9f), v3(-6, 5.3f, wz + 1.9f), 0.3f, 10);

    // Лежащий резервуар
    mb_set(mb, v3(0.26f, 0.19f, 0.14f), MAT_RUST, BONE_NONE);
    mb_cylinder(mb, v3(16, 3.0f, 10), v3(22, 3.4f, -2), 3.0f, 24);
    for (int i = 0; i < 4; i++) {
        f32 t = 0.15f + 0.23f * (f32)i;
        V3 c = v3_lerp(v3(16, 3.0f, 10), v3(22, 3.4f, -2), t);
        V3 d = v3_mul(v3_norm(v3(6, 0.4f, -12)), 0.15f);
        mb_cylinder(mb, v3_sub(c, d), v3_add(c, d), 3.15f, 24);
    }

    // Контейнеры
    mb_set(mb, v3(0.30f, 0.18f, 0.11f), MAT_RUST, BONE_NONE);
    mb_box(mb, m4_trs(v3(-15, 1.3f, 8), v3(2.4f, 2.6f, 6.0f), 0, 0.15f, 0));
    mb_box(mb, m4_trs(v3(-14.2f, 3.4f, 4.5f), v3(2.4f, 2.6f, 6.0f), 0.12f, -0.35f, 0.25f));

    // Машина неизвестного назначения со знаками
    mb_set(mb, v3(0.22f, 0.21f, 0.20f), MAT_METAL, BONE_NONE);
    box(mb, v3(-4, 1.6f, -15), v3(5.0f, 3.2f, 3.0f));
    mb_set(mb, PAL_RUST, MAT_RUST, BONE_NONE);
    box(mb, v3(-4, 3.6f, -15.5f), v3(3.6f, 0.8f, 2.0f));
    mb_cylinder(mb, v3(-6, 4.0f, -15.5f), v3(-6, 7.5f, -16.5f), 0.25f, 10);
    glyph_panel(mb, v3(-4, 1.8f, -13.5f), v3(0, 0, 1), 1.8f, 0.7f, PAL_GLYPH_GREEN);

    // Знаки на стене и колоннах
    glyph_panel(mb, v3(0, 13.5f, wz + 1.2f), v3(0, 0, 1), 6.0f, 1.6f, PAL_GLYPH_GREEN);
    glyph_panel(mb, v3(-24.0f, 16.0f, wz + 1.2f), v3(0, 0, 1), 3.0f, 0.4f, PAL_GLYPH_RED);
    glyph_panel(mb, v3(-9, 4.5f, 6.8f), v3(0, 0, 1), 0.65f, 1.6f, PAL_GLYPH_RED);
    glyph_panel(mb, v3(9, 6.0f, -5.2f), v3(0, 0, 1), 0.65f, 2.2f, PAL_GLYPH_GREEN);

    // Красные огни на колоннах
    mb_set(mb, v3(0.9f, 0.12f, 0.06f), MAT_EMISSIVE, BONE_NONE);
    box(mb, v3(-9, 8.0f, 6.85f), v3(0.18f, 0.18f, 0.1f));
    box(mb, v3(9, 8.0f, -5.15f), v3(0.18f, 0.18f, 0.1f));
    mb_set(mb, v3(0.9f, 0.6f, 0.25f), MAT_EMISSIVE, BONE_NONE);
    box(mb, v3(-4, 3.25f, -13.4f), v3(0.4f, 0.12f, 0.05f));

    weapon_racks(mb);
}

// ---------------------------------------------------------------- стойки с оружием

// Вещь на стойке: повернуть осями rot, затем сдвинуть так, чтобы её охват встал к точке at:
// по каждой оси align -1 — к минимуму, 0 — серединой, 1 — к максимуму
static void rack_item(MeshBuilder *mb, const char *id, u8 quality, M4 rot, V3 at, V3 align)
{
    u32 v0 = mb->vcount;
    weapon_build_xf(mb, def_weapon(id), (u8)weapon_look(quality), rot);
    V3 lo = v3(1e9f, 1e9f, 1e9f), hi = v3(-1e9f, -1e9f, -1e9f);
    for (u32 i = v0; i < mb->vcount; i++) {
        V3 p = v3(mb->v[i].pos[0], mb->v[i].pos[1], mb->v[i].pos[2]);
        lo = v3(minf(lo.x, p.x), minf(lo.y, p.y), minf(lo.z, p.z));
        hi = v3(maxf(hi.x, p.x), maxf(hi.y, p.y), maxf(hi.z, p.z));
    }
    f32 a[3] = { align.x, align.y, align.z }, l[3] = { lo.x, lo.y, lo.z }, h[3] = { hi.x, hi.y, hi.z };
    f32 t[3] = { at.x, at.y, at.z }, d[3];
    for (u32 k = 0; k < 3; k++) d[k] = t[k] - (a[k] < 0.0f ? l[k] : a[k] > 0.0f ? h[k] : 0.5f * (l[k] + h[k]));
    for (u32 i = v0; i < mb->vcount; i++)
        for (u32 k = 0; k < 3; k++) mb->v[i].pos[k] += d[k];
}

// Оси вещи в мире: толщина, длина (к острию), лезвие
static M4 rack_axes(V3 x, V3 y, V3 z)
{
    M4 m = m4_identity();
    m.m[0] = x.x, m.m[1] = x.y, m.m[2] = x.z;
    m.m[4] = y.x, m.m[5] = y.y, m.m[6] = y.z;
    m.m[8] = z.x, m.m[9] = z.y, m.m[10] = z.z;
    return m;
}

// Напольная стойка: брус внизу с гнёздами, брус у стены сверху; вещи стоят с наклоном к стене —
// древковое остриём вверх, мечи рукоятью вверх, остриём в гнезде
static void rack_stand(MeshBuilder *mb, f32 x0, f32 x1, f32 z, f32 top, const char *const *ids, const u8 *qs, u32 n,
                       bool hilt_up)
{
    const f32 floor = 0.1f, lean = 0.06f;
    mb_set(mb, PAL_RUST, MAT_RUST, BONE_NONE);
    box(mb, v3(0.5f * (x0 + x1), floor + 0.09f, z), v3(x1 - x0 + 0.2f, 0.18f, 0.22f));
    box(mb, v3(0.5f * (x0 + x1), top, z - top * lean - 0.04f), v3(x1 - x0 + 0.2f, 0.08f, 0.1f));
    for (u32 sd = 0; sd < 2; sd++) {
        f32 x = sd ? x1 + 0.05f : x0 - 0.05f;
        mb_box(mb, m4_trs(v3(x, floor + top * 0.5f, z - top * lean * 0.5f - 0.02f), v3(0.07f, top + 0.1f, 0.07f), lean, 0, 0));
    }
    M4 rot = hilt_up ? rack_axes(v3(0, 0, 1), v3_norm(v3(0, -1, lean)), v3(1, 0, 0))
                     : rack_axes(v3(0, 0, 1), v3_norm(v3(0, 1, -lean)), v3(-1, 0, 0));
    f32 step = n > 1 ? (x1 - x0) / (f32)(n - 1) : 0.0f;
    for (u32 i = 0; i < n; i++) rack_item(mb, ids[i], qs[i], rot, v3(x0 + step * (f32)i, floor + 0.06f, z), v3(0, -1, 0));
}

// Настенная доска: лист на двух стойках, вещи лежат на крюках клинком вниз, слева направо по качеству
static void rack_wall(MeshBuilder *mb, f32 x0, f32 x1, f32 wall_z, f32 y0, f32 y1, const char *const *ids, u32 rows)
{
    mb_set(mb, v3(0.34f, 0.33f, 0.30f), MAT_CONCRETE, BONE_NONE);
    box(mb, v3(0.5f * (x0 + x1), 0.5f * (y0 + y1), wall_z + 0.03f), v3(x1 - x0, y1 - y0, 0.04f));
    mb_set(mb, PAL_RUST, MAT_RUST, BONE_NONE);
    for (u32 sd = 0; sd < 2; sd++) box(mb, v3(sd ? x1 : x0, 0.5f * (0.1f + y1 + 0.1f), wall_z + 0.08f), v3(0.08f, y1, 0.08f));
    M4 rot = rack_axes(v3(0, 0, -1), v3(1, 0, 0), v3(0, -1, 0));
    f32 dy = (y1 - y0 - 0.3f) / (f32)rows, col = (x1 - x0 - 0.3f) / 3.0f;
    for (u32 r = 0; r < rows; r++) {
        f32 y = y1 - 0.2f - dy * ((f32)r + 0.5f);
        for (u32 q = 0; q < 3; q++) {
            f32 x = x0 + 0.25f + col * (f32)q;
            bool uniq = g_weapons[def_weapon(ids[r])].unique;
            if (uniq && q != 1) continue;
            // Крюки под вещью
            mb_set(mb, v3(0.12f, 0.12f, 0.12f), MAT_METAL, BONE_NONE);
            for (u32 h = 0; h < 2; h++) box(mb, v3(x + 0.08f + 0.35f * (f32)h, y - 0.03f, wall_z + 0.1f), v3(0.02f, 0.02f, 0.12f));
            rack_item(mb, ids[r], uniq ? Q_SOUND : (u8)q, rot, v3(x, y, wall_z + 0.13f), v3(-1, -1, 0));
        }
    }
}

// Лампы над стойками: кронштейн из стены под трубами, колпак наклонён к стойке
#define RACK_LAMPS 5
static const V3 g_rack_lamp[RACK_LAMPS] = { { 5.1f, 3.25f, -19.3f }, { 8.9f, 3.25f, -19.3f }, { -8.75f, 3.25f, -19.2f },
                                            { -4.4f, 3.25f, -19.2f }, { 23.6f, 3.25f, -18.9f } };
#define RACK_LAMP_DIR v3(0.0f, -0.88f, -0.47f)

static void rack_lamp(MeshBuilder *mb, V3 p, f32 wall_z)
{
    mb_set(mb, v3(0.05f, 0.05f, 0.05f), MAT_METAL, BONE_NONE);
    mb_cylinder(mb, v3(p.x, p.y + 0.32f, wall_z), v3(p.x, p.y + 0.32f, p.z), 0.02f, 6);
    mb_cylinder(mb, v3(p.x, p.y + 0.32f, p.z), v3(p.x, p.y + 0.2f, p.z), 0.015f, 6);
    static const V2 shade[] = { { 0.0f, 0.05f }, { 0.07f, 0.07f }, { 0.17f, 0.17f }, { 0.25f, 0.24f }, { 0.26f, 0.23f } };
    V3 d = RACK_LAMP_DIR;
    mb_set(mb, PAL_RUST, MAT_RUST, BONE_NONE);
    mb_lathe(mb, v3_sub(p, v3_mul(d, 0.25f)), v3_add(p, v3_mul(d, 0.01f)), shade, ARRAY_COUNT(shade), 12, 1.0f, 1.0f);
    mb_set(mb, v3(1.0f, 0.78f, 0.45f), MAT_EMISSIVE, BONE_NONE);
    mb_capsule(mb, v3_sub(p, v3_mul(d, 0.06f)), v3_sub(p, v3_mul(d, 0.02f)), 0.06f, 0.06f, 8, 1.0f, 1.0f);
}

u32 testroom_lights(PointLight *out, u32 max)
{
    u32 n = 0;
    for (u32 k = 0; k < RACK_LAMPS && n < max; k++)
        out[n++] = (PointLight){ .pos = v3_add(g_rack_lamp[k], v3_mul(RACK_LAMP_DIR, 0.05f)), .radius = 6.0f,
                                 .color = v3_mul(v3(1.0f, 0.70f, 0.36f), 1.5f), .cone = 0.45f, .dir = RACK_LAMP_DIR,
                                 .glow = 0.0009f };
    return n;
}

// Оружейная у задней стены зала: одноручное на доске, двуручное и древковое в напольных стойках.
// Пока только вид; подбирать их будет можно, когда вещи на земле станут телами (тред физики)
static void weapon_racks(MeshBuilder *mb)
{
    const f32 wall = -20.8f;
    static const char *const one[] = { "lopast", "zatochka", "rezak", "polosa", "ressora", "sekach", "teslo" };
    rack_wall(mb, 3.3f, 10.7f, wall, 0.45f, 2.9f, one, ARRAY_COUNT(one));
    static const char *const two[] = { "dolgaya", "dolgaya", "dolgaya", "resheto", "otval", "otval", "otval" };
    static const u8 two_q[] = { Q_JUNK, Q_WORN, Q_SOUND, Q_SOUND, Q_JUNK, Q_WORN, Q_SOUND };
    rack_stand(mb, -10.6f, -6.9f, wall + 0.55f, 1.55f, two, two_q, ARRAY_COUNT(two), true);
    static const char *const heavy[] = { "balka", "balka", "balka" };
    static const u8 heavy_q[] = { Q_JUNK, Q_WORN, Q_SOUND };
    rack_stand(mb, -5.2f, -3.6f, wall + 0.55f, 1.4f, heavy, heavy_q, ARRAY_COUNT(heavy), false);
    static const char *const pole[] = { "bagor", "bagor", "bagor", "kosa", "kosa", "kosa", "vereteno", "truba", "truba", "truba" };
    static const u8 pole_q[] = { Q_JUNK, Q_WORN, Q_SOUND, Q_JUNK, Q_WORN, Q_SOUND, Q_SOUND, Q_JUNK, Q_WORN, Q_SOUND };
    rack_stand(mb, 21.6f, 25.6f, wall + 0.75f, 2.6f, pole, pole_q, ARRAY_COUNT(pole), false);
    for (u32 k = 0; k < RACK_LAMPS; k++) rack_lamp(mb, g_rack_lamp[k], wall);
}

// Мегаструктуры у горизонта: стена, башня, пилоны, мост
static void megastructures(MeshBuilder *mb, Rng *rng)
{
    mb_set(mb, v3(0.30f, 0.29f, 0.27f), MAT_CONCRETE, BONE_NONE);
    f32 wz = -720.0f;
    box(mb, v3(0, 260.0f, wz), v3(3600.0f, 560.0f, 70.0f));
    for (int k = -14; k <= 14; k++) {
        f32 x = (f32)k * 130.0f;
        f32 h = 620.0f + rng_range(rng, -40.0f, 90.0f);
        box(mb, v3(x, h * 0.5f - 20.0f, wz + 45.0f), v3(42.0f, h, 40.0f));
        if (rng_f32(rng) < 0.4f) box(mb, v3(x, h + 10.0f, wz + 30.0f), v3(12.0f, 60.0f, 12.0f));
    }
    // Башня с кольцами
    box(mb, v3(420.0f, 450.0f, -560.0f), v3(90.0f, 1000.0f, 90.0f));
    for (int i = 0; i < 5; i++)
        box(mb, v3(420.0f, 180.0f + (f32)i * 150.0f, -560.0f), v3(150.0f, 14.0f, 150.0f));
    // Наклонный пилон
    mb_box(mb, m4_trs(v3(-520.0f, 220.0f, -420.0f), v3(60.0f, 600.0f, 60.0f), 0.0f, 0.3f, -0.35f));
    // Мост на опорах над пустыней
    mb_set(mb, v3(0.27f, 0.26f, 0.25f), MAT_CONCRETE, BONE_NONE);
    box(mb, v3(-200.0f, 140.0f, -330.0f), v3(1400.0f, 22.0f, 40.0f));
    for (int k = -3; k <= 2; k++)
        box(mb, v3((f32)k * 220.0f, 65.0f, -330.0f), v3(28.0f, 150.0f, 28.0f));
    // Огромная труба
    mb_set(mb, v3(0.30f, 0.20f, 0.13f), MAT_RUST, BONE_NONE);
    mb_cylinder(mb, v3(-1500.0f, 60.0f, -600.0f), v3(1500.0f, 90.0f, -650.0f), 32.0f, 24);
}

void testroom_build_near(MeshBuilder *mb, Rng *rng)
{
    hall(mb, rng);
}

void testroom_build_far(MeshBuilder *mb, Rng *rng)
{
    megastructures(mb, rng);
}

f32 testroom_ground(f32 x, f32 z)
{
    // Плита пола зала: верх на 0.1
    if (fabsf(x) <= 32.0f && z >= -28.0f && z <= 24.0f) return 0.1f;
    f32 h = showhall_ground(x, z, ostov_ground(x, z, town_ground(x, z, map_height(x, z))));
    // Плита арены
    f32 dx = x - ARENA_CENTER.x, dz = z - ARENA_CENTER.z;
    if (dx * dx + dz * dz < 8.5f * 8.5f) h = fmaxf(h, 0.125f);
    return h;
}

static const char *g_crowd_names[] = {
    "ЧУРИЛА", "ВЕЛИМИР", "ФЕОДУЛ", "ГОСТЯТА", "КАЛЛИСТ", "ТУЛЛ", "НЕЖДАН", "ПРОКЛ",
    "МАМЕРК", "ДОМАСЛАВ", "ЕВТИХ", "СЕВЕР", "ИЗЯСЛАВ", "ПАНКРАТ", "РУФ", "ВЫШЕСЛАВ",
};

void crowd_add(People *p, Rng *rng, u32 count)
{
    count = MIN(count, (u32)ARRAY_COUNT(g_crowd_names));
    u32 first = p->count;
    for (u32 k = 0; k < count; k++) {
        f32 x = rng_range(rng, -8.0f, 8.0f), z = rng_range(rng, -12.0f, 14.0f);
        // Каждый сам по себе: своя команда, за чужого не вступается
        u32 i = people_add(p, ROLE_WANDER, v3(x, testroom_ground(x, z), z), rng_range(rng, 0, 2 * PI_F), (u8)(TEAM_WANDER + k), 0);
        // Большинство новички с ломом в руках, изредка бывалый.
        // Свой поток случайных чисел: расстановка фигур та же, что без листов
        Rng sr;
        rng_seed(&sr, 0xC0DEu + k, 91);
        sheet_random(&p->sheet[i], &sr, g_crowd_names[k], k % 4 == 1 ? 1 : 0, g_weapon_pool[k % g_weapon_pool_count]);
        p->body[i].phase = rng_f32(rng);
        p->body[i].gait = GAIT_WALK;
        p->wait[i] = rng_range(rng, 0.0f, 3.0f);
        p->seed[i] = rng_range(rng, 0, 100.0f);
        p->variant[i] = (u8)(k % 4 % BODY_KIND_COUNT);
        p->stand[i] = k % 4 == 3;
        // Факел — у кого вторая рука свободна
        u8 wc = g_weapons[g_weapon_pool[k % g_weapon_pool_count]].wclass;
        p->torch[i] = k % 3 == 1 && wc != WC_POLEARM && wc != WC_HEAVY;
        people_look_random(p, i, k * 7919u + 101u, k * 131u + 9u);
    }
    // Первый всегда идёт к камере: сразу видно ходьбу
    if (count > 0) {
        Player *b = &p->body[first];
        player_init(b, v3(3.0f, testroom_ground(3.0f, 0.0f), 0.0f), 0.0f);
        player_order_move(b, v3(3.0f, 0.0f, 16.0f));
        b->marker = 0.0f;
        p->wait[first] = 0;
        p->stand[first] = false;
    }
}

void watchers_add(People *p, const V3 *pos, const f32 *heading, const f32 *seed, u32 count, u8 team)
{
    for (u32 k = 0; k < count; k++) {
        // Одна толпа: не вступаются, только отбиваются
        u32 i = people_add(p, ROLE_WATCHER, v3(pos[k].x, testroom_ground(pos[k].x, pos[k].z), pos[k].z), heading[k], team, 0);
        p->variant[i] = (u8)((k + 2) % BODY_KIND_COUNT);
        // Глазеть на бои ходят с чем попало: у одних лом, у других ничего, у кого-то факел
        Rng sr;
        rng_seed(&sr, 0x5EE0u + k, 92);
        bool armed = k % 3 == 0;
        u8 weapon = def_weapon(k % 2 ? "sekach" : "ressora");
        sheet_random(&p->sheet[i], &sr, name_next(p->variant[i] == BODY_FEMALE), k % 5 == 2 ? 1 : 0, weapon);
        if (!armed) {
            Item old;
            sheet_equip(&p->sheet[i], SLOT_WEAPON, (Item){ 0 }, &old);
        }
        p->sheet[i].role = "ЗРИТЕЛЬ АРЕНЫ";
        p->body[i].phase = rng_f32(&sr);
        p->body[i].gait = GAIT_WALK;
        p->seed[i] = seed[k];
        p->stand[i] = true;
        p->torch[i] = k % 5 == 1;
        people_look_random(p, i, k * 7919u + 5000u, 7000u + k);
    }
}

void crowd_step(People *pp, u32 i, Rng *rng, f32 dt, GroundFn ground)
{
    Player *p = &pp->body[i];
    if (p->ctrl != CTRL_FREE) {
        // Бой или погоня: поле боя ведёт тело, после — постоит и пойдёт дальше
        pp->wait[i] = rng_range(rng, 3.0f, 8.0f);
        player_update(p, dt, ground);
        return;
    }
    bool was = p->moving;
    if (!pp->stand[i] && !p->moving && (pp->wait[i] -= dt) <= 0.0f) {
        player_order_move(p, v3(rng_range(rng, -14.0f, 14.0f), 0.0f, rng_range(rng, -16.0f, 18.0f)));
        p->marker = 0.0f;
    }
    if (pp->stand[i] && !p->moving) {
        // Стоящий после боя возвращается на своё место и поворачивается как стоял
        V3 d = v3_sub(pp->home[i], p->pos);
        if (d.x * d.x + d.z * d.z > 0.5f * 0.5f) {
            if ((pp->wait[i] -= dt) <= 0.0f) {
                player_order_move(p, pp->home[i]);
                p->marker = 0.0f;
            }
        } else {
            f32 dh = pp->home_h[i] - p->heading;
            while (dh > PI_F) dh -= 2.0f * PI_F;
            while (dh < -PI_F) dh += 2.0f * PI_F;
            p->heading += dh * fminf(1.0f, dt * 1.5f);
        }
    }
    p->vmax = 1.15f;
    player_update(p, dt, ground);
    if (was && !p->moving) pp->wait[i] = rng_range(rng, 2.0f, 7.0f);
}

// ---------------------------------------------------------------- мобы

// Маршрут вокруг зала и арены, против часовой
#define MOB_ROUTE 6
static const V2 g_route[MOB_ROUTE] = {
    { -36.0f, -33.0f }, { 0.0f, -36.0f }, { 37.0f, -32.0f }, { 38.0f, 30.0f }, { 14.0f, 50.0f }, { -37.0f, 32.0f },
};

// Оружие и броня — имена описаний (data/defs), NULL — конец
typedef struct MobDef {
    const char *name;
    u8 tier, gait;
    const char *weapon, *armor[3];
} MobDef;

static const MobDef g_mob_defs[] = {
    { "ВЫШАТА", 1, GAIT_WALK, "ressora", { "rags", "jacket" } },
    { "ФЕОКТИСТ", 1, GAIT_JOG, "bagor", { "quilt" } },
    { "ГОРДИАН", 2, GAIT_WALK, "balka", { "quilt", "plate", "bucket" } },
    { "МИЛОНЕГ", 0, GAIT_RUN, "sekach", { "rags" } },
    { "КАССИЙ", 1, GAIT_JOG, "sekach", { "jacket", "greaves" } },
};

void mobs_add(People *p, Rng *rng)
{
    u32 n = ARRAY_COUNT(g_mob_defs);
    for (u32 k = 0; k < n; k++) {
        const MobDef *d = &g_mob_defs[k];
        // Персонаж раньше места: так шли случайные числа и прежде
        CharSheet c;
        memset(&c, 0, sizeof c);
        c.name = d->name;
        arena_random_fighter(&c.d, rng, d->tier);
        for (u32 s = 0; s < SLOT_COUNT; s++) c.d.armor[s] = ARMOR_NONE;
        for (u32 q = 0; q < PART_COUNT; q++) c.d.hp[q] = g_part_hp[q];
        c.d.blood = 100.0f * c.d.mass / 75.0f;
        Item old;
        c.d.weapon = def_weapon(d->weapon);
        sheet_equip(&c, SLOT_WEAPON, item_make(IK_WEAPON, c.d.weapon, Q_WORN, 1), &old);
        for (u32 a = 0; a < 3 && d->armor[a]; a++) {
            Item it = item_make(IK_ARMOR, def_armor(d->armor[a]), Q_WORN, 1);
            sheet_equip(&c, item_slot(it), it, &old);
        }
        sheet_equip(&c, SLOT_BACK, item_make(IK_GOODS, def_goods("pack"), Q_WORN, 1), &old);
        sheet_equip(&c, SLOT_PANTS, item_make(IK_ARMOR, def_armor("pants"), Q_WORN, 1), &old);
        // Разнесены по кругу, у каждого своя точка старта. Одна шайка: вступаются друг за друга
        u32 at = (k * 7u + 1u) % MOB_ROUTE;
        V2 a = g_route[at], b = g_route[(at + 1) % MOB_ROUTE];
        f32 t = rng_range(rng, 0.1f, 0.8f);
        f32 x = lerpf(a.x, b.x, t), z = lerpf(a.y, b.y, t);
        u32 i = people_add(p, ROLE_MOB, v3(x, testroom_ground(x, z), z), atan2f(b.x - a.x, b.y - a.y), TEAM_MOBS, FF_ASSIST);
        p->sheet[i] = c;
        p->body[i].gait = d->gait;
        p->body[i].phase = rng_f32(rng);
        p->node[i] = (u8)((at + 1) % MOB_ROUTE);
        p->variant[i] = (u8)(k % 4 % BODY_KIND_COUNT);
        p->seed[i] = rng_range(rng, 0.0f, 100.0f);
        p->height[i] = c.d.height / 1.8f;
        p->width[i] = 1.0f + 0.08f * sinf(p->seed[i] * 3.17f + 1.0f);
        p->look_seed[i] = 2000u + k;
        // Ночью шайка ходит с огнём: первый и последний, у них вторая рука свободна
        p->torch[i] = k == 0 || k == n - 1;
        player_order_move(&p->body[i], v3(b.x, 0.0f, b.y));
        p->body[i].marker = 0.0f;
        people_dress(p, i);
    }
}

void mob_step(People *pp, u32 i, Rng *rng, f32 dt, GroundFn ground)
{
    Player *p = &pp->body[i];
    if (p->ctrl != CTRL_FREE) {
        // Бой или погоня: поле боя ведёт тело, после — постоит и пойдёт дальше по кругу
        pp->wait[i] = rng_range(rng, 4.0f, 10.0f);
        player_update(p, dt, ground);
        return;
    }
    bool was = p->moving;
    p->vmax = g_gait_speed[p->gait] * (0.9f + 0.002f * pp->sheet[i].d.skill[SK_ATHLETICS]);
    if (!p->moving && (pp->wait[i] -= dt) <= 0.0f) {
        V2 t = g_route[pp->node[i]];
        // Не точно в точку: каждый круг немного иначе
        player_order_move(p, v3(t.x + rng_range(rng, -2.0f, 2.0f), 0.0f, t.y + rng_range(rng, -2.0f, 2.0f)));
        p->marker = 0.0f;
        pp->node[i] = (u8)((pp->node[i] + 1) % MOB_ROUTE);
    }
    player_update(p, dt, ground);
    // Дошёл: иногда стоит, переводит дух, иногда сразу дальше
    if (was && !p->moving) pp->wait[i] = rng_f32(rng) < 0.55f ? rng_range(rng, 6.0f, 16.0f) : 0.0f;
}
