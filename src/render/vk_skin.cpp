// Скиннинг людей (docs/people.md, «Скиннинг»): источник — вершины бинд-позы сетки с весами в общем буфере и
// веса с морфами в своём буфере; у человека — своя копия вершин и кластеров (номер сетки для экземпляра).
// Кадр: процессор выбирает LOD человека тем же правилом, что отсечение (ошибка на экране ≤ 0.75 пикселя), и
// отдаёт его рендеру как lod_min экземпляра; skin.comp пишет вершины префикса этого LOD из палитры костей и
// морфов, skin_bounds.comp пересчитывает сферы кластеров этого LOD и грубее. Дальше человек — деталь кадра мира
#include "vk_graph.h"
#include "../asset/mesh.h"
#include "../core/log.h"
#include "../core/mathf.h"

constexpr f32 LOD_PIXELS = 0.75f;  // как в cull.glsl
// Сфера сетки человека: бинд-поза × запас (руки вверх и в стороны выходят из сферы A-позы)
constexpr f32 SKIN_SPHERE_GROW = 1.6f;

bool vk_skin_init() {
    SkinGpu &k = g_vk.skin;
    if (!vk_create_buffer(k.data, SKIN_DATA_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
        return false;
    vk_buffer_name(k.data, "люди: веса и морфы");
    for (FrameSlot &fs : g_vk.frames) {
        if (!vk_create_buffer(fs.skin_jobs, RENDER_MAX_SKINNED * sizeof(SkinJobGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
            !vk_create_buffer(fs.bones, RENDER_MAX_BONES * sizeof(RenderBone), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return false;
        vk_buffer_name(fs.skin_jobs, "люди: работы кадра");
        vk_buffer_name(fs.bones, "люди: палитры костей");
    }
    k.rg_vertices = rg_buffer("сетки: вершины");
    k.rg_clusters = rg_buffer("сетки: кластеры");
    k.pipe_skin = vk_make_compute("skin.comp");
    k.pipe_bounds = vk_make_compute("skin_bounds.comp");
    return k.pipe_skin && k.pipe_bounds;
}

void vk_skin_shutdown() {
    SkinGpu &k = g_vk.skin;
    if (k.pipe_skin) vkDestroyPipeline(g_vk.device, k.pipe_skin, nullptr);
    if (k.pipe_bounds) vkDestroyPipeline(g_vk.device, k.pipe_bounds, nullptr);
    for (FrameSlot &fs : g_vk.frames) {
        vk_destroy_buffer(fs.skin_jobs);
        vk_destroy_buffer(fs.bones);
    }
    vk_destroy_buffer(k.data);
    k = SkinGpu{};
}

struct SkinUpload {
    VkBuffer staging;
    VkBufferCopy copy;
};

static void record_skin_upload(VkCommandBuffer cmd, void *user) {
    SkinUpload &u = *(SkinUpload *)user;
    vkCmdCopyBuffer(cmd, u.staging, g_vk.skin.data.buffer, 1, &u.copy);
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

u32 render_skin_create(const RenderSkinDesc &d) {
    SkinGpu &k = g_vk.skin;
    if (d.mesh == 0 || d.mesh >= RENDER_MAX_MESHES || !g_vk.meshes[d.mesh].used) { log_error("скиннинг: нет сетки"); return 0; }
    const MeshSlot &m = g_vk.meshes[d.mesh];
    u32 nv = m.vertex_count;
    if (k.count >= RENDER_MAX_SKINS) { log_error("источников скиннинга больше %u", RENDER_MAX_SKINS); return 0; }
    if (d.bones4.count != nv || d.weights4.count != nv || d.morph_count > RENDER_MAX_MORPHS ||
        d.morphs.count != (usize)nv * d.morph_count * 3 || d.lod_vertices.count != m.lod_count || !d.bone_count) {
        log_error("скиннинг: веса, морфы или LODV не сходятся с сеткой");
        return 0;
    }
    for (usize l = 0; l < d.lod_vertices.count; ++l)
        if (d.lod_vertices.data[l] > nv) { log_error("скиннинг: LODV больше вершин"); return 0; }
    VkDeviceSize skin_bytes = (VkDeviceSize)nv * 8, morph_bytes = d.morphs.count * 4, bytes = skin_bytes + morph_bytes;
    if (k.used + bytes > SKIN_DATA_BYTES) { log_error("буфер весов людей полон (%llu МБ)", (unsigned long long)(SKIN_DATA_BYTES >> 20)); return 0; }
    GpuBuffer staging;
    if (!vk_create_buffer(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return 0;
    u32 *w = (u32 *)staging.mapped;
    for (u32 i = 0; i < nv; ++i) {
        w[i * 2] = d.bones4.data[i];
        w[i * 2 + 1] = d.weights4.data[i];
    }
    if (morph_bytes) memcpy((u8 *)staging.mapped + skin_bytes, d.morphs.data, morph_bytes);
    vmaFlushAllocation(g_vk.vma, staging.alloc, 0, VK_WHOLE_SIZE);
    SkinUpload u{staging.buffer, {0, k.used, bytes}};
    bool ok = vk_one_shot(record_skin_upload, &u);
    vk_destroy_buffer(staging);
    if (!ok) return 0;
    SkinSource &s = k.src[++k.count];
    s = SkinSource{};
    s.mesh = d.mesh;
    s.vertex_count = nv;
    s.bone_count = d.bone_count;
    s.morph_count = d.morph_count;
    s.skin = k.data.address + k.used;
    s.morphs = d.morph_count ? s.skin + skin_bytes : 0;
    for (u32 i = 0; i < d.morph_count; ++i) s.step[i] = d.morph_step[i];
    for (usize l = 0; l < d.lod_vertices.count; ++l) s.lod_vertices[l] = d.lod_vertices.data[l];
    k.used += (bytes + 255) & ~(VkDeviceSize)255;
    return k.count;
}

struct SkinnedCopy {
    VkBufferCopy vtx, cls;
};

static void record_skinned_copy(VkCommandBuffer cmd, void *user) {
    SkinnedCopy &c = *(SkinnedCopy *)user;
    vkCmdCopyBuffer(cmd, g_vk.vertices.buffer, g_vk.vertices.buffer, 1, &c.vtx);
    vkCmdCopyBuffer(cmd, g_vk.clusters.buffer, g_vk.clusters.buffer, 1, &c.cls);
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

u32 render_skinned_create(u32 skin) {
    SkinGpu &k = g_vk.skin;
    if (skin == 0 || skin > k.count) { log_error("скиннинг: нет источника %u", skin); return 0; }
    const SkinSource &s = k.src[skin];
    const MeshSlot src = g_vk.meshes[s.mesh];
    u32 id = 0;
    for (u32 i = 1; i < RENDER_MAX_MESHES && !id; ++i)
        if (!g_vk.meshes[i].used) id = i;
    if (!id) { log_error("сеток больше %u", RENDER_MAX_MESHES); return 0; }
    MeshSlot &d = g_vk.meshes[id];
    // Вершины — тем же способом, что render_mesh_create: начало кратно 24 байтам
    VmaVirtualAllocationCreateInfo vi{};
    vi.size = (VkDeviceSize)(src.vertex_count + 1) * sizeof(MeshVertex);
    vi.alignment = 8;
    VkDeviceSize voff = 0, coff = 0;
    if (vmaVirtualAllocate(g_vk.vtx_block, &vi, &d.vtx, &voff) != VK_SUCCESS) {
        log_error("общий буфер вершин полон");
        d = MeshSlot{};
        return 0;
    }
    voff = (voff + sizeof(MeshVertex) - 1) / sizeof(MeshVertex) * sizeof(MeshVertex);
    vi.size = src.cluster_count;
    vi.alignment = 1;
    if (vmaVirtualAllocate(g_vk.cls_block, &vi, &d.cls, &coff) != VK_SUCCESS) {
        log_error("таблица кластеров полна");
        vmaVirtualFree(g_vk.vtx_block, d.vtx);
        d = MeshSlot{};
        return 0;
    }
    SkinnedCopy c{{(VkDeviceSize)src.first_vertex * sizeof(MeshVertex), voff, (VkDeviceSize)src.vertex_count * sizeof(MeshVertex)},
                  {(VkDeviceSize)src.first_cluster * sizeof(ClusterGpu), coff * sizeof(ClusterGpu),
                   (VkDeviceSize)src.cluster_count * sizeof(ClusterGpu)}};
    if (!vk_one_shot(record_skinned_copy, &c)) {
        vmaVirtualFree(g_vk.vtx_block, d.vtx);
        vmaVirtualFree(g_vk.cls_block, d.cls);
        d = MeshSlot{};
        return 0;
    }
    d.used = true;
    d.skin = skin;
    d.first_vertex = (u32)(voff / sizeof(MeshVertex));
    d.vertex_count = src.vertex_count;
    d.first_index = src.first_index;  // индексы источника, своих нет (d.idx пуст: освобождать нечего)
    d.index_count = src.index_count;
    d.first_cluster = (u32)coff;
    d.cluster_count = src.cluster_count;
    d.lod_count = src.lod_count;
    for (u32 i = 0; i < src.lod_count; ++i) d.lods[i] = src.lods[i];
    MeshGpu &mg = ((MeshGpu *)g_vk.mesh_table.mapped)[id];
    mg = ((const MeshGpu *)g_vk.mesh_table.mapped)[s.mesh];
    mg.first_vertex = d.first_vertex;
    mg.sphere[3] *= SKIN_SPHERE_GROW;
    mg.pad = MESH_DYNAMIC;  // вершины меняются каждый кадр: в страницы теней не идёт (docs/shadow.md)
    for (u32 i = 0; i < src.lod_count; ++i) mg.lods[i].first_cluster += d.first_cluster - src.first_cluster;
    return id;
}

// Сфера относительно камеры задевает пирамиду взгляда (как sphere_visible в cull.glsl)
static bool sphere_visible(const FrameGpu &g, const f32 c[3], f32 r) {
    f32 vx = c[0] * g.right[0] + c[1] * g.right[1] + c[2] * g.right[2];
    f32 vy = c[0] * g.up[0] + c[1] * g.up[1] + c[2] * g.up[2];
    f32 vz = c[0] * g.forward[0] + c[1] * g.forward[1] + c[2] * g.forward[2];
    if (vz < g.near_m - r) return false;
    f32 kx = f_sqrt(g.proj_x * g.proj_x + 1.0f), ky = f_sqrt(g.proj_y * g.proj_y + 1.0f);
    if (f_abs(vx) * g.proj_x - vz > r * kx) return false;
    if (f_abs(vy) * g.proj_y - vz > r * ky) return false;
    return true;
}

static void quat_rotate(const f32 q[4], const f32 v[3], f32 out[3]) {
    f32 t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

static f32 unpack_snorm16(u32 v) {
    f32 x = (f32)(i16)(u16)v / 32767.0f;
    return x < -1.0f ? -1.0f : x;
}

void vk_skin_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs) {
    SkinGpu &k = g_vk.skin;
    for (u32 i = 0; i < k.touched_count; ++i) k.lod_min[k.touched[i]] = 0;
    k.touched_count = 0;
    k.jobs = k.max_verts = k.max_clusters = k.skinned_verts = 0;
    vk_overflow("люди", f.skinned.count, RENDER_MAX_SKINNED);
    vk_overflow("кости людей", f.bones.count, RENDER_MAX_BONES);
    u32 nb = f.bones.count < RENDER_MAX_BONES ? (u32)f.bones.count : RENDER_MAX_BONES;
    if (nb) memcpy(fs.bones.mapped, f.bones.data, nb * sizeof(RenderBone));
    SkinJobGpu *jobs = (SkinJobGpu *)fs.skin_jobs.mapped;
    u32 n = f.skinned.count < RENDER_MAX_SKINNED ? (u32)f.skinned.count : RENDER_MAX_SKINNED;
    for (u32 j = 0; j < n; ++j) {
        const RenderSkinned &p = f.skinned.data[j];
        if (p.instance >= f.instances.count || p.instance >= RENDER_MAX_INSTANCES) continue;
        const RenderInstance &it = f.instances.data[p.instance];
        if (it.mesh == 0 || it.mesh >= RENDER_MAX_MESHES || !g_vk.meshes[it.mesh].skin) continue;
        const MeshSlot &m = g_vk.meshes[it.mesh];
        const SkinSource &s = k.src[m.skin];
        if (p.first_bone + s.bone_count > nb) continue;
        // LOD — как в cull.glsl: самый грубый, у которого ошибка на экране ≤ LOD_PIXELS; отсечение не возьмёт тоньше
        f32 origin[3] = {(f32)(it.cx - g.cam_cx) * 256.0f + (it.x - g.cam_x), it.y - g.cam_y,
                         (f32)(it.cz - g.cam_cz) * 256.0f + (it.z - g.cam_z)};
        f32 dist = f_sqrt(origin[0] * origin[0] + origin[1] * origin[1] + origin[2] * origin[2]);
        f32 limit = LOD_PIXELS * g.pixel_angle * (dist > 0.1f ? dist : 0.1f);
        u32 lod = 0;
        while (lod + 1 < m.lod_count && m.lods[lod + 1].error * it.scale <= limit) lod++;
        k.lod_min[p.instance] = (u8)lod;
        k.touched[k.touched_count++] = p.instance;
        // Невидимого не скинить: сфера с запасом шире, чем у отсечения (оно не возьмёт то, чего не видит процессор)
        const MeshGpu &mg = ((const MeshGpu *)g_vk.mesh_table.mapped)[it.mesh];
        f32 q[4] = {unpack_snorm16(it.rot_xy), unpack_snorm16(it.rot_xy >> 16), unpack_snorm16(it.rot_zw),
                    unpack_snorm16(it.rot_zw >> 16)};
        f32 ql = f_sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (f32 &x : q) x /= ql > 0 ? ql : 1.0f;
        f32 sc[3] = {mg.sphere[0] * it.scale, mg.sphere[1] * it.scale, mg.sphere[2] * it.scale}, c[3];
        quat_rotate(q, sc, c);
        for (u32 a = 0; a < 3; ++a) c[a] += origin[a];
        if (!sphere_visible(g, c, mg.sphere[3] * it.scale * 1.05f + 0.1f)) continue;
        SkinJobGpu &w = jobs[k.jobs++];
        w = SkinJobGpu{};
        w.src_vertex = g_vk.meshes[s.mesh].first_vertex;
        w.dst_vertex = m.first_vertex;
        w.vertex_count = s.lod_vertices[lod];
        w.first_bone = p.first_bone;
        w.skin = s.skin;
        w.morphs = s.morphs;
        w.morph_count = s.morph_count;
        for (u32 i = 0; i < s.morph_count; ++i) {
            w.morph[i] = p.morph[i];
            w.step[i] = s.step[i];
        }
        // Кластеры этого LOD и грубее идут подряд до конца кластеров сетки
        w.first_cluster = m.first_cluster + m.lods[lod].first_cluster;
        w.cluster_count = m.cluster_count - m.lods[lod].first_cluster;
        w.first_index = m.first_index;
        k.max_verts = w.vertex_count > k.max_verts ? w.vertex_count : k.max_verts;
        k.max_clusters = w.cluster_count > k.max_clusters ? w.cluster_count : k.max_clusters;
        k.skinned_verts += w.vertex_count;
    }
    if (k.jobs) vmaFlushAllocation(g_vk.vma, fs.skin_jobs.alloc, 0, VK_WHOLE_SIZE);
    if (nb) vmaFlushAllocation(g_vk.vma, fs.bones.alloc, 0, VK_WHOLE_SIZE);
    g.skin_jobs = fs.skin_jobs.address;
    g.bones = fs.bones.address;
    g.vertices_w = g_vk.vertices.address;
    g.clusters_w = g_vk.clusters.address;
    g.skin_count = k.jobs;
}

static void record_skin(VkCommandBuffer cmd, void *user) {
    const SkinGpu &k = g_vk.skin;
    VkDeviceAddress frame = *(const VkDeviceAddress *)user;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipe_skin);
    PushConsts pc{frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, (k.max_verts + 63) / 64, k.jobs, 1);
}

static void record_bounds(VkCommandBuffer cmd, void *user) {
    const SkinGpu &k = g_vk.skin;
    VkDeviceAddress frame = *(const VkDeviceAddress *)user;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipe_bounds);
    PushConsts pc{frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, k.max_clusters, k.jobs, 1);
}

void vk_skin_passes(const VkDeviceAddress *frame) {
    const SkinGpu &k = g_vk.skin;
    if (!k.jobs) return;
    rg_pass("люди: скиннинг", "люди", record_skin, (void *)frame);
    rg_use(k.rg_vertices, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    rg_pass("люди: сферы кластеров", "люди", record_bounds, (void *)frame);
    rg_use(k.rg_vertices, RG_CS_READ);
    rg_use(k.rg_clusters, RG_CS_WRITE);
}

void vk_skin_vertices_read(RgUse u) { rg_use(g_vk.skin.rg_vertices, u); }
void vk_skin_clusters_read() { rg_use(g_vk.skin.rg_clusters, RG_CS_READ); }
