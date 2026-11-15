#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Visibility buffer, земля: вершина кольца L (pc.a) из вершин кадра (ground_bake.comp) — та же, что у ground.vert
layout(location = 0) out vec2 o_rel;  // место в плане от камеры, м — для проёмов земли

void main() {
    int row = 2 * pc.frame.clip_n + 1;
    vec3 rel = pc.frame.g_verts.v[int(pc.a) * row * row + gl_VertexIndex].rel;
    o_rel = rel.xz;
    gl_Position = to_clip_jittered(rel);
}
