#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Земля этапа 1 — плоскость y = 0 (как ground_at() в src/sim/ground.h): квадрат под камерой до горизонта.
// Этап 2 заменит его clipmap по сетке высот
layout(location = 0) out vec3 v_rel;

const float GROUND_HALF_M = 30000.0;

void main() {
    const vec2 corner[6] = vec2[](vec2(-1, -1), vec2(-1, 1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(1, -1));
    vec2 c = corner[gl_VertexIndex] * GROUND_HALF_M;
    vec3 rel = vec3(c.x, -pc.frame.cam_off.y, c.y);
    gl_Position = to_clip(rel);
    v_rel = rel;
}
