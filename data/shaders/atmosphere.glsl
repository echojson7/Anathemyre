// Атмосфера (docs/sky.md): каркас неба и дымки, не свет. Планета — шар, воздух — слои с плотностью по высоте:
// Rayleigh (газ, синее небо), пыль высокого слоя (Mie), нижняя дымка (Mie, в пустыне — жёлтая пыль), озон
// (поглощение, синева сумерек). Таблицы считают sky_*.comp, здесь — их разметка и чтение:
//   пропускание (r, mu) 256×64 и многократное рассеяние (r, mu_s) 32×32 — при смене параметров;
//   вид неба от камеры (азимут от светила × высота) — каждый кадр, для солнца и для луны отдельно.
// Свет (light.glsl) берёт отсюда atmo_ambient(), atmo_sky() и atmo_fog(), сам алгоритм света — пользователя
#ifndef ATMOSPHERE_GLSL
#define ATMOSPHERE_GLSL
#include "common.glsl"

const float A_PI = 3.14159265;
const float R_GROUND = 6360e3;  // радиус планеты, м
const float R_TOP = 6460e3;     // верх атмосферы
const int TRANS_W = 256, TRANS_H = 64;
const int MS_N = 32;
const int SKY_W = 192, SKY_H = 128;   // солнце
const int MOON_W = 96, MOON_H = 64;   // луна: свет в миллион раз слабее, мелкая таблица достаточна

// Плотности слоёв на высоте h (м над уровнем моря)
struct Medium { vec3 scat_r, scat_m, scat_h, ext; };
Medium atmo_medium(float h) {
    FrameData f = pc.frame;
    h = max(h, 0.0);
    float dr = exp(-h / f.a_rayleigh.w);
    float dm = exp(-h / f.a_mie.w);
    float dh = exp(-h / f.a_haze.w);
    float oz = max(0.0, 1.0 - abs(h - 25e3) / 15e3);
    Medium m;
    m.scat_r = f.a_rayleigh.rgb * dr;
    m.scat_m = f.a_mie.rgb * dm;
    m.scat_h = f.a_haze.rgb * dh;
    m.ext = m.scat_r + m.scat_m + m.scat_h + f.a_mie_abs.rgb * dm + f.a_haze_abs.rgb * dh + f.a_ozone.rgb * oz;
    return m;
}

float phase_rayleigh(float c) { return 3.0 / (16.0 * A_PI) * (1.0 + c * c); }
// Cornette–Shanks: Henyey–Greenstein с поправкой на обратное рассеяние
float phase_mie(float c, float g) {
    float g2 = g * g;
    float k = 3.0 / (8.0 * A_PI) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + c * c) / pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5);
}

// Расстояние по лучу до шара радиуса R (из точки на высоте r, косинус к вертикали mu); < 0 — мимо
float ray_sphere(float r, float mu, float R) {
    float d = r * r * (mu * mu - 1.0) + R * R;
    if (d < 0.0) return -1.0;
    float s = sqrt(d);
    float t1 = -r * mu - s, t2 = -r * mu + s;
    return t1 > 0.0 ? t1 : t2;
}
bool hits_ground(float r, float mu) { return mu < 0.0 && r * r * (mu * mu - 1.0) + R_GROUND * R_GROUND >= 0.0; }

// Разметка таблицы пропускания (как у Bruneton): x — доля пути до верха, y — высота
vec2 trans_uv(float r, float mu) {
    float H = sqrt(R_TOP * R_TOP - R_GROUND * R_GROUND);
    float rho = sqrt(max(r * r - R_GROUND * R_GROUND, 0.0));
    float d = max(0.0, -r * mu + sqrt(max(r * r * (mu * mu - 1.0) + R_TOP * R_TOP, 0.0)));
    float dmin = R_TOP - r, dmax = rho + H;
    return vec2((d - dmin) / max(dmax - dmin, 1.0), rho / H);
}
void trans_inv(vec2 uv, out float r, out float mu) {
    float H = sqrt(R_TOP * R_TOP - R_GROUND * R_GROUND);
    float rho = H * uv.y;
    r = sqrt(rho * rho + R_GROUND * R_GROUND);
    float dmin = R_TOP - r, dmax = rho + H;
    float d = dmin + uv.x * (dmax - dmin);
    mu = d == 0.0 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
}

// Билинейная выборка таблицы в буфере, uv в [0, 1] по центрам крайних текселей
vec4 lut_fetch(Vec4s b, int w, int h, vec2 uv) {
    vec2 p = clamp(uv, 0.0, 1.0) * vec2(w - 1, h - 1);
    ivec2 i = ivec2(floor(p));
    i = min(i, ivec2(w - 2, h - 2));
    vec2 t = p - vec2(i);
    int k = i.y * w + i.x;
    vec4 a = mix(b.v[k], b.v[k + 1], t.x);
    vec4 c = mix(b.v[k + w], b.v[k + w + 1], t.x);
    return mix(a, c, t.y);
}

vec3 atmo_trans(float r, float mu) {
    if (hits_ground(r, mu)) return vec3(0.0);
    return lut_fetch(pc.frame.sky_trans, TRANS_W, TRANS_H, trans_uv(r, mu)).rgb;
}
vec3 atmo_ms(float r, float mu_s) {
    vec2 uv = vec2(mu_s * 0.5 + 0.5, (r - R_GROUND) / (R_TOP - R_GROUND));
    return lut_fetch(pc.frame.sky_ms, MS_N, MS_N, uv).rgb;
}

// Высота камеры над уровнем моря и наклон горизонта (под горизонтом камеры луч упирается в планету)
float atmo_cam_alt() { return max(pc.frame.cam_off.y + pc.frame.a_haze_abs.w, 1.0); }
float atmo_horizon(float alt) { return -acos(clamp(R_GROUND / (R_GROUND + alt), -1.0, 1.0)); }

// Таблица вида неба: u — азимут от светила 0..π, v — высота над горизонтом камеры, сгущение у горизонта
vec2 sky_uv(vec3 dir, vec3 light, float alt) {
    float el = asin(clamp(dir.y, -1.0, 1.0));
    float hz = atmo_horizon(alt);
    float e = el - hz;
    float v = e >= 0.0 ? 0.5 + 0.5 * sqrt(e / (0.5 * A_PI - hz)) : 0.5 - 0.5 * sqrt(-e / (0.5 * A_PI + hz));
    vec2 a = dir.xz, b = light.xz;
    float la = length(a), lb = length(b);
    float c = la > 1e-5 && lb > 1e-5 ? dot(a, b) / (la * lb) : 1.0;
    return vec2(acos(clamp(c, -1.0, 1.0)) / A_PI, v);
}
// Обратно: тексель → направление в плоскости, где светило на азимуте 0 (x — к светилу, z — вбок)
vec3 sky_dir(vec2 uv, float alt) {
    float hz = atmo_horizon(alt);
    float e = uv.y >= 0.5 ? (0.5 * A_PI - hz) * pow((uv.y - 0.5) * 2.0, 2.0)
                          : -(0.5 * A_PI + hz) * pow((0.5 - uv.y) * 2.0, 2.0);
    float el = e + hz;
    float az = uv.x * A_PI;
    return vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
}

// Рассеянный свет неба по направлению (без дисков солнца и луны): rgb — яркость, a — пропускание вдоль луча
vec4 atmo_sky_lut(vec3 dir) {
    FrameData f = pc.frame;
    float alt = atmo_cam_alt();
    vec4 s = lut_fetch(f.sky_sun, SKY_W, SKY_H, sky_uv(dir, f.a_sun.xyz, alt));
    vec3 m = lut_fetch(f.sky_moon, MOON_W, MOON_H, sky_uv(dir, f.a_moon.xyz, alt)).rgb;
    vec3 glow = vec3(0.55, 0.65, 1.0) * f.a_ground.w * f.a_sun.w * smoothstep(-0.2, 0.3, dir.y);
    return vec4((s.rgb * f.a_sun.w + m * f.a_moon.w) * f.a_misc.x + glow, s.a);
}
vec3 atmo_sky(vec3 dir) { return atmo_sky_lut(dir).rgb; }

// Освещённость от всего неба на плоскость с нормалью n, делённая на π (яркость ламбертовой поверхности
// с альбедо 1). Считает sky_sum.comp сферическими гармониками первого порядка, вместе с отсветом земли
vec3 atmo_ambient(vec3 n) {
    Vec4s o = pc.frame.sky_out;
    return max(o.v[0].rgb + o.v[1].rgb * n.x + o.v[2].rgb * n.y + o.v[3].rgb * n.z, vec3(0.0));
}

// Оптическая толщина отрезка от камеры: rel — точка относительно камеры. Слои экспоненциальны по высоте,
// интеграл аналитический (земля на 10–20 км почти плоская)
vec3 atmo_depth(vec3 rel) {
    FrameData f = pc.frame;
    float h0 = atmo_cam_alt();
    float d = length(rel);
    float dy = rel.y;
    vec3 tau = vec3(0.0);
    vec4 hs = vec4(f.a_rayleigh.w, f.a_mie.w, f.a_haze.w, 0.0);
    vec3 k[3];
    k[0] = f.a_rayleigh.rgb;
    k[1] = f.a_mie.rgb + f.a_mie_abs.rgb;
    k[2] = f.a_haze.rgb + f.a_haze_abs.rgb;
    for (int i = 0; i < 3; ++i) {
        float H = hs[i];
        float a = exp(-h0 / H);
        float x = dy / H;  // изменение высоты в слоях
        float integ = abs(x) > 1e-4 ? a * d * (1.0 - exp(-x)) / x : a * d * (1.0 - 0.5 * x);
        tau += k[i] * integ;
    }
    return tau;
}

// Дымка до точки поверхности: свет с поверхности гаснет по пути, взамен приходит рассеянный свет неба
// в том же направлении — в той доле, в какой отрезок набрал толщину от всего луча до края неба.
// lit — насколько освещён воздух отрезка (1 — открытое небо): в зале под крышей воздух светит отражённым светом,
// а не небом, иначе дымка 20 м зала при глазе, привыкшем к темноте, белит весь кадр (до воздуха 6д)
vec3 atmo_fog_lit(vec3 c, vec3 rel, float lit) {
    float d = length(rel);
    if (d < 1e-3) return c;
    vec3 dir = rel / d;
    vec4 sky = atmo_sky_lut(dir);
    vec3 t = exp(-atmo_depth(rel));
    vec3 k = clamp((1.0 - t) / max(1.0 - sky.a, 0.02), 0.0, 1.0);
    return c * t + sky.rgb * (k * lit);
}
vec3 atmo_fog(vec3 c, vec3 rel) { return atmo_fog_lit(c, rel, 1.0); }
#endif
