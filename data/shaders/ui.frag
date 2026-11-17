#version 460
#extension GL_GOOGLE_include_directive : require
#include "ui_common.glsl"
// Заливка (скруглённая, обводкой или с мягким краем) или буква из атласа R8 (ближайший пиксель: буквы стоят в
// целых пикселях окна)
layout(location = 0) in vec2 v_px;
layout(location = 1) flat in uint v_quad;
layout(location = 0) out vec4 o_color;

vec4 unpack_srgb(uint rgba) {
    vec4 c = unpackUnorm4x8(rgba);
    vec3 lo = c.rgb / 12.92;
    vec3 hi = pow((c.rgb + 0.055) / 1.055, vec3(2.4));
    return vec4(mix(hi, lo, lessThanEqual(c.rgb, vec3(0.04045))), c.a);
}

void main() {
    UiQuad q = up.quads.v[v_quad];
    vec4 c = unpack_srgb(q.rgba);
    if (q.uv.x >= 0.0) {
        // Пиксель атласа под пикселем окна: прямоугольник буквы того же размера, что в атласе
        vec2 t = q.uv.xy + (v_px - q.rect.xy);
        uint i = uint(t.y) * up.atlas_w + uint(t.x);
        float a = float((up.atlas.v[i >> 2] >> ((i & 3u) * 8u)) & 255u) / 255.0;
        // Сглаживание букв считалось в sRGB, смешение идёт в линейном свете: без поправки светлые буквы на
        // тёмном тонкие и рыхлые
        c.a *= pow(a, 1.0 / 1.45);
    }
    if (q.radius > 0.0 || q.stroke > 0.0) {
        // Расстояние до края скруглённого прямоугольника (< 0 — внутри), край сглажен на пиксель
        vec2 hs = (q.rect.zw - q.rect.xy) * 0.5;
        vec2 d2 = abs(v_px - (q.rect.xy + q.rect.zw) * 0.5) - hs + q.radius;
        float d = length(max(d2, 0.0)) + min(max(d2.x, d2.y), 0.0) - q.radius;
        float a = clamp(0.5 - d, 0.0, 1.0);
        if (q.stroke > 0.0) a *= clamp(0.5 + d + q.stroke, 0.0, 1.0);
        if (q.feather > 0.0) a *= smoothstep(0.0, 1.0, clamp(-d / q.feather, 0.0, 1.0));
        c.a *= a;
    } else if (q.feather > 0.0) {
        vec2 d = min(v_px - q.rect.xy, q.rect.zw - v_px);
        float e = clamp(min(d.x, d.y) / q.feather, 0.0, 1.0);
        c.a *= e * e * (3.0 - 2.0 * e);
    }
    o_color = c;
}
