// Тень рельефа дальнего плана (docs/tshadow.md): карта по уровням вокруг камеры, а не луч на пиксель.
// Тексель уровня k — квадрат TS_CELL0·2^k м; в нём «рампа» высоты: ниже y0 — тень, выше y0 + w — свет, между —
// полутень. Рампа не зависит от высоты точки, поэтому её читают и земля, и сетки над землёй (крыша руины).
// y0 хранится от сетки 8 м в центре текселя: при чтении прибавляется сетка 8 м в точке, и на крупных уровнях
// изгиб земли между текселями не даёт ложной тени в ложбинах. Карты три: кадр показывает смесь двух готовых,
// третья собирается кусками (src/render/vk_tshadow.cpp)
#ifndef TSHADOW_GLSL
#define TSHADOW_GLSL
#include "terrain.glsl"

const int TS_LEVELS = 11;       // как в vk_state.h
const int TS_W = 256;           // текселей по стороне уровня
const float TS_CELL0 = 0.25;    // тексель уровня 0, м
const float TS_BAND = 16.0;     // полоса перехода к следующему уровню у края окна, текселей
const float TS_LIT = -30000.0;  // y0 текселя без тени (светило под горизонтом)

// Рампа без тени (y0 ниже любой точки)
uint ts_lit_word() { return packHalf2x16(vec2(TS_LIT, 1.0)); }

// Луч от точки карты g0 (метры от угла) к светилу l по сетке 8 м, шаги растут до ~8 км — как прежний луч на
// пиксель (этапы 2–5). Шаг t: точка y лежит в свете шага, если y > h − l.y·t − bias, полутень — w. Рампа
// карты — верхние огибающие: y0 = max(c), y1 = max(c + w). Детали ±1 м сетка не видит: запас bias. В дюнах
// первые ~50 м луч видит дюны точно (t_dune_f вместо их размытой копии в сетке 8 м): гребень острый
vec2 t_shadow_ramp(vec2 g0, float yref, vec3 l, bool dunes, inout uint steps) {
    float y0 = -1e9, y1 = -1e9;
    float t = 3.0;
    for (int k = 0; k < 44; ++k) {
        vec2 q = g0 + l.xz * t;
        if (yref + l.y * t > 3300.0) break;
        steps++;
        float h = t_base_height(q);
        float bias = 1.2 + t * 0.002;
        if (dunes && t < 50.0) {
            h += t_dunes_f(q) - t_dune_base_f(q);
            bias = 0.3 + t * 0.002;
        }
        float c = h - l.y * t - bias;
        y0 = max(y0, c);
        y1 = max(y1, c + t * 0.035 + 0.3);
        t *= 1.2;
    }
    return vec2(y0, y1);
}

// Тексель карты m: номер с начала карт
uint ts_word(uint m, int k, ivec2 i) {
    return pc.frame.ts_maps.v[(m * uint(TS_LEVELS) + uint(k)) * uint(TS_W * TS_W) + uint(i.y * TS_W + i.x)];
}

// Рампа карты m в точке карты g (метры от угла): y0 от сетки 8 м и ширина полутени. Уровень — самый мелкий, чьё
// окно накрывает точку; у края окна — плавно к следующему
vec2 ts_sample(uint m, vec2 g) {
    FrameData f = pc.frame;
    const float c = 0.5 * float(TS_W - 1);
    vec2 r = vec2(0.0);
    float rest = 1.0;  // доля, которую ещё не дали уровни мельче
    for (int k = 0; k < TS_LEVELS && rest > 0.0; ++k) {
        float s = TS_CELL0 * float(1 << k);
        vec2 u = g / s - 0.5 - vec2(f.ts_org[m * uint(TS_LEVELS) + uint(k)]);
        float e = max(abs(u.x - c), abs(u.y - c));
        if (e > c - 1.0 && k + 1 < TS_LEVELS) continue;
        u = clamp(u, vec2(0.0), vec2(float(TS_W) - 1.001));
        ivec2 i = ivec2(floor(u));
        vec2 fr = u - vec2(i);
        vec2 a = unpackHalf2x16(ts_word(m, k, i)), b = unpackHalf2x16(ts_word(m, k, i + ivec2(1, 0)));
        vec2 d = unpackHalf2x16(ts_word(m, k, i + ivec2(0, 1))), e2 = unpackHalf2x16(ts_word(m, k, i + ivec2(1, 1)));
        vec2 v = mix(mix(a, b, fr.x), mix(d, e2, fr.x), fr.y);
        float w = k + 1 < TS_LEVELS ? 1.0 - smoothstep(c - TS_BAND, c - 1.0, e) : 1.0;
        r += v * rest * w;
        rest *= 1.0 - w;
    }
    return r;
}

// Доля света главного светила в точке rel (относительно камеры): 1 — свет, 0 — тень рельефа. Смесь двух
// готовых карт (ts_mix — ход сборки третьей): при смене солнца тень меняется плавно, а не шагами по кадрам
float t_sun_shadow(vec3 rel) {
    FrameData f = pc.frame;
    if (f.t_on == 0u || f.ts_on == 0u) return 1.0;
    vec3 p = f.cam_off + rel;
    vec2 g = p.xz + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256);
    float hb = t_base_height(g);
    vec2 ra = ts_sample(f.ts_a, g);
    float lit = clamp((p.y - hb - ra.x) / ra.y, 0.0, 1.0);
    if (f.ts_b != f.ts_a && f.ts_mix > 0.0) {
        vec2 rb = ts_sample(f.ts_b, g);
        lit = mix(lit, clamp((p.y - hb - rb.x) / rb.y, 0.0, 1.0), f.ts_mix);
    }
    return lit;
}
#endif
