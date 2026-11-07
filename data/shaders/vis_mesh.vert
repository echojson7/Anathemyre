#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Visibility buffer, сетки: вызов — видимый кластер (cull_write.comp), его номер — gl_InstanceIndex (firstInstance).
// Вершина по адресу общего буфера, как mesh.vert; во фрагмент идёт только номер кластера
layout(location = 0) flat out uint v_slot;

void main() {
    FrameData f = pc.frame;
    uvec2 vc = f.vis.v[gl_InstanceIndex];
    Instance it = f.vis_instances.v[vc.x];
    Vertex vx = f.vertices.v[gl_VertexIndex];
    vec3 local = quat_rotate(inst_quat(it), vec3(vx.px, vx.py, vx.pz) * it.scale);
    gl_Position = to_clip(rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z)) + local);
    v_slot = uint(gl_InstanceIndex);
}
