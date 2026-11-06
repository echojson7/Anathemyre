// Земля одной локации из зерна (docs/terrain.md):
//  1. крупный слой 32 м: искривлённые шумы Перлина — котловина с холмами, горы кольцом (гребневой шум), низина;
//  2. сетка 8 м во float: крупный слой гладко (Catmull-Rom) + мелкие холмы, эрозия водой (terrain_erode.cpp),
//     ровное дно низины с протоками, маски 32 м, наносы песка.
// Шумы — целые (i32/i64), эрозия и сглаживание — float только с + − × / и sqrt по порядку: одна сетка на любых
// целях. Слой деталей 1 м — terrain_node(), целый, общий с шейдером
#include "terrain.h"
#include "terrain_erode.h"
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

static inline void clamp_node(const Terrain &t, i32 &g) {
    if (g < 0) g = 0;
    if (g > t.size_m) g = t.size_m;
}

// Клетка масок 32 м вокруг узла 1 м и место в ней (0..32)
static inline void mask_cell(const Terrain &t, i32 gx, i32 gz, i32 *mx, i32 *mz, i32 *ux, i32 *uz) {
    clamp_node(t, gx);
    clamp_node(t, gz);
    *mx = gx >> TERRAIN_MASK_SHIFT;
    *ux = gx & 31;
    *mz = gz >> TERRAIN_MASK_SHIFT;
    *uz = gz & 31;
    if (*mx >= (i32)t.nm - 1) { *mx = (i32)t.nm - 2; *ux = 32; }
    if (*mz >= (i32)t.nm - 1) { *mz = (i32)t.nm - 2; *uz = 32; }
}

TerrainMasks terrain_masks(const Terrain &t, i32 gx, i32 gz) {
    i32 mx, mz, ux, uz;
    mask_cell(t, gx, gz, &mx, &mz, &ux, &uz);
    const u32 *m = t.mask + (usize)mz * t.nm + mx;
    u32 m00 = m[0], m10 = m[1], m01 = m[t.nm], m11 = m[t.nm + 1];
    auto bil = [&](u32 shift) {
        i32 a = (i32)((m00 >> shift) & 255), b = (i32)((m10 >> shift) & 255);
        i32 c = (i32)((m01 >> shift) & 255), d = (i32)((m11 >> shift) & 255);
        return (u32)(((a * (32 - ux) + b * ux) * (32 - uz) + (c * (32 - ux) + d * ux) * uz) >> 10);
    };
    return {bil(0), bil(8), bil(16), bil(24)};
}

i32 terrain_water(const Terrain &t, i32 gx, i32 gz) {
    i32 mx, mz, ux, uz;
    mask_cell(t, gx, gz, &mx, &mz, &ux, &uz);
    const u16 *w = t.water + (usize)mz * t.nm + mx;
    i32 w00 = w[0], w10 = w[1], w01 = w[t.nm], w11 = w[t.nm + 1];
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

// Сетка 8 м билинейно в узле решётки 1 м, единицы 1/1280 м (без деталей)
static i32 base_at(const Terrain &t, i32 gx, i32 gz) {
    clamp_node(t, gx);
    clamp_node(t, gz);
    i32 bx = gx >> TERRAIN_BASE_SHIFT, fx = gx & 7;
    i32 bz = gz >> TERRAIN_BASE_SHIFT, fz = gz & 7;
    if (bx >= (i32)t.n - 1) { bx = (i32)t.n - 2; fx = 8; }
    if (bz >= (i32)t.n - 1) { bz = (i32)t.n - 2; fz = 8; }
    const u16 *h = t.height + (usize)bz * t.n + bx;
    i32 h00 = h[0], h10 = h[1], h01 = h[t.n], h11 = h[t.n + 1];
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

i32 terrain_node(const Terrain &t, i32 gx, i32 gz) {
    clamp_node(t, gx);
    clamp_node(t, gz);
    i32 base = base_at(t, gx, gz);  // билинейно с весами k/8: 5 см × 64 = единица 1/1280 м
    u32 rough = terrain_masks(t, gx, gz).rough;
    if (rough == 0) return base;
    i32 d = 0;
    for (u32 k = 0; k < TERRAIN_DETAIL_OCTAVES; ++k) d += detail_noise(gx, gz, DETAIL[k].lp, t.oct_seed[k], DETAIL[k].amp);
    return base + ((d * (i32)rough) >> 8);
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

// Скользящее среднее радиуса r по строкам, потом по столбцам (край — повтор крайнего). float по порядку
static void blur_box(f32 *a, u32 n, i32 r, f32 *tmp) {
    const f32 k = 1.0f / (f32)(2 * r + 1);
    for (u32 pass = 0; pass < 2; ++pass) {
        for (u32 j = 0; j < n; ++j) {
            f32 *row = pass == 0 ? a + (usize)j * n : nullptr;
            auto at = [&](i32 i) -> f32 {
                i = i < 0 ? 0 : (i >= (i32)n ? (i32)n - 1 : i);
                return pass == 0 ? row[i] : a[(usize)i * n + j];
            };
            f32 s = 0.0f;
            for (i32 i = -r; i <= r; ++i) s += at(i);
            for (u32 i = 0; i < n; ++i) {
                tmp[i] = s * k;
                s += at((i32)i + r + 1) - at((i32)i - r);
            }
            for (u32 i = 0; i < n; ++i) {
                if (pass == 0) row[i] = tmp[i];
                else a[(usize)i * n + j] = tmp[i];
            }
        }
    }
}

// Шаг Catmull-Rom ×4 (веса /128): узел 8 м между узлами 32 м
static const i32 CR[4][4] = {{0, 128, 0, 0}, {-9, 111, 29, -3}, {-8, 72, 72, -8}, {-3, 29, 111, -9}};

// Поле 32 м (nm·nm, f32) → сетка 8 м (n·n) гладко, Catmull-Rom по x, потом по z
static void upsample4(const f32 *src, u32 nm, f32 *dst, u32 n, f32 *rowtmp) {
    for (u32 j = 0; j < n; ++j) {
        i32 cj = (i32)(j >> 2);
        const i32 *wz = CR[j & 3];
        for (u32 i = 0; i < nm; ++i) {
            f32 s = 0.0f;
            for (i32 k = 0; k < 4; ++k) {
                i32 r = cj - 1 + k;
                r = r < 0 ? 0 : (r >= (i32)nm ? (i32)nm - 1 : r);
                s += (f32)wz[k] * src[(usize)r * nm + i];
            }
            rowtmp[i] = s * (1.0f / 128.0f);
        }
        for (u32 i = 0; i < n; ++i) {
            i32 c = (i32)(i >> 2);
            const i32 *w = CR[i & 3];
            f32 s = 0.0f;
            for (i32 k = 0; k < 4; ++k) {
                i32 ci = c - 1 + k;
                ci = ci < 0 ? 0 : (ci >= (i32)nm ? (i32)nm - 1 : ci);
                s += (f32)w[k] * rowtmp[ci];
            }
            dst[(usize)j * n + i] = s * (1.0f / 128.0f);
        }
    }
}

static inline f32 q16f(i64 q) { return (f32)q * (1.0f / 65536.0f); }
static inline f32 smoothf(f32 e0, f32 e1, f32 x) {
    f32 t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

void terrain_generate(Terrain &t, const TerrainDesc &d, Arena &world, Arena &scratch) {
    t = Terrain{};
    t.chunks = d.chunks;
    t.chunk0 = -d.chunks / 2;
    t.size_m = d.chunks * 256;
    t.n = (u32)(t.size_m >> TERRAIN_BASE_SHIFT) + 1;
    t.nm = (u32)(t.size_m >> TERRAIN_MASK_SHIFT) + 1;
    t.height = arena_push<u16>(world, (usize)t.n * t.n);
    t.mask = arena_push<u32>(world, (usize)t.nm * t.nm);
    t.water = arena_push<u16>(world, (usize)t.nm * t.nm);
    auto sd = [&](u32 k) { return th(d.seed * 0x9E3779B9u + k * 0x85EBCA6Bu); };
    t.detail_seed = sd(99);
    for (u32 k = 0; k < TERRAIN_DETAIL_OCTAVES; ++k) t.oct_seed[k] = th(t.detail_seed + k);

    usize mark = arena_mark(scratch);
    const u32 nm = t.nm, n = t.n;
    const usize cells = (usize)nm * nm, nodes = (usize)n * n;
    const i32 half = t.size_m / 2;
    const i64 ring = (i64)d.ring;

    // 1. Крупный слой 32 м: высота (м), доля гор, низины и песка
    f32 *elev = arena_push<f32>(scratch, cells);
    f32 *mnt = arena_push<f32>(scratch, cells);
    f32 *vall = arena_push<f32>(scratch, cells);
    f32 *fl = arena_push<f32>(scratch, cells);
    f32 *tmp = arena_push<f32>(scratch, n);
    // Низина — полоса вдоль нуля искривлённого шума, сдвинута так, чтобы пройти в ~700 м от начала
    auto valley_noise = [&](i32 x, i32 z) {
        i32 wx = (i32)((perlin(x, z, 11, sd(15)) * 500) >> 16);
        i32 wz = (i32)((perlin(x, z, 11, sd(16)) * 500) >> 16);
        return fbm(x + wx, z + wz, 13, 2, sd(14));
    };
    const i64 vn0 = valley_noise(half + 600, half - 350);
    for (u32 j = 0; j < nm; ++j) {
        for (u32 i = 0; i < nm; ++i) {
            i32 x = (i32)i * 32, z = (i32)j * 32;
            i32 wx = (i32)((perlin(x, z, 11, sd(1)) * 420 + perlin(x, z, 9, sd(2)) * 120) >> 16);
            i32 wz = (i32)((perlin(x, z, 11, sd(3)) * 420 + perlin(x, z, 9, sd(4)) * 120) >> 16);
            i32 X = x + wx, Z = z + wz;
            // Горы кольцом: от подножия на расстоянии ring до края карты; край рваный шумом
            i64 dx = X - half, dz = Z - half;
            i64 adx = abs64(dx), adz = abs64(dz);
            i64 dist = ((adx > adz ? adx : adz) * 45 + isqrt64(dx * dx + dz * dz) * 55) / 100;
            dist += (perlin(x, z, 10, sd(5)) * 450) >> 16;
            i64 mm = smooth_q16(ring * ONE, (ring + 1300) * ONE, dist * ONE);
            i64 ridge = mm > 0 ? ridged(X, Z, 10, 5, sd(7)) : 0;
            i64 h = 300000 + ((fbm(X, Z, 13, 2, sd(6)) * 40000) >> 16);  // дно котловины, мм
            h += (mm * ((ridge * 520000) / ONE + 80000 + ((mm * 200000) >> 16))) >> 16;
            // Мягкие холмы, их потом точит эрозия
            i64 hills = fbm(X, Z, 10, 3, sd(13));
            i64 hills2 = fbm(X + 3001, Z - 1777, 11, 2, sd(18));
            h += ((((hills * 110000) >> 16) + ((hills2 * 70000) >> 16)) * (ONE - mm)) >> 16;
            i64 vn = abs64(valley_noise(x, z) - vn0);
            i64 vs = ONE - smooth_q16(ONE * 5 / 100, ONE * 11 / 100, vn);
            vs = (vs * (ONE - smooth_q16(0, ONE / 3, mm))) >> 16;
            usize k = (usize)j * nm + i;
            elev[k] = (f32)h * 0.001f;
            mnt[k] = q16f(mm);
            vall[k] = q16f(vs);
            fl[k] = elev[k];
        }
    }
    // Дно низины — сглаженная высота на 3 м ниже: ровное, медленно уходит вниз по долине
    blur_box(fl, nm, 10, tmp);
    for (usize k = 0; k < cells; ++k) fl[k] -= 3.0f;

    // 2. Сетка 8 м во float: крупный слой гладко + мелкие холмы; дно низины опущено до эрозии
    f32 *H = arena_push<f32>(scratch, nodes);
    f32 *V = arena_push<f32>(scratch, nodes);   // доля низины
    f32 *F = arena_push<f32>(scratch, nodes);   // дно низины, м
    f32 *M = arena_push<f32>(scratch, nodes);   // доля гор
    upsample4(elev, nm, H, n, tmp);
    upsample4(vall, nm, V, n, tmp);
    upsample4(fl, nm, F, n, tmp);
    upsample4(mnt, nm, M, n, tmp);
    RowNoise hills[3];
    const i32 hill_lp[3] = {5, 4, 3};  // 256, 128, 64 м
    const f32 hill_m[3] = {9.0f, 4.0f, 1.6f};
    for (u32 k = 0; k < 3; ++k) row_noise_init(hills[k], hill_lp[k], sd(20 + k), n, scratch);
    for (u32 j = 0; j < n; ++j) {
        for (RowNoise &r : hills) row_noise_row(r, j, n);
        for (u32 i = 0; i < n; ++i) {
            usize k = (usize)j * n + i;
            f32 v = V[k] < 0.0f ? 0.0f : (V[k] > 1.0f ? 1.0f : V[k]);
            V[k] = v;
            f32 hs = 0.0f;
            for (u32 o = 0; o < 3; ++o) hs += q16f(row_noise_at(hills[o], i)) * hill_m[o];
            // На горах мелкие бугры сильнее, на дне низины их нет
            f32 m = M[k] < 0.0f ? 0.0f : M[k];
            f32 e = H[k] + hs * (0.6f + 1.4f * m) * (1.0f - v);
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
    terrain_erode(H, n, ep, dep, flw, scratch);

    // 4. После эрозии: середина дна низины снова ровная и не ниже уровня воды; протоки прорезаны
    for (u32 j = 0; j < n; ++j) {
        for (u32 i = 0; i < n; ++i) {
            usize k = (usize)j * n + i;
            f32 e = H[k] * 8.0f;
            f32 v = V[k];
            e += (F[k] - e) * smoothf(0.4f, 0.8f, v);
            f32 lift = smoothf(0.25f, 0.45f, v);
            if (e < F[k] + 0.1f) e += (F[k] + 0.1f - e) * lift;
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
    blur_box(dep, n, 4, tmp);
    blur_box(flw, n, 4, tmp);
    for (u32 j = 0; j < nm; ++j) {
        for (u32 i = 0; i < nm; ++i) {
            usize k = (usize)j * nm + i;
            u32 ci = i * 4 < n ? i * 4 : n - 1, cj = j * 4 < n ? j * 4 : n - 1;
            u32 i0 = ci >= 2 ? ci - 2 : 0, i1 = ci + 2 < n ? ci + 2 : n - 1;
            u32 j0 = cj >= 2 ? cj - 2 : 0, j1 = cj + 2 < n ? cj + 2 : n - 1;
            f32 gx = f_abs(H[(usize)cj * n + i1] - H[(usize)cj * n + i0]) / ((f32)(i1 - i0) * 8.0f);
            f32 gz = f_abs(H[(usize)j1 * n + ci] - H[(usize)j0 * n + ci]) / ((f32)(j1 - j0) * 8.0f);
            f32 slope = gx > gz ? gx : gz;
            f32 sdep = 0.0f, sflw = 0.0f;
            for (u32 b = j0; b <= j1; ++b)
                for (u32 a = i0; a <= i1; ++a) {
                    sdep += dep[(usize)b * n + a];
                    sflw += flw[(usize)b * n + a];
                }
            f32 area = (f32)((i1 - i0 + 1) * (j1 - j0 + 1));
            sdep = sdep * 8.0f / area;  // метры
            sflw /= area;
            f32 depq = f_clamp(sdep / 1.5f, 0.0f, 1.0f);
            f32 flowq = f_clamp(sflw / 6.0f, 0.0f, 1.0f);
            f32 v = vall[k], m = mnt[k];
            f32 flat = 1.0f - smoothf(0.35f, 0.7f, slope);
            // Песок наносами на склонах и у подножий: пятна шума и отложения; дно низины — сырой луг
            f32 sp = q16f(fbm((i32)i * 32 + 977, (i32)j * 32 - 311, 10, 4, sd(17)));
            f32 sand = smoothf(0.2f, 0.55f, sp + depq * 0.35f + slope * 0.3f - v * 0.5f - m * 0.5f) * flat;
            f32 grass = (1.0f - sand) * (1.0f - smoothf(0.45f, 0.95f, slope)) * (1.0f - smoothf(0.35f, 0.85f, m));
            f32 wet = f_clamp(v * 1.2f + flowq * 0.5f, 0.0f, 1.0f) * (1.0f - m);
            f32 rubble = q16f(fbm((i32)i * 32, (i32)j * 32, 9, 2, sd(11)));
            f32 rough = 0.35f + m * 0.45f + slope * 1.2f + rubble * 0.35f;
            rough = f_clamp(rough, 0.0f, 1.0f) * (1.0f - sand * 0.85f) * (1.0f - grass * 0.55f) * (1.0f - v * 0.9f);
            auto b8 = [](f32 q) { return (u32)(f_clamp(q, 0.0f, 1.0f) * 255.0f + 0.5f); };
            t.mask[k] = b8(rough) | (b8(sand) << 8) | (b8(grass) << 16) | (b8(wet) << 24);
            // Вода — в низине; уровень — ровное дно
            i32 wl = (i32)(fl[k] * 20.0f + 0.5f);
            t.water[k] = (u16)(v > 0.6f ? (wl < 1 ? 1 : (wl > 65535 ? 65535 : wl)) : 0);
        }
    }

    // 6. Наносы песка — низкие гряды поперёк ветра с запада-юго-запада, где песок; в сетку 5 см
    RowNoise dwarp;
    row_noise_init(dwarp, 6, sd(30), n, scratch);  // 512 м
    constexpr i64 L1 = 90 * 256;  // длина волны, м Q8
    for (u32 j = 0; j < n; ++j) {
        row_noise_row(dwarp, j, n);
        u32 mz = j >> 2, tz = j & 3, mz1 = mz + 1 < nm ? mz + 1 : mz;
        for (u32 i = 0; i < n; ++i) {
            usize k = (usize)j * n + i;
            f32 e = H[k];
            u32 mx = i >> 2, tx = i & 3, mx1 = mx + 1 < nm ? mx + 1 : mx;
            u32 s00 = (t.mask[(usize)mz * nm + mx] >> 8) & 255, s10 = (t.mask[(usize)mz * nm + mx1] >> 8) & 255;
            u32 s01 = (t.mask[(usize)mz1 * nm + mx] >> 8) & 255, s11 = (t.mask[(usize)mz1 * nm + mx1] >> 8) & 255;
            f32 sand = (f32)((s00 * (4 - tx) + s10 * tx) * (4 - tz) + (s01 * (4 - tx) + s11 * tx) * tz) / (16.0f * 255.0f);
            if (sand > 0.0f) {
                i64 x = (i64)i * 8, z = (i64)j * 8;
                i64 u1 = ((x * 24 + z * 7) * 256) / 25 + (row_noise_at(dwarp, i) * 60 >> 8);
                f32 p1 = q16f(dune_profile(phase_q16(u1, L1)));
                e += (p1 - 0.45f) * 4.0f * sand * sand * (1.0f - V[k]);
            }
            i32 u = (i32)(e * 20.0f + 0.5f);
            t.height[k] = (u16)(u < 0 ? 0 : (u > 65535 ? 65535 : u));
        }
    }
    arena_restore(scratch, mark);
    t.hash = hash_combine(hash_bytes(t.height, nodes * sizeof(u16)), hash_bytes(t.mask, cells * sizeof(u32)));
    t.hash = hash_combine(t.hash, hash_bytes(t.water, cells * sizeof(u16)));
    t.hash = hash_combine(t.hash, t.detail_seed);
}

// --- описание ---

static void terrain_err(char *err, usize cap, u32 line, const char *what) {
    if (!cap) return;
    usize k = 0;
    char num[12];
    u32 nd = 0;
    do { num[nd++] = (char)('0' + line % 10); line /= 10; } while (line && nd < sizeof num);
    const char *pre = "строка ";
    for (const char *p = pre; *p && k + 1 < cap; ++p) err[k++] = *p;
    while (nd && k + 1 < cap) err[k++] = num[--nd];
    if (k + 2 < cap) { err[k++] = ':'; err[k++] = ' '; }
    for (const char *p = what; *p && k + 1 < cap; ++p) err[k++] = *p;
    err[k] = 0;
}

// seed <n> · chunks <n, чётное> · ring <м>
bool terrain_desc_parse(const u8 *text, usize size, TerrainDesc &out, char *err, usize err_cap) {
    out = TerrainDesc{};
    TextReader r = text_reader(text, size);
    TextLine l;
    while (text_next(r, l)) {
        if (l.count != 2) { terrain_err(err, err_cap, l.line, "ожидается <ключ> <значение>"); return false; }
        bool ok = true;
        if (str_eq(l.w[0], "seed")) ok = parse_u32(l.w[1], out.seed);
        else if (str_eq(l.w[0], "chunks")) ok = parse_i32(l.w[1], out.chunks) && out.chunks >= 4 && out.chunks <= 200 && out.chunks % 2 == 0;
        else if (str_eq(l.w[0], "ring")) ok = parse_f32(l.w[1], out.ring) && out.ring >= 256;
        else { terrain_err(err, err_cap, l.line, "неизвестный ключ"); return false; }
        if (!ok) { terrain_err(err, err_cap, l.line, "плохое значение"); return false; }
    }
    return true;
}
