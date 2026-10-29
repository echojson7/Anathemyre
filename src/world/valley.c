#include "valley.h"

#include "../core/jobs.h"

static f32 *g_h;    // карта высот VALLEY_RES x VALLEY_RES, строки по Z
static f32 *g_far;  // грубая карта вокруг: склоны, сходящие в пустошь (VALLEY_FAR_RES², шаг VALLEY_FAR_STEP)

static f32 hash2(i32 x, i32 z)
{
    u32 h = (u32)x * 374761393u + (u32)z * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (f32)(h & 0xFFFFFF) / 16777215.0f;
}

static f32 vnoise(f32 x, f32 z)
{
    i32 ix = (i32)floorf(x), iz = (i32)floorf(z);
    f32 fx = x - (f32)ix, fz = z - (f32)iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    f32 a = hash2(ix, iz), b = hash2(ix + 1, iz), c = hash2(ix, iz + 1), d = hash2(ix + 1, iz + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}

f32 valley_fbm(f32 x, f32 z, int octaves)
{
    f32 s = 0.0f, a = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        s += vnoise(x, z) * a;
        norm += a;
        a *= 0.5f;
        x = x * 2.03f + 17.1f;
        z = z * 2.03f - 9.7f;
    }
    return s / norm;
}

static f32 smooth(f32 a, f32 b, f32 x)
{
    f32 t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

f32 valley_river_x(f32 z)
{
    return 48.0f * sinf(z * 0.0105f + 0.6f) + 20.0f * sinf(z * 0.027f + 2.1f) + 7.0f * sinf(z * 0.061f + 0.3f);
}

static f32 river_slope(f32 z)
{
    return 48.0f * 0.0105f * cosf(z * 0.0105f + 0.6f) + 20.0f * 0.027f * cosf(z * 0.027f + 2.1f) +
           7.0f * 0.061f * cosf(z * 0.061f + 0.3f);
}

// Броды: река шире и мельче
static f32 ford(f32 z)
{
    return smooth(0.75f, 0.95f, sinf(z * 0.021f + 0.4f));
}

f32 valley_river_w(f32 z)
{
    return 7.5f + 2.5f * sinf(z * 0.019f + 1.3f) + 1.5f * sinf(z * 0.047f) + 3.0f * ford(z);
}

f32 valley_water_y(f32 z)
{
    return 3.0f - z * 0.008f;
}

f32 valley_river_dist(f32 x, f32 z)
{
    f32 k = river_slope(z);
    return fabsf(x - valley_river_x(z)) / sqrtf(1.0f + k * k);
}

V3 valley_spawn(void)
{
    const f32 z = 30.0f;
    f32 x = valley_river_x(z) + valley_river_w(z) + 14.0f;
    return v3(x, valley_height(x, z), z);
}

f32 valley_forest(f32 x, f32 z)
{
    // Рощи пятнами; у воды полоса прибрежных деревьев; у места появления отряда поляна
    f32 f = smooth(0.48f, 0.72f, valley_fbm(x * 0.009f + 40.0f, z * 0.009f - 13.0f, 4));
    f32 d = valley_river_dist(x, z) - valley_river_w(z);
    f32 bank = smooth(2.0f, 6.0f, d) * smooth(26.0f, 10.0f, d) * 0.55f;
    f = clampf(fmaxf(f, bank * valley_fbm(x * 0.04f, z * 0.04f, 2) * 1.6f), 0.0f, 1.0f);
    f32 sx = valley_river_x(30.0f) + valley_river_w(30.0f) + 14.0f - x, sz = 30.0f - z;
    return f * smooth(16.0f, 45.0f, sqrtf(sx * sx + sz * sz));
}

f32 valley_dry(f32 x, f32 z)
{
    return smooth(0.55f, 0.78f, valley_fbm(x * 0.014f - 70.0f, z * 0.014f + 5.0f, 4));
}

f32 valley_height_raw(f32 x, f32 z)
{
    f32 wy = valley_water_y(z);
    f32 d = valley_river_dist(x, z), w = valley_river_w(z);
    f32 n1 = valley_fbm(x * 0.006f, z * 0.006f, 4);
    f32 n2 = valley_fbm(x * 0.025f + 31.0f, z * 0.025f - 17.0f, 4);
    // Пойма у реки, дальше склоны, к краям — горы
    f32 plain = 22.0f + 40.0f * n1;
    f32 up = fmaxf(d - plain, 0.0f);
    f32 hill = 0.06f * up + 300.0f * (1.0f - expf(-(up / 420.0f) * (up / 420.0f)));
    hill *= 0.55f + 0.9f * n2;
    // Уступы: где обрывы, где пологие террасы; на высоких горах не нужны
    // Высота ступени и её край гуляют, чтобы уступы не шли ровными горизонталями
    f32 m = smooth(0.45f, 0.62f, valley_fbm(x * 0.011f + 50.0f, z * 0.011f - 20.0f, 3));
    f32 step = 4.0f + 5.0f * valley_fbm(x * 0.004f - 11.0f, z * 0.004f + 3.0f, 2);
    f32 warp = (valley_fbm(x * 0.02f + 5.0f, z * 0.02f + 9.0f, 2) - 0.5f) * step * 1.6f;
    f32 t = (hill + warp) / step, fl = floorf(t);
    f32 e = lerpf(0.45f, 0.07f, m);
    f32 terr = (fl + smooth(0.5f - e, 0.5f + e, t - fl)) * step - warp;
    hill = lerpf(hill, terr, (0.1f + 0.9f * m) * smooth(90.0f, 50.0f, hill));
    f32 bumps = (valley_fbm(x * 0.08f, z * 0.08f, 2) - 0.5f) * 0.7f;
    f32 h = wy + 1.4f + (n2 - 0.5f) * 1.2f * clampf(d / 25.0f, 0.0f, 1.0f) + bumps * 0.8f + hill;
    // Русло и берег
    f32 bank = w + 3.5f + 2.0f * n2;
    if (d < bank) {
        f32 depth = lerpf(1.4f, 0.35f, ford(z));
        f32 bed = wy - 0.15f - depth * (1.0f - (d / w) * (d / w));
        h = d < w ? bed : lerpf(wy - 0.15f, h, smooth(0.0f, 1.0f, (d - w) / (bank - w)));
    }
    return h;
}

static void init_rows(void *ctx, u32 begin, u32 end)
{
    f32 *h = ctx;
    for (u32 i = begin; i < end; i++)
        for (u32 j = 0; j < VALLEY_RES; j++)
            h[i * VALLEY_RES + j] = valley_height_raw((f32)j - VALLEY_HALF, (f32)i - VALLEY_HALF);
}

static void init_far_rows(void *ctx, u32 begin, u32 end)
{
    f32 *h = ctx;
    for (u32 i = begin; i < end; i++)
        for (u32 j = 0; j < VALLEY_FAR_RES; j++)
            h[i * VALLEY_FAR_RES + j] = valley_height_raw((f32)j * VALLEY_FAR_STEP - VALLEY_FAR_HALF, (f32)i * VALLEY_FAR_STEP - VALLEY_FAR_HALF);
}

void valley_init(Arena *perm)
{
    if (g_h) return;
    f32 *h = ARENA_PUSH_ARRAY(perm, f32, (size_t)VALLEY_RES * VALLEY_RES);
    f32 *far = ARENA_PUSH_ARRAY(perm, f32, (size_t)VALLEY_FAR_RES * VALLEY_FAR_RES);
    if (!h || !far) return;
    jobs_for(VALLEY_RES, 8, init_rows, h);
    jobs_for(VALLEY_FAR_RES, 8, init_far_rows, far);
    g_h = h;
    g_far = far;
}

// Грубая карта: билинейно; за её краем — по формуле
static f32 far_height(f32 x, f32 z)
{
    f32 u = (x + VALLEY_FAR_HALF) / VALLEY_FAR_STEP, w = (z + VALLEY_FAR_HALF) / VALLEY_FAR_STEP;
    if (!g_far || u < 0.0f || w < 0.0f || u >= VALLEY_FAR_RES - 1 || w >= VALLEY_FAR_RES - 1) return valley_height_raw(x, z);
    i32 j = (i32)u, i = (i32)w;
    f32 fx = u - (f32)j, fz = w - (f32)i;
    const f32 *r0 = g_far + (size_t)i * VALLEY_FAR_RES + j, *r1 = r0 + VALLEY_FAR_RES;
    return lerpf(lerpf(r0[0], r0[1], fx), lerpf(r1[0], r1[1], fx), fz);
}

f32 valley_height(f32 x, f32 z)
{
    f32 u = x + VALLEY_HALF, w = z + VALLEY_HALF;
    if (!g_h || u < 0.0f || w < 0.0f || u >= VALLEY_RES - 1 || w >= VALLEY_RES - 1) return far_height(x, z);
    i32 j = (i32)u, i = (i32)w;
    f32 fx = u - (f32)j, fz = w - (f32)i;
    const f32 *r0 = g_h + (size_t)i * VALLEY_RES + j, *r1 = r0 + VALLEY_RES;
    // Как треугольники сетки рельефа: диагональ из (0,0) в (1,1)
    f32 h = fx >= fz ? r0[0] + (r0[1] - r0[0]) * fx + (r1[1] - r0[1]) * fz : r0[0] + (r1[0] - r0[0]) * fz + (r1[1] - r1[0]) * fx;
    // У края подробной карты — плавно к грубой, без ступеньки
    f32 edge = clampf((fmaxf(fabsf(x), fabsf(z)) - (VALLEY_HALF - 8.0f)) / 8.0f, 0.0f, 1.0f);
    return edge > 0.0f ? lerpf(h, far_height(x, z), edge) : h;
}

V3 valley_normal(f32 x, f32 z)
{
    const f32 e = 1.0f;
    f32 dx = valley_height(x + e, z) - valley_height(x - e, z);
    f32 dz = valley_height(x, z + e) - valley_height(x, z - e);
    return v3_norm(v3(-dx, 2.0f * e, -dz));
}

#define C_LUSH v3(0.05f, 0.078f, 0.022f)

f32 valley_ground_dry(f32 x, f32 z, f32 h)
{
    f32 wy = valley_water_y(z), d = valley_river_dist(x, z);
    return clampf(valley_dry(x, z) + smooth(25.0f, 80.0f, h - wy) * 0.25f - smooth(30.0f, 8.0f, d) * 0.3f, 0.0f, 1.0f);
}

// Земля: сочная трава у воды, сухая на возвышенностях, подстилка в рощах, проплешины, ил и галька у реки.
// За краем подробной карты деревьев нет, поэтому лес там — тёмная хвоя пятнами
V3 valley_color(f32 x, f32 z, f32 h, V3 n)
{
    bool far = fabsf(x) > VALLEY_HALF || fabsf(z) > VALLEY_HALF;
    f32 wy = valley_water_y(z), d = valley_river_dist(x, z), w = valley_river_w(z);
    f32 dry = valley_ground_dry(x, z, h);
    V3 c = v3_lerp(C_LUSH, v3(0.15f, 0.13f, 0.06f), dry);
    c = v3_lerp(c, far ? v3(0.035f, 0.05f, 0.028f) : v3(0.085f, 0.07f, 0.04f), smooth(0.45f, 0.85f, valley_forest(x, z)) * (far ? 0.9f : 0.75f));
    f32 bare = smooth(0.68f, 0.8f, valley_fbm(x * 0.05f + 9.0f, z * 0.05f - 4.0f, 3));
    c = v3_lerp(c, v3(0.15f, 0.115f, 0.075f), bare * 0.7f);
    f32 gravel = smooth(0.5f, 0.62f, valley_fbm(x * 0.09f - 3.0f, z * 0.09f + 8.0f, 2));
    c = v3_lerp(c, v3_lerp(v3(0.09f, 0.075f, 0.055f), v3(0.26f, 0.24f, 0.20f), gravel), smooth(w + 4.0f, w + 0.5f, d));
    if (h < wy - 0.1f && d < w + 2.0f) c = v3(0.10f, 0.09f, 0.07f);
    return v3_lerp(c, v3(0.15f, 0.115f, 0.08f), smooth(0.85f, 0.7f, n.y) * 0.6f);
}
