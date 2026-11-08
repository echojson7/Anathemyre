#include "sky.h"
#include "../core/text.h"
#include <math.h>
#include <stdio.h>

static constexpr f64 PI_D = 3.14159265358979323846;
static constexpr f64 SYNODIC_DAYS = 29.530588;  // от новолуния до новолуния
static constexpr f64 SIDEREAL_PER_DAY = 366.2422 / 365.2422;  // звёзды обгоняют солнце на оборот за год
static constexpr f32 R_GROUND = 6360e3f, R_TOP = 6460e3f;  // как в atmosphere.glsl
static constexpr f32 SUN_RADIUS = 0.00465f, MOON_RADIUS = 0.0045f;  // угловые радиусы, рад

bool sky_parse(SkyDesc &d, const u8 *text, usize size, char *err, usize err_cap) {
    struct Key {
        const char *name;
        f32 *v;
        u32 n;
    };
    const Key keys[] = {
        {"latitude", &d.latitude, 1},     {"season", &d.season, 1},       {"moon_age", &d.moon_age, 1},
        {"sun", &d.sun, 1},               {"moon_boost", &d.moon_boost, 1}, {"moon_albedo", &d.moon_albedo, 1},
        {"rayleigh", &d.rayleigh, 1},     {"mie", &d.mie, 1},             {"mie_g", &d.mie_g, 1},
        {"haze", &d.haze, 1},             {"haze_height", &d.haze_height, 1}, {"haze_tint", d.haze_tint, 3},
        {"ozone", &d.ozone, 1},           {"alt0", &d.alt0, 1},           {"ground", d.ground, 3},
        {"stars", &d.stars, 1},           {"night_glow", &d.night_glow, 1}, {"exposure_ev", &d.exposure_ev, 1},
        {"adapt", &d.adapt, 1},           {"exposure_ref", &d.exposure_ref, 1}, {"night_blue", &d.night_blue, 1},
        {"adapt_dark", &d.adapt_dark, 1}, {"adapt_light", &d.adapt_light, 1},
        {"sky_gain", &d.sky_gain, 1},     {"desert_mie", &d.desert_mie, 1}, {"desert_haze", &d.desert_haze, 1},
        {"desert_haze_height", &d.desert_haze_height, 1}, {"desert_haze_tint", d.desert_haze_tint, 3},
        {"desert_ground", d.desert_ground, 3},
    };
    TextReader r = text_reader(text, size);
    TextLine l;
    while (text_next(r, l)) {
        const Key *k = nullptr;
        for (const Key &q : keys)
            if (str_eq(l.w[0], q.name)) k = &q;
        if (!k) {
            snprintf(err, err_cap, "строка %u: неизвестный ключ %.*s", l.line, (int)l.w[0].n, l.w[0].p);
            return false;
        }
        if (l.count != k->n + 1) {
            snprintf(err, err_cap, "строка %u: у %s %u чисел", l.line, k->name, k->n);
            return false;
        }
        for (u32 i = 0; i < k->n; ++i)
            if (!parse_f32(l.w[1 + i], k->v[i])) {
                snprintf(err, err_cap, "строка %u: плохое число", l.line);
                return false;
            }
    }
    if (d.haze_height < 10.0f || d.desert_haze_height < 10.0f || d.mie_g <= -0.99f || d.mie_g >= 0.99f || d.sun <= 0.0f || d.exposure_ref <= 0.0f ||
        d.adapt_dark <= 0.0f || d.adapt_light <= 0.0f) {
        snprintf(err, err_cap, "haze_height и desert_haze_height ≥ 10, |mie_g| < 0.99, sun > 0, exposure_ref > 0, adapt_dark и adapt_light > 0");
        return false;
    }
    return true;
}

f64 sky_clock_ticks(const Clock &prev, const Clock &cur, f32 alpha) {
    f64 a = (f64)prev.tick, b = (f64)cur.tick;
    // Скачок часов (отладка) или первый кадр — без протяжки через полсуток
    if (cur.tick < prev.tick || cur.tick - prev.tick > (u64)cur.day_ticks / 8) return b;
    return a + (b - a) * (f64)alpha;
}

f32 sky_hour(const Clock &c, f64 ticks) {
    f64 days = ticks / (f64)(c.day_ticks ? c.day_ticks : 1);
    return (f32)((days - floor(days)) * 24.0);
}

// Направление на точку небесной сферы: часовой угол h, склонение dec, широта lat. Оси мира: x — восток,
// y — вверх, z — север
static void celestial(f64 h, f64 dec, f64 lat, f32 out[3]) {
    out[0] = (f32)(-cos(dec) * sin(h));
    out[1] = (f32)(sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(h));
    out[2] = (f32)(cos(lat) * sin(dec) - sin(lat) * cos(dec) * cos(h));
}

struct Air {
    f32 rayleigh[3], mie[3], mie_abs[3], haze[3], haze_abs[3], ozone[3];
    f32 hr, hm, hh;
};

static void ext_at(const Air &a, f32 h, f32 out[3]) {
    h = h > 0 ? h : 0;
    f32 dr = expf(-h / a.hr), dm = expf(-h / a.hm), dh = expf(-h / a.hh);
    f32 oz = 1.0f - fabsf(h - 25e3f) / 15e3f;
    oz = oz > 0 ? oz : 0;
    for (u32 k = 0; k < 3; ++k)
        out[k] = a.rayleigh[k] * dr + (a.mie[k] + a.mie_abs[k]) * dm + (a.haze[k] + a.haze_abs[k]) * dh + a.ozone[k] * oz;
}

// Пропускание от глаза (высота alt) до края атмосферы к направлению dir. Светило у горизонта уходит за
// планету плавно, по своему угловому радиусу
static void transmittance(const Air &a, f32 alt, const f32 dir[3], f32 radius, f32 out[3]) {
    f32 r = R_GROUND + alt, mu = dir[1];
    f32 hz = -acosf(R_GROUND / r);  // наклон горизонта
    f32 el = asinf(mu);
    f32 vis = (el - hz + radius) / (2.0f * radius);
    vis = vis < 0 ? 0 : (vis > 1 ? 1 : vis);
    if (vis <= 0) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    f32 mu_c = sinf(fmaxf(el, hz + 1e-4f));
    f32 disc = r * r * (mu_c * mu_c - 1.0f) + R_TOP * R_TOP;
    f32 len = -r * mu_c + sqrtf(fmaxf(disc, 0.0f));
    const u32 N = 64;
    f32 tau[3] = {}, t0 = 0;
    for (u32 i = 0; i < N; ++i) {
        f32 x = (f32)(i + 1) / (f32)N;
        f32 t1 = len * x * x, t = 0.5f * (t0 + t1), dt = t1 - t0;
        t0 = t1;
        f32 h = sqrtf(r * r + t * t + 2.0f * r * mu_c * t) - R_GROUND;
        f32 e[3];
        ext_at(a, h, e);
        for (u32 k = 0; k < 3; ++k) tau[k] += e[k] * dt;
    }
    for (u32 k = 0; k < 3; ++k) out[k] = expf(-tau[k]) * vis;
}

static f32 lum(const f32 c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }

void sky_build(const SkyDesc &desc, f32 desert, const Clock &clock, f64 ticks, f32 cam_y, RenderSky &o,
               RenderLight &key) {
    o = RenderSky{};
    // Воздух пустыни: пыль и дымка смешиваются с воздухом долины (таблицы неба пересчитаются сами)
    SkyDesc d = desc;
    f32 w = desert < 0 ? 0 : (desert > 1 ? 1 : desert);
    auto mix = [w](f32 a, f32 b) { return a + (b - a) * w; };
    d.mie = mix(desc.mie, desc.desert_mie);
    d.haze = mix(desc.haze, desc.desert_haze);
    d.haze_height = mix(desc.haze_height, desc.desert_haze_height);
    for (u32 k = 0; k < 3; ++k) {
        d.haze_tint[k] = mix(desc.haze_tint[k], desc.desert_haze_tint[k]);
        d.ground[k] = mix(desc.ground[k], desc.desert_ground[k]);
    }
    const f64 deg = PI_D / 180.0;
    f64 days = ticks / (f64)(clock.day_ticks ? clock.day_ticks : 1);
    f64 frac = days - floor(days);
    f64 lat = d.latitude * deg;
    // Солнце: полдень в 12:00, часовой угол растёт к западу
    f64 h_sun = (frac - 0.5) * 2.0 * PI_D;
    f64 dec = d.season * deg;
    celestial(h_sun, dec, lat, o.sun_dir);
    // Луна отстаёт от солнца на свой возраст: в новолуние рядом с ним, в полнолуние напротив
    f64 age = d.moon_age + days;
    f64 phase = 2.0 * PI_D * (age / SYNODIC_DAYS - floor(age / SYNODIC_DAYS));
    f64 dec_m = dec * cos(phase) + 5.1 * deg * sin(2.0 * PI_D * age / 27.2122);
    celestial(h_sun - phase, dec_m, lat, o.moon_dir);
    // Звёзды: оборот небесной сферы за звёздные сутки. Строки — оси сферы в осях мира
    f64 th = 2.0 * PI_D * days * SIDEREAL_PER_DAY;
    f32 ax[3], ay[3];
    celestial(th, 0.0, lat, ax);
    celestial(th + 0.5 * PI_D, 0.0, lat, ay);
    const f32 pole[3] = {0.0f, (f32)sin(lat), (f32)cos(lat)};
    for (u32 k = 0; k < 3; ++k) {
        o.star[0][k] = ax[k];
        o.star[1][k] = ay[k];
        o.star[2][k] = pole[k];
    }
    o.star[0][3] = d.stars;

    // Воздух: единицы файла → 1/м
    Air a;
    const f32 ray[3] = {5.802e-6f, 13.558e-6f, 33.1e-6f};
    const f32 oz[3] = {0.650e-6f, 1.881e-6f, 0.085e-6f};
    for (u32 k = 0; k < 3; ++k) {
        a.rayleigh[k] = o.rayleigh[k] = ray[k] * d.rayleigh;
        a.mie[k] = o.mie[k] = 3.996e-6f * d.mie;
        a.mie_abs[k] = o.mie_abs[k] = 4.40e-6f * d.mie;
        a.haze[k] = o.haze[k] = d.haze * 1e-3f * d.haze_tint[k];
        a.haze_abs[k] = o.haze_abs[k] = d.haze * 1e-3f * (1.0f - d.haze_tint[k]);
        a.ozone[k] = o.ozone[k] = oz[k] * d.ozone;
        o.ground[k] = d.ground[k];
    }
    a.hr = o.rayleigh_h = 8000.0f;
    a.hm = o.mie_h = 1200.0f;
    a.hh = o.haze_h = d.haze_height;
    o.mie_g = d.mie_g;
    o.alt0 = d.alt0;
    o.moon_albedo = d.moon_albedo;
    o.night_glow = d.night_glow * 1e-6f;
    o.sun_radius = SUN_RADIUS;
    o.moon_radius = MOON_RADIUS;

    o.sun_e = d.sun;
    // Луна — ламбертов шар: освещённость = солнце · альбедо · телесный угол / π · фаза
    f32 cos_el = o.sun_dir[0] * o.moon_dir[0] + o.sun_dir[1] * o.moon_dir[1] + o.sun_dir[2] * o.moon_dir[2];
    f32 alpha = (f32)PI_D - acosf(cos_el < -1 ? -1 : (cos_el > 1 ? 1 : cos_el));  // угол солнце–луна–мы
    f32 fphase = (((f32)PI_D - alpha) * cosf(alpha) + sinf(alpha)) / (f32)PI_D;
    o.moon_e = d.sun * d.moon_albedo * MOON_RADIUS * MOON_RADIUS * fphase * d.moon_boost;

    f32 alt = cam_y + d.alt0;
    alt = alt > 1.0f ? alt : 1.0f;
    transmittance(a, alt, o.sun_dir, SUN_RADIUS, o.sun_t);
    transmittance(a, alt, o.moon_dir, MOON_RADIUS, o.moon_t);
    // Глаз (docs/light.md, «Экспозиция»): «норма» — EV100 освещённости exposure_ref (лк)
    o.post[0] = d.exposure_ev;
    o.post[1] = d.adapt;
    o.post[2] = log2f(d.exposure_ref / 2.5f);
    o.post[3] = d.night_blue;
    o.sky_gain = d.sky_gain;
    o.adapt_dark = d.adapt_dark;
    o.adapt_light = d.adapt_light;

    // Главный свет light.glsl — то светило, что сейчас светит сильнее. Переход — когда солнце
    // ушло за горизонт и его свет уже меньше лунного, поэтому скачка направления не видно
    f32 es[3], em[3];
    for (u32 k = 0; k < 3; ++k) {
        es[k] = o.sun_e * o.sun_t[k];
        em[k] = o.moon_e * o.moon_t[k];
    }
    bool sun = lum(es) * (o.sun_dir[1] > 0 ? o.sun_dir[1] : 0) >= lum(em) * (o.moon_dir[1] > 0 ? o.moon_dir[1] : 0);
    for (u32 k = 0; k < 3; ++k) {
        key.sun_dir[k] = sun ? o.sun_dir[k] : o.moon_dir[k];
        key.sun_color[k] = sun ? es[k] : em[k];
    }
}
