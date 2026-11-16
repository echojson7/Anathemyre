// Облака (docs/clouds.md): объёмные кучевые в слое высот и тонкий слой перистых над ними. Луч по облаку — дорого,
// поэтому облака считаются не на пиксель кадра, а в карту купола неба вокруг камеры: по частям, несколько фаз
// за кадр (cloud_map.comp). Кадр читает карту: два слота с перетеканием, каждый перенесён к своему кадру по
// сносу ветром и сдвигу камеры (cloud_view). Тень облаков на землю — карта в осях облаков (cloud_shadow).
// Случайности по номеру кадра нет: форма — от места и времени
#ifndef CLOUDS_GLSL
#define CLOUDS_GLSL
#include "atmosphere.glsl"

const int CLOUD_N = 128;             // шум: куб CLOUD_N³ слов (cloud_noise.comp)
const float CLOUD_RMAX = 1.06;       // край карты купола в стереографической проекции: до 3° ниже горизонта
const float CLOUD_FAR = 120e3;       // дальше кучевых нет, м
const float CIRRUS_FAR = 280e3;      // перистых
const int CLOUD_SH_N = 512;          // карта тени: тексели по кругу (окно вокруг камеры)
const float CLOUD_SH_M = 64.0;       // её тексель, м
const float CLOUD_TILE = 7200.0;     // куб шума формы, м
const float CLOUD_DETAIL = 1100.0;   // куб шума мелочи, м
const float CLOUD_LMAX = 16.0;       // предел яркости текселя карты, в нормах слота
const float CLOUD_DMAX = 300e3;      // предел расстояния в текселе карты, м

float cl_remap(float v, float a, float b) { return clamp((v - a) / (b - a), 0.0, 1.0); }

// Шум по решётке на плоскости (0..1) и сумма октав: поля облаков, перистые
float cl_h2(ivec2 p) {
    uint h = uint(p.x) * 0x8DA6B343u ^ uint(p.y) * 0xD8163841u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h >> 8) * (1.0 / 16777216.0);
}
float cl_vnoise(vec2 p) {
    vec2 fl = floor(p), w = p - fl;
    w = w * w * (3.0 - 2.0 * w);
    ivec2 i = ivec2(fl);
    return mix(mix(cl_h2(i), cl_h2(i + ivec2(1, 0)), w.x), mix(cl_h2(i + ivec2(0, 1)), cl_h2(i + ivec2(1, 1)), w.x), w.y);
}
float cl_fbm(vec2 p, int n) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < n; ++i) {
        s += a * cl_vnoise(p);
        p = p * 2.03 + vec2(17.3, 5.1);
        a *= 0.5;
    }
    return s / (1.0 - a * 2.0);
}

// Куб шума, трилинейно, по кругу; p — в долях куба. r — клубы (Перлин–Ворли), g, b, a — ячеистый шум трёх
// масштабов, от крупного к мелкому
vec4 cloud_noise(vec3 p) {
    CloudWords b = pc.frame.cl_noise;
    vec3 q = p * float(CLOUD_N) - 0.5;
    vec3 fl = floor(q);
    vec3 t = q - fl;
    ivec3 i0 = ivec3(fl) & (CLOUD_N - 1), i1 = (i0 + 1) & (CLOUD_N - 1);
    int z0 = i0.z * CLOUD_N * CLOUD_N, z1 = i1.z * CLOUD_N * CLOUD_N, y0 = i0.y * CLOUD_N, y1 = i1.y * CLOUD_N;
    vec4 c00 = mix(unpackUnorm4x8(b.v[z0 + y0 + i0.x]), unpackUnorm4x8(b.v[z0 + y0 + i1.x]), t.x);
    vec4 c10 = mix(unpackUnorm4x8(b.v[z0 + y1 + i0.x]), unpackUnorm4x8(b.v[z0 + y1 + i1.x]), t.x);
    vec4 c01 = mix(unpackUnorm4x8(b.v[z1 + y0 + i0.x]), unpackUnorm4x8(b.v[z1 + y0 + i1.x]), t.x);
    vec4 c11 = mix(unpackUnorm4x8(b.v[z1 + y1 + i0.x]), unpackUnorm4x8(b.v[z1 + y1 + i1.x]), t.x);
    return mix(mix(c00, c10, t.y), mix(c01, c11, t.y), t.z);
}

// Погода в точке (оси облаков, м): x — покров 0..1 (какую долю шума формы оставить облаком), y — рост облака
// в долях слоя. Поля в десятки километров: где-то облака гуще и выше, где-то просветы
vec2 cloud_weather(vec2 q) {
    float k = pc.frame.cl_shape.z;
    float w = cl_fbm(q * (1.0 / 23000.0) + vec2(31.7, 11.3), 3);
    float tall = cl_vnoise(q * (1.0 / 9000.0) + vec2(5.3, 47.1));
    return vec2(clamp(k + (w - 0.5) * 1.5, 0.0, 1.0), mix(0.45, 1.0, smoothstep(0.3, 0.8, tall * 0.5 + w * 0.5)));
}

// Плотность облака (ослабление, 1/м): p.xz — оси облаков, м, p.y — высота над морем, м; tev — время формы, с.
// Форма — по Nubis (Schneider 2015): шум клубов, срезанный покровом и профилем высоты (плоский низ, круглый
// верх), мелочь объедает края. fine — с мелочью (ближние шаги луча взгляда)
float cloud_density(vec3 p, float tev, bool fine) {
    FrameData f = pc.frame;
    float h = (p.y - f.cl_shape.x) / (f.cl_shape.y - f.cl_shape.x);
    if (h <= 0.0 || h >= 1.0 || f.cl_shape.z <= 0.0) return 0.0;
    vec2 wt = cloud_weather(p.xz);
    float prof = smoothstep(0.0, 0.1, h) * (1.0 - smoothstep(wt.y * 0.45, wt.y, h));
    if (wt.x * prof < 0.02) return 0.0;
    // Шум формы медленно идёт вверх сквозь профиль: облако клубится
    vec4 n = cloud_noise(vec3(p.x, p.y - 0.4 * tev, p.z) * (1.0 / CLOUD_TILE));
    float d = cl_remap(n.r * prof, 1.0 - wt.x, 1.0 - wt.x + 0.45);
    if (d <= 0.0) return 0.0;
    if (fine) {
        vec4 m = cloud_noise(p * (1.0 / CLOUD_DETAIL) + vec3(0.004, -0.003, 0.002) * tev);
        float hf = m.g * 0.5 + m.b * 0.3 + m.a * 0.2;
        // У низа края рваные (клочья), выше — клубы
        d = cl_remap(d, mix(1.0 - hf, hf, clamp(h * 4.0, 0.0, 1.0)) * 0.75, 1.0);
    } else {
        d = cl_remap(d, 0.25, 1.0);
    }
    return min(d * 2.5, 1.0) * f.cl_shape.w * mix(0.6, 1.3, h);
}

// Перистые: оптическая толщина слоя по вертикали в точке (оси облаков, м). Поля в сотню километров, в них
// волокна вдоль ветра
float cirrus_tau(vec2 q) {
    FrameData f = pc.frame;
    float k = f.cl_cirrus.y;
    if (k <= 0.0) return 0.0;
    vec2 w = f.cl_cirrus.zw;
    vec2 p = vec2(dot(q, w), dot(q, vec2(-w.y, w.x)));
    float field = cl_fbm(p * (1.0 / 70000.0) + vec2(3.1, 9.7), 3);
    float cov = clamp(k + (field - 0.5) * 1.6, 0.0, 1.0);
    if (cov <= 0.02) return 0.0;
    vec2 s = vec2(p.x / 15000.0, p.y / 5500.0);
    s += (vec2(cl_fbm(s * 0.7 + 11.0, 2), cl_fbm(s * 0.7 + 37.0, 2)) - 0.5) * 2.2;  // пряди гнутся
    float v = cl_fbm(s, 5);
    float d = cl_remap(v, 1.0 - cov * 0.85, 1.0 - cov * 0.85 + 0.5);
    if (d <= 0.0) return 0.0;
    // Нити вдоль ветра только рябят толщину пряди (сами по себе они читаются царапинами); край пряди тает
    float fib = cl_fbm(vec2(p.x / 7000.0, p.y / 900.0) + s * 1.3, 4);
    float thin = cl_fbm(vec2(p.x / 2600.0, p.y / 1500.0) + 71.0, 3);
    return d * d * smoothstep(0.15, 0.85, fib * 0.6 + thin * 0.4) * 0.7;
}

float cl_hg(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * A_PI * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}
// Свет светила, рассеянный каплями к глазу, на единицу освещённости (1/ср): tau — толщина облака от точки к
// светилу, c — косинус угла рассеяния. Октавы многократного рассеяния (Wrenninge 2013; Hillaire 2016): каждая
// слабее, проходит глубже и светит ровнее; последнее слагаемое — свет, прошедший сотни рассеяний: белый бок
// со стороны солнца и свет сквозь облако — где оно тоньше, низ светлее (два потока)
float cloud_sun(float tau, float c) {
    float s = 0.0, a = 1.0, b = 1.0, g = 1.0;
    for (int i = 0; i < 4; ++i) {
        s += a * exp(-tau * b) * mix(cl_hg(c, 0.8 * g), cl_hg(c, -0.3 * g), 0.3);
        a *= 0.55;
        b *= 0.35;
        g *= 0.5;
    }
    return 1.3 * s + 0.16 / (1.0 + 0.15 * tau);
}

// Карта купола: стереографическая проекция от надира — зенит в центре, горизонт у края, у горизонта тексели
// вдвое мельче (облака там дальше)
vec3 cl_dir(vec2 uv) {
    vec2 p = (uv - 0.5) * (2.0 * CLOUD_RMAX);
    float r2 = dot(p, p);
    return vec3(2.0 * p.x, 1.0 - r2, 2.0 * p.y) / (1.0 + r2);
}
vec2 cl_uv(vec3 d) { return d.xz / ((1.0 + d.y) * (2.0 * CLOUD_RMAX)) + 0.5; }

// Тексель карты: два слова — r, g | b, пропускание, расстояние. Цвет — в нормах слота (cl_info), корнем
uvec2 cl_pack(vec3 c, float t, float dist) {
    uvec3 e = uvec3(sqrt(clamp(c / CLOUD_LMAX, 0.0, 1.0)) * 65535.0 + 0.5);
    uint ut = uint(sqrt(clamp(t, 0.0, 1.0)) * 255.0 + 0.5);
    uint ud = uint(sqrt(clamp(dist / CLOUD_DMAX, 0.0, 1.0)) * 255.0 + 0.5);
    return uvec2(e.x | (e.y << 16), e.z | (ut << 16) | (ud << 24));
}
void cl_unpack(uint at, out vec3 c, out vec2 td) {
    CloudWords m = pc.frame.cl_map;
    uint w0 = m.v[at], w1 = m.v[at + 1u];
    vec3 e = vec3(float(w0 & 0xFFFFu), float(w0 >> 16), float(w1 & 0xFFFFu)) * (1.0 / 65535.0);
    c = e * e * CLOUD_LMAX;
    td = vec2(float((w1 >> 16) & 0xFFu), float(w1 >> 24)) * (1.0 / 255.0);
    td *= td;
}

// Облака по направлению взгляда: L — их свет (до дымки воздуха перед ними), T — пропускание того, что за ними,
// dist — расстояние до них, м
struct Cloud {
    vec3 L;
    float T, dist;
};
Cloud cl_slot(uint slot, vec3 d) {
    FrameData f = pc.frame;
    Cloud o = Cloud(vec3(0.0), 1.0, 0.0);
    if (d.y < -0.05) return o;
    int n = int(f.cl_size);
    vec2 p = clamp(cl_uv(d) * float(n) - 0.5, vec2(0.0), vec2(n - 1) - 0.001);
    ivec2 i = ivec2(floor(p));
    vec2 t = p - vec2(i);
    uint at = (slot * uint(n * n) + uint(i.y * n + i.x)) * 2u;
    vec3 c0, c1, c2, c3;
    vec2 a0, a1, a2, a3;
    cl_unpack(at, c0, a0);
    cl_unpack(at + 2u, c1, a1);
    cl_unpack(at + uint(n) * 2u, c2, a2);
    cl_unpack(at + uint(n) * 2u + 2u, c3, a3);
    o.L = mix(mix(c0, c1, t.x), mix(c2, c3, t.x), t.y) * f.cl_info.v[slot * 4u].x;
    vec2 a = mix(mix(a0, a1, t.x), mix(a2, a3, t.x), t.y);
    o.T = a.x;
    o.dist = a.y * CLOUD_DMAX;
    return o;
}
Cloud cloud_view(vec3 dir) {
    FrameData f = pc.frame;
    Cloud o = Cloud(vec3(0.0), 1.0, 0.0);
    if (f.cl_on == 0u) return o;
    // Перенос: точка облака на средней высоте слоя ушла с ветром и сдвигом камеры со сборки слота
    float s = ray_sphere(R_GROUND + atmo_cam_alt(), dir.y, R_GROUND + 0.5 * (f.cl_shape.x + f.cl_shape.y));
    if (s <= 0.0) return o;
    vec3 p = dir * min(s, CLOUD_FAR);
    if (f.cl_mix >= 1.0) return cl_slot(f.cl_slot_b, normalize(p + f.cl_off_b.xyz));
    Cloud a = cl_slot(f.cl_slot_a, normalize(p + f.cl_off_a.xyz));
    if (f.cl_mix <= 0.0) return a;
    Cloud b = cl_slot(f.cl_slot_b, normalize(p + f.cl_off_b.xyz));
    return Cloud(mix(a.L, b.L, f.cl_mix), mix(a.T, b.T, f.cl_mix), mix(a.dist, b.dist, f.cl_mix));
}

// Небо с облаками: sky — рассеянный свет неба по направлению (atmo_sky_lut), вернёт цвет; tc — пропускание
// облаков для того, что за воздухом (диски, ореол, звёзды). Воздух перед облаком светит как раньше и гасит
// облако, воздух за ним виден сквозь него
vec3 cloud_over_sky(vec3 dir, vec4 sky, out float tc) {
    Cloud c = cloud_view(dir);
    tc = c.T;
    if (c.T >= 1.0) return sky.rgb;
    vec3 t = exp(-atmo_depth(dir * c.dist));
    vec3 k = clamp((1.0 - t) / max(1.0 - sky.a, 0.02), 0.0, 1.0);
    return sky.rgb * mix(vec3(c.T), vec3(1.0), k) + c.L * t;
}

// Точка карты тени: середина слоя кучевых над точкой, от которой луч к светилу l пришёл бы к камере (окно карты
// идёт за камерой). Светило ниже ~5° — тени уходят за окно, наклон луча ограничен
float cl_sun_y(vec3 l) { return max(l.y, 0.08); }
vec2 cl_shadow_center(vec3 l) {
    FrameData f = pc.frame;
    return f.cl_cam.xz + l.xz * ((0.5 * (f.cl_shape.x + f.cl_shape.y) - f.cl_cam.y) / cl_sun_y(l));
}
// Тень облаков на точку rel (от камеры, м): доля света главного светила, что доходит. За окном карты и при
// низком светиле — средняя
float cloud_shadow(vec3 rel) {
    FrameData f = pc.frame;
    if (f.cl_on == 0u) return 1.0;
    vec3 l = normalize(f.sun_dir.xyz);
    if (l.y <= 0.0) return 1.0;
    vec2 c = cl_shadow_center(l);
    vec2 q = c + rel.xz - l.xz * (rel.y / cl_sun_y(l));
    vec2 e = abs(q - c) / (0.5 * float(CLOUD_SH_N) * CLOUD_SH_M);
    float w = (1.0 - smoothstep(0.75, 0.95, max(e.x, e.y))) * smoothstep(0.06, 0.18, l.y);
    if (w <= 0.0) return f.cl_mean_t;
    vec2 u = q / CLOUD_SH_M - 0.5;
    vec2 fl = floor(u);
    vec2 t = u - fl;
    ivec2 i0 = ivec2(fl) & (CLOUD_SH_N - 1), i1 = (i0 + 1) & (CLOUD_SH_N - 1);
    CloudWords m = f.cl_shadow;
    float a = mix(uintBitsToFloat(m.v[i0.y * CLOUD_SH_N + i0.x]), uintBitsToFloat(m.v[i0.y * CLOUD_SH_N + i1.x]), t.x);
    float b = mix(uintBitsToFloat(m.v[i1.y * CLOUD_SH_N + i0.x]), uintBitsToFloat(m.v[i1.y * CLOUD_SH_N + i1.x]), t.x);
    return mix(f.cl_mean_t, mix(a, b, t.y), w);
}
#endif
