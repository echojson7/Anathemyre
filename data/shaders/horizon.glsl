// Карта горизонта земли (docs/horizon.md): в каждом узле сетки 8 м — угол горизонта в H_DIRS направлениях и
// расстояние до того, что его закрывает. От солнца не зависит: считается раз на землю (horizon.comp), свет
// читает 4 узла × 2 соседних направления. Тень земли от солнца и луны — h_sun_shadow(); на этапе 6 — дальняя
// тень за картой теней и видимость неба для земли
#ifndef HORIZON_GLSL
#define HORIZON_GLSL
#include "terrain.glsl"

const int H_DIRS = 16;              // направлений, через 22.5°; узел — H_DIRS / 2 слов u32
const float H_EYE = 1.2;            // м над сеткой 8 м: детали ±1 м сетка не видит, точка в ямке не затеняет себя
const float H_BIAS = 0.002;         // запас тангенса (как у прежнего луча): кривизна, высоты по 5 см
const float H_FAR = 12000.0;        // дальше луч сборки не идёт, м
const float H_D0 = 8.0;             // расстояние кода 0, м
const float H_DQ = 0.11332869;      // ln 1.12: шаг кода расстояния (64 кода — 8 м … ~10 км)
const float H_ANGLE = 1.5707963 / 1023.0;  // угол на единицу кода (10 бит, 0…90°)

// Направление a (0..H_DIRS−1) в метрах карты (x, z)
vec2 h_dir(int a) {
    float phi = float(a) * (6.2831853 / float(H_DIRS));
    return vec2(cos(phi), sin(phi));
}

// Код направления: угол 10 бит, расстояние 6 бит
uint h_encode(float angle, float dist) {
    uint ua = uint(clamp(round(angle / H_ANGLE), 0.0, 1023.0));
    uint ud = uint(clamp(round(log(max(dist, H_D0) / H_D0) / H_DQ), 0.0, 63.0));
    return ua | (ud << 10);
}
// x — угол горизонта, рад; y — код расстояния (логарифм, мешается линейно)
vec2 h_decode(uint c) { return vec2(float(c & 1023u) * H_ANGLE, float(c >> 10)); }
float h_dist(float code) { return H_D0 * exp(code * H_DQ); }

// Узел k, направление a: слово k·H_DIRS/2 + a/2, половина a & 1
vec2 h_load(int k, int a) {
    uint w = pc.frame.h_map.v[k * (H_DIRS / 2) + (a >> 1)];
    return h_decode((a & 1) == 0 ? (w & 0xFFFFu) : (w >> 16));
}

// Видимость главного светила (солнце или луна) для точки rel (от камеры), 1 — свет, 0 — тень, мягко.
// Горизонт — билинейно по узлам и линейно по направлениям. Точка не на сетке 8 м сдвигает его по своей высоте
// над сеткой (rise) и расстоянию до заслонки: крыша выше — горизонт ниже, ямка деталей ниже — выше. Край —
// как у прежнего луча: полутень 0.035 рад + 0.3 м на расстояние до заслонки.
// Мелкая тень: точка в ямке деталей (ниже сетки 8 м больше чем на H_EYE) прячется за плоскость сетки, если
// солнце над ней низко — то, что прежний луч видел первым шагом (3 м). Детали ±1 м сетка не видит, так что
// без этого склоны гор теряют прожилки тени в ложбинах деталей
float h_sun_shadow(vec3 rel, vec3 l) {
    FrameData f = pc.frame;
    if (f.h_on == 0u || l.y <= 0.0) return 1.0;
#ifdef T_SHADOW_STEPS
    T_SHADOW_STEPS++;  // счётчик работы освещения (docs/tiles.md): выборки карты горизонта
#endif
    vec3 p = f.cam_off + rel;
    vec2 g = p.xz + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256);
    vec2 b = clamp(g / 8.0, vec2(0.0), vec2(f.t_nx - 1, f.t_nz - 1) - 0.001);
    ivec2 i = ivec2(floor(b));
    vec2 fr = b - vec2(i);
    int k0 = i.y * f.t_nx + i.x;
    int ks[4] = int[4](k0, k0 + 1, k0 + f.t_nx, k0 + f.t_nx + 1);
    vec4 hc = vec4(t_hgt(ks[0]), t_hgt(ks[1]), t_hgt(ks[2]), t_hgt(ks[3])) * 0.05;
    float hb = mix(mix(hc.x, hc.y, fr.x), mix(hc.z, hc.w, fr.x), fr.y);
    vec2 grad = vec2(mix(hc.y - hc.x, hc.w - hc.z, fr.y), mix(hc.z - hc.x, hc.w - hc.y, fr.x)) / 8.0;
    float rise = p.y - hb;
    float lift = rise > 1.5 ? rise - 1.5 : min(rise, 0.0);  // детали ±1.5 м горизонт не двигают
    float u = atan(l.z, l.x) * (float(H_DIRS) / 6.2831853);
    u = u < 0.0 ? u + float(H_DIRS) : u;
    int a0 = int(floor(u)) & (H_DIRS - 1), a1 = (a0 + 1) & (H_DIRS - 1);
    float fa = fract(u);
    vec2 c[4];
    for (int n = 0; n < 4; ++n) {
        vec2 h = mix(h_load(ks[n], a0), h_load(ks[n], a1), fa);
        if (lift != 0.0) h.x = clamp(atan(tan(h.x) - lift / h_dist(h.y)), 0.0, 1.5707963);
        c[n] = h;
    }
    vec2 h = mix(mix(c[0], c[1], fr.x), mix(c[2], c[3], fr.x), fr.y);
    float w = 0.035 + 0.3 / h_dist(h.y);
    float lit = clamp((asin(l.y) - h.x) / w, 0.0, 1.0);
    // Мелкая тень: зазор над плоскостью сетки через 3 м к светилу, ширина полутени там — как у луча
    float gap = rise + H_EYE + 3.0 * (l.y - dot(grad, l.xz));
    return min(lit, clamp(gap / 0.405, 0.0, 1.0));
}
#endif
