#version 460
#extension GL_GOOGLE_include_directive : require
// Тени огней картами (docs/lamp_shadows.md): глубина грани — расстояние от огня до точки заслонки (точное на
// фрагмент, а не по оси грани): выборка сравнивает с ним расстояние приёмника. Тест глубины оставляет ближнюю.
// Свой светильник огня (lsm_own) заслонкой не считается
#include "common.glsl"
#include "lamp_map.glsl"
layout(location = 0) in vec3 v_rel;
layout(location = 1) flat in uint v_skip;

void main() {
    if (v_skip != 0u) discard;
    LsmJob job = pc.frame.lsm_jobs.v[pc.a];
    if (lsm_own(v_rel, job.axis, job.face, job.self_r)) discard;
    gl_FragDepth = lsm_depth(length(v_rel));
}
