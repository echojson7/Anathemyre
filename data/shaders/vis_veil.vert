#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Visibility buffer, кластеры машины под мороком (docs/tat.md): как vis_mesh.vert, во фрагмент ещё точка в осях
// сетки и видимость экземпляра (младший байт материала, RENDER_VEIL)
layout(location = 0) flat out uint v_slot;
layout(location = 1) flat out float v_vis;
layout(location = 2) out vec3 v_local;

void main() {
    FrameData f = pc.frame;
    uvec2 vc = f.vis.v[gl_InstanceIndex];
    Instance it = f.vis_instances.v[vc.x];
    Vertex vx = f.vertices.v[gl_VertexIndex];
    vec3 p = vec3(vx.px, vx.py, vx.pz) * it.scale;
    gl_Position = to_clip_jittered(rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z)) + quat_rotate(inst_quat(it), p));
    v_slot = uint(gl_InstanceIndex);
    v_vis = float(it.material & 0xFFu) / 255.0;
    v_local = p;
}
