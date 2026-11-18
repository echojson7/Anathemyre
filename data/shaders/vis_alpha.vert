#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Visibility buffer, карточки с прозрачным краем (волосы, docs/hair.md): как vis_mesh.vert, во фрагмент ещё
// развёртка вершины и материал треугольника (первая вершина, как resolve.glsl)
layout(location = 0) flat out uint v_slot;
layout(location = 1) flat out uint v_mat;
layout(location = 2) out vec2 v_uv;

void main() {
    FrameData f = pc.frame;
    uvec2 vc = f.vis.v[gl_InstanceIndex];
    Instance it = f.vis_instances.v[vc.x];
    MeshInfo m = f.meshes.v[it.mesh];
    Vertex vx = f.vertices.v[gl_VertexIndex];
    vec3 local = quat_rotate(inst_quat(it), vec3(vx.px, vx.py, vx.pz) * it.scale);
    gl_Position = to_clip_jittered(rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z)) + local);
    v_slot = uint(gl_InstanceIndex);
    v_mat = it.material < 0xFFFFFF00u ? it.material : vx.mats & 0xFFFFu;
    // Копия со скиннингом берёт развёртку у источника: те же номера вершин от его бинд-позы
    uint bind = m.lods[0].w;
    uint ui = (m.flags & MESH_UV) != 0u ? uint(gl_VertexIndex) - m.first_vertex + (bind != 0u ? bind - 1u : m.first_vertex) : 0u;
    v_uv = (m.flags & MESH_UV) != 0u ? unpackHalf2x16(f.uvs.v[ui].uv) : vec2(0.0);
}
