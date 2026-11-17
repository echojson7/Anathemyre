#version 460
#extension GL_GOOGLE_include_directive : require
#include "ui_common.glsl"
// Экземпляр — прямоугольник, 6 вершин; координаты в пикселях окна от левого верхнего угла.
// Рисуется пересечение с обрезкой: форма (скругление, обводка, атлас) считается по полному прямоугольнику
layout(location = 0) out vec2 v_px;       // пиксель окна
layout(location = 1) flat out uint v_quad;

void main() {
    const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    UiQuad q = up.quads.v[gl_InstanceIndex];
    vec2 lo = max(q.rect.xy, q.clip.xy), hi = max(min(q.rect.zw, q.clip.zw), lo);
    vec2 p = mix(lo, hi, corners[gl_VertexIndex]);
    v_px = p;
    v_quad = gl_InstanceIndex;
    gl_Position = vec4(p / up.screen * 2.0 - 1.0, 0.0, 1.0);
}
