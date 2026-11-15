// Векторы движения для TAA (docs/aa.md): куда точка пикселя была видна в прошлом кадре. Пишет освещение
// (light_cells.comp) по своему пикселю: точку оно уже знает из разбора visibility buffer. Сетки, кроме людей,
// неподвижны — у них движется только камера; у людей прошлое место — из прошлых вершин (skin.comp) и прошлого
// экземпляра (vk_skin.cpp). Небо — только поворот камеры
#ifndef MOTION_GLSL
#define MOTION_GLSL
#include "resolve.glsl"

// Точка относительно камеры → пиксель кадра без дрожания (дробно, от угла); prev — вид прошлого кадра
vec2 aa_project(vec3 rel, bool prev) {
    FrameData f = pc.frame;
    vec3 r = prev ? f.prev_right.xyz : f.right.xyz;
    vec3 u = prev ? f.prev_up.xyz : f.up.xyz;
    vec3 w = prev ? f.prev_forward.xyz : f.forward.xyz;
    vec2 pj = prev ? f.prev_proj : vec2(f.proj_x, f.proj_y);
    float vz = max(dot(rel, w), 1e-3);
    vec2 ndc = vec2(dot(rel, r) * pj.x, -dot(rel, u) * pj.y) / vz;
    return (ndc + 1.0) / f.ndc_per_pixel;
}

vec3 prev_local(uint v) {
    uvec2 e = pc.frame.skin_prev.v[v];
    return vec3(unpackHalf2x16(e.x), unpackHalf2x16(e.y).x);
}

// Прошлое место точки rel сетки с номером id (относительно камеры этого кадра). Неподвижная сетка — та же точка;
// человек — те же веса в треугольнике прошлых вершин, поставленном прошлым экземпляром. Прошлого нет (человек
// только что появился в кадре) — как неподвижный
vec3 mesh_prev_rel(uint id, vec3 rel) {
    FrameData f = pc.frame;
    uvec2 vc = f.vis.v[(id >> 7) - 1u];
    Instance it = f.vis_instances.v[vc.x];
    MeshInfo m = f.meshes.v[it.mesh];
    if ((m.flags & MESH_DYNAMIC) == 0u) return rel;
    MeshPrev mp = f.mesh_prev.v[it.mesh];
    if (mp.frame != f.aa_frame) return rel;
    Cluster cl = f.clusters.v[vc.y];
    uint base = m.first_index + cl.first_index + (id & 127u) * 3u;
    uint ia = f.indices.v[base] + m.first_vertex, ib = f.indices.v[base + 1u] + m.first_vertex;
    uint ic = f.indices.v[base + 2u] + m.first_vertex;
    Vertex va = f.vertices.v[ia], vb = f.vertices.v[ib], vx = f.vertices.v[ic];
    vec4 q = inst_quat(it);
    vec3 o = rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z));
    vec3 pa = o + quat_rotate(q, vec3(va.px, va.py, va.pz) * it.scale);
    vec3 pb = o + quat_rotate(q, vec3(vb.px, vb.py, vb.pz) * it.scale);
    vec3 pcc = o + quat_rotate(q, vec3(vx.px, vx.py, vx.pz) * it.scale);
    // Веса точки в треугольнике (точка лежит в его плоскости: её дал разбор этого же треугольника)
    vec3 e1 = pb - pa, e2 = pcc - pa, d = rel - pa;
    float d00 = dot(e1, e1), d01 = dot(e1, e2), d11 = dot(e2, e2), d20 = dot(d, e1), d21 = dot(d, e2);
    float den = d00 * d11 - d01 * d01;
    if (abs(den) < 1e-20) return rel;
    float bv = (d11 * d20 - d01 * d21) / den, bw = (d00 * d21 - d01 * d20) / den;
    vec3 lp = prev_local(ia) * (1.0 - bv - bw) + prev_local(ib) * bv + prev_local(ic) * bw;
    vec4 pq = normalize(vec4(unpackSnorm2x16(mp.rot_xy), unpackSnorm2x16(mp.rot_zw)));
    return rel_to_camera(ivec2(mp.cx, mp.cz), vec3(mp.x, mp.y, mp.z)) + quat_rotate(pq, lp * mp.scale);
}

// Вектор движения пикселя: xy — прошлое место минус нынешнее, пиксели; z — глубина вдоль взгляда (по ней сглаживание
// берёт вектор ближайшего соседа: края предметов не тянут за собой фон); небо — дальше всего
vec4 aa_motion(Hit h, uint id) {
    FrameData f = pc.frame;
    if (h.kind == HIT_SKY) return vec4(aa_project(h.rel, true) - aa_project(h.rel, false), 60000.0, 0.0);
    vec3 p = h.kind == HIT_MESH ? mesh_prev_rel(id, h.rel) : h.rel;
    vec2 mv = aa_project(p + f.prev_delta.xyz, true) - aa_project(h.rel, false);
    return vec4(clamp(mv, vec2(-30000.0), vec2(30000.0)), min(dot(h.rel, f.forward.xyz), 59000.0), 0.0);
}
#endif
