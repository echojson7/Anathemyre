// Земля из зерна (docs/terrain.md), локации в ряд с запада на восток (долина, пустыня), между ними хребет с перевалом:
//  1. крупный слой 32 м: искривлённые шумы Перлина — котловина с холмами, горы кольцом (гребневой шум), низина;
//  2. сетка 8 м во float: крупный слой гладко (Catmull-Rom) + мелкие холмы, эрозия водой (terrain_erode.cpp),
//     ровное дно низины с протоками, маски 32 м, наносы песка, в пустыне — драа и дюны (terrain_dune).
// Шумы — целые (i32/i64), эрозия и сглаживание — float только с + − × / и sqrt по порядку: одна сетка на любых
// целях. Слой деталей 1 м — terrain_node(), целый, общий с шейдером
#include "terrain.h"
#include "terrain_erode.h"
#include "dune_map.h"
#include "../core/hash.h"
#include "../core/text.h"

// --- общее с data/shaders/terrain.glsl: хеш, шум значений, октавы деталей. Только 32-битные операции ---

static inline u32 th(u32 v) {
    u32 s = v * 747796405u + 2891336453u;
    u32 w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
static inline u32 th2(i32 x, i32 z, u32 seed) { return th((u32)x ^ th((u32)z ^ th(seed))); }

// Октавы слоя деталей: период 2^lp м, размах в единицах 1/1280 м (при шероховатости 255)
struct DetailOctave {
    i32 lp;
    i32 amp;
};
static constexpr DetailOctave DETAIL[TERRAIN_DETAIL_OCTAVES] = {{5, 640}, {4, 420}, {3, 380}, {2, 230}, {1, 115}};

// Шум значений на решётке 2^lp м в узле решётки 1 м, ±amp. lp ≤ 5: все произведения в пределах i32
static inline i32 detail_noise(i32 gx, i32 gz, i32 lp, u32 seed, i32 amp) {
    i32 p = 1 << lp;
    i32 cx = gx >> lp, cz = gz >> lp;
    i32 fx = gx & (p - 1), fz = gz & (p - 1);
    i32 wx = ((fx * fx * (3 * p - 2 * fx)) << 8) >> (3 * lp);
    i32 wz = ((fz * fz * (3 * p - 2 * fz)) << 8) >> (3 * lp);
    i32 v00 = (i32)(th2(cx, cz, seed) >> 16) - 32768;
    i32 v10 = (i32)(th2(cx + 1, cz, seed) >> 16) - 32768;
    i32 v01 = (i32)(th2(cx, cz + 1, seed) >> 16) - 32768;
    i32 v11 = (i32)(th2(cx + 1, cz + 1, seed) >> 16) - 32768;
    i32 a = v00 + (((v10 - v00) * wx) >> 8);
    i32 b = v01 + (((v11 - v01) * wx) >> 8);
    i32 v = a + (((b - a) * wz) >> 8);
    return (v * amp) >> 15;
}

static inline void clamp_node(const Terrain &t, i32 &gx, i32 &gz) {
    gx = gx < 0 ? 0 : (gx > t.size_x ? t.size_x : gx);
    gz = gz < 0 ? 0 : (gz > t.size_z ? t.size_z : gz);
}

// Клетка масок 32 м вокруг узла 1 м: номер левого нижнего узла и место в клетке (0..32)
static inline usize mask_cell(const Terrain &t, i32 gx, i32 gz, i32 *ux, i32 *uz) {
    clamp_node(t, gx, gz);
    i32 mx = gx >> TERRAIN_MASK_SHIFT, mz = gz >> TERRAIN_MASK_SHIFT;
    *ux = gx & 31;
    *uz = gz & 31;
    if (mx >= (i32)t.nmx - 1) { mx = (i32)t.nmx - 2; *ux = 32; }
    if (mz >= (i32)t.nmz - 1) { mz = (i32)t.nmz - 2; *uz = 32; }
    return (usize)mz * t.nmx + (usize)mx;
}

// Билинейно байт shift четырёх слов масок, 0..255
static inline u32 mask_bil(const u32 *m, u32 row, u32 shift, i32 ux, i32 uz) {
    i32 a = (i32)((m[0] >> shift) & 255), b = (i32)((m[1] >> shift) & 255);
    i32 c = (i32)((m[row] >> shift) & 255), d = (i32)((m[row + 1] >> shift) & 255);
    return (u32)(((a * (32 - ux) + b * ux) * (32 - uz) + (c * (32 - ux) + d * ux) * uz) >> 10);
}

TerrainMasks terrain_masks(const Terrain &t, i32 gx, i32 gz) {
    i32 ux, uz;
    const u32 *m = t.mask + mask_cell(t, gx, gz, &ux, &uz);
    return {mask_bil(m, t.nmx, 0, ux, uz), mask_bil(m, t.nmx, 8, ux, uz), mask_bil(m, t.nmx, 16, ux, uz),
            mask_bil(m, t.nmx, 24, ux, uz)};
}

TerrainMasks2 terrain_masks2(const Terrain &t, i32 gx, i32 gz) {
    i32 ux, uz;
    const u32 *m = t.mask2 + mask_cell(t, gx, gz, &ux, &uz);
    return {mask_bil(m, t.nmx, 0, ux, uz), mask_bil(m, t.nmx, 8, ux, uz)};
}

i32 terrain_water(const Terrain &t, i32 gx, i32 gz) {
    i32 ux, uz;
    const u16 *w = t.water + mask_cell(t, gx, gz, &ux, &uz);
    const u32 nm = t.nmx;
    i32 w00 = w[0], w10 = w[1], w01 = w[nm], w11 = w[nm + 1];
    if (!(w00 | w10 | w01 | w11)) return 0;
    // У края воды пустые узлы берут среднее непустых минус 1.5 м: уровень не проваливается к нулю, но и
    // не разливается за край низины прямой кромкой клетки 32 м
    if (!w00 || !w10 || !w01 || !w11) {
        i32 k = (w00 != 0) + (w10 != 0) + (w01 != 0) + (w11 != 0);
        i32 s = (w00 + w10 + w01 + w11) / k - 30;
        if (s < 1) s = 1;
        if (!w00) w00 = s;
        if (!w10) w10 = s;
        if (!w01) w01 = s;
        if (!w11) w11 = s;
    }
    // 5 см × 64 = единица 1/1280 м; веса /32 по каждой оси
    return ((w00 * (32 - ux) + w10 * ux) * (32 - uz) + (w01 * (32 - ux) + w11 * ux) * uz) / 16;
}

void terrain_detail_octave(u32 k, i32 *lp, i32 *amp) {
    *lp = DETAIL[k].lp;
    *amp = DETAIL[k].amp;
}

// Клетка сетки 8 м вокруг узла 1 м: номер левого нижнего узла и место в клетке (0..8)
static inline usize base_cell(const Terrain &t, i32 gx, i32 gz, i32 *fx, i32 *fz) {
    clamp_node(t, gx, gz);
    i32 bx = gx >> TERRAIN_BASE_SHIFT, bz = gz >> TERRAIN_BASE_SHIFT;
    *fx = gx & 7;
    *fz = gz & 7;
    if (bx >= (i32)t.nx - 1) { bx = (i32)t.nx - 2; *fx = 8; }
    if (bz >= (i32)t.nz - 1) { bz = (i32)t.nz - 2; *fz = 8; }
    return (usize)bz * t.nx + (usize)bx;
}

// Сетка 8 м билинейно в узле решётки 1 м, единицы 1/1280 м (без деталей)
static i32 base_at(const Terrain &t, i32 gx, i32 gz) {
    i32 fx, fz;
    const u16 *h = t.height + base_cell(t, gx, gz, &fx, &fz);
    i32 h00 = h[0], h10 = h[1], h01 = h[t.nx], h11 = h[t.nx + 1];
    return (h00 * (8 - fx) + h10 * fx) * (8 - fz) + (h01 * (8 - fx) + h11 * fx) * fz;
}

i32 terrain_base_slope(const Terrain &t, i32 gx, i32 gz) {
    // Разности через 8 м по билинейной сетке: наклон меняется плавно, без ступенек по клеткам
    i32 dx = base_at(t, gx + 4, gz) - base_at(t, gx - 4, gz);
    i32 dz = base_at(t, gx, gz + 4) - base_at(t, gx, gz - 4);
    dx = dx < 0 ? -dx : dx;
    dz = dz < 0 ? -dz : dz;
    return (dx > dz ? dx : dz) / 8;
}

// Дюны сетки 8 м билинейно в узле решётки 1 м, единицы 1/1280 м
static i32 dune_base_at(const Terrain &t, i32 gx, i32 gz) {
    i32 fx, fz;
    const u16 *h = t.dune + base_cell(t, gx, gz, &fx, &fz);
    i32 h00 = h[0], h10 = h[1], h01 = h[t.nx], h11 = h[t.nx + 1];
    if (!(h00 | h10 | h01 | h11)) return 0;
    return ((h00 * (8 - fx) + h10 * fx) * (8 - fz) + (h01 * (8 - fx) + h11 * fx) * fz) / 32;  // веса 64, единица 2
}

// Дюны из карты 2 м в узле решётки 1 м (билинейно, веса 0 или ½ — точно), единицы 1/1280 м. has — у локации
// есть карта. gx, gz — уже в пределах карты
static i32 fine_at(const Terrain &t, i32 gx, i32 gz, bool *has) {
    i32 nloc = t.size_x / t.loc_m;
    i32 loc = gx / t.loc_m;
    loc = loc >= nloc ? nloc - 1 : loc;
    i32 off = t.fine_off[loc];
    *has = off >= 0;
    if (off < 0) return 0;
    i32 lx = gx - loc * t.loc_m;
    i32 i = lx >> TERRAIN_FINE_SHIFT, j = gz >> TERRAIN_FINE_SHIFT, fx = lx & 1, fz = gz & 1;
    const u16 *f = t.fine + off + (usize)j * t.fine_n + i;
    i32 h00 = f[0], h10 = fx ? f[1] : 0, h01 = fz ? f[t.fine_n] : 0, h11 = fx && fz ? f[t.fine_n + 1] : 0;
    return ((h00 * (2 - fx) + h10 * fx) * (2 - fz) + (h01 * (2 - fx) + h11 * fx) * fz) / 2;  // веса 4, единица 2
}

i32 terrain_node(const Terrain &t, i32 gx, i32 gz) {
    clamp_node(t, gx, gz);
    i32 h = base_at(t, gx, gz);  // билинейно с весами k/8: 5 см × 64 = единица 1/1280 м
    // Дюны в сетке 8 м размыты: между узлами — карта 2 м или точная функция вместо билинейной смеси (в узлах
    // они равны)
    i32 db = dune_base_at(t, gx, gz);
    bool has;
    i32 fd = fine_at(t, gx, gz, &has);
    if (has) h += fd - db;
    else {
        u32 amp = terrain_masks2(t, gx, gz).dune;
        if (db | (i32)amp) h += terrain_dune(gx, gz, amp, t.dune_seed) - db;
    }
    u32 rough = terrain_masks(t, gx, gz).rough;
    if (rough == 0) return h;
    i32 d = 0;
    for (u32 k = 0; k < TERRAIN_DETAIL_OCTAVES; ++k) d += detail_noise(gx, gz, DETAIL[k].lp, t.oct_seed[k], DETAIL[k].amp);
    return h + ((d * (i32)rough) >> 8);
}

// --- генерация: только CPU, i64 можно ---

// Направления градиентов шума Перлина, Q15
static const i32 GRAD[16][2] = {
    {32767, 0},      {30274, 12540},   {23170, 23170},   {12540, 30274},
    {0, 32767},      {-12540, 30274},  {-23170, 23170},  {-30274, 12540},
    {-32767, 0},     {-30274, -12540}, {-23170, -23170}, {-12540, -30274},
    {0, -32767},     {12540, -30274},  {23170, -23170},  {30274, -12540},
};
constexpr i64 ONE = 65536;  // Q16

static inline i64 fade_q16(i64 t) {  // 6t⁵ − 15t⁴ + 10t³
    i64 t3 = ((t * t) >> 16) * t >> 16;
    i64 b = ((t * (t * 6 - 15 * ONE)) >> 16) + 10 * ONE;
    return (t3 * b) >> 16;
}
static inline i64 clamp_q16(i64 v, i64 lo, i64 hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline i64 abs64(i64 v) { return v < 0 ? -v : v; }
static inline i64 smooth_q16(i64 e0, i64 e1, i64 x) {
    i64 t = clamp_q16(((x - e0) << 16) / (e1 - e0), 0, ONE);
    return (((t * t) >> 16) * (3 * ONE - 2 * t)) >> 16;
}

// Шум Перлина с периодом решётки 2^lp м в точке (x, z) целых метров, Q16 примерно в [−1, 1]
static i64 perlin(i32 x, i32 z, i32 lp, u32 seed) {
    i32 cx = x >> lp, cz = z >> lp;
    i64 fx = ((i64)(x & ((1 << lp) - 1)) << 16) >> lp;
    i64 fz = ((i64)(z & ((1 << lp) - 1)) << 16) >> lp;
    auto g = [&](i32 ix, i32 iz, i64 dx, i64 dz) {
        u32 h = th2(ix, iz, seed) >> 28;
        return (GRAD[h][0] * dx + GRAD[h][1] * dz) >> 15;
    };
    i64 n00 = g(cx, cz, fx, fz), n10 = g(cx + 1, cz, fx - ONE, fz);
    i64 n01 = g(cx, cz + 1, fx, fz - ONE), n11 = g(cx + 1, cz + 1, fx - ONE, fz - ONE);
    i64 u = fade_q16(fx), v = fade_q16(fz);
    i64 a = n00 + (((n10 - n00) * u) >> 16);
    i64 b = n01 + (((n11 - n01) * u) >> 16);
    return ((a + (((b - a) * v) >> 16)) * 181) >> 7;  // ×√2: размах 2D-шума ~0.7
}

// Сумма октав, периоды 2^lp, 2^(lp−1)…, вес каждой следующей вдвое меньше; Q16 в [−1, 1]
static i64 fbm(i32 x, i32 z, i32 lp, i32 octaves, u32 seed) {
    i64 sum = 0, amp = ONE, norm = 0;
    for (i32 o = 0; o < octaves; ++o) {
        sum += (perlin(x, z, lp - o, seed + (u32)o * 101u) * amp) >> 16;
        norm += amp;
        amp >>= 1;
    }
    return (sum << 16) / norm;
}

// Гребневой шум: острые хребты и V-долины; следующая октава сильнее там, где предыдущая высока. Q16 в [0, 1]
static i64 ridged(i32 x, i32 z, i32 lp, i32 octaves, u32 seed) {
    i64 sum = 0, amp = ONE, norm = 0, weight = ONE;
    for (i32 o = 0; o < octaves; ++o) {
        i64 r = ONE - abs64(clamp_q16(perlin(x, z, lp - o, seed + (u32)o * 131u), -ONE, ONE));
        r = (r * r) >> 16;
        r = (r * weight) >> 16;
        weight = clamp_q16(r * 2, 0, ONE);
        sum += (r * amp) >> 16;
        norm += amp;
        amp >>= 1;
    }
    return (sum << 16) / norm;
}

// Шум значений сеткой 8 м построчно: в строке значения решётки по z смешаны один раз, по x — таблицей весов
struct RowNoise {
    i32 lp;      // период решётки 2^lp узлов
    u32 seed;
    i32 *col;    // значения решётки, смешанные по z для текущей строки, Q15
    i32 *w;      // веса плавного шага по месту в клетке, Q8
};

static void row_noise_init(RowNoise &r, i32 lp, u32 seed, u32 n, Arena &a) {
    r.lp = lp;
    r.seed = seed;
    r.col = arena_push<i32>(a, (n >> lp) + 3);
    i32 p = 1 << lp;
    r.w = arena_push<i32>(a, (usize)p);
    for (i32 f = 0; f < p; ++f) r.w[f] = (i32)((((i64)f * f * (3 * p - 2 * f)) << 8) >> (3 * lp));
}
static void row_noise_row(RowNoise &r, u32 j, u32 n) {
    i32 cz = (i32)(j >> r.lp);
    i32 wz = r.w[j & ((1u << r.lp) - 1)];
    u32 ncol = (n >> r.lp) + 2;
    for (u32 c = 0; c <= ncol; ++c) {
        i32 a = (i32)(th2((i32)c, cz, r.seed) >> 16) - 32768;
        i32 b = (i32)(th2((i32)c, cz + 1, r.seed) >> 16) - 32768;
        r.col[c] = a + (((b - a) * wz) >> 8);
    }
}
// Q16 в [−1, 1)
static inline i64 row_noise_at(const RowNoise &r, u32 i) {
    u32 c = i >> r.lp;
    i32 w = r.w[i & ((1u << r.lp) - 1)];
    i32 a = r.col[c], b = r.col[c + 1];
    return (i64)(a + (((b - a) * w) >> 8)) * 2;
}

// Профиль дюны по фазе φ ∈ [0, 1) (Q16): длинный пологий наветренный склон до гребня, короткий крутой
// подветренный. Q16 в [0, 1]
static inline i64 dune_profile(i64 phi) {
    constexpr i64 CREST = 47186;  // 0.72
    i64 t = phi < CREST ? (phi << 16) / CREST : ((ONE - phi) << 16) / (ONE - CREST);
    return (((t * t) >> 16) * (3 * ONE - 2 * t)) >> 16;
}
// Фаза точки u (метры Q8) на волне длины lambda (метры Q8)
static inline i64 phase_q16(i64 u, i64 lambda) {
    i64 m = u % lambda;
    if (m < 0) m += lambda;
    return (m << 16) / lambda;
}

// Целый корень (i64)
static i64 isqrt64(i64 v) {
    i64 r = 0, bit = (i64)1 << 40;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// Скользящее среднее радиуса r по строкам, потом по столбцам (край — повтор крайнего). Поле w·h, tmp — не
// меньше большей стороны. float по порядку
static void blur_box(f32 *a, u32 w, u32 h, i32 r, f32 *tmp) {
    const f32 k = 1.0f / (f32)(2 * r + 1);
    for (u32 pass = 0; pass < 2; ++pass) {
        const u32 lines = pass == 0 ? h : w, len = pass == 0 ? w : h;
        for (u32 j = 0; j < lines; ++j) {
            auto at = [&](i32 i) -> f32 {
                i = i < 0 ? 0 : (i >= (i32)len ? (i32)len - 1 : i);
                return pass == 0 ? a[(usize)j * w + (usize)i] : a[(usize)i * w + j];
            };
            f32 s = 0.0f;
            for (i32 i = -r; i <= r; ++i) s += at(i);
            for (u32 i = 0; i < len; ++i) {
                tmp[i] = s * k;
                s += at((i32)i + r + 1) - at((i32)i - r);
            }
            for (u32 i = 0; i < len; ++i) {
                if (pass == 0) a[(usize)j * w + i] = tmp[i];
                else a[(usize)i * w + j] = tmp[i];
            }
        }
    }
}

// Шаг Catmull-Rom ×4 (веса /128): узел 8 м между узлами 32 м
static const i32 CR[4][4] = {{0, 128, 0, 0}, {-9, 111, 29, -3}, {-8, 72, 72, -8}, {-3, 29, 111, -9}};

// Поле 32 м (mw·mh, f32) → сетка 8 м (w·h) гладко, Catmull-Rom по x, потом по z
static void upsample4(const f32 *src, u32 mw, u32 mh, f32 *dst, u32 w, u32 h, f32 *rowtmp) {
    for (u32 j = 0; j < h; ++j) {
        i32 cj = (i32)(j >> 2);
        const i32 *wz = CR[j & 3];
        for (u32 i = 0; i < mw; ++i) {
            f32 s = 0.0f;
            for (i32 k = 0; k < 4; ++k) {
                i32 r = cj - 1 + k;
                r = r < 0 ? 0 : (r >= (i32)mh ? (i32)mh - 1 : r);
                s += (f32)wz[k] * src[(usize)r * mw + i];
            }
            rowtmp[i] = s * (1.0f / 128.0f);
        }
        for (u32 i = 0; i < w; ++i) {
            i32 c = (i32)(i >> 2);
            const i32 *wx = CR[i & 3];
            f32 s = 0.0f;
            for (i32 k = 0; k < 4; ++k) {
                i32 ci = c - 1 + k;
                ci = ci < 0 ? 0 : (ci >= (i32)mw ? (i32)mw - 1 : ci);
                s += (f32)wx[k] * rowtmp[ci];
            }
            dst[(usize)j * w + i] = s * (1.0f / 128.0f);
        }
    }
}

static inline f32 q16f(i64 q) { return (f32)q * (1.0f / 65536.0f); }
static inline f32 smoothf(f32 e0, f32 e1, f32 x) {
    f32 t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// Крупный слой локации в точке 32 м: высота, доля гор, низины, пустыни и силы дюн
struct LocField {
    f32 floor, mount;   // высота дна и прибавка гор, м
    f32 mm, vs;         // доля гор, доля низины
    f32 desert, dunes;  // пустыня, сила дюн
};

void terrain_generate(Terrain &t, const TerrainDesc &d, Arena &world, Arena &scratch) {
    t = Terrain{};
    const i32 L = d.chunks * 256;
    const u32 nloc = d.locations ? d.locations : 1;
    t.chunks_x = d.chunks * (i32)nloc;
    t.chunks_z = d.chunks;
    t.chunk0 = -d.chunks / 2;
    t.loc_m = L;
    t.size_x = t.chunks_x * 256;
    t.size_z = t.chunks_z * 256;
    t.nx = (u32)(t.size_x >> TERRAIN_BASE_SHIFT) + 1;
    t.nz = (u32)(t.size_z >> TERRAIN_BASE_SHIFT) + 1;
    t.nmx = (u32)(t.size_x >> TERRAIN_MASK_SHIFT) + 1;
    t.nmz = (u32)(t.size_z >> TERRAIN_MASK_SHIFT) + 1;
    const u32 nmx = t.nmx, nmz = t.nmz, nx = t.nx, nz = t.nz;
    const usize cells = (usize)nmx * nmz, nodes = (usize)nx * nz;
    t.height = arena_push<u16>(world, nodes);
    t.dune = arena_push<u16>(world, nodes);
    t.mask = arena_push<u32>(world, cells);
    t.mask2 = arena_push<u32>(world, cells);
    t.water = arena_push<u16>(world, cells);
    auto sd = [&](u32 k) { return th(d.seed * 0x9E3779B9u + k * 0x85EBCA6Bu); };
    t.detail_seed = sd(99);
    for (u32 k = 0; k < TERRAIN_DETAIL_OCTAVES; ++k) t.oct_seed[k] = th(t.detail_seed + k);
    t.dune_seed = sd(98);

    usize mark = arena_mark(scratch);
    const i32 half = L / 2;
    const i64 ring = (i64)d.ring;
    const u32 wmax = nx > nz ? nx : nz;

    // 1. Крупный слой 32 м: высота (м), доля гор, низины, пустыни и дюн
    f32 *elev = arena_push<f32>(scratch, cells);
    f32 *mnt = arena_push<f32>(scratch, cells);
    f32 *vall = arena_push<f32>(scratch, cells);
    f32 *fl = arena_push<f32>(scratch, cells);
    f32 *des = arena_push<f32>(scratch, cells);
    f32 *dam = arena_push<f32>(scratch, cells);
    f32 *tmp = arena_push<f32>(scratch, wmax);
    // Низина долины — полоса вдоль нуля искривлённого шума, сдвинута так, чтобы пройти в ~700 м от центра
    auto valley_noise = [&](i32 x, i32 z) {
        i32 wx = (i32)((perlin(x, z, 11, sd(15)) * 500) >> 16);
        i32 wz = (i32)((perlin(x, z, 11, sd(16)) * 500) >> 16);
        return fbm(x + wx, z + wz, 13, 2, sd(14));
    };
    const i64 vn0 = valley_noise(half + 600, half - 350);
    // Локация loc в точке (x, z), искривлённой в (X, Z): горы кольцом от подножия ring до края локации
    auto field = [&](u32 loc, i32 x, i32 z, i32 X, i32 Z) {
        LocField f{};
        i64 dx = X - ((i64)loc * L + half), dz = Z - half;
        i64 adx = abs64(dx), adz = abs64(dz);
        i64 dist = ((adx > adz ? adx : adz) * 45 + isqrt64(dx * dx + dz * dz) * 55) / 100;
        dist += (perlin(x, z, 10, sd(5)) * 450) >> 16;
        i64 mm = smooth_q16(ring * ONE, (ring + 1300) * ONE, dist * ONE);
        i64 ridge = mm > 0 ? ridged(X, Z, 10, 5, sd(7)) : 0;
        f.mm = q16f(mm);
        if (d.biome[loc] == BIOME_DESERT) {
            // Пустыня: дно чуть выше долины, широкие волны; горы ниже и голее; дюны — по пятнам шума 2 км
            i64 h = 330000 + ((fbm(X, Z, 12, 2, sd(60)) * 25000) >> 16);
            f.floor = (f32)h * 0.001f;
            f.mount = (f32)((mm * ((ridge * 330000) / ONE + 50000 + ((mm * 120000) >> 16))) >> 16) * 0.001f;
            f.desert = 1.0f;
            f32 dn = q16f(fbm(X + 7001, Z - 3301, 11, 2, sd(61)));
            // У карты дюн (лидар, ветер) своя смена размеров — сила ровная, гаснет только у гор
            f32 share = d.dune_path[loc][0] ? 1.0f : f_clamp(0.72f + dn * 0.6f, 0.25f, 1.0f);
            f.dunes = share * (1.0f - smoothf(0.0f, 0.5f, f.mm));
            return f;
        }
        i64 h = 300000 + ((fbm(X, Z, 13, 2, sd(6)) * 40000) >> 16);  // дно котловины, мм
        // Мягкие холмы, их потом точит эрозия
        i64 hills = fbm(X, Z, 10, 3, sd(13));
        i64 hills2 = fbm(X + 3001, Z - 1777, 11, 2, sd(18));
        h += ((((hills * 110000) >> 16) + ((hills2 * 70000) >> 16)) * (ONE - mm)) >> 16;
        f.floor = (f32)h * 0.001f;
        f.mount = (f32)((mm * ((ridge * 520000) / ONE + 80000 + ((mm * 200000) >> 16))) >> 16) * 0.001f;
        if (loc == 0) {
            i64 vn = abs64(valley_noise(x, z) - vn0);
            i64 vs = ONE - smooth_q16(ONE * 5 / 100, ONE * 11 / 100, vn);
            f.vs = q16f((vs * (ONE - smooth_q16(0, ONE / 3, mm))) >> 16);
        }
        return f;
    };
    for (u32 j = 0; j < nmz; ++j) {
        for (u32 i = 0; i < nmx; ++i) {
            i32 x = (i32)i * 32, z = (i32)j * 32;
            i32 wx = (i32)((perlin(x, z, 11, sd(1)) * 420 + perlin(x, z, 9, sd(2)) * 120) >> 16);
            i32 wz = (i32)((perlin(x, z, 11, sd(3)) * 420 + perlin(x, z, 9, sd(4)) * 120) >> 16);
            i32 X = x + wx, Z = z + wz;
            // Ближайший стык локаций: в полосе ±900 м поля обеих смешаны (там горы с обеих сторон)
            i32 seam = (X + L / 2) / L;
            seam = seam < 1 ? 1 : (seam > (i32)nloc - 1 ? (i32)nloc - 1 : seam);
            i32 sx = seam * L;
            u32 loc = X < L ? 0u : ((u32)(X / L) >= nloc ? nloc - 1 : (u32)(X / L));
            LocField f = field(loc, x, z, X, Z);
            f32 pass = 0.0f;
            if (nloc > 1) {
                i32 ds = X - sx;
                if (ds > -900 && ds < 900) {
                    LocField a = field((u32)seam - 1, x, z, X, Z), b = field((u32)seam, x, z, X, Z);
                    f32 k = smoothf(-900.0f, 900.0f, (f32)ds);
                    f.floor = a.floor + (b.floor - a.floor) * k;
                    f.mount = a.mount + (b.mount - a.mount) * k;
                    f.mm = a.mm + (b.mm - a.mm) * k;
                    f.vs = a.vs + (b.vs - a.vs) * k;
                    f.desert = a.desert + (b.desert - a.desert) * k;
                    f.dunes = a.dunes + (b.dunes - a.dunes) * k;
                }
                // Перевал через хребет между локациями: извилистая седловина ~0.5 км шириной, севернее центра
                i64 zp = half + 650 + ((perlin(x, z, 11, sd(62)) * 500 + perlin(x, z, 9, sd(63)) * 120) >> 16);
                f32 q = (f32)abs64((i64)Z - zp);
                f32 ds2 = (f32)(ds < 0 ? -ds : ds);
                pass = (1.0f - smoothf(200.0f, 750.0f, q)) * (1.0f - smoothf(2300.0f, 3200.0f, ds2));
                f.mount *= 1.0f - 0.93f * pass;
                f.mm *= 1.0f - 0.9f * pass;
                f.floor += pass * 18.0f;
                f.vs *= 1.0f - pass;
            }
            usize k = (usize)j * nmx + i;
            elev[k] = f.floor + f.mount;
            mnt[k] = f.mm;
            vall[k] = f.vs;
            des[k] = f.desert;
            dam[k] = f.dunes;
            fl[k] = elev[k];
        }
    }
    // Дно низины — сглаженная высота на 3 м ниже: ровное, медленно уходит вниз по долине
    blur_box(fl, nmx, nmz, 10, tmp);
    for (usize k = 0; k < cells; ++k) fl[k] -= 3.0f;

    // 2. Сетка 8 м во float: крупный слой гладко + мелкие холмы; дно низины опущено до эрозии
    f32 *H = arena_push<f32>(scratch, nodes);
    f32 *V = arena_push<f32>(scratch, nodes);   // доля низины
    f32 *F = arena_push<f32>(scratch, nodes);   // дно низины, м
    f32 *M = arena_push<f32>(scratch, nodes);   // доля гор
    f32 *D = arena_push<f32>(scratch, nodes);   // доля пустыни
    upsample4(elev, nmx, nmz, H, nx, nz, tmp);
    upsample4(vall, nmx, nmz, V, nx, nz, tmp);
    upsample4(fl, nmx, nmz, F, nx, nz, tmp);
    upsample4(mnt, nmx, nmz, M, nx, nz, tmp);
    upsample4(des, nmx, nmz, D, nx, nz, tmp);
    RowNoise hills[3];
    const i32 hill_lp[3] = {5, 4, 3};  // 256, 128, 64 м
    const f32 hill_m[3] = {9.0f, 4.0f, 1.6f};
    for (u32 k = 0; k < 3; ++k) row_noise_init(hills[k], hill_lp[k], sd(20 + k), nx, scratch);
    for (u32 j = 0; j < nz; ++j) {
        for (RowNoise &r : hills) row_noise_row(r, j, nx);
        for (u32 i = 0; i < nx; ++i) {
            usize k = (usize)j * nx + i;
            f32 v = f_clamp(V[k], 0.0f, 1.0f);
            V[k] = v;
            f32 hs = 0.0f;
            for (u32 o = 0; o < 3; ++o) hs += q16f(row_noise_at(hills[o], i)) * hill_m[o];
            // На горах мелкие бугры сильнее, на дне низины их нет; дно пустыни под дюнами ровнее
            f32 m = M[k] < 0.0f ? 0.0f : M[k];
            f32 de = f_clamp(D[k], 0.0f, 1.0f) * (1.0f - m);
            f32 e = H[k] + hs * (0.6f + 1.4f * m) * (1.0f - v) * (1.0f - 0.75f * de);
            f32 c = smoothf(0.15f, 0.85f, v);
            H[k] = (e + (F[k] - e) * c) * (1.0f / 8.0f);  // в клетках 8 м для эрозии
        }
    }

    // 3. Эрозия водой (terrain_erode.cpp) на сетке 8 м: долины, промоины, отроги, конусы выноса
    f32 *dep = arena_push<f32>(scratch, nodes);
    f32 *flw = arena_push<f32>(scratch, nodes);
    for (usize k = 0; k < nodes; ++k) dep[k] = flw[k] = 0.0f;
    ErodeParams ep;
    ep.droplets = (u32)(nodes * 4 / 10);
    ep.seed = sd(40);
    ep.radius = 2;
    ep.lifetime = 40;
    terrain_erode(H, nx, nz, ep, dep, flw, scratch);

    // 4. После эрозии: середина дна низины снова ровная и не ниже уровня воды; протоки прорезаны
    for (u32 j = 0; j < nz; ++j) {
        for (u32 i = 0; i < nx; ++i) {
            usize k = (usize)j * nx + i;
            f32 e = H[k] * 8.0f;
            f32 v = V[k];
            if (v > 0.0f) {
                e += (F[k] - e) * smoothf(0.4f, 0.8f, v);
                f32 lift = smoothf(0.25f, 0.45f, v);
                if (e < F[k] + 0.1f) e += (F[k] + 0.1f - e) * lift;
            }
            if (v > 0.3f) {
                // Две сетки извилистых русел вдоль нуля искривлённого шума: берега на 0.35 м выше воды,
                // русла на 0.7 м ниже
                i32 x = (i32)i * 8, z = (i32)j * 8;
                i32 ox = (i32)((perlin(x, z, 9, sd(50)) * 140 + perlin(x, z, 6, sd(51)) * 22) >> 16);
                i32 oz = (i32)((perlin(x, z, 9, sd(52)) * 140 + perlin(x, z, 6, sd(53)) * 22) >> 16);
                f32 c1 = 1.0f - smoothf(0.0f, 0.09f, q16f(abs64(perlin(x + ox, z + oz, 9, sd(54)))));
                f32 c2 = 0.85f * (1.0f - smoothf(0.0f, 0.07f, q16f(abs64(perlin(x - oz, z + ox, 8, sd(55))))));
                f32 ch = c1 > c2 ? c1 : c2;
                e += smoothf(0.3f, 0.7f, v) * (0.35f - 1.05f * ch);
            }
            H[k] = e;
        }
    }

    // 5. Маски 32 м по сетке 8 м: уклон, отложенный эрозией грунт и сток. Отложения и сток рваные от узла к
    // узлу: сначала сглажены, иначе билинейная смесь масок показала бы квадраты 32 м
    blur_box(dep, nx, nz, 4, tmp);
    blur_box(flw, nx, nz, 4, tmp);
    auto b8 = [](f32 q) { return (u32)(f_clamp(q, 0.0f, 1.0f) * 255.0f + 0.5f); };
    for (u32 j = 0; j < nmz; ++j) {
        for (u32 i = 0; i < nmx; ++i) {
            usize k = (usize)j * nmx + i;
            u32 ci = i * 4 < nx ? i * 4 : nx - 1, cj = j * 4 < nz ? j * 4 : nz - 1;
            u32 i0 = ci >= 2 ? ci - 2 : 0, i1 = ci + 2 < nx ? ci + 2 : nx - 1;
            u32 j0 = cj >= 2 ? cj - 2 : 0, j1 = cj + 2 < nz ? cj + 2 : nz - 1;
            f32 gx = f_abs(H[(usize)cj * nx + i1] - H[(usize)cj * nx + i0]) / ((f32)(i1 - i0) * 8.0f);
            f32 gz = f_abs(H[(usize)j1 * nx + ci] - H[(usize)j0 * nx + ci]) / ((f32)(j1 - j0) * 8.0f);
            f32 slope = gx > gz ? gx : gz;
            f32 sdep = 0.0f, sflw = 0.0f;
            for (u32 b = j0; b <= j1; ++b)
                for (u32 a = i0; a <= i1; ++a) {
                    sdep += dep[(usize)b * nx + a];
                    sflw += flw[(usize)b * nx + a];
                }
            f32 area = (f32)((i1 - i0 + 1) * (j1 - j0 + 1));
            sdep = sdep * 8.0f / area;  // метры
            sflw /= area;
            f32 depq = f_clamp(sdep / 1.5f, 0.0f, 1.0f);
            f32 flowq = f_clamp(sflw / 6.0f, 0.0f, 1.0f);
            f32 v = vall[k], m = mnt[k], de = des[k];
            f32 flat = 1.0f - smoothf(0.35f, 0.7f, slope);
            // Долина. Песок наносами на склонах и у подножий: пятна шума и отложения; дно низины — сырой луг
            f32 sp = q16f(fbm((i32)i * 32 + 977, (i32)j * 32 - 311, 10, 4, sd(17)));
            f32 sand = smoothf(0.2f, 0.55f, sp + depq * 0.35f + slope * 0.3f - v * 0.5f - m * 0.5f) * flat;
            f32 grass = (1.0f - sand) * (1.0f - smoothf(0.45f, 0.95f, slope)) * (1.0f - smoothf(0.35f, 0.85f, m));
            f32 wet = f_clamp(v * 1.2f + flowq * 0.5f, 0.0f, 1.0f) * (1.0f - m);
            f32 rubble = q16f(fbm((i32)i * 32, (i32)j * 32, 9, 2, sd(11)));
            f32 rough = 0.35f + m * 0.45f + slope * 1.2f + rubble * 0.35f;
            rough = f_clamp(rough, 0.0f, 1.0f) * (1.0f - sand * 0.85f) * (1.0f - grass * 0.55f) * (1.0f - v * 0.9f);
            // Пустыня: песок всюду, кроме крутых склонов гор (песок затекает между ними и лежит у подножий)
            f32 rocky = smoothf(0.1f, 0.4f, m) * smoothf(0.15f, 0.45f, slope + rubble * 0.1f);
            f32 sand_d = 1.0f - rocky;
            f32 rough_d = rocky * (0.55f + 0.45f * m);
            sand += (sand_d - sand) * de;
            grass *= 1.0f - de;
            wet *= 1.0f - de;
            rough += (rough_d - rough) * de;
            t.mask[k] = b8(rough) | (b8(sand) << 8) | (b8(grass) << 16) | (b8(wet) << 24);
            t.mask2[k] = b8(dam[k] * de * (1.0f - rocky)) | (b8(de) << 8);
            // Вода — в низине; уровень — ровное дно
            i32 wl = (i32)(fl[k] * 20.0f + 0.5f);
            t.water[k] = (u16)(v > 0.6f ? (wl < 1 ? 1 : (wl > 65535 ? 65535 : wl)) : 0);
        }
    }

    // Дюны из карт (лидар или симуляция ветра) × сила дюн из маски
    t.fine_n = (u32)(L >> TERRAIN_FINE_SHIFT) + 1;
    const usize fn2 = (usize)t.fine_n * t.fine_n;
    u32 nmaps = 0;
    for (u32 loc = 0; loc < nloc; ++loc)
        if (dune_map_check(d.dune_data[loc], d.dune_size[loc], t.fine_n, t.fine_n)) ++nmaps;
    if (nmaps) {
        t.fine_count = fn2 * nmaps;
        t.fine = arena_push<u16>(world, t.fine_count);
        usize off = 0;
        for (u32 loc = 0; loc < nloc; ++loc) {
            if (!dune_map_check(d.dune_data[loc], d.dune_size[loc], t.fine_n, t.fine_n)) continue;
            u16 *f = t.fine + off;
            if (!dune_map_decode(d.dune_data[loc], d.dune_size[loc], f, t.fine_n, t.fine_n)) {
                for (usize k = 0; k < fn2; ++k) f[k] = 0;
            }
            for (u32 j = 0; j < t.fine_n; ++j)
                for (u32 i = 0; i < t.fine_n; ++i) {
                    usize k = (usize)j * t.fine_n + i;
                    i32 gx = (i32)loc * L + (i32)(i << TERRAIN_FINE_SHIFT), gz = (i32)(j << TERRAIN_FINE_SHIFT);
                    i32 amp = (i32)terrain_masks2(t, gx, gz).dune;
                    i32 v = ((i32)f[k] * 32 * amp) / 255;  // 5 см → 1/640 м
                    f[k] = (u16)(v > 65535 ? 65535 : v);
                }
            t.fine_off[loc] = (i32)off;
            off += fn2;
        }
    }

    // 6. Наносы песка в долине — низкие гряды поперёк ветра с запада-юго-запада, где песок. В пустыне —
    // большие пологие гряды (~2 км, до ~60 м) и поверх них дюны (terrain_dune) с точностью узла
    RowNoise dwarp;
    row_noise_init(dwarp, 6, sd(30), nx, scratch);  // 512 м
    constexpr i64 L1 = 90 * 256;     // длина волны наносов, м Q8
    constexpr i64 L2 = 2048 * 256;   // длина волны больших гряд пустыни, м Q8
    for (u32 j = 0; j < nz; ++j) {
        row_noise_row(dwarp, j, nx);
        u32 mz = j >> 2, tz = j & 3, mz1 = mz + 1 < nmz ? mz + 1 : mz;
        for (u32 i = 0; i < nx; ++i) {
            usize k = (usize)j * nx + i;
            f32 e = H[k];
            u32 mx = i >> 2, tx = i & 3, mx1 = mx + 1 < nmx ? mx + 1 : mx;
            auto bil = [&](const u32 *mk, u32 shift) {
                u32 s00 = (mk[(usize)mz * nmx + mx] >> shift) & 255, s10 = (mk[(usize)mz * nmx + mx1] >> shift) & 255;
                u32 s01 = (mk[(usize)mz1 * nmx + mx] >> shift) & 255, s11 = (mk[(usize)mz1 * nmx + mx1] >> shift) & 255;
                return (f32)((s00 * (4 - tx) + s10 * tx) * (4 - tz) + (s01 * (4 - tx) + s11 * tx) * tz) / (16.0f * 255.0f);
            };
            f32 sand = bil(t.mask, 8);
            f32 desert = bil(t.mask2, 8), dunes = bil(t.mask2, 0);
            i64 x = (i64)i * 8, z = (i64)j * 8;
            if (sand > 0.0f && desert < 1.0f) {
                i64 u1 = ((x * 24 + z * 7) * 256) / 25 + (row_noise_at(dwarp, i) * 60 >> 8);
                f32 p1 = q16f(dune_profile(phase_q16(u1, L1)));
                e += (p1 - 0.45f) * 4.0f * sand * sand * (1.0f - V[k]) * (1.0f - desert);
            }
            u32 nloc_x = (u32)(x / L) >= nloc ? nloc - 1 : (u32)(x / L);
            bool mapped = t.fine_off[nloc_x] >= 0;
            if (dunes > 0.0f && !mapped) {
                i64 w2 = (perlin((i32)x, (i32)z, 12, sd(64)) * 500 + perlin((i32)x, (i32)z, 10, sd(65)) * 120) >> 16;
                i64 u2 = ((x * 24 - z * 7) * 256) / 25 + w2 * 256;
                f32 p2 = q16f(dune_profile(phase_q16(u2, L2)));
                f32 a2 = 0.55f + 0.45f * q16f(perlin((i32)x, (i32)z, 10, sd(66)));
                e += p2 * 60.0f * a2 * dunes;
            }
            i32 dn;
            if (mapped) {
                bool has;
                dn = fine_at(t, (i32)i * 8, (i32)j * 8, &has);
            } else {
                dn = terrain_dune((i32)i * 8, (i32)j * 8, terrain_masks2(t, (i32)i * 8, (i32)j * 8).dune, t.dune_seed);
            }
            t.dune[k] = (u16)(dn >> 1);
            i32 u = (i32)(e * 20.0f + 0.5f) + dn / 64;
            t.height[k] = (u16)(u < 0 ? 0 : (u > 65535 ? 65535 : u));
        }
    }
    arena_restore(scratch, mark);
    terrain_stamps_apply(t, d);
    t.hash = hash_combine(hash_bytes(t.height, nodes * sizeof(u16)), hash_bytes(t.mask, cells * sizeof(u32)));
    t.hash = hash_combine(t.hash, hash_bytes(t.water, cells * sizeof(u16)));
    t.hash = hash_combine(t.hash, hash_bytes(t.mask2, cells * sizeof(u32)));
    t.hash = hash_combine(t.hash, hash_bytes(t.dune, nodes * sizeof(u16)));
    if (t.fine) t.hash = hash_combine(t.hash, hash_bytes(t.fine, t.fine_count * sizeof(u16)));
    t.hash = hash_combine(t.hash, t.detail_seed ^ ((u64)t.dune_seed << 32));
}

// --- описание ---

// seed <n> · chunks <n, чётное> · ring <м> · locations <valley|desert>… (с запада на восток) ·
// dunes <номер локации> <файл .dun в data/> · pad <x> <z> <w> <d> [<переход м>]
bool terrain_desc_parse(const u8 *text, usize size, TerrainDesc &out, char *err, usize err_cap) {
    out = TerrainDesc{};
    TextReader r = text_reader(text, size);
    TextLine l;
    while (text_next(r, l)) {
        if (l.count >= 2 && str_eq(l.w[0], "locations")) {
            if (l.count - 1 > TERRAIN_MAX_LOCATIONS) { text_err(err, err_cap, l.line, "слишком много локаций"); return false; }
            out.locations = l.count - 1;
            for (u32 k = 1; k < l.count; ++k) {
                if (str_eq(l.w[k], "valley")) out.biome[k - 1] = BIOME_VALLEY;
                else if (str_eq(l.w[k], "desert")) out.biome[k - 1] = BIOME_DESERT;
                else { text_err(err, err_cap, l.line, "локация: valley или desert"); return false; }
            }
            continue;
        }
        if (l.count == 3 && str_eq(l.w[0], "dunes")) {
            u32 loc = 0;
            if (!parse_u32(l.w[1], loc) || loc >= TERRAIN_MAX_LOCATIONS || l.w[2].n >= sizeof out.dune_path[0]) {
                text_err(err, err_cap, l.line, "dunes <локация> <файл>");
                return false;
            }
            memcpy(out.dune_path[loc], l.w[2].p, l.w[2].n);
            out.dune_path[loc][l.w[2].n] = 0;
            continue;
        }
        if ((l.count == 5 || l.count == 6) && str_eq(l.w[0], "pad")) {
            if (out.pad_count >= TERRAIN_MAX_PADS) { text_err(err, err_cap, l.line, "слишком много площадок"); return false; }
            TerrainPad &p = out.pad[out.pad_count++];
            p.margin = 16;
            if (!parse_i32(l.w[1], p.x) || !parse_i32(l.w[2], p.z) || !parse_i32(l.w[3], p.w) || !parse_i32(l.w[4], p.d) ||
                (l.count == 6 && !parse_i32(l.w[5], p.margin)) || p.w < 1 || p.d < 1 || p.w > 512 || p.d > 512 ||
                p.margin < 1 || p.margin > 256) {
                text_err(err, err_cap, l.line, "pad <x> <z> <ширина 1..512> <глубина 1..512> [<переход 1..256 м>]");
                return false;
            }
            continue;
        }
        if (l.count != 2) { text_err(err, err_cap, l.line, "ожидается <ключ> <значение>"); return false; }
        bool ok = true;
        if (str_eq(l.w[0], "seed")) ok = parse_u32(l.w[1], out.seed);
        else if (str_eq(l.w[0], "chunks")) ok = parse_i32(l.w[1], out.chunks) && out.chunks >= 4 && out.chunks <= 200 && out.chunks % 2 == 0;
        else if (str_eq(l.w[0], "ring")) ok = parse_f32(l.w[1], out.ring) && out.ring >= 256;
        else { text_err(err, err_cap, l.line, "неизвестный ключ"); return false; }
        if (!ok) { text_err(err, err_cap, l.line, "плохое значение"); return false; }
    }
    return true;
}
