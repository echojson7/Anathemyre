#version 460
#extension GL_GOOGLE_include_directive : require
#include "ground_vertex.glsl"
// Земля clipmap'ом: кольцо L (pc.a), вершина — ground_vertex() (ground_vertex.glsl). Нормаль считается там для
// колец от 2 м; ближние кольца нормаль берут во фрагменте по клетке 1 м
layout(location = 0) out vec3 v_rel;
layout(location = 1) out vec3 v_normal;
layout(location = 2) flat out uint v_level;
layout(location = 3) out float v_wdepth;  // глубина воды над землёй, м (< 0 — суша)

void main() {
    int L = int(pc.a);
    GroundVtx g = ground_vertex(L, gl_VertexIndex);
    gl_Position = to_clip(g.rel);
    v_rel = g.rel;
    v_normal = g.normal;
    v_level = uint(L);
    v_wdepth = g.wdepth;
}
