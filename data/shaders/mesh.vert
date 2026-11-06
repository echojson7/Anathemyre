#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Сетка экземплярами: вершина по адресу общего буфера (gl_VertexIndex уже со смещением сетки),
// экземпляр — gl_InstanceIndex в буфере кадра. Положение — относительно камеры
layout(location = 0) out vec3 v_rel;
layout(location = 1) out vec3 v_normal;
layout(location = 2) flat out uvec4 v_mat;  // материал 0, 1, зерно
layout(location = 3) out vec2 v_blend_ao;

void main() {
    FrameData f = pc.frame;
    Vertex vx = f.vertices.v[gl_VertexIndex];
    Instance it = f.instances.v[gl_InstanceIndex];
    vec4 q = normalize(vec4(unpackSnorm2x16(it.rot_xy), unpackSnorm2x16(it.rot_zw)));
    vec3 local = quat_rotate(q, vec3(vx.px, vx.py, vx.pz) * it.scale);
    vec3 rel = rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z)) + local;
    gl_Position = to_clip(rel);
    v_rel = rel;
    v_normal = quat_rotate(q, oct_decode(vx.normal));
    uint m0 = vx.mats & 0xFFFFu, m1 = vx.mats >> 16;
    if (it.material != 0xFFFFFFFFu) m0 = m1 = it.material;
    v_mat = uvec4(m0, m1, it.seed, 0u);
    v_blend_ao = vec2(float(vx.misc & 0xFFu) / 255.0, float((vx.misc >> 8) & 0xFFu) / 255.0);
}
