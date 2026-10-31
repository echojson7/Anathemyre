// Остов: формы неровного бетона и мелочей на стенах (data/defs/objects.txt). Раскладка руины — заготовка
// ostov в data/defs/prefabs.txt. Цвета — материалы: выбеленный бетон (concrete_pale), ржавчина светлее (rust_pale)
#include "ostov.h"

#include "../core/rng.h"
#include "props.h"
#include "shapes.h"

// Уровни руины: пол, галерея — для высоты под ногами
#define FY  0.1f
#define GY  4.2f
// Пол руины (цоколь)
#define PX0 -99.0f
#define PX1 -61.5f
#define PZ0 -51.0f
#define PZ1 20.5f
// Лестница на площадку: от нижней ступени (z = ST_Z1) на север к верхней (ST_Z0)
#define ST_X0 -73.0f
#define ST_X1 -70.2f
#define ST_Z0 3.6f
#define ST_Z1 7.8f

// ---------------------------------------------------------------- шум

static f32 hash3(i32 x, i32 y, i32 z, u32 s)
{
    u32 h = (u32)x * 0x8da6b343u ^ (u32)y * 0xd8163841u ^ (u32)z * 0xcb1ab31fu ^ s * 0x165667b1u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (f32)(h & 0xFFFFFFu) / 16777216.0f;
}

// Шум значений 0..1
static f32 vnoise3(V3 p, u32 s)
{
    f32 fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
    i32 ix = (i32)fx, iy = (i32)fy, iz = (i32)fz;
    f32 tx = p.x - fx, ty = p.y - fy, tz = p.z - fz;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    tz = tz * tz * (3 - 2 * tz);
    f32 c[2][2];
    for (int k = 0; k < 2; k++)
        for (int j = 0; j < 2; j++)
            c[k][j] = lerpf(hash3(ix, iy + j, iz + k, s), hash3(ix + 1, iy + j, iz + k, s), tx);
    return lerpf(lerpf(c[0][0], c[0][1], ty), lerpf(c[1][0], c[1][1], ty), tz);
}

static f32 fbm3(V3 p, u32 s)
{
    return 0.5f * vnoise3(p, s) + 0.3f * vnoise3(v3_mul(p, 2.03f), s + 1) + 0.2f * vnoise3(v3_mul(p, 4.11f), s + 2);
}

static V3 vpos(const MeshBuilder *mb, u32 i) { return v3(mb->v[i].pos[0], mb->v[i].pos[1], mb->v[i].pos[2]); }

// Треугольник лицом в сторону out
static void tri_out(MeshBuilder *mb, u32 a, u32 b, u32 c, V3 out)
{
    V3 pa = vpos(mb, a);
    if (v3_dot(v3_cross(v3_sub(vpos(mb, b), pa), v3_sub(vpos(mb, c), pa)), out) < 0.0f) {
        u32 t = b;
        b = c;
        c = t;
    }
    mb_tri(mb, a, b, c);
}

// ---------------------------------------------------------------- неровный бетон

// Грани шестигранника: низ, верх, -Z, +X, +Z, -X
enum { F_BOT, F_TOP, F_ZN, F_XP, F_ZP, F_XN };
#define HF(f) (1u << (f))

// Углы: 0..3 снизу (x-z-, x+z-, x+z+, x-z+), 4..7 сверху в том же порядке.
// Грань — f0 f1 f2 f3, точка грани lerp(lerp(f0, f1, u), lerp(f3, f2, u), v), наружу cross(f1 - f0, f3 - f0)
static const u8 k_face[6][4] = { { 0, 1, 2, 3 }, { 4, 7, 6, 5 }, { 0, 4, 5, 1 }, { 1, 5, 6, 2 }, { 3, 2, 6, 7 }, { 0, 3, 7, 4 } };
// Оси u и v граней (0 X, 1 Y, 2 Z) и рёбра вдоль каждой оси
static const u8 k_face_axes[6][2] = { { 0, 2 }, { 2, 0 }, { 1, 0 }, { 1, 2 }, { 0, 1 }, { 2, 1 } };
static const u8 k_axis_edges[3][4][2] = {
    { { 0, 1 }, { 3, 2 }, { 4, 5 }, { 7, 6 } },
    { { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } },
    { { 0, 3 }, { 1, 2 }, { 5, 6 }, { 4, 7 } },
};

// Как ломать бетон
typedef struct Rough {
    f32 cell;    // шаг сетки в середине грани, м (у рёбер сетка гуще)
    f32 bump;    // волна по граням (неровная опалубка, наплывы), м
    f32 chip;    // глубина сколов на рёбрах, м
    f32 chip_w;  // ширина полосы сколов от ребра, м
    f32 brk;     // глубина рваного излома на сломанных гранях, м
    u8  hide;    // грани, которые не строятся (стоят на полу, упираются в плиту): HF(...)
    u8  broken;  // сломанные грани: рваный излом и торчащая арматура
    u32 seed;
} Rough;

static Rough rough(f32 cell, f32 bump, f32 chip, f32 chip_w, u8 hide, u32 seed)
{
    return (Rough){ .cell = cell, .bump = bump, .chip = chip, .chip_w = chip_w, .brk = 0.25f, .hide = hide, .seed = seed };
}

typedef struct Hex {
    V3  c[8];
    V3  n[6];
    f32 d[6];
} Hex;

// Доли длины ребра для узлов сетки: у концов гуще (там сколы), в середине шаг cell
static u32 grid_ts(f32 len, f32 cell, f32 *t, u32 max)
{
    u32 n = 0;
    if (len < 0.9f) {
        u32 k = (u32)CLAMP((i32)ceilf(len / 0.15f), 1, (i32)max - 1);
        for (u32 i = 0; i <= k; i++) t[n++] = (f32)i / (f32)k;
        return n;
    }
    static const f32 band[] = { 0.0f, 0.07f, 0.18f, 0.32f };
    for (u32 b = 0; b < 4; b++) t[n++] = band[b] / len;
    f32 inner = len - 2.0f * band[3];
    u32 k = (u32)CLAMP((i32)ceilf(inner / cell), 1, (i32)max - 9);
    for (u32 i = 1; i < k; i++) t[n++] = (band[3] + inner * (f32)i / (f32)k) / len;
    for (u32 b = 4; b-- > 0;) t[n++] = (len - band[b]) / len;
    return n;
}

// Сдвиг точки поверхности: внутрь по нормали каждой грани, на которой она лежит. Зависит только от
// точки, поэтому на общем ребре две грани сдвигают его одинаково и щелей нет
static V3 rough_disp(const Hex *h, const Rough *r, V3 p)
{
    f32 e[6];
    for (u32 f = 0; f < 6; f++) e[f] = h->d[f] - v3_dot(h->n[f], p);
    f32 spots = smoothstepf(0.3f, 0.75f, vnoise3(v3_mul(p, 1.6f), r->seed + 7));
    V3 D = v3(0, 0, 0);
    for (u32 f = 0; f < 6; f++) {
        if ((r->hide & HF(f)) || e[f] > 0.003f) continue;
        f32 in = r->bump * (fbm3(v3_mul(p, 0.9f), r->seed + f * 3) - 0.5f) * 2.0f;
        f32 k = 0.0f;
        for (u32 g = 0; g < 6; g++) {
            if (g == f || (r->hide & HF(g))) continue;
            bool bg = r->broken & HF(g);
            f32 cw = r->chip_w * (bg ? 2.0f : 1.0f);
            k = maxf(k, (bg ? 2.5f : 1.0f) * (1.0f - smoothstepf(0.0f, cw, e[g])));
        }
        // Рёбра всегда чуть сбиты, местами сколоты глубоко, кое-где выбит целый кусок угла
        f32 bite = smoothstepf(0.6f, 0.85f, vnoise3(v3_mul(p, 0.45f), r->seed + 9));
        in += r->chip * minf(k, 1.0f) * (0.2f + 0.8f * spots + 2.2f * bite) + r->chip * maxf(k - 1.0f, 0.0f);
        if (r->broken & HF(f)) in += r->brk * (0.2f + 0.8f * fbm3(v3_mul(p, 2.3f), r->seed + 50 + f));
        D = v3_sub(D, v3_mul(h->n[f], in));
    }
    return D;
}

static V3 bilerp(const V3 *q, f32 u, f32 v) { return v3_lerp(v3_lerp(q[0], q[1], u), v3_lerp(q[3], q[2], u), v); }

static void rebar_bar(MeshBuilder *mb, Rng *rng, V3 base, V3 out)
{
    V3 tip = v3_add(base, v3_mul(out, rng_range(rng, 0.15f, 0.8f)));
    mb_cylinder(mb, v3_sub(base, v3_mul(out, 0.3f)), tip, 0.022f, 4);
    // Часть прутьев загнута вниз или вбок
    if (rng_f32(rng) < 0.6f) {
        V3 bend = v3_norm(v3(rng_range(rng, -1, 1), rng_range(rng, -1.5f, 0.3f), rng_range(rng, -1, 1)));
        mb_cylinder(mb, tip, v3_add(tip, v3_mul(bend, rng_range(rng, 0.2f, 0.7f))), 0.022f, 4);
    }
}

// Неровный бетонный шестигранник с плоскими гранями (брус, плита, трапеция контрфорса)
static void rough_hex(MeshBuilder *mb, const V3 c[8], const Rough *r)
{
    Hex h;
    for (u32 i = 0; i < 8; i++) h.c[i] = c[i];
    for (u32 f = 0; f < 6; f++) {
        V3 f0 = c[k_face[f][0]], f1 = c[k_face[f][1]], f3 = c[k_face[f][3]];
        h.n[f] = v3_norm(v3_cross(v3_sub(f1, f0), v3_sub(f3, f0)));
        h.d[f] = v3_dot(h.n[f], f0);
    }
    enum { TMAX = 96 };
    f32 ts[3][TMAX];
    u32 tn[3];
    for (u32 a = 0; a < 3; a++) {
        f32 len = 0.0f;
        for (u32 k = 0; k < 4; k++) len = maxf(len, v3_len(v3_sub(c[k_axis_edges[a][k][1]], c[k_axis_edges[a][k][0]])));
        tn[a] = grid_ts(len, r->cell, ts[a], TMAX);
    }
    for (u32 f = 0; f < 6; f++) {
        if (r->hide & HF(f)) continue;
        V3 q[4] = { c[k_face[f][0]], c[k_face[f][1]], c[k_face[f][2]], c[k_face[f][3]] };
        u32 au = k_face_axes[f][0], av = k_face_axes[f][1];
        u32 nu = tn[au], nv = tn[av];
        if (!mb_reserve(mb, nu * nv, (nu - 1) * (nv - 1) * 6)) return;
        u32 v0 = mb->vcount, i0 = mb->icount;
        for (u32 j = 0; j < nv; j++)
            for (u32 i = 0; i < nu; i++) {
                V3 p = bilerp(q, ts[au][i], ts[av][j]);
                mb_vertex(mb, v3_add(p, rough_disp(&h, r, p)), h.n[f]);
            }
        for (u32 j = 0; j + 1 < nv; j++)
            for (u32 i = 0; i + 1 < nu; i++) {
                u32 a = v0 + j * nu + i, b = a + 1, cc = a + nu + 1, d = a + nu;
                mb_tri(mb, a, b, cc);
                mb_tri(mb, a, cc, d);
            }
        mb_smooth_normals(mb, v0, i0);
    }
    // Арматура из изломов: прутья с шагом ~25 см, торчат и гнутся
    if (!r->broken) return;
    V3 col = mb->color;
    u8 mat = mb->mat;
    Rng rng;
    rng_seed(&rng, r->seed, 3);
    prop_mat(mb, g_mat.rust_pale);
    for (u32 f = 0; f < 6; f++) {
        if (!(r->broken & HF(f))) continue;
        V3 q[4] = { c[k_face[f][0]], c[k_face[f][1]], c[k_face[f][2]], c[k_face[f][3]] };
        f32 area = v3_len(v3_cross(v3_sub(q[1], q[0]), v3_sub(q[3], q[0])));
        u32 bars = (u32)CLAMP((i32)(area * 5.0f), 2, 18);
        for (u32 k = 0; k < bars; k++) {
            V3 p = bilerp(q, rng_range(&rng, 0.12f, 0.88f), rng_range(&rng, 0.12f, 0.88f));
            p = v3_add(p, rough_disp(&h, r, p));
            rebar_bar(mb, &rng, p, h.n[f]);
        }
    }
    mb_set(mb, col, (Material)mat, BONE_NONE);
}

// Брус между углами lo и hi, оси мира
static void rough_box(MeshBuilder *mb, V3 lo, V3 hi, const Rough *r)
{
    V3 c[8];
    for (u32 i = 0; i < 8; i++) {
        bool xp = i == 1 || i == 2 || i == 5 || i == 6, zp = i == 2 || i == 3 || i == 6 || i == 7;
        c[i] = v3(xp ? hi.x : lo.x, i >= 4 ? hi.y : lo.y, zp ? hi.z : lo.z);
    }
    rough_hex(mb, c, r);
}

// Брус с центром c и полуосями ax, ay, az (правая тройка)
static void rough_frame(MeshBuilder *mb, V3 c, V3 ax, V3 ay, V3 az, const Rough *r)
{
    V3 k[8];
    for (u32 i = 0; i < 8; i++) {
        f32 sx = i == 1 || i == 2 || i == 5 || i == 6 ? 1.0f : -1.0f, sz = i == 2 || i == 3 || i == 6 || i == 7 ? 1.0f : -1.0f;
        f32 sy = i >= 4 ? 1.0f : -1.0f;
        k[i] = v3_add(c, v3_add(v3_mul(ax, sx), v3_add(v3_mul(ay, sy), v3_mul(az, sz))));
    }
    rough_hex(mb, k, r);
}

// Обломок: брус под случайным поворотом, сильно сбитый
static void chunk(MeshBuilder *mb, Rng *rng, V3 c, f32 s)
{
    f32 ry = rng_range(rng, 0, 2 * PI_F), rx = rng_range(rng, -0.5f, 0.5f), rz = rng_range(rng, -0.5f, 0.5f);
    M4 m = m4_mul(m4_rot_y(ry), m4_mul(m4_rot_x(rx), m4_rot_z(rz)));
    V3 sz = v3(s * rng_range(rng, 0.8f, 1.8f), s * rng_range(rng, 0.5f, 1.0f), s * rng_range(rng, 0.7f, 1.4f));
    Rough r = rough(0.5f, 0.04f * s, 0.3f * s, 0.45f * s, 0, rng_below(rng, 1u << 30));
    // Крупные обломки с одной стороны рваные, с прутьями
    if (s > 0.6f && rng_f32(rng) < 0.5f) {
        r.broken = HF(rng_f32(rng) < 0.5f ? F_XP : F_ZN);
        r.brk = 0.12f * s;
    }
    rough_frame(mb, c, m4_dir(m, v3(sz.x * 0.5f, 0, 0)), m4_dir(m, v3(0, sz.y * 0.5f, 0)), m4_dir(m, v3(0, 0, sz.z * 0.5f)), &r);
}

// ---------------------------------------------------------------- плита с проломами

// Клетка плиты павильона в осях плиты (середина верха — 0): 0 цела, 1 пролом (рваный край), 2 круглое окно (ровный край)
#define OCULUS_X 1.0f
#define OCULUS_Z 1.5f
#define OCULUS_R 1.6f

static u8 roof_cell(f32 x, f32 z)
{
    f32 dx = x - OCULUS_X, dz = z - OCULUS_Z;
    if (dx * dx + dz * dz < (OCULUS_R + 0.2f) * (OCULUS_R + 0.2f)) return 2;
    f32 jit = (vnoise3(v3(x * 0.45f, 0.0f, z * 0.45f), 91) - 0.5f) * 0.9f + (vnoise3(v3(x * 1.3f, 0.0f, z * 1.3f), 92) - 0.5f) * 0.35f;
    f32 ex = (x + 9.0f) / 5.0f, ez = (z + 2.3f) / 3.4f;
    if (ex * ex + ez * ez + jit < 1.0f) return 1;
    // Западный край над проломом обвалился
    if (x < -12.0f + jit * 2.0f && z > -5.3f + jit * 2.5f && z < 1.3f + jit * 2.5f) return 1;
    return 0;
}

#define SLAB_N 80

static u8  g_cell[SLAB_N][SLAB_N];
static u32 g_vt[SLAB_N + 1][SLAB_N + 1], g_vb[SLAB_N + 1][SLAB_N + 1];

// Плита x0..x1, z0..z1 сеткой cell: верх top, толщина thick, низ провисает и бугрится. В проломах край
// рваный (узлы сдвинуты, плита к краю тоньше, торчат прутья), у круглого окна ровный
static void broken_slab(MeshBuilder *mb, Rng *rng, f32 x0, f32 x1, f32 z0, f32 z1, f32 top, f32 thick, f32 cell)
{
    u32 nx = MIN((u32)((x1 - x0) / cell + 0.5f), (u32)SLAB_N), nz = MIN((u32)((z1 - z0) / cell + 0.5f), (u32)SLAB_N);
    for (u32 j = 0; j < nz; j++)
        for (u32 i = 0; i < nx; i++) g_cell[j][i] = roof_cell(x0 + ((f32)i + 0.5f) * cell, z0 + ((f32)j + 0.5f) * cell);
#define CELL(i, j) ((i) < 0 || (j) < 0 || (i) >= (i32)nx || (j) >= (i32)nz ? 255 : g_cell[j][i])
    // Узел: сдвиг у рваного края и износ (доля пролома рядом)
    static V3 pt[SLAB_N + 1][SLAB_N + 1], pb[SLAB_N + 1][SLAB_N + 1], jit2[SLAB_N + 1][SLAB_N + 1];
    for (i32 j = 0; j <= (i32)nz; j++)
        for (i32 i = 0; i <= (i32)nx; i++) {
            u32 brk_adj = 0, holes = 0;
            for (i32 dj = -1; dj <= 0; dj++)
                for (i32 di = -1; di <= 0; di++) brk_adj += CELL(i + di, j + dj) == 1;
            for (i32 dj = -2; dj <= 1; dj++)
                for (i32 di = -2; di <= 1; di++) holes += CELL(i + di, j + dj) == 1;
            f32 x = x0 + (f32)i * cell, z = z0 + (f32)j * cell;
            V3 q = v3(x, 0.0f, z);
            f32 jx = 0.0f, jz = 0.0f;
            if (brk_adj) {
                jx = (vnoise3(v3_mul(q, 2.7f), 11) - 0.5f) * cell * 0.95f;
                jz = (vnoise3(v3_mul(q, 2.7f), 12) - 0.5f) * cell * 0.95f;
            }
            f32 ero = (f32)holes / 16.0f;
            // Низ плиты: провис, наплывы бетона и выбоины, где бетон отслоился до арматуры
            f32 sag = 0.2f * (fbm3(v3(x * 0.3f, 0.0f, z * 0.3f), 13) - 0.5f) * 2.0f +
                      0.12f * (fbm3(v3(x * 1.1f, 0.0f, z * 1.1f), 14) - 0.5f) * 2.0f;
            f32 spall = vnoise3(v3(x * 0.55f, 0.0f, z * 0.55f), 20);
            if (spall > 0.68f) sag -= (spall - 0.68f) / 0.32f * 0.22f;
            f32 ty = top + 0.03f * (vnoise3(v3_mul(q, 1.1f), 15) - 0.5f) - ero * 0.45f * vnoise3(v3_mul(q, 3.1f), 16);
            f32 by = top - thick - sag + ero * 0.4f * vnoise3(v3_mul(q, 3.3f), 17);
            if (by > ty - 0.18f) by = ty - 0.18f;
            pt[j][i] = v3(x + jx, ty, z + jz);
            pb[j][i] = v3(x + jx, by, z + jz);
            jit2[j][i] = brk_adj ? v3((vnoise3(v3_mul(q, 4.1f), 18) - 0.5f) * 0.35f, 0.0f, (vnoise3(v3_mul(q, 4.1f), 19) - 0.5f) * 0.35f)
                                 : v3(0, 0, 0);
            g_vt[j][i] = g_vb[j][i] = 0xFFFFFFFFu;
        }
    // Верх и низ: общие узлы, гладкие нормали
    for (u32 pass = 0; pass < 2; pass++) {
        u32 v0 = mb->vcount, i0 = mb->icount;
        V3 out = v3(0, pass == 0 ? 1.0f : -1.0f, 0);
        for (u32 j = 0; j < nz; j++)
            for (u32 i = 0; i < nx; i++) {
                if (g_cell[j][i] != 0) continue;
                if (!mb_reserve(mb, 4, 6)) return;
                u32 id[4];
                static const u32 di[4] = { 0, 1, 1, 0 }, dj[4] = { 0, 0, 1, 1 };
                for (u32 k = 0; k < 4; k++) {
                    u32 ii = i + di[k], jj = j + dj[k];
                    u32 *slot = pass == 0 ? &g_vt[jj][ii] : &g_vb[jj][ii];
                    if (*slot == 0xFFFFFFFFu) *slot = mb_vertex(mb, pass == 0 ? pt[jj][ii] : pb[jj][ii], out);
                    id[k] = *slot;
                }
                tri_out(mb, id[0], id[1], id[2], out);
                tri_out(mb, id[0], id[2], id[3], out);
            }
        mb_smooth_normals(mb, v0, i0);
    }
    // Торцы: у пролома рваные (средний ряд узлов сдвинут), снаружи ровные
    for (i32 j = 0; j < (i32)nz; j++)
        for (i32 i = 0; i < (i32)nx; i++) {
            if (g_cell[j][i] != 0) continue;
            static const i32 ni[4] = { -1, 1, 0, 0 }, nj[4] = { 0, 0, -1, 1 };
            for (u32 s = 0; s < 4; s++) {
                u8 nb = CELL(i + ni[s], j + nj[s]);
                if (nb == 0) continue;
                // Узлы ребра клетки со стороны соседа
                i32 ai, aj, bi, bj;
                if (ni[s] != 0) {
                    ai = bi = ni[s] < 0 ? i : i + 1;
                    aj = j;
                    bj = j + 1;
                } else {
                    aj = bj = nj[s] < 0 ? j : j + 1;
                    ai = i;
                    bi = i + 1;
                }
                V3 out = v3((f32)ni[s], 0, (f32)nj[s]);
                if (!mb_reserve(mb, 6, 12)) return;
                V3 at = pt[aj][ai], bt = pt[bj][bi], ab = pb[aj][ai], bb = pb[bj][bi];
                V3 am = v3_add(v3_lerp(at, ab, 0.5f), jit2[aj][ai]), bm = v3_add(v3_lerp(bt, bb, 0.5f), jit2[bj][bi]);
                u32 i0 = mb->icount, v0 = mb->vcount;
                u32 a0 = mb_vertex(mb, at, out), b0 = mb_vertex(mb, bt, out);
                u32 a1 = mb_vertex(mb, am, out), b1 = mb_vertex(mb, bm, out);
                u32 a2 = mb_vertex(mb, ab, out), b2 = mb_vertex(mb, bb, out);
                tri_out(mb, a0, b0, b1, out);
                tri_out(mb, a0, b1, a1, out);
                tri_out(mb, a1, b1, b2, out);
                tri_out(mb, a1, b2, a2, out);
                mb_smooth_normals(mb, v0, i0);
                // Прутья из рваного края, часть длинных висит вниз
                if (nb == 1 && rng_f32(rng) < 0.45f) {
                    V3 col = mb->color;
                    prop_mat(mb, g_mat.rust_pale);
                    V3 p = v3_lerp(am, bm, rng_f32(rng));
                    if (rng_f32(rng) < 0.25f) {
                        V3 tip = v3_add(p, v3(out.x * 0.3f, -rng_range(rng, 0.8f, 2.0f), out.z * 0.3f));
                        mb_cylinder(mb, v3_sub(p, v3_mul(out, 0.3f)), v3_add(p, v3_mul(out, 0.25f)), 0.022f, 4);
                        mb_cylinder(mb, v3_add(p, v3_mul(out, 0.25f)), tip, 0.022f, 4);
                    } else rebar_bar(mb, rng, p, out);
                    mb_set(mb, col, MAT_CONCRETE, BONE_NONE);
                }
            }
        }
#undef CELL
}

// Труба-обечайка круглого окна: стенки внутри и снаружи, кольца сверху и снизу
static void collar(MeshBuilder *mb, V3 c, f32 rin, f32 rout, f32 y0, f32 y1, u32 segs)
{
    for (u32 part = 0; part < 4; part++) {
        if (!mb_reserve(mb, (segs + 1) * 2, segs * 6)) return;
        u32 v0 = mb->vcount, i0 = mb->icount;
        for (u32 k = 0; k <= segs; k++) {
            f32 a = 2.0f * PI_F * (f32)k / (f32)segs;
            // Край чуть неровный: бетон лили на месте
            f32 w = 1.0f + 0.03f * (vnoise3(v3(cosf(a) * 3.0f, 0.0f, sinf(a) * 3.0f), 21) - 0.5f);
            V3 d = v3(cosf(a), 0, sinf(a));
            V3 p0, p1;
            if (part < 2) {
                f32 r = (part == 0 ? rout : rin) * w;
                p0 = v3(c.x + d.x * r, y0, c.z + d.z * r);
                p1 = v3(c.x + d.x * r, y1, c.z + d.z * r);
            } else {
                f32 y = part == 2 ? y1 : y0;
                p0 = v3(c.x + d.x * rin * w, y, c.z + d.z * rin * w);
                p1 = v3(c.x + d.x * rout * w, y, c.z + d.z * rout * w);
            }
            mb_vertex(mb, p0, d);
            mb_vertex(mb, p1, d);
        }
        for (u32 k = 0; k < segs; k++) {
            u32 a = v0 + k * 2, b = a + 1, a1 = a + 2, b1 = a + 3;
            V3 mid = v3_lerp(vpos(mb, a), vpos(mb, b1), 0.5f);
            V3 radial = v3_norm(v3(mid.x - c.x, 0, mid.z - c.z));
            V3 out = part == 0 ? radial : part == 1 ? v3_mul(radial, -1.0f) : v3(0, part == 2 ? 1.0f : -1.0f, 0);
            tri_out(mb, a, b, b1, out);
            tri_out(mb, a, b1, a1, out);
        }
        mb_smooth_normals(mb, v0, i0);
    }
}

// ---------------------------------------------------------------- песок, трубы, кабели

// Нанос песка у стены: по стене (u) на ±a, от стены (вдоль n) до b, высота h у стены
static void drift(MeshBuilder *mb, V3 c, V3 u, V3 n, f32 a, f32 b, f32 h, u32 seed)
{
    u32 nu = (u32)CLAMP((i32)(2.0f * a / 0.4f), 2, 60), nv = (u32)CLAMP((i32)(b / 0.35f), 2, 30);
    if (!mb_reserve(mb, (nu + 1) * (nv + 1), nu * nv * 6)) return;
    prop_mat(mb, g_mat.sand);
    u32 v0 = mb->vcount, i0 = mb->icount;
    for (u32 j = 0; j <= nv; j++)
        for (u32 i = 0; i <= nu; i++) {
            f32 s = (f32)i / (f32)nu * 2.0f - 1.0f, t = (f32)j / (f32)nv;
            V3 p = v3_add(c, v3_add(v3_mul(u, s * a), v3_mul(n, t * b)));
            f32 ripple = 0.85f + 0.3f * vnoise3(v3_mul(p, 0.8f), seed);
            f32 y = h * (1.0f - s * s) * (1.0f - t) * (1.0f - t) * ripple;
            p.y += (i == 0 || i == nu || j == nv) ? -0.04f : y + 0.01f;
            mb_vertex(mb, p, v3(0, 1, 0));
        }
    for (u32 j = 0; j < nv; j++)
        for (u32 i = 0; i < nu; i++) {
            u32 a0 = v0 + j * (nu + 1) + i, b0 = a0 + 1, a1 = a0 + nu + 1, b1 = a1 + 1;
            tri_out(mb, a0, b0, b1, v3(0, 1, 0));
            tri_out(mb, a0, b1, a1, v3(0, 1, 0));
        }
    mb_smooth_normals(mb, v0, i0);
}

// Холм песка и пыли (под проломом, у входа)
static void mound(MeshBuilder *mb, V3 c, f32 rad, f32 h, u32 seed)
{
    u32 n = 18;
    if (!mb_reserve(mb, (n + 1) * (n + 1), n * n * 6)) return;
    prop_mat(mb, g_mat.sand);
    u32 v0 = mb->vcount, i0 = mb->icount;
    for (u32 j = 0; j <= n; j++)
        for (u32 i = 0; i <= n; i++) {
            f32 x = ((f32)i / (f32)n * 2.0f - 1.0f), z = ((f32)j / (f32)n * 2.0f - 1.0f);
            V3 p = v3(c.x + x * rad, c.y, c.z + z * rad);
            f32 r = sqrtf(x * x + z * z) * (0.85f + 0.3f * vnoise3(v3_mul(p, 0.7f), seed));
            f32 y = r < 1.0f ? h * (1.0f - r * r) * (1.0f - r * r) : 0.0f;
            p.y += y > 0.0f ? y + 0.01f : -0.04f;
            mb_vertex(mb, p, v3(0, 1, 0));
        }
    for (u32 j = 0; j < n; j++)
        for (u32 i = 0; i < n; i++) {
            u32 a0 = v0 + j * (n + 1) + i, b0 = a0 + 1, a1 = a0 + n + 1, b1 = a1 + 1;
            tri_out(mb, a0, b0, b1, v3(0, 1, 0));
            tri_out(mb, a0, b1, a1, v3(0, 1, 0));
        }
    mb_smooth_normals(mb, v0, i0);
}

// Решётка вентиляции: рама и косые планки, на стене с нормалью n (по оси X или Z)
static void vent(MeshBuilder *mb, Rng *rng, V3 c, V3 n, f32 w, f32 h)
{
    V3 u = v3_norm(v3_cross(v3(0, 1, 0), n));
    mb_set(mb, prop_shade(rng, mat_c(g_mat.rust_pale), 0.15f), MAT_RUST, BONE_NONE);
    V3 back = v3_sub(c, v3_mul(n, 0.02f));
    // Рама
    for (int s = -1; s <= 1; s += 2) {
        mb_box(mb, m4_mul(m4_translate(v3_add(back, v3_mul(u, (f32)s * w * 0.5f))), m4_mul(m4_rot_y(atan2f(n.x, n.z)), m4_scale(v3(0.06f, h, 0.1f)))));
        mb_box(mb, m4_mul(m4_translate(v3_add(back, v3(0, (f32)s * h * 0.5f, 0))), m4_mul(m4_rot_y(atan2f(n.x, n.z)), m4_scale(v3(w + 0.06f, 0.06f, 0.1f)))));
    }
    // Планки; пара выбита
    mb_set(mb, prop_shade(rng, mat_c(g_mat.iron), 0.1f), MAT_METAL, BONE_NONE);
    for (f32 y = -h * 0.5f + 0.1f; y < h * 0.5f - 0.05f; y += 0.12f) {
        if (rng_f32(rng) < 0.12f) continue;
        M4 m = m4_mul(m4_translate(v3_add(back, v3(0, y, 0))), m4_mul(m4_rot_y(atan2f(n.x, n.z)), m4_mul(m4_rot_x(0.6f), m4_scale(v3(w, 0.015f, 0.1f)))));
        mb_box(mb, m);
    }
    // За решёткой темно
    mb_set(mb, v3(0.004f, 0.004f, 0.004f), MAT_EMISSIVE, BONE_NONE);
    mb_quad(mb, v3_sub(c, v3_mul(n, 0.06f)), v3_mul(u, w * 0.5f), v3(0, h * 0.5f, 0));
}

// Стальная плита на болтах (закладная), иногда с обрывком кронштейна
static void plate(MeshBuilder *mb, Rng *rng, V3 c, V3 n, f32 w, f32 h)
{
    V3 u = v3_norm(v3_cross(v3(0, 1, 0), n));
    f32 yaw = atan2f(n.x, n.z);
    mb_set(mb, prop_shade(rng, mat_c(g_mat.rust_pale), 0.2f), MAT_RUST, BONE_NONE);
    mb_box(mb, m4_mul(m4_translate(v3_add(c, v3_mul(n, 0.015f))), m4_mul(m4_rot_y(yaw), m4_scale(v3(w, h, 0.03f)))));
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2) {
            V3 b = v3_add(c, v3_add(v3_mul(u, (f32)i * (w * 0.5f - 0.07f)), v3(0, (f32)j * (h * 0.5f - 0.07f), 0)));
            mb_cylinder(mb, v3_add(b, v3_mul(n, 0.02f)), v3_add(b, v3_mul(n, 0.07f)), 0.03f, 6);
        }
    if (rng_f32(rng) < 0.5f) {
        mb_set(mb, prop_shade(rng, mat_c(g_mat.iron), 0.1f), MAT_METAL, BONE_NONE);
        V3 a = v3_add(c, v3_mul(n, 0.03f));
        mb_box(mb, m4_mul(m4_translate(v3_add(a, v3_mul(n, 0.25f))), m4_mul(m4_rot_y(yaw), m4_scale(v3(0.08f, 0.08f, 0.5f)))));
        mb_box(mb, m4_mul(m4_translate(v3_add(a, v3_add(v3_mul(n, 0.48f), v3(0, -0.15f, 0)))), m4_mul(m4_rot_y(yaw), m4_scale(v3(0.08f, 0.3f, 0.08f)))));
    }
}

// Коробка щита с трубкой-кабелем вверх
static void junction(MeshBuilder *mb, Rng *rng, V3 c, V3 n, f32 up)
{
    f32 yaw = atan2f(n.x, n.z);
    mb_set(mb, prop_shade(rng, mat_c(g_mat.iron), 0.15f), MAT_METAL, BONE_NONE);
    mb_box(mb, m4_mul(m4_translate(v3_add(c, v3_mul(n, 0.1f))), m4_mul(m4_rot_y(yaw), m4_scale(v3(0.45f, 0.6f, 0.2f)))));
    mb_set(mb, prop_shade(rng, mat_c(g_mat.rust_pale), 0.15f), MAT_RUST, BONE_NONE);
    mb_box(mb, m4_mul(m4_translate(v3_add(c, v3_add(v3_mul(n, 0.21f), v3(0, 0.05f, 0)))), m4_mul(m4_rot_y(yaw), m4_scale(v3(0.4f, 0.5f, 0.02f)))));
    V3 t0 = v3_add(c, v3_add(v3_mul(n, 0.08f), v3(0, 0.3f, 0)));
    mb_cylinder(mb, t0, v3_add(t0, v3(0, up, 0)), 0.03f, 6);
    for (f32 y = 0.8f; y < up; y += 1.2f) mb_box(mb, m4_mul(m4_translate(v3_add(t0, v3(0, y, 0))), m4_mul(m4_rot_y(yaw), m4_scale(v3(0.1f, 0.04f, 0.1f)))));
}

// Слив из края плиты: ржавый носик
static void scupper(MeshBuilder *mb, Rng *rng, V3 c, V3 out)
{
    mb_set(mb, prop_shade(rng, mat_c(g_mat.rust_pale), 0.15f), MAT_RUST, BONE_NONE);
    V3 tip = v3_add(c, v3_add(v3_mul(out, 0.5f), v3(0, -0.12f, 0)));
    mb_cylinder(mb, v3_sub(c, v3_mul(out, 0.2f)), tip, 0.07f, 8);
}

// ---------------------------------------------------------------- формы объектов

// Как ломать бетон у каждого вида: шаг сетки, волна, сколы, полоса сколов, излом
typedef struct RoughKind {
    f32 cell, bump, chip, chip_w, brk;
} RoughKind;
static const RoughKind k_rough[] = {
    { 1.3f, 0.06f, 0.14f, 0.35f, 0.45f },  // 0 стена
    { 0.8f, 0.03f, 0.10f, 0.20f, 0.35f },  // 1 балка, колонна, плита галереи, пояс
    { 0.5f, 0.02f, 0.06f, 0.12f, 0.25f },  // 2 пилястра, ступень, скамья, плита пола
    { 1.0f, 0.02f, 0.15f, 0.30f, 0.25f },  // 3 цоколь
    { 0.55f, 0.035f, 0.12f, 0.25f, 0.35f },  // 4 обломок: кусок плиты, колонны, балки
    { 0.6f, 0.02f, 0.08f, 0.18f, 0.25f },  // 5 капитель, контрфорс
};

static Rough kind_rough(const ShapeCtx *c, u32 kind)
{
    const RoughKind *k = &k_rough[kind];
    Rough r = rough(k->cell, k->bump, k->chip, k->chip_w, c->var[0], rng_below(c->r, 1u << 30));
    r.brk = k->brk;
    return r;
}

// Брус: точка — середина, var[0] — невидимые грани, var[1] — сломанные (биты: низ, верх, −z, +x, +z, −x)
static void sh_rough(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    prop_paint(mb, c->r, c->mat[0], c->tint);
    Rough r = kind_rough(c, c->arg);
    r.broken = c->var[1];
    u32 v0 = mb->vcount;
    V3 h = v3_mul(c->size, 0.5f);
    rough_box(mb, v3_mul(h, -1.0f), h, &r);
    shape_to_world(mb, v0, c->L);
}

// Обломок вдоль +x с наклоном var[1] градусов (больше нуля — конец вверх): size — ширина, высота, длина.
// var[0] — сломанные грани (−z у начала, +z у конца)
static void sh_piece(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    prop_paint(mb, c->r, c->mat[0], c->tint);
    Rough r = kind_rough(c, 4);
    r.hide = 0;
    r.broken = c->var[0];
    f32 p = shape_deg(c->var[1]);
    V3 fwd = v3_add(v3_mul(m4_dir(c->L, v3(1, 0, 0)), cosf(p)), v3(0, sinf(p), 0));
    V3 up = fabsf(fwd.y) > 0.95f ? v3(1, 0, 0) : v3(0, 1, 0);
    V3 side = v3_norm(v3_cross(up, fwd));
    V3 upv = v3_cross(fwd, side);
    rough_frame(mb, c->pos, v3_mul(side, c->size.x * 0.5f), v3_mul(upv, c->size.y * 0.5f), v3_mul(fwd, c->size.z * 0.5f), &r);
}

// Капитель: низ — сечение колонны, верх расширен до size; точка — середина. var[0] — невидимые грани
static void sh_capital(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    prop_paint(mb, c->r, c->mat[0], c->tint);
    Rough r = kind_rough(c, 5);
    f32 tx = c->size.x * 0.5f, tz = c->size.z * 0.5f, bx = tx * 0.62f, bz = tz * 0.65f, h = c->size.y * 0.5f;
    V3 k[8] = { v3(-bx, -h, -bz), v3(bx, -h, -bz), v3(bx, -h, bz), v3(-bx, -h, bz), v3(-tx, h, -tz), v3(tx, h, -tz), v3(tx, h, tz), v3(-tx, h, tz) };
    u32 v0 = mb->vcount;
    rough_hex(mb, k, &r);
    shape_to_world(mb, v0, c->L);
}

// Контрфорс: стена, у которой грань −x наклонена внутрь (верх уже низа на 37 %); точка — середина
static void sh_buttress(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    prop_paint(mb, c->r, c->mat[0], c->tint);
    Rough r = kind_rough(c, 5);
    V3 h = v3_mul(c->size, 0.5f);
    f32 tx = h.x - c->size.x * 0.37f;
    V3 k[8] = { v3(-h.x, -h.y, -h.z), v3(h.x, -h.y, -h.z), v3(h.x, -h.y, h.z), v3(-h.x, -h.y, h.z),
                v3(-tx, h.y, -h.z), v3(h.x, h.y, -h.z), v3(h.x, h.y, h.z), v3(-tx, h.y, h.z) };
    u32 v0 = mb->vcount;
    rough_hex(mb, k, &r);
    shape_to_world(mb, v0, c->L);
}

// Плита пола, приподнятая и чуть сдвинутая: size — длина, толщина, ширина; точка — середина
static void sh_floor_plate(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    prop_paint(mb, c->r, c->mat[0], c->tint);
    Rng *rng = c->r;
    M4 m = m4_mul(m4_rot_y(c->yaw), m4_mul(m4_rot_x(rng_range(rng, -0.06f, 0.06f)), m4_rot_z(rng_range(rng, -0.06f, 0.06f))));
    Rough r = kind_rough(c, 2);
    r.hide = HF(F_BOT);
    rough_frame(mb, c->pos, m4_dir(m, v3(c->size.x * 0.5f, 0, 0)), m4_dir(m, v3(0, c->size.y * 0.5f, 0)), m4_dir(m, v3(0, 0, c->size.z * 0.5f)), &r);
}

// Плита павильона на колоннах: пролом с обвалом и прутьями, круглое окно с обечайкой, крюки снизу.
// Точка — середина верха, size — ширина, толщина, глубина
static void sh_roof(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    Rng *rng = c->r;
    u32 v0 = mb->vcount;
    f32 w = c->size.x * 0.5f, d = c->size.z * 0.5f, t = c->size.y;
    prop_mat(mb, c->mat[0]);
    broken_slab(mb, rng, -w, w, -d, d, 0.0f, t, 0.5f);
    mb_set(mb, prop_shade(rng, mat_c(c->mat[0]), 0.04f), MAT_CONCRETE, BONE_NONE);
    collar(mb, v3(OCULUS_X, 0, OCULUS_Z), OCULUS_R, OCULUS_R + 0.45f, -t - 0.25f, 0.9f, 32);
    // Прутья, свисающие из пролома длинными плетями
    prop_mat(mb, g_mat.rust_pale);
    for (u32 k = 0; k < 10; k++) {
        V3 a = v3(rng_range(rng, -12.0f, -5.0f), 0.4f - t, rng_range(rng, -5.2f, 0.9f));
        if (roof_cell(a.x, a.z) != 0) continue;
        V3 b = v3_add(a, v3(rng_range(rng, -1.2f, 1.2f), -rng_range(rng, 1.0f, 3.5f), rng_range(rng, -0.6f, 0.6f)));
        mb_cylinder(mb, a, b, 0.022f, 4);
    }
    // Крюки под плитой
    mb_set(mb, prop_shade(rng, mat_c(g_mat.rust_pale), 0.15f), MAT_RUST, BONE_NONE);
    for (u32 k = 0; k < 7; k++) {
        f32 x = rng_range(rng, -4.0f, 12.0f), z = rng_range(rng, -9.5f, 9.5f);
        if (roof_cell(x, z) != 0) continue;
        mb_cylinder(mb, v3(x, 0.1f - t, z), v3(x, -0.25f - t, z), 0.025f, 4);
        mb_cylinder(mb, v3(x, -0.25f - t, z), v3(x + 0.08f, -0.32f - t, z), 0.025f, 4);
    }
    shape_to_world(mb, v0, c->L);
}

// Тонкая круглая колонна: ствол с поясом внизу, сбитый край пояса; точка — низ, size.y — высота
static void sh_column_round(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    mb_set(mb, prop_shade(c->r, mat_c(c->mat[0]), 0.04f), MAT_CONCRETE, BONE_NONE);
    f32 h = c->size.y;
    V2 prof[] = { { 0.0f, 0.42f }, { 0.35f, 0.42f }, { 0.4f, 0.33f }, { 3.0f, 0.31f }, { h - 0.5f, 0.3f }, { h - 0.3f, 0.4f }, { h, 0.4f } };
    u32 v0 = mb->vcount, i0 = mb->icount;
    mb_lathe(mb, v3(0, 0, 0), v3(0, h, 0), prof, ARRAY_COUNT(prof), 18, 1.0f, 1.0f);
    for (u32 i = v0; i < mb->vcount; i++) {
        V3 p = vpos(mb, i);
        if (p.y > 0.45f) continue;
        f32 k = vnoise3(v3_mul(p, 3.0f), 41);
        if (k < 0.45f) continue;
        V3 dn = v3_norm(v3(p.x, 0, p.z));
        mb->v[i].pos[0] -= dn.x * (k - 0.45f) * 0.25f;
        mb->v[i].pos[2] -= dn.z * (k - 0.45f) * 0.25f;
    }
    mb_smooth_normals(mb, v0, i0);
    shape_to_world(mb, v0, c->L);
}

// Холм песка: size.x — радиус, size.y — высота
static void sh_mound(const ShapeCtx *c) { mound(c->mb, c->pos, c->size.x, c->size.y, 31 + rng_below(c->r, 1000)); }

// Нанос песка у стены: вдоль +x на ±size.x, от стены вдоль +z на size.z (меньше нуля — вдоль −z), высота size.y
static void sh_drift(const ShapeCtx *c)
{
    f32 b = c->size.z;
    drift(c->mb, c->pos, m4_dir(c->L, v3(1, 0, 0)), m4_dir(c->L, v3(0, 0, b < 0.0f ? -1.0f : 1.0f)), c->size.x, fabsf(b), c->size.y,
          51 + rng_below(c->r, 1000));
}

// Россыпь обломков: size.x — радиус, size.y и size.z — самый крупный и мелкий, var[0] — сколько.
// Материалы 0 и 1 вперемешку
static void sh_rubble(const ShapeCtx *c)
{
    Rng *rng = c->r;
    f32 rad = c->size.x;
    for (u32 i = 0; i < c->var[0]; i++) {
        f32 a = rng_range(rng, 0, 2 * PI_F), d = rad * sqrtf(rng_f32(rng));
        f32 s = rng_range(rng, c->size.z, c->size.y) * (1.0f - 0.5f * d / rad);
        mb_set(c->mb, prop_shade(rng, mat_c(c->mat[rng_f32(rng) < 0.5f ? 0 : 1]), 0.08f), MAT_CONCRETE, BONE_NONE);
        chunk(c->mb, rng, v3(c->pos.x + cosf(a) * d, c->pos.y + s * 0.2f, c->pos.z + sinf(a) * d), s);
    }
}

// Люк в стене: тёмный проём и ржавая створка, приоткрытая; лицом вдоль +z
static void sh_hatch(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    u32 v0 = mb->vcount;
    prop_mat(mb, g_mat.hollow);
    mb_quad(mb, v3(0, 0, 0), v3(0.55f, 0, 0), v3(0, 1.0f, 0));
    mb_set(mb, prop_shade(c->r, mat_c(c->mat[0]), 0.1f), MAT_RUST, BONE_NONE);
    mb_box(mb, m4_trs(v3(0.9f, 0, 0.08f), v3(0.06f, 2.1f, 1.1f), 0, -0.67f, 0));
    shape_to_world(mb, v0, c->L);
}

// Мелочи на стене: стена позади, лицом вдоль +z, точка — середина
static void sh_vent(const ShapeCtx *c)
{
    u32 v0 = c->mb->vcount;
    vent(c->mb, c->r, v3(0, 0, 0), v3(0, 0, 1), c->size.x, c->size.y);
    shape_to_world(c->mb, v0, c->L);
}
static void sh_plate(const ShapeCtx *c)
{
    u32 v0 = c->mb->vcount;
    plate(c->mb, c->r, v3(0, 0, 0), v3(0, 0, 1), c->size.x, c->size.y);
    shape_to_world(c->mb, v0, c->L);
}
// Щит с трубкой вверх на size.y
static void sh_junction(const ShapeCtx *c)
{
    u32 v0 = c->mb->vcount;
    junction(c->mb, c->r, v3(0, 0, 0), v3(0, 0, 1), c->size.y);
    shape_to_world(c->mb, v0, c->L);
}
static void sh_scupper(const ShapeCtx *c)
{
    u32 v0 = c->mb->vcount;
    scupper(c->mb, c->r, v3(0, 0, 0), v3(0, 0, 1));
    shape_to_world(c->mb, v0, c->L);
}

// Лоток с кабелями по стене вдоль +x на size.x: консоли, местами сорванный лоток, три кабеля на половине длины
static void sh_tray(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    Rng *rng = c->r;
    u32 v0 = mb->vcount;
    f32 len = c->size.x;
    mb_set(mb, prop_shade(rng, mat_c(g_mat.iron), 0.1f), MAT_METAL, BONE_NONE);
    for (f32 t = 0.0f; t < len; t += 2.5f) mb_box(mb, m4_trs(v3(t, 0, 0.3f), v3(0.06f, 0.06f, 0.6f), 0, 0, 0));
    mb_set(mb, prop_shade(rng, mat_c(g_mat.rust_pale), 0.15f), MAT_RUST, BONE_NONE);
    for (f32 t = 0.0f; t < len - 2.0f; t += 6.0f) {
        if (rng_f32(rng) < 0.25f) continue;
        mb_box(mb, m4_trs(v3(t + 3.0f, 0.08f, 0.4f), v3(5.8f, 0.04f, 0.45f), 0, 0, 0));
        mb_box(mb, m4_trs(v3(t + 3.0f, 0.15f, 0.62f), v3(5.8f, 0.12f, 0.02f), 0, 0, 0));
    }
    prop_mat(mb, g_mat.cable);
    for (int k = 0; k < 3; k++) {
        f32 z = 0.25f + (f32)k * 0.1f;
        mb_cylinder(mb, v3(0, 0.15f, z), v3(len * 0.47f, 0.15f, z), 0.025f, 5);
    }
    shape_to_world(mb, v0, c->L);
}

// Ржавая скоба-лестница на стене: точка — низ, size.y — высота, стена позади
static void sh_staples(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    u32 v0 = mb->vcount;
    f32 h = c->size.y;
    mb_set(mb, prop_shade(c->r, mat_c(g_mat.rust_pale), 0.1f), MAT_RUST, BONE_NONE);
    for (f32 y = 0.6f; y < h - 0.1f; y += 0.35f) mb_cylinder(mb, v3(-0.3f, y, 0), v3(0.3f, y, 0), 0.02f, 4);
    for (int s = -1; s <= 1; s += 2) mb_cylinder(mb, v3((f32)s * 0.32f, 0.4f, -0.02f), v3((f32)s * 0.32f, h, -0.02f), 0.025f, 4);
    shape_to_world(mb, v0, c->L);
}

const Shape g_ostov_shapes[] = {
    { "ostov_wall", sh_rough, 0 },
    { "ostov_block", sh_rough, 1 },
    { "ostov_trim", sh_rough, 2 },
    { "ostov_plinth", sh_rough, 3 },
    { "ostov_piece", sh_piece, 0 },
    { "ostov_capital", sh_capital, 0 },
    { "ostov_buttress", sh_buttress, 0 },
    { "ostov_floor_plate", sh_floor_plate, 0 },
    { "ostov_roof", sh_roof, 0 },
    { "ostov_column", sh_column_round, 0 },
    { "ostov_mound", sh_mound, 0 },
    { "ostov_drift", sh_drift, 0 },
    { "ostov_rubble", sh_rubble, 0 },
    { "ostov_hatch", sh_hatch, 0 },
    { "ostov_vent", sh_vent, 0 },
    { "ostov_plate", sh_plate, 0 },
    { "ostov_junction", sh_junction, 0 },
    { "ostov_scupper", sh_scupper, 0 },
    { "ostov_tray", sh_tray, 0 },
    { "ostov_staples", sh_staples, 0 },
};
const u32 g_ostov_shape_count = ARRAY_COUNT(g_ostov_shapes);

void ostov_rough_box(MeshBuilder *mb, V3 lo, V3 hi, f32 cell, f32 chip, u8 hide, u32 seed)
{
    Rough r = rough(cell, 0.05f, chip, 0.30f, hide, seed);
    rough_box(mb, lo, hi, &r);
}

f32 ostov_ground(f32 x, f32 z, f32 base)
{
    if (x < PX0 || x > PX1 || z < PZ0 || z > PZ1) return base;
    // Лестница пандусом, площадка и галерея на одной высоте
    if (x >= ST_X0 && x <= ST_X1 && z >= ST_Z0 && z <= ST_Z1) return FY + (ST_Z1 - z) / (ST_Z1 - ST_Z0) * (GY - FY);
    if (x >= -76.0f && x <= ST_X1 && z >= -6.0f && z < ST_Z0) return GY;
    if (x >= -76.0f && x <= -72.0f && z >= -50.0f && z < -6.0f) return GY;
    return FY;
}
