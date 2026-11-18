#version 460
#extension GL_GOOGLE_include_directive : require
// Тени солнца (docs/shadow.md): кластер заслонки в окно места уровня (набор и уровень). Окно — SM_WIN·SM_PAGE текселей в стороне, ровно
// область вывода (тексель окна = пиксель прохода); высота к светилу идёт во фрагмент. pc.a = 1 — слой подвижных
// («Подвижные заслонки»): список его вызовов; у машины под мороком тень — доля её видимости (v_keep)
#include "shadow.glsl"
layout(location = 0) out float v_h;
layout(location = 1) flat out int v_k;
layout(location = 2) flat out float v_keep;

void main() {
    FrameData f = pc.frame;
    uvec2 e = pc.a == 0u ? f.sm_list.v[gl_InstanceIndex] : f.sm_dyn_list.v[gl_InstanceIndex];
    Instance it = f.vis_instances.v[e.x];
    int k = int(e.y >> 27);
    Vertex vx = f.vertices.v[gl_VertexIndex];
    vec3 rel = rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z)) +
               quat_rotate(inst_quat(it), vec3(vx.px, vx.py, vx.pz) * it.scale);
    vec2 w = sm_window(rel, k) / float(SM_WIN * SM_PAGE / 2) - 1.0;
    gl_Position = vec4(w, 0.0, 1.0);
    v_h = sm_height(rel, sm_set_of(k));
    v_k = k;
    // Морок (RENDER_VEIL, docs/tat.md): младший байт материала — видимость 0..255
    bool veil = it.material >= 0xFFFFFF00u && (it.material & 0xFFu) < 255u;
    v_keep = veil ? mix(SM_DYN_KEEP, 1.0, float(it.material & 0xFFu) / 255.0) : 1.0;
}
