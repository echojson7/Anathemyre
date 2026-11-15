// Огни кадра (6е, docs/lamps.md). Лампа карты — поток в люменах и цвет кельвинами; свету кадра нужна сила света
// в канделах: I = Φ / (4π · доля сферы), где доля сферы у направленной лампы — (1 + назад) / 2 (light.glsl
// светит назад долей back). Дальность — где освещённость падает до LAMP_E_CUT лк (дальше окно гасит её в ноль)
#include "lamps.h"
#include "../core/hash.h"
#include "../core/math3.h"
#include "../sim/ground.h"

static constexpr f32 LAMP_E_CUT = 2.0f;      // лк: дальше лампа не светит (окно light.glsl доводит до нуля)
static constexpr f32 LAMP_RANGE_MAX = 16.0f;  // м
static constexpr f32 LAMPS_VIEW_R = 96.0f, LAMPS_FADE_R = 80.0f;  // круг ламп кадра и начало угасания, м

void lamp_kelvin_rgb(f32 kelvin, f32 rgb[3]) {
    f64 t = (f64)f_clamp(kelvin, 1667.0f, 25000.0f);
    f64 t2 = t * t, t3 = t2 * t;
    f64 x = t <= 4000.0 ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                        : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    f64 x2 = x * x, x3 = x2 * x;
    f64 y = t <= 2222.0   ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
            : t <= 4000.0 ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                          : 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    // xyY (Y = 1) → XYZ → линейный sRGB (D65)
    f64 X = x / y, Z = (1.0 - x - y) / y;
    f64 r = 3.2404542 * X - 1.5371385 - 0.4985314 * Z;
    f64 g = -0.9692660 * X + 1.8760108 + 0.0415560 * Z;
    f64 b = 0.0556434 * X - 0.2040259 + 1.0572252 * Z;
    r = r < 0 ? 0 : r;
    g = g < 0 ? 0 : g;
    b = b < 0 ? 0 : b;
    // Отрезанное отрицательное меняет яркость: вернуть Y = 1
    f64 lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    rgb[0] = (f32)(r / lum);
    rgb[1] = (f32)(g / lum);
    rgb[2] = (f32)(b / lum);
}

static Vec3 rel_of(const WPos &eye, i32 cx, i32 cz, f32 x, f32 y, f32 z) {
    Vec2 d = wpos_delta_xz(eye.cx, eye.cz, eye.x, eye.z, cx, cz, x, z);
    return {d.x, y - eye.y, d.y};
}

// Сила света (кд) и дальность (м) лампы с потоком lm
static void lamp_power(f32 lm, bool directed, f32 back, f32 half_len, f32 &cd, f32 &range) {
    f32 sphere = directed ? 0.5f * (1.0f + back) : 1.0f;
    cd = lm / (4.0f * PI_F * sphere);
    range = f_min(f_sqrt(cd / LAMP_E_CUT) + half_len, LAMP_RANGE_MAX);
}

static void build_map_lamps(const Lamps &l, const WPos &eye, RenderPointLight *out, u32 &n, u32 cap) {
    for (u32 i = 0; i < l.count && n < cap; ++i) {
        Vec3 rel = rel_of(eye, l.cx[i], l.cz[i], l.x[i], l.y[i], l.z[i]);
        f32 d = length(rel);
        if (d > LAMPS_VIEW_R) continue;
        f32 fade = d > LAMPS_FADE_R ? 1.0f - (d - LAMPS_FADE_R) / (LAMPS_VIEW_R - LAMPS_FADE_R) : 1.0f;
        f32 rgb[3];
        lamp_kelvin_rgb(l.kelvin[i], rgb);
        bool directed = l.fx[i] != 0.0f || l.fy[i] != 0.0f || l.fz[i] != 0.0f;
        f32 half_len = f_sqrt(l.ax[i] * l.ax[i] + l.ay[i] * l.ay[i] + l.az[i] * l.az[i]);
        f32 cd, range;
        lamp_power(l.lm[i], directed, l.back[i], half_len, cd, range);
        cd *= fade * fade * (3.0f - 2.0f * fade);
        RenderPointLight &o = out[n++];
        o = RenderPointLight{rel.x, rel.y, rel.z, range, rgb[0] * cd, rgb[1] * cd, rgb[2] * cd, l.size[i],
                             l.ax[i], l.ay[i], l.az[i], l.back[i], l.fx[i], l.fy[i], l.fz[i], LIGHT_SHADOW};
    }
}

// Тестовые огни (F3): по одному в клетке 8 м мира в круге TEST_LIGHTS_R м вокруг опоры камеры, место, высота,
// поток и цвет — из хеша клетки: костры (тёплые колбы) и реже холодные лампы. Проверяют списки froxels и свет
constexpr f32 TEST_LIGHT_CELL = 8.0f, TEST_LIGHTS_R = 44.0f;
static void build_test_lights(const Camera &cam, const Terrain &t, const WPos &eye, RenderPointLight *ls, u32 &n,
                              u32 cap) {
    const i32 per_chunk = (i32)(CHUNK_M / TEST_LIGHT_CELL), rc = (i32)(TEST_LIGHTS_R / TEST_LIGHT_CELL) + 1;
    const WPos &c = cam.pivot;
    i32 gx0 = c.cx * per_chunk + f_floor_i(c.x / TEST_LIGHT_CELL), gz0 = c.cz * per_chunk + f_floor_i(c.z / TEST_LIGHT_CELL);
    for (i32 dz = -rc; dz <= rc; ++dz)
        for (i32 dx = -rc; dx <= rc && n < cap; ++dx) {
            i32 gx = gx0 + dx, gz = gz0 + dz;
            u64 h = hash_combine(hash_mix((u64)(u32)gx), (u64)(u32)gz);
            auto u01 = [&](u32 k) { return (f32)((h >> (k * 12)) & 0xFFF) / 4095.0f; };
            WPos p;
            p.cx = gx >= 0 ? gx / per_chunk : -((-gx - 1) / per_chunk) - 1;
            p.cz = gz >= 0 ? gz / per_chunk : -((-gz - 1) / per_chunk) - 1;
            p.x = ((f32)(gx - p.cx * per_chunk) + 0.2f + 0.6f * u01(0)) * TEST_LIGHT_CELL;
            p.z = ((f32)(gz - p.cz * per_chunk) + 0.2f + 0.6f * u01(1)) * TEST_LIGHT_CELL;
            Vec2 d = wpos_delta_xz(c, p);
            if (d.x * d.x + d.y * d.y > TEST_LIGHTS_R * TEST_LIGHTS_R) continue;
            p.y = ground_at(t, p).height + 0.8f + 2.5f * u01(2);
            Vec3 rel = rel_of(eye, p.cx, p.cz, p.x, p.y, p.z);
            bool cold = u01(4) < 0.25f;
            f32 rgb[3];
            lamp_kelvin_rgb(cold ? 6500.0f : 1900.0f, rgb);
            f32 cd, range;
            lamp_power(400.0f + 1600.0f * u01(3), false, 0.0f, 0.0f, cd, range);
            ls[n++] = RenderPointLight{rel.x, rel.y, rel.z, range, rgb[0] * cd, rgb[1] * cd, rgb[2] * cd, 0.15f,
                                       0, 0, 0, 0, 0, 0, 0, LIGHT_SHADOW};
        }
}

void lamps_build(const Lamps *lamps, bool test, const Camera &cam, const Terrain *t, const WPos &eye, Arena &frame,
                 RenderFrame &out) {
    const u32 nt = (u32)(2 * (i32)(TEST_LIGHTS_R / TEST_LIGHT_CELL) + 3);
    u32 cap = (lamps ? lamps->count : 0) + (test && t ? nt * nt : 0);
    if (cap > RENDER_MAX_LIGHTS) cap = RENDER_MAX_LIGHTS;
    if (!cap) return;
    RenderPointLight *ls = arena_push<RenderPointLight>(frame, cap);
    u32 n = 0;
    if (lamps) build_map_lamps(*lamps, eye, ls, n, cap);
    if (test && t) build_test_lights(cam, *t, eye, ls, n, cap);
    out.lights = {ls, n};
}
