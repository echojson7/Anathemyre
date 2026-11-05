#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) out vec4 v_color;

void main() {
    Vtx p = pc.data.v[gl_VertexIndex];
    gl_Position = vec4(to_ndc(p.pos), 0.0, 1.0);
    v_color = unpack_srgb(p.a);
}
