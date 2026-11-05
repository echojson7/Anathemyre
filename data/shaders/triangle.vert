#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) out vec4 v_color;

// Треугольник этапа 0 в углу экрана: проверка, что конвейер и swapchain живы
void main() {
    const vec2 p[3] = vec2[](vec2(-0.95, 0.95), vec2(-0.75, 0.95), vec2(-0.85, 0.75));
    const vec3 c[3] = vec3[](vec3(0.55, 0.12, 0.08), vec3(0.45, 0.38, 0.22), vec3(0.20, 0.22, 0.20));
    gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
    v_color = vec4(c[gl_VertexIndex], 1.0);
}
