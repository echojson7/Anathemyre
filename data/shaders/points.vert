#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) out vec4 v_color;

// Точка — квадрат из двух треугольников вокруг центра, размер в пикселях кадра
void main() {
    const vec2 corner[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    PointVtx p = pc.frame.points.v[gl_InstanceIndex];
    vec4 c = to_clip(vec3(p.x, p.y, p.z));
    c.xy += corner[gl_VertexIndex] * 0.5 * p.size * pc.frame.ndc_per_pixel * c.w;
    gl_Position = c;
    v_color = unpack_srgb(p.rgba);
}
