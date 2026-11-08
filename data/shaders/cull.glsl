// Отсечение и LOD на GPU (docs/frame.md, «Кадр мира»): экземпляр → сфера сетки в пирамиде взгляда → LOD по ошибке
// на экране (как pick_lod в src/view/view.cpp) → кластеры LOD, чьи сферы в пирамиде. cull_count.comp и
// cull_write.comp зовут одну и ту же функцию, поэтому счёт и запись совпадают до кластера
#ifndef CULL_GLSL
#define CULL_GLSL
#include "common.glsl"

const uint MAX_MESHES = 4096u;    // RENDER_MAX_MESHES
const float LOD_PIXELS = 0.75;    // как в view.cpp

// Сфера относительно камеры задевает пирамиду взгляда (дальней плоскости нет)
bool sphere_visible(vec3 c, float r) {
    FrameData f = pc.frame;
    float vx = dot(c, f.right.xyz), vy = dot(c, f.up.xyz), vz = dot(c, f.forward.xyz);
    if (vz < f.near_m - r) return false;
    // Боковые плоскости: |vx|·proj_x ≤ vz, расстояние до плоскости — с её длиной нормали
    float kx = sqrt(f.proj_x * f.proj_x + 1.0), ky = sqrt(f.proj_y * f.proj_y + 1.0);
    if (abs(vx) * f.proj_x - vz > r * kx) return false;
    if (abs(vy) * f.proj_y - vz > r * ky) return false;
    return true;
}

// Сколько кластеров экземпляра i видно и какой LOD; with_write — записать их вызовы с места first
uint cull_instance(uint i, bool with_write, uint first, out uint lod) {
    FrameData f = pc.frame;
    lod = 0u;
    Instance it = f.vis_instances.v[i];
    if (it.mesh == 0u || it.mesh >= MAX_MESHES) return 0u;
    MeshInfo m = f.meshes.v[it.mesh];
    if (m.lod_count == 0u) return 0u;
    vec4 q = inst_quat(it);
    vec3 origin = rel_to_camera(ivec2(it.cx, it.cz), vec3(it.x, it.y, it.z));
    if (!sphere_visible(origin + quat_rotate(q, m.sphere.xyz * it.scale), m.sphere.w * it.scale)) return 0u;
    float limit = LOD_PIXELS * f.pixel_angle * max(length(origin), 0.1);
    while (lod + 1u < m.lod_count && uintBitsToFloat(m.lods[lod + 1u].z) * it.scale <= limit) lod++;
    // Не тоньше lod_min: у человека скинены вершины только этого LOD и грубее (src/render/vk_skin.cpp)
    lod = clamp(max(lod, it.lod_min), 0u, m.lod_count - 1u);
    uvec4 L = m.lods[lod];
    uint n = 0u;
    for (uint k = 0u; k < L.y; ++k) {
        uint ci = L.x + k;
        Cluster c = f.clusters.v[ci];
        if (!sphere_visible(origin + quat_rotate(q, c.sphere.xyz * it.scale), c.sphere.w * it.scale)) continue;
        if (with_write) {
            uint slot = first + n;
            if (slot >= f.vis_max) break;
            f.vis.v[slot] = uvec2(i, ci);
            DrawCmd d;
            d.index_count = c.tri_count * 3u;
            d.instance_count = 1u;
            d.first_index = m.first_index + c.first_index;
            d.vertex_offset = int(m.first_vertex);
            d.first_instance = slot;
            f.draws.v[slot] = d;
        }
        n++;
    }
    return n;
}
#endif
