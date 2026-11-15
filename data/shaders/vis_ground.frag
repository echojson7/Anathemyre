#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Visibility buffer, земля (вершины — vis_ground.vert): номер = старший бит | кольцо << 20 | треугольник кольца.
// В проёме земли (штамп hole, docs/terrain.md) пикселя нет: там спуск в подземную постройку
layout(location = 0) in vec2 i_rel;
layout(location = 0) out uint o_id;

void main() {
    for (uint k = 0u; k < pc.frame.t_hole_n; ++k) {
        vec4 h = pc.frame.t_hole[k];
        if (i_rel.x >= h.x && i_rel.x < h.z && i_rel.y >= h.y && i_rel.y < h.w) discard;
    }
    o_id = VIS_GROUND | (pc.a << 20) | uint(gl_PrimitiveID);
}
