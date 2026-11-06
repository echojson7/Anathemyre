#version 460
#extension GL_GOOGLE_include_directive : require
#include "shade.glsl"
layout(location = 0) in vec3 v_rel;
layout(location = 0) out vec4 o_color;

void main() {
    uint m = pc.frame.ground_material;
    o_color = vec4(shade(v_rel, vec3(0.0, 1.0, 0.0), m, m, 0.0, 1.0, 0u), 1.0);
}
