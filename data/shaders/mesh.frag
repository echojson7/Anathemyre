#version 460
#extension GL_GOOGLE_include_directive : require
#include "shade.glsl"
#include "post.glsl"
layout(location = 0) in vec3 v_rel;
layout(location = 1) in vec3 v_normal;
layout(location = 2) flat in uvec4 v_mat;
layout(location = 3) in vec2 v_blend_ao;
layout(location = 0) out vec4 o_color;

void main() {
    vec3 c = shade(v_rel, normalize(v_normal), v_mat.x, v_mat.y, v_blend_ao.x, v_blend_ao.y, v_mat.z);
    o_color = vec4(post_color(c), 1.0);
}
