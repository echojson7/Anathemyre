#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) out vec4 v_color;

void main() {
    LineVtx l = pc.frame.lines.v[gl_VertexIndex];
    gl_Position = to_clip(vec3(l.x, l.y, l.z));
    v_color = unpack_srgb(l.rgba);
}
