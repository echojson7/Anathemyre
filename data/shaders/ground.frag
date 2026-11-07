#version 460
#extension GL_GOOGLE_include_directive : require
#include "ground_surface.glsl"
#include "light.glsl"
#include "post.glsl"
// Земля forward (этапы 1–4): ground_surface() → light_surface(). Нормаль треугольника — по производным
layout(location = 0) in vec3 v_rel;
layout(location = 1) in vec3 v_normal;
layout(location = 2) flat in uint v_level;
layout(location = 3) in float v_wdepth;
layout(location = 0) out vec4 o_color;

void main() {
    vec3 ng = normalize(cross(dFdy(v_rel), dFdx(v_rel)));
    if (ng.y < 0.0) ng = -ng;
    Surface s = ground_surface(v_rel, v_normal, v_level, v_wdepth, ng);
    o_color = vec4(post_color(light_surface(s)), 1.0);
}
