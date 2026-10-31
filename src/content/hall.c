#include "hall.h"
#include "props.h"
#include "../defs/defs.h"

#include <string.h>

#include "../world/map.h"
#include "ostov.h"
#include "weapon.h"

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
    prop_mat(mb, g_mat.concrete);

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
    prop_mat(mb, g_mat.concrete_dark);
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
        mb_set(mb, rng_f32(rng) < 0.5f ? mat_c(g_mat.concrete) : mat_c(g_mat.concrete_dark), MAT_CONCRETE, BONE_NONE);
        mb_box(mb, m4_trs(c, v3(s * rng_range(rng, 0.8f, 2.0f), s, s * rng_range(rng, 0.7f, 1.5f)),
                          rng_range(rng, -0.4f, 0.4f), rng_range(rng, 0, PI_F), rng_range(rng, -0.4f, 0.4f)));
        if (rng_f32(rng) < 0.35f) {
            prop_mat(mb, g_mat.rust);
            V3 tip = v3_add(c, v3(rng_range(rng, -0.6f, 0.6f), rng_range(rng, 0.8f, 1.8f), rng_range(rng, -0.6f, 0.6f)));
            mb_cylinder(mb, c, tip, 0.025f, 6);
        }
    }

    // Трубы вдоль задней стены
    prop_mat(mb, g_mat.rust);
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
    prop_mat(mb, g_mat.rust);
    box(mb, v3(-4, 3.6f, -15.5f), v3(3.6f, 0.8f, 2.0f));
    mb_cylinder(mb, v3(-6, 4.0f, -15.5f), v3(-6, 7.5f, -16.5f), 0.25f, 10);
    glyph_panel(mb, v3(-4, 1.8f, -13.5f), v3(0, 0, 1), 1.8f, 0.7f, mat_c(g_mat.glyph_green));

    // Знаки на стене и колоннах
    glyph_panel(mb, v3(0, 13.5f, wz + 1.2f), v3(0, 0, 1), 6.0f, 1.6f, mat_c(g_mat.glyph_green));
    glyph_panel(mb, v3(-24.0f, 16.0f, wz + 1.2f), v3(0, 0, 1), 3.0f, 0.4f, mat_c(g_mat.glyph_red));
    glyph_panel(mb, v3(-9, 4.5f, 6.8f), v3(0, 0, 1), 0.65f, 1.6f, mat_c(g_mat.glyph_red));
    glyph_panel(mb, v3(9, 6.0f, -5.2f), v3(0, 0, 1), 0.65f, 2.2f, mat_c(g_mat.glyph_green));

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
    prop_mat(mb, g_mat.rust);
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
    prop_mat(mb, g_mat.rust);
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
const V3 g_hall_lamp[HALL_LAMPS] = { { 5.1f, 3.25f, -19.3f }, { 8.9f, 3.25f, -19.3f }, { -8.75f, 3.25f, -19.2f },
                                            { -4.4f, 3.25f, -19.2f }, { 23.6f, 3.25f, -18.9f } };

static void rack_lamp(MeshBuilder *mb, V3 p, f32 wall_z)
{
    mb_set(mb, v3(0.05f, 0.05f, 0.05f), MAT_METAL, BONE_NONE);
    mb_cylinder(mb, v3(p.x, p.y + 0.32f, wall_z), v3(p.x, p.y + 0.32f, p.z), 0.02f, 6);
    mb_cylinder(mb, v3(p.x, p.y + 0.32f, p.z), v3(p.x, p.y + 0.2f, p.z), 0.015f, 6);
    static const V2 shade[] = { { 0.0f, 0.05f }, { 0.07f, 0.07f }, { 0.17f, 0.17f }, { 0.25f, 0.24f }, { 0.26f, 0.23f } };
    V3 d = HALL_LAMP_DIR;
    prop_mat(mb, g_mat.rust);
    mb_lathe(mb, v3_sub(p, v3_mul(d, 0.25f)), v3_add(p, v3_mul(d, 0.01f)), shade, ARRAY_COUNT(shade), 12, 1.0f, 1.0f);
    mb_set(mb, v3(1.0f, 0.78f, 0.45f), MAT_EMISSIVE, BONE_NONE);
    mb_capsule(mb, v3_sub(p, v3_mul(d, 0.06f)), v3_sub(p, v3_mul(d, 0.02f)), 0.06f, 0.06f, 8, 1.0f, 1.0f);
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
    for (u32 k = 0; k < HALL_LAMPS; k++) rack_lamp(mb, g_hall_lamp[k], wall);
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

void hall_build_near(MeshBuilder *mb, Rng *rng)
{
    hall(mb, rng);
}

void hall_build_far(MeshBuilder *mb, Rng *rng)
{
    megastructures(mb, rng);
}

bool hall_floor(f32 x, f32 z) { return fabsf(x) <= 32.0f && z >= -28.0f && z <= 24.0f; }
