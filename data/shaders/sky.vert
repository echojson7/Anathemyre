#version 460
#extension GL_GOOGLE_include_directive : require
// Небо — треугольник на весь экран на глубине 0 (бесконечность при обратной глубине): рисуется после
// непрозрачного, тест глубины GREATER_OR_EQUAL пропускает только пустые пиксели
#include "common.glsl"
layout(location = 0) out vec2 v_ndc;

void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    v_ndc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
