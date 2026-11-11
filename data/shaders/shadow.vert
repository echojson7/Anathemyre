#version 460
#extension GL_GOOGLE_include_directive : require
// Тени солнца (docs/shadow.md): кластер заслонки в окно уровня. Окно — SM_WIN·SM_PAGE текселей в стороне, ровно
// область вывода (тексель окна = пиксель прохода); высота к светилу идёт во фрагмент
#include "shadow.glsl"
layout(location = 0) out float v_h;
layout(location = 1) flat out int v_k;

void main() {
    FrameData f = pc.frame;
    uvec2 e = f.sm_list.v[gl_InstanceIndex];
    Instance it = f.vis_instances.v[e.x];
    int k = int(e.y >> 28);
    Vertex vx = f.vertices.v[gl_VertexIndex];
    vec3 rel = rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z)) +
               quat_rotate(inst_quat(it), vec3(vx.px, vx.py, vx.pz) * it.scale);
    vec2 w = sm_window(rel, k) / float(SM_WIN * SM_PAGE / 2) - 1.0;
    gl_Position = vec4(w, 0.0, 1.0);
    v_h = sm_height(rel);
    v_k = k;
}
