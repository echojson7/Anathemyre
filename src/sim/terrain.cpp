// Земля из зерна целыми числами (docs/terrain.md). Два прохода:
//  1. крупный слой 32 м: искривлённые шумы Перлина — котловины и плато, хребты (гребневой шум), столовые горы
//     уступами; там же маски песка и шероховатости;
//  2. сетка 8 м: крупный слой гладко (Catmull-Rom) + холмы шумом значений + поля дюн по ветру.
// Всё в целых (i32/i64), поэтому одинаково на любых целях без правил float. Слой деталей 1 м — terrain_node()
#include "terrain.h"
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

void terrain_masks(const Terrain &t, i32 gx, i32 gz, u32 *rough, u32 *sand) {
    clamp_node(t, gx);
    clamp_node(t, gz);
    i32 mx = gx >> TERRAIN_MASK_SHIFT, ux = gx & 31;
    i32 mz = gz >> TERRAIN_MASK_SHIFT, uz = gz & 31;
    if (mx >= (i32)t.nm - 1) { mx = (i32)t.nm - 2; ux = 32; }
    if (mz >= (i32)t.nm - 1) { mz = (i32)t.nm - 2; uz = 32; }
    const u16 *m = t.mask + (usize)mz * t.nm + mx;
    i32 m00 = m[0], m10 = m[1], m01 = m[t.nm], m11 = m[t.nm + 1];
    auto bil = [&](i32 shift) {
        i32 a = (m00 >> shift) & 255, b = (m10 >> shift) & 255, c = (m01 >> shift) & 255, d = (m11 >> shift) & 255;
        return (u32)(((a * (32 - ux) + b * ux) * (32 - uz) + (c * (32 - ux) + d * ux) * uz) >> 10);
    };
    *rough = bil(0);
    *sand = bil(8);
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
    u32 rough = 0, sand = 0;
    terrain_masks(t, gx, gz, &rough, &sand);
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

void terrain_generate(Terrain &t, const TerrainDesc &d, Arena &world, Arena &scratch) {
    t = Terrain{};
    t.chunks = d.chunks;
    t.chunk0 = -d.chunks / 2;
    t.size_m = d.chunks * 256;
    t.n = (u32)(t.size_m >> TERRAIN_BASE_SHIFT) + 1;
    t.nm = (u32)(t.size_m >> TERRAIN_MASK_SHIFT) + 1;
    t.height = arena_push<u16>(world, (usize)t.n * t.n);
    t.mask = arena_push<u16>(world, (usize)t.nm * t.nm);
    auto sd = [&](u32 k) { return th(d.seed * 0x9E3779B9u + k * 0x85EBCA6Bu); };
    t.detail_seed = sd(99);
    for (u32 k = 0; k < TERRAIN_DETAIL_OCTAVES; ++k) t.oct_seed[k] = th(t.detail_seed + k);

    usize mark = arena_mark(scratch);
    const u32 nm = t.nm, n = t.n;
    const i32 half = t.size_m / 2;
    // Равнина у начала координат: квадрат радиуса в м², i64
    const i64 plain_r = (i64)(d.start_plain > 0 ? d.start_plain : 0);

    // 1. Крупный слой 32 м: высота (мм), доли гор и плато (Q16) для масок
    i32 *elev = arena_push<i32>(scratch, (usize)nm * nm);
    i32 *mount = arena_push<i32>(scratch, (usize)nm * nm);
    i32 *sandq = arena_push<i32>(scratch, (usize)nm * nm);
    i32 *mesa_q = arena_push<i32>(scratch, (usize)nm * nm);   // шум столовых гор, Q16: уступы режет сетка 8 м
    i32 *mesa_r = arena_push<i32>(scratch, (usize)nm * nm);   // доля области столовых гор, Q16
    for (u32 j = 0; j < nm; ++j) {
        for (u32 i = 0; i < nm; ++i) {
            i32 x = (i32)i * 32, z = (i32)j * 32;
            i32 wx = (i32)((perlin(x, z, 12, sd(1)) * 700 + perlin(x, z, 10, sd(2)) * 180) >> 16);
            i32 wz = (i32)((perlin(x, z, 12, sd(3)) * 700 + perlin(x, z, 10, sd(4)) * 180) >> 16);
            i32 X = x + wx, Z = z + wz;
            i64 cont = fbm(X, Z, 14, 3, sd(5));
            i64 mm = smooth_q16(ONE * 2 / 100, ONE * 38 / 100, fbm(x + wx / 2, z + wz / 2, 13, 2, sd(6)));
            // Стартовая равнина: горы и плато гаснут к центру карты
            i64 dx = x - half, dz = z - half;
            i64 dist = 0;
            {
                i64 d2 = dx * dx + dz * dz;  // целый корень: метры
                i64 r = 0, bit = (i64)1 << 40;
                while (bit > d2) bit >>= 2;
                while (bit) {
                    if (d2 >= r + bit) { d2 -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
                    bit >>= 2;
                }
                dist = r;
            }
            i64 open = plain_r ? smooth_q16(plain_r * ONE, plain_r * ONE * 22 / 10, dist * ONE) : ONE;
            mm = (mm * open) >> 16;
            i64 ridge = ridged(X, Z, 11, 5, sd(7));
            i64 mesa_reg = (smooth_q16(0, ONE * 3 / 10, fbm(x, z, 13, 2, sd(8))) * (ONE - mm)) >> 16;
            mesa_reg = (mesa_reg * open) >> 16;
            i64 mesa_n = fbm(X, Z, 11, 3, sd(9));
            i64 tier1 = smooth_q16(ONE * 14 / 100, ONE * 20 / 100, mesa_n);
            i64 h = 380000 + ((cont * 320000) >> 16);                        // равнины и котловины
            h += (mm * ((ridge * 1100000) / ONE + 220000)) >> 16;  // хребты над подножием
            if (h < 10000) h = 10000;
            if (h > 3200000) h = 3200000;
            usize k = (usize)j * nm + i;
            elev[k] = (i32)h;
            mesa_q[k] = (i32)mesa_n;
            mesa_r[k] = (i32)mesa_reg;
            mount[k] = (i32)clamp_q16(mm + ((mesa_reg * tier1) >> 16), 0, ONE);
            // Песок копится в низинах, пятнами
            i64 low = ONE - smooth_q16(-ONE * 4 / 10, ONE * 3 / 10, cont);
            i64 sn = fbm(X, Z, 12, 2, sd(10)) + ((low * 6) >> 4) - ONE / 5;
            sandq[k] = (i32)((smooth_q16(-ONE / 4, ONE / 6, sn) * (ONE - mount[k])) >> 16);
        }
    }
    // Маски по уклону крупного слоя: на крутом нет песка, шероховатость выше
    for (u32 j = 0; j < nm; ++j) {
        for (u32 i = 0; i < nm; ++i) {
            usize k = (usize)j * nm + i;
            u32 i0 = i ? i - 1 : i, i1 = i + 1 < nm ? i + 1 : i;
            u32 j0 = j ? j - 1 : j, j1 = j + 1 < nm ? j + 1 : j;
            i64 gx = abs64(elev[(usize)j * nm + i1] - elev[(usize)j * nm + i0]) * ONE / ((i64)(i1 - i0) * 32000);
            i64 gz = abs64(elev[(usize)j1 * nm + i] - elev[(usize)j0 * nm + i]) * ONE / ((i64)(j1 - j0) * 32000);
            i64 slope = gx > gz ? gx : gz;  // м/м, Q16
            i64 sand = (sandq[k] * (ONE - smooth_q16(ONE * 8 / 100, ONE * 25 / 100, slope))) >> 16;
            i64 rubble = fbm((i32)i * 32, (i32)j * 32, 9, 2, sd(11));
            i64 rough = ONE * 45 / 100 + ((mount[k] * 6) >> 4) + ((slope * 3) >> 1) + ((rubble * 3) >> 3);
            rough = (clamp_q16(rough, 0, ONE) * (ONE - ((sand * 14) >> 4))) >> 16;
            u32 r8 = (u32)((clamp_q16(rough, 0, ONE) * 255) >> 16);
            u32 s8 = (u32)((clamp_q16(sand, 0, ONE) * 255) >> 16);
            t.mask[k] = (u16)(r8 | (s8 << 8));
        }
    }

    // 2. Сетка 8 м. Крупный слой — Catmull-Rom ×4: сначала по x для всех строк 32 м, потом по z
    static const i32 CR[4][4] = {{0, 128, 0, 0}, {-9, 111, 29, -3}, {-8, 72, 72, -8}, {-3, 29, 111, -9}};
    i32 *xr[3];
    const i32 *src[3] = {elev, mesa_q, mesa_r};
    for (u32 f = 0; f < 3; ++f) {
        xr[f] = arena_push<i32>(scratch, (usize)nm * n);
        for (u32 j = 0; j < nm; ++j) {
            const i32 *e = src[f] + (usize)j * nm;
            for (u32 i = 0; i < n; ++i) {
                i32 c = (i32)(i >> 2);
                const i32 *w = CR[i & 3];
                i64 s = 0;
                for (i32 k = 0; k < 4; ++k) {
                    i32 ci = c - 1 + k;
                    ci = ci < 0 ? 0 : (ci >= (i32)nm ? (i32)nm - 1 : ci);
                    s += (i64)w[k] * e[ci];
                }
                xr[f][(usize)j * n + i] = (i32)(s / 128);
            }
        }
    }
    // Холмы и искривление дюн — шум значений построчно
    RowNoise hills[4], dwarp[2], damp;
    const i32 hill_lp[4] = {5, 4, 3, 2};          // 256, 128, 64, 32 м
    const i64 hill_mm[4] = {14000, 7000, 3500, 1600};
    for (u32 k = 0; k < 4; ++k) row_noise_init(hills[k], hill_lp[k], sd(20 + k), n, scratch);
    row_noise_init(dwarp[0], 6, sd(30), n, scratch);   // 512 м
    row_noise_init(dwarp[1], 8, sd(31), n, scratch);   // 2048 м
    row_noise_init(damp, 7, sd(32), n, scratch);       // 1024 м
    // Ветер с запада-юго-запада: направления с целой длиной (24, 7)/25 и (12, 35)/37
    constexpr i64 L1 = 190 * 256, L2 = 520 * 256;
    for (u32 j = 0; j < n; ++j) {
        for (RowNoise &r : hills) row_noise_row(r, j, n);
        for (RowNoise &r : dwarp) row_noise_row(r, j, n);
        row_noise_row(damp, j, n);
        i32 cj = (i32)(j >> 2);
        const i32 *wz = CR[j & 3];
        usize rows[4];
        for (i32 k = 0; k < 4; ++k) {
            i32 r = cj - 1 + k;
            r = r < 0 ? 0 : (r >= (i32)nm ? (i32)nm - 1 : r);
            rows[k] = (usize)r * n;
        }
        // Маски по z: строки 32 м и вес
        u32 mz = j >> 2, tz = j & 3;
        u32 mz1 = mz + 1 < nm ? mz + 1 : mz;
        for (u32 i = 0; i < n; ++i) {
            i64 up[3] = {0, 0, 0};
            for (u32 f = 0; f < 3; ++f) {
                for (i32 k = 0; k < 4; ++k) up[f] += (i64)wz[k] * xr[f][rows[k] + i];
                up[f] /= 128;
            }
            i64 h = up[0];
            // Столовые горы: уступы по гладкому шуму на сетке 8 м — края обрывов ровные, без лесенки 32 м
            if (up[2] > 0) {
                i64 tier1 = smooth_q16(ONE * 14 / 100, ONE * 20 / 100, up[1]);
                i64 tier2 = smooth_q16(ONE * 38 / 100, ONE * 42 / 100, up[1]);
                h += (clamp_q16(up[2], 0, ONE) * ((tier1 * 120000 + tier2 * 80000) >> 16)) >> 16;
            }
            u32 mx = i >> 2, tx = i & 3;
            u32 mx1 = mx + 1 < nm ? mx + 1 : mx;
            u32 q00 = t.mask[(usize)mz * nm + mx], q10 = t.mask[(usize)mz * nm + mx1];
            u32 q01 = t.mask[(usize)mz1 * nm + mx], q11 = t.mask[(usize)mz1 * nm + mx1];
            auto bil = [&](u32 sh) {
                i64 a = (q00 >> sh) & 255, b = (q10 >> sh) & 255, c = (q01 >> sh) & 255, e = (q11 >> sh) & 255;
                i64 v = ((a * (4 - tx) + b * tx) * (4 - tz) + (c * (4 - tx) + e * tx) * tz);
                return (v << 16) / (16 * 255);  // Q16
            };
            i64 rough = bil(0), sand = bil(8);
            // Холмы: сильнее на шероховатом, слабее на песке
            i64 hk = ONE * 35 / 100 + ((rough * 65) / 100);
            hk = (hk * (ONE - ((sand * 6) / 10))) >> 16;
            i64 hs = 0;
            for (u32 k = 0; k < 4; ++k) hs += row_noise_at(hills[k], i) * hill_mm[k];
            h += ((hs >> 16) * hk) >> 16;
            // Дюны там, где песок
            if (sand > 0) {
                i64 x = (i64)i * 8, z = (i64)j * 8;
                i64 warp = row_noise_at(dwarp[0], i) * 90 + row_noise_at(dwarp[1], i) * 300;  // м, Q16
                i64 u1 = ((x * 24 + z * 7) * 256) / 25 + (warp >> 8);
                i64 u2 = ((x * 12 + z * 35) * 256) / 37 + (warp >> 7);
                i64 p1 = dune_profile(phase_q16(u1, L1));
                i64 p2 = dune_profile(phase_q16(u2, L2));
                // Поля дюн с плоскими промежутками; поперечная волна рвёт гребни на серпы
                i64 field = smooth_q16(-ONE / 10, ONE * 35 / 100, row_noise_at(damp, i));
                i64 a1 = 3000 + ((field * 16000) >> 16);  // 3–19 м от подошвы до гребня
                i64 segn = clamp_q16(p2 / 2 + row_noise_at(hills[0], i) / 2 + ONE / 4, 0, ONE);
                i64 seg = ONE * 35 / 100 + ((segn * 65) / 100);
                i64 dune = ((((p1 - ONE * 45 / 100) * a1) >> 16) * seg) >> 16;
                h += (dune * sand) >> 16;
            }
            i64 u = (h + 25) / 50;
            t.height[(usize)j * n + i] = (u16)(u < 0 ? 0 : (u > 65535 ? 65535 : u));
        }
    }
    arena_restore(scratch, mark);
    t.hash = hash_combine(hash_bytes(t.height, (usize)t.n * t.n * sizeof(u16)),
                          hash_bytes(t.mask, (usize)t.nm * t.nm * sizeof(u16)));
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

// seed <n> · chunks <n, чётное> · start_plain <м>
bool terrain_desc_parse(const u8 *text, usize size, TerrainDesc &out, char *err, usize err_cap) {
    out = TerrainDesc{};
    TextReader r = text_reader(text, size);
    TextLine l;
    while (text_next(r, l)) {
        if (l.count != 2) { terrain_err(err, err_cap, l.line, "ожидается <ключ> <значение>"); return false; }
        bool ok = true;
        if (str_eq(l.w[0], "seed")) ok = parse_u32(l.w[1], out.seed);
        else if (str_eq(l.w[0], "chunks")) ok = parse_i32(l.w[1], out.chunks) && out.chunks >= 4 && out.chunks <= 200 && out.chunks % 2 == 0;
        else if (str_eq(l.w[0], "start_plain")) ok = parse_f32(l.w[1], out.start_plain) && out.start_plain >= 0;
        else { terrain_err(err, err_cap, l.line, "неизвестный ключ"); return false; }
        if (!ok) { terrain_err(err, err_cap, l.line, "плохое значение"); return false; }
    }
    return true;
}
