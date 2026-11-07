#version 460
#extension GL_GOOGLE_include_directive : require
#include "ui_common.glsl"
// Заливка с мягким краем или буква из атласа R8 (ближайший пиксель: буквы стоят в целых пикселях окна)
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
        // Сглаживание букв считалось в sRGB: доля покрытия — в гамме, иначе светлые буквы на тёмном тонки
        c.a *= pow(a, 1.0 / 1.6);
    }
    if (q.feather > 0.0) {
        vec2 d = min(v_px - q.rect.xy, q.rect.zw - v_px);
        float e = clamp(min(d.x, d.y) / q.feather, 0.0, 1.0);
        c.a *= e * e * (3.0 - 2.0 * e);
    }
    o_color = c;
}
