// Разбор visibility buffer (docs/frame.md, «Кадр мира»): номер пикселя → треугольник сетки или земли → точка
// пересечения луча пикселя с треугольником и величины вершин в ней. Производных фрагмента нет: точка и
// глубина — из луча, нормаль треугольника — из его вершин
#ifndef RESOLVE_GLSL
#define RESOLVE_GLSL
#include "common.glsl"

const uint HIT_SKY = 0u, HIT_MESH = 1u, HIT_GROUND = 2u;

struct Hit {
    vec3 rel;      // точка относительно камеры
    uint kind;     // HIT_*
    vec3 n;        // нормаль вершин в точке (мир)
    float blend;   // сетка: доля второго материала; земля: глубина воды
    vec3 ng;       // нормаль треугольника (земля — вверх)
    float ao;      // сетка: запечённое затенение
    uint m0, m1, seed, level;  // сетка: материалы и зерно; земля: кольцо
};

// Луч пикселя (центр) относительно камеры, его проекция на взгляд = 1: точка = dir · глубина
vec3 pixel_ray(ivec2 px) {
    FrameData f = pc.frame;
    vec2 ndc = (vec2(px) + 0.5) * f.ndc_per_pixel - 1.0;
    return f.forward.xyz + f.right.xyz * (ndc.x / f.proj_x) - f.up.xyz * (ndc.y / f.proj_y);
}

// Пересечение луча с плоскостью треугольника: барицентрические веса (b0, b1, b2) и глубина в w
vec4 ray_tri(vec3 dir, vec3 a, vec3 b, vec3 c) {
    vec3 e1 = b - a, e2 = c - a;
    vec3 pv = cross(dir, e2);
    float det = dot(e1, pv);
    if (abs(det) < 1e-20) return vec4(1.0, 0.0, 0.0, dot(a, pc.frame.forward.xyz));
    float inv = 1.0 / det;
    float u = dot(-a, pv) * inv;
    vec3 qv = cross(-a, e1);
    float v = dot(dir, qv) * inv;
    float t = dot(e2, qv) * inv;
    // Центр пикселя у самого края мог уйти за треугольник на волос: держим веса в треугольнике
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0 - u);
    return vec4(1.0 - u - v, u, v, t);
}

Hit resolve(ivec2 px, uint id) {
    FrameData f = pc.frame;
    Hit h;
    h.kind = HIT_SKY;
    h.rel = pixel_ray(px);
    h.n = h.ng = vec3(0.0, 1.0, 0.0);
    h.blend = h.ao = 0.0;
    h.m0 = h.m1 = h.seed = h.level = 0u;
    if (id == 0u) return h;
    vec3 dir = h.rel;
    if ((id & VIS_GROUND) != 0u) {
        uint L = (id >> 20) & 0x7FFu;
        uint base = f.g_first[L] + (id & 0xFFFFFu) * 3u;
        // Вершины кольца уже посчитаны на кадр (ground_bake.comp)
        uint row = uint(2 * f.clip_n + 1), first = L * row * row;
        GroundVert a = f.g_verts.v[first + f.g_indices.v[base]];
        GroundVert b = f.g_verts.v[first + f.g_indices.v[base + 1u]];
        GroundVert c = f.g_verts.v[first + f.g_indices.v[base + 2u]];
        vec4 w = ray_tri(dir, a.rel, b.rel, c.rel);
        h.kind = HIT_GROUND;
        h.rel = dir * w.w;
        h.n = a.normal * w.x + b.normal * w.y + c.normal * w.z;
        h.blend = a.wdepth * w.x + b.wdepth * w.y + c.wdepth * w.z;
        h.ng = normalize(cross(b.rel - a.rel, c.rel - a.rel));
        if (h.ng.y < 0.0) h.ng = -h.ng;
        h.level = L;
        return h;
    }
    uvec2 vc = f.vis.v[(id >> 7) - 1u];
    Instance it = f.vis_instances.v[vc.x];
    Cluster cl = f.clusters.v[vc.y];
    MeshInfo m = f.meshes.v[it.mesh];
    uint base = m.first_index + cl.first_index + (id & 127u) * 3u;
    Vertex va = f.vertices.v[f.indices.v[base] + m.first_vertex];
    Vertex vb = f.vertices.v[f.indices.v[base + 1u] + m.first_vertex];
    Vertex vcx = f.vertices.v[f.indices.v[base + 2u] + m.first_vertex];
    vec4 q = inst_quat(it);
    vec3 o = rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z));
    vec3 pa = o + quat_rotate(q, vec3(va.px, va.py, va.pz) * it.scale);
    vec3 pb = o + quat_rotate(q, vec3(vb.px, vb.py, vb.pz) * it.scale);
    vec3 pcc = o + quat_rotate(q, vec3(vcx.px, vcx.py, vcx.pz) * it.scale);
    vec4 w = ray_tri(dir, pa, pb, pcc);
    h.kind = HIT_MESH;
    h.rel = dir * w.w;
    h.n = quat_rotate(q, oct_decode(va.normal) * w.x + oct_decode(vb.normal) * w.y + oct_decode(vcx.normal) * w.z);
    h.ng = h.n;
    vec3 bl = vec3(float(va.misc & 0xFFu), float(vb.misc & 0xFFu), float(vcx.misc & 0xFFu)) / 255.0;
    vec3 ao = vec3(float((va.misc >> 8) & 0xFFu), float((vb.misc >> 8) & 0xFFu), float((vcx.misc >> 8) & 0xFFu)) / 255.0;
    h.blend = dot(bl, w.xyz);
    h.ao = dot(ao, w.xyz);
    // Материалы — у первой вершины треугольника, как flat у растра (провоцирующая вершина)
    h.m0 = va.mats & 0xFFFFu;
    h.m1 = va.mats >> 16;
    if (it.material != 0xFFFFFFFFu) h.m0 = h.m1 = it.material;
    h.seed = it.seed;
    return h;
}
#endif
