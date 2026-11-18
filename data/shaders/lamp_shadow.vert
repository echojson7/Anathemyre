#version 460
#extension GL_GOOGLE_include_directive : require
// Тени огней картами (docs/lamp_shadows.md): кластер заслонки в грань куба огня. Точка — от огня в осях грани
// (как их берёт выборка куба); расстояние до огня пишет фрагмент. Светящееся (стекло ламп, табло) заслонкой не
// считается: колба стоит вокруг своего огня, а стекло пропускает свет
#include "common.glsl"
#include "lamp_map.glsl"
#include "material_kinds.glsl"
layout(location = 0) out vec3 v_rel;
layout(location = 1) flat out uint v_skip;

void main() {
    FrameData f = pc.frame;
    LsmJob job = f.lsm_jobs.v[pc.a];
    uvec2 e = f.lsm_list.v[gl_InstanceIndex];
    Instance it = f.vis_instances.v[e.x];
    Vertex vx = f.vertices.v[gl_VertexIndex];
    vec3 rel = rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z)) - job.c +
               quat_rotate(inst_quat(it), vec3(vx.px, vx.py, vx.pz) * it.scale);
    uint mat = it.material < 0xFFFFFF00u ? it.material : (vx.mats & 0xFFFFu);
    v_skip = mat < 1024u && f.materials.v[mat].kind == KIND_GLOW ? 1u : 0u;  // RENDER_MAX_MATERIALS
    v_rel = rel;
    vec3 q = lsm_face_coords(job.layer % 6u, rel);
    // Отсечение ближней плоскостью: z ≥ 0 ⇔ ma ≥ LSM_NEAR; дальней нет (глубину пишет фрагмент)
    gl_Position = vec4(q.x, q.y, q.z - LSM_NEAR, q.z);
}
