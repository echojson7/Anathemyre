// Зал: формы стоек с оружием и ламп над ними (data/defs/objects.txt). Раскладка зала, ринга и мегаструктур —
// заготовки hall и horizon в data/defs/prefabs.txt
#include "hall.h"

#include "../defs/defs.h"
#include "props.h"
#include "shapes.h"
#include "weapon.h"

static void box(MeshBuilder *mb, V3 c, V3 size) { mb_box(mb, m4_trs(c, size, 0, 0, 0)); }

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

// Лампа: кронштейн из стены (wall_z) к точке p, колпак наклонён к стойке
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

// Стойки строятся в осях экземпляра: точка — середина низа у стены, size.x — ширина, size.y — высота
static const char *const k_one[] = { "lopast", "zatochka", "rezak", "polosa", "ressora", "sekach", "teslo" };
static const char *const k_two[] = { "dolgaya", "dolgaya", "dolgaya", "resheto", "otval", "otval", "otval" };
static const u8 k_two_q[] = { Q_JUNK, Q_WORN, Q_SOUND, Q_SOUND, Q_JUNK, Q_WORN, Q_SOUND };
static const char *const k_heavy[] = { "balka", "balka", "balka" };
static const u8 k_heavy_q[] = { Q_JUNK, Q_WORN, Q_SOUND };
static const char *const k_pole[] = { "bagor", "bagor", "bagor", "kosa", "kosa", "kosa", "vereteno", "truba", "truba", "truba" };
static const u8 k_pole_q[] = { Q_JUNK, Q_WORN, Q_SOUND, Q_JUNK, Q_WORN, Q_SOUND, Q_SOUND, Q_JUNK, Q_WORN, Q_SOUND };

// Доска на стене: одноручное по рядам, слева направо по качеству
static void sh_rack_board(const ShapeCtx *c)
{
    u32 v0 = c->mb->vcount;
    f32 w = c->size.x * 0.5f;
    rack_wall(c->mb, -w, w, 0.0f, 0.45f, c->size.y, k_one, ARRAY_COUNT(k_one));
    shape_to_world(c->mb, v0, c->L);
}

// Напольная стойка: arg 0 — двуручное рукоятью вверх, 1 — тяжёлое, 2 — древковое
static void sh_rack_stand(const ShapeCtx *c)
{
    u32 v0 = c->mb->vcount;
    f32 w = c->size.x * 0.5f;
    if (c->arg == 0) rack_stand(c->mb, -w, w, 0.0f, c->size.y, k_two, k_two_q, ARRAY_COUNT(k_two), true);
    else if (c->arg == 1) rack_stand(c->mb, -w, w, 0.0f, c->size.y, k_heavy, k_heavy_q, ARRAY_COUNT(k_heavy), false);
    else rack_stand(c->mb, -w, w, 0.0f, c->size.y, k_pole, k_pole_q, ARRAY_COUNT(k_pole), false);
    shape_to_world(c->mb, v0, c->L);
}

// Лампа над стойкой: точка — колпак, стена позади на size.z
static void sh_hall_lamp(const ShapeCtx *c)
{
    u32 v0 = c->mb->vcount;
    rack_lamp(c->mb, v3(0, 0, 0), -c->size.z);
    shape_to_world(c->mb, v0, c->L);
}

const Shape g_hall_shapes[] = {
    { "rack_board", sh_rack_board, 0 },
    { "rack_two", sh_rack_stand, 0 },
    { "rack_heavy", sh_rack_stand, 1 },
    { "rack_pole", sh_rack_stand, 2 },
    { "hall_lamp", sh_hall_lamp, 0 },
};
const u32 g_hall_shape_count = ARRAY_COUNT(g_hall_shapes);

bool hall_floor(f32 x, f32 z) { return fabsf(x) <= 32.0f && z >= -28.0f && z <= 24.0f; }
