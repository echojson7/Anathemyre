#version 460
#extension GL_GOOGLE_include_directive : require
// Небо forward (этапы 1–4): пустые пиксели после непрозрачного, sky_radiance() — sky.glsl
#include "sky.glsl"
#include "post.glsl"
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o_color;

void main() {
    FrameData f = pc.frame;
    float sx = v_ndc.x / f.proj_x, sy = -v_ndc.y / f.proj_y;
    vec3 dir = normalize(f.forward.xyz + f.right.xyz * sx + f.up.xyz * sy);
    o_color = vec4(post_color(sky_radiance(dir)), 1.0);
}
