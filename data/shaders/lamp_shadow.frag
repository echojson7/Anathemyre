#version 460
#extension GL_GOOGLE_include_directive : require
// Тени огней картами (docs/lamp_shadows.md): глубина грани — расстояние от огня до точки заслонки (точное на
// фрагмент, а не по оси грани): выборка сравнивает с ним расстояние приёмника. Тест глубины оставляет ближнюю.
// Свой светильник огня (lsm_own) заслонкой не считается. Машина под мороком — ситом по текселю грани с долей v_keep
// (узор неподвижен: фильтр выборок сводит его в полутень)
#include "common.glsl"
#include "lamp_map.glsl"
layout(location = 0) in vec3 v_rel;
layout(location = 1) flat in uint v_skip;
layout(location = 2) flat in float v_keep;

void main() {
    if (v_skip != 0u) discard;
    if (v_keep < 1.0 && fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) >= v_keep) discard;
    LsmJob job = pc.frame.lsm_jobs.v[pc.a];
    if (lsm_own(v_rel, job.axis, job.face, job.self_r)) discard;
    gl_FragDepth = lsm_depth(length(v_rel));
}
