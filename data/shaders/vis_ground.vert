#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Visibility buffer, земля: вершина кольца L (pc.a) из вершин кадра (ground_bake.comp) — та же, что у ground.vert
void main() {
    int row = 2 * pc.frame.clip_n + 1;
    gl_Position = to_clip(pc.frame.g_verts.v[int(pc.a) * row * row + gl_VertexIndex].rel);
}
