// Кадр мира (этап 5, docs/frame.md «Кадр мира», docs/render.md «Проходы с этапа 5»):
// 1) экземпляры лежат на GPU постоянно, CPU копирует только изменившиеся;
// 2) отсечение и LOD в compute (cull_*.comp): экземпляр → сфера сетки → LOD → кластеры, упорядоченно префиксной
//    суммой; вызовы — vkCmdDrawIndexedIndirectCount, один на видимый кластер;
//    вершины земли — раз за кадр на все кольца (ground_bake.comp), их читают visibility buffer и разбор;
// 3) visibility buffer: кластеры и земля пишут номер треугольника и глубину;
// 4) плитки 8×8 по видам (light_tiles.comp, docs/tiles.md), освещение в compute по виду своим конвейером
//    (light_cells.comp, косвенный dispatch): surface() → light_surface() раз на ячейку, линейный цвет в HDR;
// 5) стиль (style.frag) — в картинку swapchain, поверх — отладочные отрезки и точки, потом интерфейс
#include "vk_graph.h"
#include "../core/log.h"

static RenderInstance g_mirror[RENDER_MAX_INSTANCES];  // что сейчас лежит в WorldGpu::inst
constexpr u32 MAX_COPIES = 64;
static VkBufferCopy g_copies[MAX_COPIES];  // изменившиеся куски экземпляров этого кадра
static u32 g_ncopies = 0;

bool vk_world_layout() {
    WorldGpu &w = g_vk.world;
    // 0, 1 — visibility buffer и HDR; 2 — поле расстояний мира для выборки светом (сэмплер неизменный: трилинейно,
    // по x и y с заворотом кольца, по z — по краю слоя), 3 — оно же для записи сборкой (docs/sdf.md)
    auto si = vk_info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!vk_check(vkCreateSampler(g_vk.device, &si, nullptr, &g_vk.sdf.sampler), "vkCreateSampler")) return false;
    VkDescriptorSetLayoutBinding b[4] = {};
    for (u32 i = 0; i < 4; ++i) {
        b[i].binding = i;
        b[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | (i < 2 ? VK_SHADER_STAGE_FRAGMENT_BIT : 0);
    }
    b[2].pImmutableSamplers = &g_vk.sdf.sampler;
    auto li = vk_info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount = 4;
    li.pBindings = b;
    if (!vk_check(vkCreateDescriptorSetLayout(g_vk.device, &li, nullptr, &w.set_layout), "vkCreateDescriptorSetLayout"))
        return false;
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    auto pi = vk_info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pi.maxSets = 1;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = ps;
    if (!vk_check(vkCreateDescriptorPool(g_vk.device, &pi, nullptr, &w.pool), "vkCreateDescriptorPool")) return false;
    auto ai = vk_info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = w.pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &w.set_layout;
    return vk_check(vkAllocateDescriptorSets(g_vk.device, &ai, &w.set), "vkAllocateDescriptorSets");
}

bool vk_world_init() {
    WorldGpu &w = g_vk.world;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkBufferUsageFlags ind = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (!vk_create_buffer(w.inst, RENDER_MAX_INSTANCES * sizeof(RenderInstance), ssbo | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true) ||
        !vk_create_buffer(w.cull, CULL_BYTES, ssbo | ind | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true) ||
        !vk_create_buffer(w.draws, VIS_MAX_CLUSTERS * sizeof(VkDrawIndexedIndirectCommand), ssbo | ind, true) ||
        !vk_create_buffer(w.vis, VIS_MAX_CLUSTERS * 8ull, ssbo, true) ||
        !vk_create_buffer(w.gverts, (VkDeviceSize)CLIP_LEVELS * (2 * CLIP_N + 1) * (2 * CLIP_N + 1) * 32, ssbo, true))
        return false;
    for (FrameSlot &fs : g_vk.frames)
        if (!vk_create_buffer(fs.inst_upload, RENDER_MAX_INSTANCES * sizeof(RenderInstance), VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
            return false;
    const char *names[] = {"мир: экземпляры", "мир: счёт отсечения", "мир: вызовы", "мир: видимые кластеры",
                           "мир: вершины земли"};
    GpuBuffer *bs[] = {&w.inst, &w.cull, &w.draws, &w.vis, &w.gverts};
    u8 *rs[] = {&w.rg_inst, &w.rg_cull, &w.rg_draws, &w.rg_vis, &w.rg_gverts};
    for (u32 i = 0; i < 5; ++i) {
        vk_buffer_name(*bs[i], names[i]);
        *rs[i] = rg_buffer(names[i]);
    }
    w.rg_id = rg_image("мир: номера треугольников", VK_IMAGE_ASPECT_COLOR_BIT);
    w.rg_hdr = rg_image("мир: HDR", VK_IMAGE_ASPECT_COLOR_BIT);
    w.rg_tiles = rg_buffer("мир: плитки по видам");
    w.inst_count = 0;
    PipeDesc vm{"vis_mesh.vert", "vis_mesh.frag"};
    vm.cull = VK_CULL_MODE_BACK_BIT;
    vm.color = VK_FORMAT_R32_UINT;
    PipeDesc vg{"vis_ground.vert", "vis_ground.frag"};
    vg.cull = VK_CULL_MODE_BACK_BIT;
    vg.color = VK_FORMAT_R32_UINT;
    PipeDesc st{"sky.vert", "style.frag"};
    st.depth_test = false;
    st.depth_write = false;
    w.pipe_vis_mesh = vk_make_pipeline(vm);
    w.pipe_vis_ground = vk_make_pipeline(vg);
    w.pipe_style = vk_make_pipeline(st);
    w.pipe_cull_count = vk_make_compute("cull_count.comp");
    w.pipe_cull_scan = vk_make_compute("cull_scan.comp");
    w.pipe_cull_write = vk_make_compute("cull_write.comp");
    w.pipe_ground_bake = vk_make_compute("ground_bake.comp");
    w.pipe_tiles = vk_make_compute("light_tiles.comp");
    // Освещение: конвейер на вид плиток, вид — константа специализации TILE_KIND
    bool lights = true;
    for (u32 k = 0; k < TILE_KINDS; ++k) {
        VkSpecializationMapEntry me{0, 0, sizeof(u32)};
        VkSpecializationInfo si{1, &me, sizeof(u32), &k};
        w.pipe_light[k] = vk_make_compute("light_cells.comp", &si);
        lights = lights && w.pipe_light[k];
    }
    return w.pipe_vis_mesh && w.pipe_vis_ground && w.pipe_style && w.pipe_cull_count && w.pipe_cull_scan &&
           w.pipe_cull_write && lights && w.pipe_tiles && w.pipe_ground_bake && vk_gcache_init() && vk_shadow_init() && vk_sdf_init();
}

static void destroy_image(GpuImage &im) {
    if (im.view) vkDestroyImageView(g_vk.device, im.view, nullptr);
    if (im.image) vmaDestroyImage(g_vk.vma, im.image, im.alloc);
    im = GpuImage{};
}

void vk_world_shutdown() {
    WorldGpu &w = g_vk.world;
    VkPipeline ps[] = {w.pipe_vis_mesh,  w.pipe_vis_ground, w.pipe_style, w.pipe_cull_count,
                       w.pipe_cull_scan, w.pipe_cull_write, w.pipe_tiles, w.pipe_ground_bake};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    for (VkPipeline p : w.pipe_light)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    destroy_image(w.id);
    destroy_image(w.hdr);
    vk_destroy_buffer(w.inst);
    vk_destroy_buffer(w.cull);
    vk_destroy_buffer(w.draws);
    vk_destroy_buffer(w.vis);
    vk_destroy_buffer(w.gverts);
    vk_destroy_buffer(w.tiles);
    for (FrameSlot &fs : g_vk.frames) vk_destroy_buffer(fs.inst_upload);
    vk_gcache_shutdown();
    vk_shadow_shutdown();
    vk_sdf_shutdown();
    if (w.pool) vkDestroyDescriptorPool(g_vk.device, w.pool, nullptr);
    if (w.set_layout) vkDestroyDescriptorSetLayout(g_vk.device, w.set_layout, nullptr);
    if (g_vk.sdf.sampler) vkDestroySampler(g_vk.device, g_vk.sdf.sampler, nullptr);
    g_vk.sdf.sampler = VK_NULL_HANDLE;
    w = WorldGpu{};
}

static bool create_image(GpuImage &im, VkFormat format, VkImageUsageFlags usage) {
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {g_vk.extent.width, g_vk.extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &im.image, &im.alloc, nullptr), "vmaCreateImage кадра мира")) return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &im.view), "vkCreateImageView кадра мира");
}

// Картинки кадра мира под размер swapchain; при смене размера — новые и новый набор для шейдеров
static bool ensure_images() {
    WorldGpu &w = g_vk.world;
    if (w.id.image && w.extent.width == g_vk.extent.width && w.extent.height == g_vk.extent.height) return true;
    vkDeviceWaitIdle(g_vk.device);
    destroy_image(w.id);
    destroy_image(w.hdr);
    if (!create_image(w.id, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT) ||
        !create_image(w.hdr, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT))
        return false;
    w.extent = g_vk.extent;
    vk_image_name(w.id.image, "мир: номера треугольников");
    vk_image_name(w.hdr.image, "мир: HDR");
    // Плитки по видам: на вид — косвенный dispatch и список на все плитки кадра
    vk_destroy_buffer(w.tiles);
    w.tile_cap = ((w.extent.width + 7) / 8) * ((w.extent.height + 7) / 8);
    if (!vk_create_buffer(w.tiles, TILE_HEADER + (VkDeviceSize)TILE_KINDS * w.tile_cap * 4,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          true))
        return false;
    vk_buffer_name(w.tiles, "мир: плитки по видам");
    rg_reset(w.rg_tiles);
    rg_bind_image(w.rg_id, w.id.image);
    rg_bind_image(w.rg_hdr, w.hdr.image);
    VkDescriptorImageInfo ii[2] = {{VK_NULL_HANDLE, w.id.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, w.hdr.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[2] = {};
    for (u32 i = 0; i < 2; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = w.set;
        wr[i].dstBinding = i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(g_vk.device, 2, wr, 0, nullptr);
    return true;
}

void vk_world_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs) {
    WorldGpu &w = g_vk.world;
    g.meshes = g_vk.mesh_table.address;
    g.clusters = g_vk.clusters.address;
    g.vis_instances = w.inst.address;
    g.vis = w.vis.address;
    g.cull = w.cull.address;
    g.draws = w.draws.address;
    g.indices = g_vk.indices.address;
    g.vis_max = VIS_MAX_CLUSTERS;
    g.g_verts = w.gverts.address;
    g_ncopies = 0;
    // Изменившиеся экземпляры — кусками подряд в буфер слота и копией на GPU; кусков больше MAX_COPIES — одним
    // куском от первого изменившегося до последнего
    u32 n = f.instances.count < RENDER_MAX_INSTANCES ? (u32)f.instances.count : RENDER_MAX_INSTANCES;
    RenderInstance *up = (RenderInstance *)fs.inst_upload.mapped;
    u32 first_dirty = n, last_dirty = 0;
    // Экземпляр, каким его увидит GPU: lod_min ставит рендер (у людей — LOD, вершины которого скинены, vk_skin.cpp)
    auto eff = [&f](u32 i) {
        RenderInstance e = f.instances.data[i];
        e.lod_min = g_vk.skin.lod_min[i];
        return e;
    };
    for (u32 i = 0; i < n;) {
        RenderInstance e = eff(i);
        if (i < w.inst_count && !memcmp(&g_mirror[i], &e, sizeof(RenderInstance))) { ++i; continue; }
        u32 j = i;
        while (j < n) {
            e = eff(j);
            if (j < w.inst_count && !memcmp(&g_mirror[j], &e, sizeof(RenderInstance))) break;
            g_mirror[j] = up[j] = e;
            ++j;
        }
        if (g_ncopies < MAX_COPIES) {
            VkDeviceSize off = (VkDeviceSize)i * sizeof(RenderInstance);
            g_copies[g_ncopies++] = {off, off, (VkDeviceSize)(j - i) * sizeof(RenderInstance)};
        }
        first_dirty = i < first_dirty ? i : first_dirty;
        last_dirty = j;
        i = j;
    }
    if (g_ncopies == MAX_COPIES) {
        // Дыры между кусками тоже копируются: в буфере слота там должны лежать те же экземпляры
        for (u32 i = first_dirty; i < last_dirty; ++i) up[i] = g_mirror[i];
        VkDeviceSize off = (VkDeviceSize)first_dirty * sizeof(RenderInstance);
        g_copies[0] = {off, off, (VkDeviceSize)(last_dirty - first_dirty) * sizeof(RenderInstance)};
        g_ncopies = 1;
    }
    w.inst_count = n;
    g.instance_count = n;
    if (g_ncopies) vmaFlushAllocation(g_vk.vma, fs.inst_upload.alloc, 0, VK_WHOLE_SIZE);
    bool images = ensure_images();
    if (!images) g.instance_count = 0;
    g.tiles = w.tiles.address;
    g.tile_cap = w.tile_cap;
    vk_gcache_fill(g, f, images);
    vk_shadow_fill(g, f, fs, images);
    vk_sdf_fill(g, f, fs, images);
}

static void dispatch(VkCommandBuffer cmd, VkPipeline p, VkDeviceAddress frame, u32 gx, u32 gy) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    PushConsts pc{frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, gx, gy, 1);
}

// Кадр мира в графе: что нужно записи проходов
struct WorldFrame {
    VkDeviceAddress frame;
    FrameSlot *fs;
    u32 img, npoints, nlines;
    bool ground;
};
static WorldFrame g_wf;

static void record_inst_upload(VkCommandBuffer cmd, void *) {
    vkCmdCopyBuffer(cmd, g_wf.fs->inst_upload.buffer, g_vk.world.inst.buffer, g_ncopies, g_copies);
}

// Вершины земли кадра: все кольца, по ряду групп на кольцо
static void record_ground_bake(VkCommandBuffer cmd, void *) {
    const u32 row = 2 * CLIP_N + 1;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.world.pipe_ground_bake);
    PushConsts pg{g_wf.frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pg, &pg);
    vkCmdDispatch(cmd, (row * row + 63) / 64, CLIP_LEVELS, 1);
}

static u32 cull_groups() { return (g_vk.world.inst_count + 63) / 64; }
static void record_cull_count(VkCommandBuffer cmd, void *) {
    if (cull_groups()) dispatch(cmd, g_vk.world.pipe_cull_count, g_wf.frame, cull_groups(), 1);
}
static void record_cull_scan(VkCommandBuffer cmd, void *) { dispatch(cmd, g_vk.world.pipe_cull_scan, g_wf.frame, 1, 1); }
static void record_cull_write(VkCommandBuffer cmd, void *) {
    if (cull_groups()) dispatch(cmd, g_vk.world.pipe_cull_write, g_wf.frame, cull_groups(), 1);
}

static VkRenderingAttachmentInfo depth_attachment(VkAttachmentLoadOp load, VkAttachmentStoreOp store) {
    auto da = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    da.imageView = g_vk.depth.view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    da.loadOp = load;
    da.storeOp = store;
    da.clearValue.depthStencil = {0.0f, 0};
    return da;
}

static void begin_rendering(VkCommandBuffer cmd, const VkRenderingAttachmentInfo &ca, const VkRenderingAttachmentInfo &da) {
    auto ri = vk_info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = {{0, 0}, g_vk.extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    ri.pDepthAttachment = &da;
    vkCmdBeginRendering(cmd, &ri);
    VkViewport vp{0, 0, (f32)g_vk.extent.width, (f32)g_vk.extent.height, 0, 1};
    VkRect2D sc{{0, 0}, g_vk.extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

// Visibility buffer: номер треугольника и глубина
static void record_visibility(VkCommandBuffer cmd, void *) {
    const WorldGpu &w = g_vk.world;
    auto ca = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    ca.imageView = w.id.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color.uint32[0] = 0;  // пусто — небо
    // Глубина сохраняется: по ней потом рисуются отладочные отрезки и точки
    begin_rendering(cmd, ca, depth_attachment(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE));
    PushConsts pc{g_wf.frame, 0, 0};
    if (w.inst_count) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w.pipe_vis_mesh);
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdBindIndexBuffer(cmd, g_vk.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirectCount(cmd, w.draws.buffer, 0, w.cull.buffer, 0, VIS_MAX_CLUSTERS,
                                      sizeof(VkDrawIndexedIndirectCommand));
    }
    if (g_wf.ground) {
        const TerrainGpu &tg = g_vk.terrain;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w.pipe_vis_ground);
        vkCmdBindIndexBuffer(cmd, tg.index.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (i32 L = 0; L < CLIP_LEVELS; ++L) {
            PushConsts pl{g_wf.frame, (u32)L, 0};
            vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pl, &pl);
            u32 first = L == 0 ? tg.first_full : tg.first_ring[tg.ring[L]];
            u32 count = L == 0 ? tg.count_full : tg.count_ring;
            vkCmdDrawIndexed(cmd, count, 1, first, 0, 0);
        }
    }
    vkCmdEndRendering(cmd);
}

// Плитки по видам: косвенные dispatch с нуля (x = 0 плиток, y = z = 1), потом разбор
static void record_tiles_clear(VkCommandBuffer cmd, void *) {
    u32 head[TILE_KINDS * 4];
    for (u32 k = 0; k < TILE_KINDS; ++k) {
        head[k * 4] = 0;
        head[k * 4 + 1] = head[k * 4 + 2] = 1;
        head[k * 4 + 3] = 0;
    }
    vkCmdUpdateBuffer(cmd, g_vk.world.tiles.buffer, 0, sizeof head, head);
}

static void record_tiles(VkCommandBuffer cmd, void *) {
    const WorldGpu &w = g_vk.world;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &w.set, 0, nullptr);
    dispatch(cmd, w.pipe_tiles, g_wf.frame, (g_vk.extent.width + 7) / 8, (g_vk.extent.height + 7) / 8);
}

// Освещение плиток одного вида раз на ячейку → HDR: группа на плитку, число групп пишет разбор плиток
static void record_light(VkCommandBuffer cmd, void *user) {
    const WorldGpu &w = g_vk.world;
    u32 kind = (u32)(uintptr_t)user;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, w.pipe_light[kind]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &w.set, 0, nullptr);
    PushConsts pc{g_wf.frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatchIndirect(cmd, w.tiles.buffer, (VkDeviceSize)kind * 16);
}

// Стиль в картинку swapchain, поверх — отладочные отрезки и точки по глубине кадра мира
static void record_style(VkCommandBuffer cmd, void *) {
    const WorldGpu &w = g_vk.world;
    auto sa = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    sa.imageView = g_vk.views[g_wf.img];
    sa.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sa.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // стиль закрывает весь кадр
    sa.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    begin_rendering(cmd, sa, depth_attachment(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE));
    PushConsts pc{g_wf.frame, 0, 0};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w.pipe_style);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.layout, 0, 1, &w.set, 0, nullptr);
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    if (g_wf.nlines) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_lines);
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, g_wf.nlines, 1, 0, 0);
    }
    if (g_wf.npoints) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_points);
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, 6, g_wf.npoints, 0, 0);
    }
    vkCmdEndRendering(cmd);
}

bool vk_world_passes(FrameSlot &fs, const RenderFrame &f, u32 img, u32 npoints, u32 nlines) {
    const WorldGpu &w = g_vk.world;
    if (!w.id.image) return false;
    g_wf = WorldFrame{fs.frame.address, &fs, img, npoints, nlines, g_vk.terrain.on && f.ground};
    // 1. Люди (вершины и сферы кластеров), изменившиеся экземпляры и вершины земли кадра
    vk_skin_passes(&fs.frame.address);
    if (g_ncopies) {
        rg_pass("мир: экземпляры", "земля", record_inst_upload, nullptr);
        rg_use(w.rg_inst, RG_COPY_WRITE);
    }
    if (g_wf.ground) {
        rg_pass("земля: вершины", "земля", record_ground_bake, nullptr);
        rg_use(w.rg_gverts, RG_CS_WRITE);
    }
    // 2. Отсечение: счёт, префиксная сумма, запись вызовов
    rg_pass("отсечение: счёт", "отсечение", record_cull_count, nullptr);
    rg_use(w.rg_inst, RG_CS_READ);
    rg_use(w.rg_cull, RG_CS_WRITE);
    vk_skin_clusters_read();
    rg_pass("отсечение: сумма", "отсечение", record_cull_scan, nullptr);
    rg_use(w.rg_cull, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    rg_pass("отсечение: вызовы", "отсечение", record_cull_write, nullptr);
    rg_use(w.rg_inst, RG_CS_READ);
    rg_use(w.rg_cull, RG_CS_READ);
    rg_use(w.rg_draws, RG_CS_WRITE);
    rg_use(w.rg_vis, RG_CS_WRITE);
    vk_skin_clusters_read();
    // 3. Visibility buffer
    rg_pass("visibility", "visibility", record_visibility, nullptr);
    rg_use(w.rg_id, RG_COLOR, RG_DISCARD);
    rg_use(g_vk.rg_depth, RG_DEPTH, RG_DISCARD);
    rg_use(w.rg_draws, RG_INDIRECT);
    rg_use(w.rg_cull, RG_INDIRECT);
    rg_use(w.rg_vis, RG_VS_READ);
    rg_use(w.rg_inst, RG_VS_READ);
    rg_use(w.rg_gverts, RG_VS_READ);
    vk_skin_vertices_read(RG_VS_READ);
    // Поле расстояний мира (docs/sdf.md): засыпка и сборка грязных кирпичей, экземпляры уже на GPU
    vk_sdf_passes();
    // 4. Плитки по видам и освещение: вид — свой конвейер и своя строка F3. Виды пишут разные пиксели HDR, но
    // барьер между ними граф ставит (запись после записи): без него слой проверки синхронизации видел бы гонку,
    // а цена — хвост одного dispatch, пока следующий не начался (docs/tiles.md)
    // Разбор плиток заодно пишет пикселям земли их тексели кеша и заявляет недостающие страницы; страницы
    // запекаются до освещения (docs/gcache.md)
    vk_gcache_begin(g_wf.frame);
    vk_shadow_begin(g_wf.frame);
    rg_pass("плитки: обнулить", "плитки", record_tiles_clear, nullptr);
    rg_use(w.rg_tiles, RG_COPY_WRITE);
    rg_pass("плитки: разбор", "плитки", record_tiles, nullptr);
    rg_use(w.rg_id, RG_IMG_CS_READ);
    rg_use(w.rg_tiles, RG_CS_WRITE);
    rg_use(w.rg_gverts, RG_CS_READ);
    rg_use(w.rg_vis, RG_CS_READ);  // точки пикселей сеток — заявки страниц теней
    rg_use(w.rg_inst, RG_CS_READ);
    vk_skin_vertices_read(RG_CS_READ);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_gcache_tiles_uses();
    vk_shadow_tiles_uses();
    vk_gcache_bake_passes();
    vk_shadow_passes();
    static const char *const light_names[TILE_KINDS] = {"освещение: небо", "освещение: земля", "освещение: сетки",
                                                        "освещение: смесь", "освещение: земля на пиксель"};
    for (u32 k = 0; k < TILE_KINDS; ++k) {
        rg_pass(light_names[k], light_names[k], record_light, (void *)(uintptr_t)k);
        rg_use(w.rg_tiles, RG_INDIRECT);
        // Земля отдаёт плитки, текселей которых нет в кеше, виду «земля на пиксель» (дописывает его список)
        rg_use(w.rg_tiles, k == TILE_KIND_GROUND ? RG_CS_WRITE : RG_CS_READ);
        rg_use(w.rg_id, RG_IMG_CS_READ);
        rg_use(w.rg_hdr, RG_IMG_CS_WRITE, k == 0 ? RG_DISCARD : 0);
        rg_use(w.rg_vis, RG_CS_READ);
        rg_use(w.rg_inst, RG_CS_READ);
        rg_use(w.rg_gverts, RG_CS_READ);
        rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
        vk_skin_vertices_read(RG_CS_READ);
        vk_sky_reads(RG_CS_READ);
        vk_froxel_reads(RG_CS_READ);
        vk_gcache_reads();
        vk_horizon_reads(RG_CS_READ);
        vk_shadow_reads();
        vk_sdf_reads();
    }
    // 5. Стиль, отрезки, точки
    rg_pass("стиль", "стиль", record_style, nullptr);
    rg_use(g_vk.rg_swap, RG_COLOR, RG_DISCARD);
    rg_use(w.rg_hdr, RG_IMG_FS_READ);
    rg_use(g_vk.rg_depth, RG_DEPTH);
    vk_sky_reads(RG_FS_READ);
    return true;
}

u32 render_vis_clusters() { return g_vk.world.vis_clusters; }
