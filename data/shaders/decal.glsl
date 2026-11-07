// Декали (docs/ruin.md, «Декали»): потёки, грязь у пола, пыль, пятна поверх Surface детали. Коробка проекции —
// RenderDecal (src/render/render.h); узор считается в осях коробки по точке ячейки (пиксельность сохраняется).
// Цвет и шероховатость — из материала декали (p0), сила — extra.x (0 — 1). Этапы 1–4 перебирают все декали
// кадра; на этапе 5 — списки на клетку, как у огней, и атлас .tex вместо узора по виду (docs/render.md)
#ifndef DECAL_GLSL
#define DECAL_GLSL
#include "surface.glsl"

// Шум значений по плоскости в осях декали (метры), решётка 1, [0, 1]
float d_noise(vec2 x, uint seed) {
    vec2 c = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    ivec3 k = ivec3(int(c.x), int(c.y), 0);
    float a = hash3(k, seed), b = hash3(k + ivec3(1, 0, 0), seed);
    float d = hash3(k + ivec3(0, 1, 0), seed), e = hash3(k + ivec3(1, 1, 0), seed);
    return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}
float d_fbm(vec2 x, uint seed) {
    return d_noise(x, seed) * 0.57 + d_noise(x * 2.03, seed + 1u) * 0.29 + d_noise(x * 4.1, seed + 2u) * 0.14;
}

// Доля декали в точке l (метры в осях коробки, от середины), нормаль nl в осях коробки
float decal_amount(Decal d, vec3 l, vec3 nl) {
    vec3 q = l / d.h;  // −1…1 внутри
    // Мягкий край коробки, чтобы граница не читалась прямой линией
    float edge = 1.0 - smoothstep(0.8, 1.0, max(abs(q.x), abs(q.z)));
    bool vert = abs(nl.y) < 0.6;
    float along = abs(nl.x) > abs(nl.z) ? l.z : l.x;   // вдоль вертикальной грани
    float along_h = abs(nl.x) > abs(nl.z) ? d.h.z : d.h.x;
    uint sd = d.seed * 7919u;
    if (d.kind == 0u) {  // потёки: столбцы вниз от верха коробки разной длины и ширины
        if (!vert) return 0.0;
        float side = 1.0 - smoothstep(0.75, 1.0, abs(along) / along_h);
        float from_top = d.h.y - l.y;
        float w = 0.035 + 0.05 * hash3(ivec3(int(floor(along / 0.4)), 0, 1), sd);
        float col = floor(along / w);
        float r1 = hash3(ivec3(int(col), 0, 2), sd), r2 = hash3(ivec3(int(col), 0, 3), sd);
        float len = (0.15 + 0.85 * r1 * r1) * 2.0 * d.h.y;
        float wob = (d_noise(vec2(along / 0.3, from_top / 0.4), sd + 5u) - 0.5) * 0.25;
        float s = r2 > 0.35 ? pow(clamp(1.0 - (from_top + wob) / len, 0.0, 1.0), 1.4) * (0.45 + 0.55 * r2) : 0.0;
        // Широкий размыв под краем: где вода стекала чаще
        float wash = d_fbm(vec2(along / 0.9, 0.0), sd + 9u) * pow(clamp(1.0 - from_top / (1.6 * d.h.y), 0.0, 1.0), 2.0);
        return max(s, wash * 0.65) * side * (1.0 - smoothstep(d.h.y * 0.85, d.h.y, -l.y));
    }
    if (d.kind == 1u) {  // грязь у пола: полоса снизу с рваным верхом и брызгами
        float hb = l.y + d.h.y;
        float top = 2.0 * d.h.y * (0.45 + 0.35 * d_fbm(vec2(along / 0.8, 0.0), sd + 3u));
        float band = 1.0 - smoothstep(top * 0.5, top, hb);
        float spl = d_noise(vec2(along / 0.06, hb / 0.06), sd + 4u) > 0.8 && hb < top * 1.4 ? 0.5 : 0.0;
        float a = max(band * (0.65 + 0.35 * d_noise(vec2(along / 0.15, hb / 0.15), sd + 6u)), spl);
        return a * (vert ? 1.0 : 0.5) * edge;
    }
    if (d.kind == 2u) {  // пыль и песок на полу: пятна с рваным краем, гуще к середине коробки
        if (nl.y < 0.4) return 0.0;
        float r = length(q.xz);
        float f = d_fbm(l.xz / 1.6, sd + 7u) + 0.18 * (d_noise(l.xz / 0.18, sd + 8u) - 0.5);
        return smoothstep(0.42, 0.6, f + 0.3 - 0.55 * r * r) * edge;
    }
    // Пятна: заливы с рваным краем на любой грани
    vec2 pl = abs(nl.y) > 0.6 ? l.xz : vec2(along, l.y);
    float f = d_fbm(pl / 0.9, sd + 11u) + 0.12 * (d_noise(pl / 0.12, sd + 12u) - 0.5);
    float r = length(q);
    return smoothstep(0.5, 0.62, f + 0.25 - 0.45 * r * r) * edge;
}

// Наложить декали кадра на поверхность детали. rel — точка ячейки относительно камеры
Surface apply_decals(Surface s, vec3 rel, vec3 n) {
    FrameData f = pc.frame;
    for (uint k = 0u; k < f.decal_count; ++k) {
        Decal d = f.decals.v[k];
        vec3 dp = rel - d.c;
        vec3 ux = vec3(d.ux, 0.0, d.uz), uz = vec3(-d.uz, 0.0, d.ux);
        vec3 l = vec3(dot(dp, ux), dp.y, dot(dp, uz));
        if (any(greaterThan(abs(l), d.h))) continue;
        vec3 nl = vec3(dot(n, ux), n.y, dot(n, uz));
        float a = decal_amount(d, l, nl);
        if (a <= 0.0) continue;
        Material m = f.materials.v[d.material];
        float strength = m.p2.x > 0.0 ? m.p2.x : 1.0;
        a = clamp(a * strength, 0.0, 1.0);
        s.albedo = mix(s.albedo, m.p0.rgb, a);
        s.rough = mix(s.rough, m.p0.a, a);
    }
    return s;
}
#endif
