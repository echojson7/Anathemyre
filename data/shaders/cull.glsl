// Отсечение и LOD на GPU (docs/frame.md, «Кадр мира»): экземпляр → сфера сетки в пирамиде взгляда → LOD по ошибке
// на экране (как pick_lod в src/view/view.cpp) → кластеры LOD, чьи сферы в пирамиде. cull_count.comp и
// cull_write.comp зовут одну и ту же функцию, поэтому счёт и запись совпадают до кластера
#ifndef CULL_GLSL
#define CULL_GLSL
#include "common.glsl"

const uint MAX_MESHES = 4096u;    // RENDER_MAX_MESHES
const float LOD_PIXELS = 0.75;    // как в view.cpp
const uint VEIL_MAX = 4096u;       // VIS_VEIL_MAX: мест списка морока за списком кадра
const uint ALPHA_MAX = 16384u;     // VIS_ALPHA_MAX: мест списка карточек с прозрачным краем за списком морока

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

// Группа из CULL_GROUP потоков на экземпляр (17.11): кластеры LOD идут по потокам кусками по 64, место кластера в
// списке — сколько видимых кластеров раньше него (биты куска в общей памяти), поэтому порядок вызовов и номера в
// visibility buffer те же, что при одном потоке на экземпляр. Раньше поток один проходил все кластеры сетки
// подряд, и большая постройка (сотни кластеров) держала весь проход своей длинной цепочкой чтений
const uint CULL_GROUP = 64u;
shared uint s_cull_mask[2];
shared uint s_cull_vbase;

// Экземпляр группы: dispatch бывает двумерным (больше 65535 экземпляров)
uint cull_group_instance() { return gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x; }

// Сколько кластеров экземпляра i видно и какой LOD; with_write — записать их вызовы с места first. Зовут все потоки
// группы; ответ — у всех
uint cull_instance(uint i, bool with_write, uint first, out uint lod) {
    FrameData f = pc.frame;
    uint t = gl_LocalInvocationIndex;
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
    // Морок (docs/tat.md): кластеры машины под мороком рисует свой конвейер (vis_veil) из мест за списком кадра;
    // место в главном списке остаётся пустым вызовом. Место в списке морока — одно прибавление на экземпляр
    // Карточки с прозрачным краем (волосы, MESH_ALPHA) — так же, свой список за списком морока и конвейер vis_alpha
    // с отсечением по альфе карты: discard в главном конвейере снял бы ранний тест глубины со всех сеток
    bool veil = with_write && it.material >= 0xFFFFFF00u && (it.material & 0xFFu) < 255u;
    bool alpha = with_write && !veil && (m.flags & MESH_ALPHA) != 0u;
    uint special_max = veil ? VEIL_MAX : ALPHA_MAX;
    uint special_at = veil ? f.vis_max : f.vis_max + VEIL_MAX;
    if (t == 0u) {
        uint nv = f.cull.count[i] & 0x1FFFFFFFu;
        s_cull_vbase = veil ? atomicAdd(f.cull.veil_count, nv) : (alpha ? atomicAdd(f.cull.alpha_count, nv) : 0u);
    }
    uint n = 0u;
    for (uint base = 0u; base < L.y; base += CULL_GROUP) {
        if (t < 2u) s_cull_mask[t] = 0u;
        barrier();
        uint k = base + t;
        uint ci = L.x + k;
        bool vis = false;
        Cluster c;
        if (k < L.y) {
            c = f.clusters.v[ci];
            vis = sphere_visible(origin + quat_rotate(q, c.sphere.xyz * it.scale), c.sphere.w * it.scale);
        }
        if (vis) atomicOr(s_cull_mask[t >> 5], 1u << (t & 31u));
        barrier();
        uint m0 = s_cull_mask[0], m1 = s_cull_mask[1];
        if (with_write && vis) {
            uint below = t < 32u ? bitCount(m0 & ((1u << t) - 1u)) : bitCount(m0) + bitCount(m1 & ((1u << (t - 32u)) - 1u));
            uint slot = first + n + below;
            if (slot < f.vis_max) {
                f.vis.v[slot] = uvec2(i, ci);
                DrawCmd d;
                d.index_count = c.tri_count * 3u;
                d.instance_count = 1u;
                d.first_index = m.first_index + c.first_index;
                d.vertex_offset = int(m.first_vertex);
                d.first_instance = slot;
                if (veil || alpha) {
                    uint vb = s_cull_vbase;
                    if (vb + n + below < special_max) {
                        uint vs = special_at + vb + n + below;
                        f.vis.v[vs] = uvec2(i, ci);
                        d.first_instance = vs;
                        f.draws.v[vs] = d;
                    }
                    d.instance_count = 0u;
                    d.first_instance = slot;
                }
                f.draws.v[slot] = d;
            }
        }
        n += bitCount(m0) + bitCount(m1);
        barrier();  // маска куска прочитана всеми до обнуления под следующий
    }
    return n;
}
#endif
