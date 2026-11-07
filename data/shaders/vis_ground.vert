#version 460
#extension GL_GOOGLE_include_directive : require
#include "ground_vertex.glsl"
// Visibility buffer, земля: та же вершина кольца, что у ground.vert, только положение
void main() { gl_Position = to_clip(ground_vertex(int(pc.a), gl_VertexIndex).rel); }
