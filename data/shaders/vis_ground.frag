#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Visibility buffer, земля (вершины — vis_ground.vert): номер = старший бит | кольцо << 20 | треугольник кольца
layout(location = 0) out uint o_id;

void main() { o_id = VIS_GROUND | (pc.a << 20) | uint(gl_PrimitiveID); }
