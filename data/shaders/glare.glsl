// Блеск (docs/glare.md): глаз и объектив рассеивают часть света вокруг яркого места — солнце сияет на несколько
// градусов, лампа светится, край облака у солнца горит. Кадр после сглаживания сводится пирамидой (уровни вдвое
// мельче, glare.comp), уровни складываются обратно сверху вниз: сумма размытий всё шире, каждое чуть слабее —
// рассеяние в глазу спадает с углом примерно как 1/θ². Стиль подмешивает долю a_night2.z (post.glsl)
#ifndef GLARE_GLSL
#define GLARE_GLSL
#include "atmosphere.glsl"

const float GLARE_FALL = 0.8;  // вес следующего (вдвое шире) уровня

vec3 gr_get(int off, ivec2 sz, ivec2 p) {
    p = clamp(p, ivec2(0), sz - 1);
    uvec2 w = pc.frame.gr_buf.v[off + p.y * sz.x + p.x];
    return vec3(unpackHalf2x16(w.x), unpackHalf2x16(w.y).x);
}
// Билинейно: q — в текселях уровня (центр текселя i — i + 0.5)
vec3 gr_lerp(int off, ivec2 sz, vec2 q) {
    vec2 t = q - 0.5;
    ivec2 i = ivec2(floor(t));
    vec2 f = t - vec2(i);
    return mix(mix(gr_get(off, sz, i), gr_get(off, sz, i + ivec2(1, 0)), f.x),
               mix(gr_get(off, sz, i + ivec2(0, 1)), gr_get(off, sz, i + ivec2(1, 1)), f.x), f.y);
}
// Свет блеска в пикселе кадра (координаты пикселя, центр — +0.5), с весами уровней в сумме 1
vec3 glare_at(vec2 px) {
    FrameData f = pc.frame;
    ivec4 l = f.gr_lv[0];
    float s = (1.0 - pow(GLARE_FALL, float(f.gr_n))) / (1.0 - GLARE_FALL);
    return gr_lerp(l.w, l.xy, px * 0.5) / s;
}

// Корона у солнца: тонкие лучи вокруг диска — свет, рассеянный волокнами хрусталика и ресницами (Spencer 1995,
// «ciliary corona»). Лучи медленно переливаются: глаз не стоит на месте. vis — доля диска, что видна (0 — закрыт)
float gr_hash(uint i) {
    uint h = i * 0x8DA6B343u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return float(h >> 8) * (1.0 / 16777216.0);
}
// Шум по кругу: n клеток на оборот, u — доля оборота
float gr_ring(float u, uint n, uint salt) {
    float x = u * float(n);
    float i = floor(x), t = x - i;
    uint a = uint(mod(i, float(n))), b = uint(mod(i + 1.0, float(n)));
    t = t * t * (3.0 - 2.0 * t);
    return mix(gr_hash(a + salt), gr_hash(b + salt), t);
}
vec3 glare_corona(vec3 dir, float vis) {
    FrameData f = pc.frame;
    vec3 s = f.a_sun.xyz;
    float c = dot(dir, s);
    if (vis <= 0.0 || c < 0.94) return vec3(0.0);
    float rs = f.a_sun_halo.w;
    float r = acos(min(c, 1.0));
    vec3 ax = normalize(cross(s, abs(s.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    vec3 ay = cross(ax, s);
    float u = atan(dot(dir, ay), dot(dir, ax)) * (0.5 / A_PI) + 0.5;
    float t = f.a_night.y;
    float a = gr_ring(u + 0.003 * sin(0.7 * t), 240u, 11u) * gr_ring(u - 0.002 * sin(0.53 * t + 1.0), 97u, 977u);
    float len = mix(3.0, 12.0, gr_ring(u, 47u, 4093u));  // длина лучей, в радиусах диска
    float streak = (0.15 + pow(a, 2.0) * 3.0) * (1.0 - smoothstep(0.3 * len, len, r / rs));
    float fall = rs * rs / max(r * r, rs * rs) * smoothstep(0.9, 1.6, r / rs);
    vec3 tc = f.a_sun_t.rgb / max(max(f.a_sun_t.r, max(f.a_sun_t.g, f.a_sun_t.b)), 1e-6);
    return tc * (atmo_sun_cap() * f.a_night2.z * 0.05 * vis * streak * fall);
}
#endif
