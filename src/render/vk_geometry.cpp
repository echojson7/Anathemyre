// Общая геометрия (docs/frame.md): один буфер вершин и один индексов на все сетки, сетка — номер в таблице.
// Место выдают виртуальные блоки VMA; освобождённое возвращается через FREE_DELAY_FRAMES кадров.
// Шейдер читает вершины сам по адресу буфера (без входа вершин конвейера), как потом visibility buffer
#include "vk_state.h"
#include "../asset/mesh.h"
#include "../core/log.h"
#include "../core/mathf.h"

static bool make_block(VmaVirtualBlock &b, VkDeviceSize size) {
    VmaVirtualBlockCreateInfo bi{};
    bi.size = size;
    return vk_check(vmaCreateVirtualBlock(&bi, &b), "vmaCreateVirtualBlock");
}

bool vk_geometry_init() {
    // Источник копии — для людей: копия вершин и кластеров на человека (vk_skin.cpp)
    const VkBufferUsageFlags dst = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!vk_create_buffer(g_vk.vertices, GEOMETRY_VERTEX_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true)) return false;
    if (!vk_create_buffer(g_vk.indices, GEOMETRY_INDEX_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true)) return false;
    if (!vk_create_buffer(g_vk.materials, RENDER_MAX_MATERIALS * sizeof(RenderMaterial), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        return false;
    // Пока таблицы нет — один материал «missing», чтобы шейдер не читал мусор
    RenderMaterial m{};
    m.p[0] = 1.0f; m.p[2] = 1.0f; m.p[3] = 1.0f; m.p[7] = 0.25f;
    memcpy(g_vk.materials.mapped, &m, sizeof m);
    // Кадр мира: таблица сеток (номер сетки → вершины, индексы, LOD с кластерами) и общая таблица кластеров
    if (!vk_create_buffer(g_vk.mesh_table, RENDER_MAX_MESHES * sizeof(MeshGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
    memset(g_vk.mesh_table.mapped, 0, RENDER_MAX_MESHES * sizeof(MeshGpu));
    if (!vk_create_buffer(g_vk.clusters, GEOMETRY_MAX_CLUSTERS * sizeof(ClusterGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true))
        return false;
    return make_block(g_vk.vtx_block, GEOMETRY_VERTEX_BYTES) && make_block(g_vk.idx_block, GEOMETRY_INDEX_BYTES) &&
           make_block(g_vk.cls_block, GEOMETRY_MAX_CLUSTERS);
}

static void free_slot(u32 id) {
    MeshSlot &s = g_vk.meshes[id];
    if (s.vtx) vmaVirtualFree(g_vk.vtx_block, s.vtx);
    if (s.idx) vmaVirtualFree(g_vk.idx_block, s.idx);
    if (s.cls) vmaVirtualFree(g_vk.cls_block, s.cls);
    if (!s.skin) g_vk.shadow.mesh_gen++;  // страницы теней нарисованы и из этой сетки (копии людей в них не входят)
    s = MeshSlot{};
    memset((MeshGpu *)g_vk.mesh_table.mapped + id, 0, sizeof(MeshGpu));  // lod_count 0: отсечение её пропускает
}

void vk_geometry_shutdown() {
    for (u32 i = 1; i < RENDER_MAX_MESHES; ++i)
        if (g_vk.meshes[i].used) free_slot(i);
    g_vk.pending_count = 0;
    if (g_vk.vtx_block) vmaDestroyVirtualBlock(g_vk.vtx_block);
    if (g_vk.idx_block) vmaDestroyVirtualBlock(g_vk.idx_block);
    if (g_vk.cls_block) vmaDestroyVirtualBlock(g_vk.cls_block);
    g_vk.vtx_block = g_vk.idx_block = g_vk.cls_block = VK_NULL_HANDLE;
    vk_destroy_buffer(g_vk.vertices);
    vk_destroy_buffer(g_vk.indices);
    vk_destroy_buffer(g_vk.materials);
    vk_destroy_buffer(g_vk.mesh_table);
    vk_destroy_buffer(g_vk.clusters);
}

void vk_geometry_collect() {
    u32 k = 0;
    for (u32 i = 0; i < g_vk.pending_count; ++i) {
        PendingFree p = g_vk.pending[i];
        if (g_vk.frames_drawn >= p.frame) free_slot(p.mesh);
        else g_vk.pending[k++] = p;
    }
    g_vk.pending_count = k;
}

bool vk_one_shot(void (*record)(VkCommandBuffer, void *), void *user) {
    auto pi = vk_info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pi.queueFamilyIndex = g_vk.queue_family;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (!vk_check(vkCreateCommandPool(g_vk.device, &pi, nullptr, &pool), "vkCreateCommandPool")) return false;
    auto ai = vk_info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    bool ok = vk_check(vkAllocateCommandBuffers(g_vk.device, &ai, &cmd), "vkAllocateCommandBuffers");
    if (ok) {
        auto bi = vk_info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        record(cmd, user);
        vkEndCommandBuffer(cmd);
        auto cbi = vk_info<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        cbi.commandBuffer = cmd;
        auto si = vk_info<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos = &cbi;
        ok = vk_check(vkQueueSubmit2(g_vk.queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit2") &&
             vk_check(vkQueueWaitIdle(g_vk.queue), "vkQueueWaitIdle");
    }
    vkDestroyCommandPool(g_vk.device, pool, nullptr);
    return ok;
}

struct UploadCopy {
    VkBuffer staging;
    VkBufferCopy vtx, idx, cls;
};

static void record_upload(VkCommandBuffer cmd, void *user) {
    UploadCopy &u = *(UploadCopy *)user;
    vkCmdCopyBuffer(cmd, u.staging, g_vk.vertices.buffer, 1, &u.vtx);
    vkCmdCopyBuffer(cmd, u.staging, g_vk.indices.buffer, 1, &u.idx);
    vkCmdCopyBuffer(cmd, u.staging, g_vk.clusters.buffer, 1, &u.cls);
    // Копия видна чтению вершин, индексов и кластеров в следующих кадрах (растр и compute)
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

// Сфера вокруг вершин индексов [first, first + count): середина коробки и наибольшее расстояние до неё
static void bound_sphere(const MeshVertex *v, const u32 *idx, u32 count, f32 out[4]) {
    f32 lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (u32 i = 0; i < count; ++i) {
        const MeshVertex &p = v[idx ? idx[i] : i];
        const f32 c[3] = {p.px, p.py, p.pz};
        for (u32 a = 0; a < 3; ++a) {
            lo[a] = c[a] < lo[a] ? c[a] : lo[a];
            hi[a] = c[a] > hi[a] ? c[a] : hi[a];
        }
    }
    f32 r2 = 0;
    for (u32 a = 0; a < 3; ++a) out[a] = (lo[a] + hi[a]) * 0.5f;
    for (u32 i = 0; i < count; ++i) {
        const MeshVertex &p = v[idx ? idx[i] : i];
        f32 dx = p.px - out[0], dy = p.py - out[1], dz = p.pz - out[2];
        f32 d = dx * dx + dy * dy + dz * dz;
        r2 = d > r2 ? d : r2;
    }
    out[3] = f_sqrt(r2) * 1.0001f + 1e-4f;
}

u32 render_mesh_create(Span<const MeshVertex> vertices, Span<const u32> indices, Span<const RenderLod> lods_in,
                       Span<const RenderCluster> clusters_in) {
    if (!vertices.count || !indices.count) return 0;
    for (const RenderLod &l : lods_in)
        if ((u64)l.first_index + l.index_count > indices.count ||
            (u64)l.first_cluster + l.cluster_count > clusters_in.count) {
            log_error("LOD за индексами или кластерами сетки");
            return 0;
        }
    for (const RenderCluster &c : clusters_in)
        if (c.tri_count > MESH_MAX_CLUSTER_TRIS || (u64)c.first_index + c.tri_count * 3ull > indices.count) {
            log_error("кластер за индексами сетки");
            return 0;
        }
    // LOD и кластеры, как их увидит рендер. LOD без кластеров режется на куски по 124 треугольника подряд
    RenderLod lods[RENDER_MESH_MAX_LODS];
    u32 nlods = lods_in.count < RENDER_MESH_MAX_LODS ? (u32)lods_in.count : RENDER_MESH_MAX_LODS;
    for (u32 i = 0; i < nlods; ++i) lods[i] = lods_in.data[i];
    if (!nlods) lods[nlods++] = {0, (u32)indices.count, 0, 0, 0.0f};
    u32 ncls = (u32)clusters_in.count;
    for (u32 i = 0; i < nlods; ++i)
        if (!lods[i].cluster_count) ncls += (lods[i].index_count / 3 + MESH_MAX_CLUSTER_TRIS - 1) / MESH_MAX_CLUSTER_TRIS;
    u32 id = 0;
    for (u32 i = 1; i < RENDER_MAX_MESHES && !id; ++i)
        if (!g_vk.meshes[i].used) id = i;
    if (!id) { log_error("сеток больше %u", RENDER_MAX_MESHES); return 0; }
    MeshSlot &s = g_vk.meshes[id];
    VkDeviceSize vbytes = vertices.count * sizeof(MeshVertex), ibytes = indices.count * sizeof(u32);
    // Начало вершин должно делиться на 24 байта (vertexOffset — номер вершины), а VMA понимает только
    // выравнивание степенью двойки: место берётся на вершину больше и начало округляется вверх до 24
    VmaVirtualAllocationCreateInfo vi{};
    vi.size = vbytes + sizeof(MeshVertex);
    vi.alignment = 8;
    VkDeviceSize voff = 0, ioff = 0;
    if (vmaVirtualAllocate(g_vk.vtx_block, &vi, &s.vtx, &voff) != VK_SUCCESS) {
        log_error("общий буфер вершин полон (%llu МБ)", (unsigned long long)(GEOMETRY_VERTEX_BYTES >> 20));
        return 0;
    }
    voff = (voff + sizeof(MeshVertex) - 1) / sizeof(MeshVertex) * sizeof(MeshVertex);
    vi.size = ibytes;
    vi.alignment = sizeof(u32);
    if (vmaVirtualAllocate(g_vk.idx_block, &vi, &s.idx, &ioff) != VK_SUCCESS) {
        log_error("общий буфер индексов полон (%llu МБ)", (unsigned long long)(GEOMETRY_INDEX_BYTES >> 20));
        free_slot(id);
        return 0;
    }
    VkDeviceSize coff = 0;
    vi.size = ncls;
    vi.alignment = 1;
    if (vmaVirtualAllocate(g_vk.cls_block, &vi, &s.cls, &coff) != VK_SUCCESS) {
        log_error("таблица кластеров полна (%u)", GEOMETRY_MAX_CLUSTERS);
        free_slot(id);
        return 0;
    }
    s.used = true;
    s.first_vertex = (u32)(voff / sizeof(MeshVertex));
    s.vertex_count = (u32)vertices.count;
    s.first_index = (u32)(ioff / sizeof(u32));
    s.index_count = (u32)indices.count;
    s.first_cluster = (u32)coff;
    s.cluster_count = ncls;
    s.lod_count = nlods;
    for (u32 i = 0; i < nlods; ++i) s.lods[i] = lods[i];

    // Загрузка: временный буфер, копия на GPU, ожидание. Только при загрузке, не в игровом цикле
    VkDeviceSize cbytes = (VkDeviceSize)ncls * sizeof(ClusterGpu);
    GpuBuffer staging;
    if (!vk_create_buffer(staging, vbytes + ibytes + cbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) { free_slot(id); return 0; }
    memcpy(staging.mapped, vertices.data, vbytes);
    memcpy((u8 *)staging.mapped + vbytes, indices.data, ibytes);
    // Кластеры: свои, потом нарезанные для LOD без кластеров; строка таблицы сеток — с номерами в общей таблице
    ClusterGpu *cg = (ClusterGpu *)((u8 *)staging.mapped + vbytes + ibytes);
    for (usize i = 0; i < clusters_in.count; ++i) {
        const RenderCluster &c = clusters_in.data[i];
        cg[i] = ClusterGpu{c.first_index, c.tri_count, {c.sphere[0], c.sphere[1], c.sphere[2], c.sphere[3]}, {0, 0}};
    }
    MeshGpu &mg = ((MeshGpu *)g_vk.mesh_table.mapped)[id];
    mg = MeshGpu{};
    mg.first_vertex = s.first_vertex;
    mg.first_index = s.first_index;
    mg.lod_count = nlods;
    bound_sphere(vertices.data, nullptr, (u32)vertices.count, mg.sphere);
    g_vk.shadow.mesh_gen++;  // номер сетки мог достаться новой: страницы теней — с нуля
    u32 nc = (u32)clusters_in.count;
    for (u32 i = 0; i < nlods; ++i) {
        RenderLod &l = lods[i];
        if (!l.cluster_count) {
            l.first_cluster = nc;
            for (u32 t = 0; t < l.index_count / 3; t += MESH_MAX_CLUSTER_TRIS) {
                u32 tris = l.index_count / 3 - t < MESH_MAX_CLUSTER_TRIS ? l.index_count / 3 - t : MESH_MAX_CLUSTER_TRIS;
                ClusterGpu &c = cg[nc++];
                c = ClusterGpu{l.first_index + t * 3, tris, {}, {0, 0}};
                bound_sphere(vertices.data, indices.data + c.first_index, tris * 3, c.sphere);
                l.cluster_count++;
            }
        }
        mg.lods[i] = {s.first_cluster + l.first_cluster, l.cluster_count, l.error, 0};
    }
    vmaFlushAllocation(g_vk.vma, staging.alloc, 0, VK_WHOLE_SIZE);
    UploadCopy u{staging.buffer, {0, voff, vbytes}, {vbytes, ioff, ibytes}, {vbytes + ibytes, coff * sizeof(ClusterGpu), cbytes}};
    bool ok = vk_one_shot(record_upload, &u);
    vk_destroy_buffer(staging);
    if (!ok) { free_slot(id); return 0; }
    return id;
}

void render_mesh_destroy(u32 mesh) {
    if (mesh == 0 || mesh >= RENDER_MAX_MESHES || !g_vk.meshes[mesh].used || g_vk.meshes[mesh].dying) return;
    if (g_vk.pending_count >= MAX_PENDING_FREES) {
        // Очередь полна: дождаться GPU и освободить сразу
        vkDeviceWaitIdle(g_vk.device);
        g_vk.frames_drawn += FREE_DELAY_FRAMES;
        vk_geometry_collect();
        g_vk.frames_drawn -= FREE_DELAY_FRAMES;
    }
    g_vk.meshes[mesh].dying = true;
    g_vk.pending[g_vk.pending_count++] = {mesh, g_vk.frames_drawn + FREE_DELAY_FRAMES};
}

bool render_set_materials(Span<const RenderMaterial> materials) {
    if (materials.count > RENDER_MAX_MATERIALS) { log_error("материалов больше %u", RENDER_MAX_MATERIALS); return false; }
    vkDeviceWaitIdle(g_vk.device);
    memcpy(g_vk.materials.mapped, materials.data, materials.count * sizeof(RenderMaterial));
    g_vk.gcache.materials_gen++;  // тексели кеша земли считаны из старых материалов
    vmaFlushAllocation(g_vk.vma, g_vk.materials.alloc, 0, VK_WHOLE_SIZE);
    return true;
}
