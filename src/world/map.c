#include "map.h"

#include "../core/jobs.h"
#include "valley.h"
#include "../core/palette.h"

#define MAP_N ((u32)(2.0f * MAP_HALF / MAP_STEP) + 1u)  // узлов крупного рельефа на сторону
#define MAP_S (MAP_N + 2u)                              // с полем в узел за краем: выборке нужны соседи

// Лагерь (зал, арена, Скудель): ровные площадки и мелкие дюны до CAMP_R0, к CAMP_R1 — дюны пустоши
#define CAMP_R0 400.0f
#define CAMP_R1 1300.0f
// Крупного рельефа (увалы, гряды) у лагеря и долины нет: от RELIEF_R0 до RELIEF_R1 он вырастает
#define RELIEF_R0 2600.0f
#define RELIEF_R1 5500.0f
// Долина: своя высота в квадрате VALLEY_HALF, склоны сходят в дюны ещё на VALLEY_FADE
#define VALLEY_FADE 700.0f

#define C_STONE v3(0.27f, 0.245f, 0.21f)

static struct {
    f32 *h;      // крупный рельеф, м: основание и гряды
    u8  *dune;   // размах дюн 0..255
    u8  *rock;   // каменистость гряд 0..255
    u32  seed;
} M;

static u32 hash_u(i32 x, i32 z, u32 seed)
{
    u32 h = (u32)x * 374761393u + (u32)z * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Шум значений; seed 0 — прежний рельеф лагеря (дюны те же, что были)
static f32 hash2(i32 x, i32 z, u32 seed) { return (f32)(hash_u(x, z, seed) & 0xFFFFFF) / 16777215.0f; }

static f32 vnoise(f32 x, f32 z, u32 seed)
{
    i32 ix = (i32)floorf(x), iz = (i32)floorf(z);
    f32 fx = x - (f32)ix, fz = z - (f32)iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    f32 a = hash2(ix, iz, seed), b = hash2(ix + 1, iz, seed), c = hash2(ix, iz + 1, seed), d = hash2(ix + 1, iz + 1, seed);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}

// Мелкие дюны у лагеря: волны 80, 40 и 20 м
static f32 dunes_near(f32 x, f32 z)
{
    f32 d = 0.0f, amp = 1.0f, freq = 0.012f;
    for (int i = 0; i < 3; i++) {
        d += vnoise(x * freq, z * freq, 0) * amp;
        amp *= 0.5f;
        freq *= 2.1f;
    }
    return d;
}

// Дюны пустоши 0..1: валы ~300 м поперёк ветра (вытянуты по Z), на них помельче. Склоны до ~30°
static f32 dunes_big(f32 x, f32 z)
{
    u32 s = M.seed + 31;
    f32 w = (vnoise(x / 900.0f, z / 900.0f, s) - 0.5f) * 260.0f;
    f32 d = vnoise((x + w) / 300.0f, z / 900.0f, s + 1) + 0.35f * vnoise(x / 170.0f, z / 260.0f, s + 2) +
            0.12f * vnoise(x / 75.0f, z / 75.0f, s + 3);
    return d / 1.47f;
}

// Площадка Скудели ровная, с запасом вокруг
static f32 town_flat(f32 x, f32 z)
{
    f32 dx = fmaxf(fmaxf(44.0f - x, x - 112.0f), 0.0f), dz = fmaxf(fmaxf(-30.0f - z, z - 38.0f), 0.0f);
    return clampf((sqrtf(dx * dx + dz * dz) - 30.0f) / 50.0f, 0.0f, 1.0f);
}

// Площадка Остова (руина к западу от зала, game/ostov.c) ровная, с запасом вокруг
static f32 ostov_flat(f32 x, f32 z)
{
    f32 dx = fmaxf(fmaxf(-100.0f - x, x + 60.0f), 0.0f), dz = fmaxf(fmaxf(-52.0f - z, z - 22.0f), 0.0f);
    return clampf((sqrtf(dx * dx + dz * dz) - 6.0f) / 40.0f, 0.0f, 1.0f);
}

// Площадка Мерильни (цех к югу от зала, game/showhall.c) ровная, с запасом вокруг
static f32 showhall_flat(f32 x, f32 z)
{
    f32 dx = fmaxf(fmaxf(-22.0f - x, x - 22.0f), 0.0f), dz = fmaxf(fmaxf(-96.0f - z, z + 68.0f), 0.0f);
    return clampf((sqrtf(dx * dx + dz * dz) - 8.0f) / 40.0f, 0.0f, 1.0f);
}

// Лагерь: площадки у зала, посёлка, Остова и Мерильни ровные, дальше мелкие дюны
static f32 camp_height(f32 x, f32 z, f32 r)
{
    f32 flat = fminf(fminf(fminf(clampf((r - 45.0f) / 60.0f, 0.0f, 1.0f), town_flat(x, z)), ostov_flat(x, z)), showhall_flat(x, z));
    if (flat <= 0.0f) return -0.05f;
    return -0.05f + flat * flat * (dunes_near(x, z) - 0.6f) * 9.0f;
}

typedef struct Coarse {
    f32 h, dune, rock;
} Coarse;

// Шум градиентов, примерно −1..1: нули идут плавными линиями, без сетки шума значений.
// Градиент в узле — одно из 16 направлений
static f32 gnoise(f32 x, f32 z, u32 seed)
{
    static const f32 gx[16] = { 1.0f, 0.924f, 0.707f, 0.383f, 0.0f, -0.383f, -0.707f, -0.924f,
                                -1.0f, -0.924f, -0.707f, -0.383f, 0.0f, 0.383f, 0.707f, 0.924f };
    i32 ix = (i32)floorf(x), iz = (i32)floorf(z);
    f32 fx = x - (f32)ix, fz = z - (f32)iz;
    f32 d[4];
    for (u32 k = 0; k < 4; k++) {
        u32 g = hash_u(ix + (i32)(k & 1), iz + (i32)(k >> 1), seed) >> 28;
        d[k] = gx[g] * (fx - (f32)(k & 1)) + gx[(g + 4) & 15] * (fz - (f32)(k >> 1));
    }
    f32 ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f), uz = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);
    return 1.4f * lerpf(lerpf(d[0], d[1], ux), lerpf(d[2], d[3], ux), uz);
}

// Крупный рельеф в точке по формуле: увалы, котловины, гряды; размах дюн и каменистость
static Coarse coarse_raw(f32 x, f32 z)
{
    u32 s = M.seed;
    f32 grow = smoothstepf(RELIEF_R0, RELIEF_R1, sqrtf(x * x + z * z));
    f32 base = (gnoise(x / 6500.0f, z / 6500.0f, s) + 0.45f * gnoise(x / 2900.0f, z / 2900.0f, s + 1)) * 55.0f;
    // Гряды: гребни вдоль нулей шума, изломанные сдвигом координат; есть не везде
    f32 wx = x + gnoise(x / 3000.0f, z / 3000.0f, s + 11) * 700.0f;
    f32 wz = z + gnoise(x / 3000.0f + 5.3f, z / 3000.0f - 2.1f, s + 12) * 700.0f;
    // Вдоль гряд шум растянут втрое: гребни тянутся на километры (с юго-запада на северо-восток)
    f32 ax = wx * 0.866f + wz * 0.5f, az = wz * 0.866f - wx * 0.5f;
    f32 r1 = 1.0f - fminf(fabsf(gnoise(ax / 3600.0f, az / 1200.0f, s + 13)), 1.0f);
    f32 r2 = 1.0f - fminf(fabsf(gnoise(ax / 900.0f, az / 420.0f, s + 14)), 1.0f);
    f32 mask = smoothstepf(-0.2f, 0.3f, gnoise(x / 5200.0f, z / 5200.0f, s + 15));
    r1 *= r1 * r1;
    f32 ridge = r1 * r1 * (0.75f + 0.25f * r2 * r2) * mask * grow;
    // Где высокие дюны, где плоский такыр; на грядах дюны мельче
    f32 dune = (0.15f + 0.85f * smoothstepf(-0.35f, 0.35f, gnoise(x / 3200.0f, z / 3200.0f, s + 16))) *
               (1.0f - 0.8f * clampf(ridge * 1.5f, 0.0f, 1.0f));
    return (Coarse){ base * grow + ridge * 220.0f, dune, clampf(ridge * 1.6f, 0.0f, 1.0f) };
}

static void coarse_rows(void *ctx, u32 begin, u32 end)
{
    (void)ctx;
    for (u32 i = begin; i < end; i++)
        for (u32 j = 0; j < MAP_S; j++) {
            Coarse c = coarse_raw(-MAP_HALF + ((f32)j - 1.0f) * MAP_STEP, -MAP_HALF + ((f32)i - 1.0f) * MAP_STEP);
            size_t k = (size_t)i * MAP_S + j;
            M.h[k] = c.h;
            M.dune[k] = (u8)(c.dune * 255.0f + 0.5f);
            M.rock[k] = (u8)(c.rock * 255.0f + 0.5f);
        }
}

bool map_init(Arena *perm, u64 seed)
{
    M.seed = (u32)(seed * 0x9E3779B97F4A7C15ull >> 32);
    size_t n = (size_t)MAP_S * MAP_S;
    f32 *h = ARENA_PUSH_ARRAY(perm, f32, n);
    M.dune = ARENA_PUSH_ARRAY(perm, u8, n);
    M.rock = ARENA_PUSH_ARRAY(perm, u8, n);
    if (!h || !M.dune || !M.rock) return false;
    M.h = h;
    jobs_for(MAP_S, 8, coarse_rows, NULL);
    return true;
}

// Катмулл-Ром по четырём узлам
static f32 cubic(f32 a, f32 b, f32 c, f32 d, f32 t)
{
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}

// Крупный рельеф: высота — гладко по 4×4 узлам, остальное по 2×2. За краем карты — край
static Coarse coarse_at(f32 x, f32 z)
{
    if (!M.h) return coarse_raw(x, z);
    f32 u = clampf((x + MAP_HALF) / MAP_STEP + 1.0f, 1.0f, (f32)MAP_N - 0.001f);
    f32 v = clampf((z + MAP_HALF) / MAP_STEP + 1.0f, 1.0f, (f32)MAP_N - 0.001f);
    u32 j = (u32)u, i = (u32)v;
    f32 fx = u - (f32)j, fz = v - (f32)i;
    f32 row[4];
    for (u32 r = 0; r < 4; r++) {
        const f32 *p = M.h + (size_t)(i + r - 1) * MAP_S + j - 1;
        row[r] = cubic(p[0], p[1], p[2], p[3], fx);
    }
    size_t k = (size_t)i * MAP_S + j;
    f32 d = lerpf(lerpf(M.dune[k], M.dune[k + 1], fx), lerpf(M.dune[k + MAP_S], M.dune[k + MAP_S + 1], fx), fz);
    f32 r = lerpf(lerpf(M.rock[k], M.rock[k + 1], fx), lerpf(M.rock[k + MAP_S], M.rock[k + MAP_S + 1], fx), fz);
    return (Coarse){ cubic(row[0], row[1], row[2], row[3], fz), d / 255.0f, r / 255.0f };
}

f32 map_valley(f32 x, f32 z)
{
    f32 dx = fmaxf(fabsf(x - VALLEY_ORIGIN.x) - VALLEY_HALF, 0.0f), dz = fmaxf(fabsf(z - VALLEY_ORIGIN.z) - VALLEY_HALF, 0.0f);
    if (dx >= VALLEY_FADE || dz >= VALLEY_FADE) return 0.0f;
    if (dx == 0.0f && dz == 0.0f) return 1.0f;
    // Склоны сходят в дюны неровно: где за 250 м, где за 700
    f32 fade = VALLEY_FADE * (0.35f + 0.65f * smoothstepf(-0.5f, 0.5f, gnoise(x / 450.0f, z / 450.0f, 77)));
    return 1.0f - smoothstepf(0.0f, fade, sqrtf(dx * dx + dz * dz));
}

// Точка карты: высота и то, из чего потом берётся цвет
typedef struct Sample {
    f32    h;
    f32    desert;  // доля пустоши за лагерем 0..1
    f32    valley;  // доля долины 0..1
    f32    vh;      // высота долины в её координатах
    Coarse c;
} Sample;

static Sample sample(f32 x, f32 z)
{
    Sample s = { 0 };
    f32 r = sqrtf(x * x + z * z);
    if (r <= CAMP_R0) s.h = camp_height(x, z, r);
    else {
        // Пустошь: крупный рельеф и дюны его размаха
        s.c = coarse_at(x, z);
        s.desert = smoothstepf(CAMP_R0, CAMP_R1, r);
        s.h = s.c.h + s.c.dune * dunes_big(x, z) * 45.0f;
        if (s.desert < 1.0f) s.h = lerpf(camp_height(x, z, r), s.h, s.desert);
    }
    s.valley = map_valley(x, z);
    if (s.valley > 0.0f) {
        s.vh = valley_height(x - VALLEY_ORIGIN.x, z - VALLEY_ORIGIN.z);
        s.h = lerpf(s.h, VALLEY_ORIGIN.y + s.vh, s.valley);
    }
    return s;
}

f32 map_height(f32 x, f32 z) { return sample(x, z).h; }

static u32 pack_color(V3 a, f32 look)
{
    u32 r = (u32)(clampf(a.x, 0.0f, 1.0f) * 255.0f + 0.5f), g = (u32)(clampf(a.y, 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 b = (u32)(clampf(a.z, 0.0f, 1.0f) * 255.0f + 0.5f), l = (u32)(clampf(look, 0.0f, 1.0f) * 255.0f + 0.5f);
    return r | g << 8 | b << 16 | l << 24;
}

static u32 color_of(f32 x, f32 z, const Sample *s)
{
    V3 a = PAL_SAND;
    f32 look = 0.0f;
    if (s->desert > 0.0f) {
        // Песок пятнами светлее и темнее, на грядах камень
        f32 tone = 0.9f + 0.2f * vnoise(x / 700.0f, z / 700.0f, M.seed + 21);
        a = v3_lerp(a, v3_lerp(v3_mul(PAL_SAND, tone), C_STONE, s->c.rock * 0.8f), s->desert);
        look = s->c.rock * 0.85f * s->desert;
    }
    if (s->valley > 0.0f) {
        f32 lx = x - VALLEY_ORIGIN.x, lz = z - VALLEY_ORIGIN.z;
        a = v3_lerp(a, valley_color(lx, lz, s->vh, valley_normal(lx, lz)), s->valley);
        look = lerpf(look, 1.0f, s->valley);
    }
    return pack_color(a, look);
}

u32 map_color(f32 x, f32 z, f32 h)
{
    Sample s = sample(x, z);
    s.h = h;
    return color_of(x, z, &s);
}

typedef struct FillJob {
    f32  x0, z0, step;
    u32  w;
    f32 *h;
    u32 *c;
} FillJob;

static void fill_rows(void *ctx, u32 begin, u32 end)
{
    const FillJob *j = ctx;
    for (u32 i = begin; i < end; i++)
        for (u32 k = 0; k < j->w; k++) {
            f32 x = j->x0 + (f32)k * j->step, z = j->z0 + (f32)i * j->step;
            Sample s = sample(x, z);
            j->h[(size_t)i * j->w + k] = s.h;
            j->c[(size_t)i * j->w + k] = color_of(x, z, &s);
        }
}

void map_fill(f32 x0, f32 z0, f32 step, u32 w, u32 h, f32 *height, u32 *color, void *user)
{
    (void)user;
    FillJob j = { x0, z0, step, w, height, color };
    // Полоса при сдвиге камеры — сразу здесь, всё кольцо целиком — по потокам
    jobs_for(h, w * h > 4096 ? 4 : h, fill_rows, &j);
}
