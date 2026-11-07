// Кадр мира (этап 5, docs/frame.md «Кадр мира», docs/render.md «Проходы с этапа 5»):
// 1) экземпляры лежат на GPU постоянно, CPU копирует только изменившиеся;
// 2) отсечение и LOD в compute (cull_*.comp): экземпляр → сфера сетки → LOD → кластеры, упорядоченно префиксной
//    суммой; вызовы — vkCmdDrawIndexedIndirectCount, один на видимый кластер;
// 3) visibility buffer: кластеры и земля пишут номер треугольника и глубину;
// 4) освещение в compute (light_cells.comp): surface() → light_surface() раз на ячейку, линейный цвет в HDR;
// 5) стиль (style.frag) — в картинку swapchain, поверх — отладочные отрезки и точки, потом интерфейс
#include "vk_state.h"
#include "../core/log.h"

static RenderInstance g_mirror[RENDER_MAX_INSTANCES];  // что сейчас лежит в WorldGpu::inst
constexpr u32 MAX_COPIES = 64;
static VkBufferCopy g_copies[MAX_COPIES];  // изменившиеся куски экземпляров этого кадра
static u32 g_ncopies = 0;

bool vk_world_layout() {
    WorldGpu &w = g_vk.world;
    VkDescriptorSetLayoutBinding b[2] = {};
    for (u32 i = 0; i < 2; ++i) {
        b[i].binding = i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    auto li = vk_info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount = 2;
    li.pBindings = b;
    if (!vk_check(vkCreateDescriptorSetLayout(g_vk.device, &li, nullptr, &w.set_layout), "vkCreateDescriptorSetLayout"))
        return false;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};
    auto pi = vk_info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
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
        !vk_create_buffer(w.vis, VIS_MAX_CLUSTERS * 8ull, ssbo, true))
        return false;
    for (FrameSlot &fs : g_vk.frames)
        if (!vk_create_buffer(fs.inst_upload, RENDER_MAX_INSTANCES * sizeof(RenderInstance), VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
            !vk_create_buffer(fs.readback, 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT))
            return false;
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
    w.pipe_light = vk_make_compute("light_cells.comp");
    return w.pipe_vis_mesh && w.pipe_vis_ground && w.pipe_style && w.pipe_cull_count && w.pipe_cull_scan &&
           w.pipe_cull_write && w.pipe_light;
}

static void destroy_image(GpuImage &im) {
    if (im.view) vkDestroyImageView(g_vk.device, im.view, nullptr);
    if (im.image) vmaDestroyImage(g_vk.vma, im.image, im.alloc);
    im = GpuImage{};
}

void vk_world_shutdown() {
    WorldGpu &w = g_vk.world;
    VkPipeline ps[] = {w.pipe_vis_mesh, w.pipe_vis_ground, w.pipe_style, w.pipe_cull_count,
                       w.pipe_cull_scan, w.pipe_cull_write, w.pipe_light};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    destroy_image(w.id);
    destroy_image(w.hdr);
    vk_destroy_buffer(w.inst);
    vk_destroy_buffer(w.cull);
    vk_destroy_buffer(w.draws);
    vk_destroy_buffer(w.vis);
    for (FrameSlot &fs : g_vk.frames) {
        vk_destroy_buffer(fs.inst_upload);
        vk_destroy_buffer(fs.readback);
    }
    if (w.pool) vkDestroyDescriptorPool(g_vk.device, w.pool, nullptr);
    if (w.set_layout) vkDestroyDescriptorSetLayout(g_vk.device, w.set_layout, nullptr);
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

void vk_world_read(FrameSlot &fs) {
    if (!fs.world) return;
    vmaInvalidateAllocation(g_vk.vma, fs.readback.alloc, 0, VK_WHOLE_SIZE);
    g_vk.world.vis_clusters = *(const u32 *)fs.readback.mapped;
    fs.world = false;
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
    g_ncopies = 0;
    if (f.style.forward) {
        g.instance_count = 0;
        return;
    }
    // Изменившиеся экземпляры — кусками подряд в буфер слота и копией на GPU; кусков больше MAX_COPIES — одним
    // куском от первого изменившегося до последнего
    u32 n = f.instances.count < RENDER_MAX_INSTANCES ? (u32)f.instances.count : RENDER_MAX_INSTANCES;
    RenderInstance *up = (RenderInstance *)fs.inst_upload.mapped;
    u32 first_dirty = n, last_dirty = 0;
    for (u32 i = 0; i < n;) {
        if (i < w.inst_count && !memcmp(&g_mirror[i], &f.instances.data[i], sizeof(RenderInstance))) { ++i; continue; }
        u32 j = i;
        while (j < n && (j >= w.inst_count || memcmp(&g_mirror[j], &f.instances.data[j], sizeof(RenderInstance)))) {
            g_mirror[j] = up[j] = f.instances.data[j];
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
    if (!ensure_images()) g.instance_count = 0;
}

static void dispatch(VkCommandBuffer cmd, VkPipeline p, VkDeviceAddress frame, u32 gx, u32 gy) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    PushConsts pc{frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, gx, gy, 1);
}

bool vk_world_record(VkCommandBuffer cmd, FrameSlot &fs, const RenderFrame &f, u32 img, u32 npoints, u32 nlines) {
    WorldGpu &w = g_vk.world;
    if (!w.id.image) return false;
    const VkPipelineStageFlags2 CS = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    const VkAccessFlags2 R = VK_ACCESS_2_SHADER_STORAGE_READ_BIT, W = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    const VkDeviceAddress frame = fs.frame.address;
    u32 ninst = w.inst_count;

    // Прошлый кадр мог ещё читать буферы отсечения и картинки: всё прошлое — до записи этого кадра
    vk_memory_barrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    if (g_ncopies) {
        vkCmdCopyBuffer(cmd, fs.inst_upload.buffer, w.inst.buffer, g_ncopies, g_copies);
        vk_memory_barrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          CS | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, R);
    }
    // 1. Отсечение: счёт, префиксная сумма, запись вызовов
    u32 groups = (ninst + 63) / 64;
    if (groups) dispatch(cmd, w.pipe_cull_count, frame, groups, 1);
    vk_memory_barrier(cmd, CS, W, CS, R | W);
    dispatch(cmd, w.pipe_cull_scan, frame, 1, 1);
    vk_memory_barrier(cmd, CS, W, CS, R | W);
    if (groups) dispatch(cmd, w.pipe_cull_write, frame, groups, 1);
    vk_memory_barrier(cmd, CS, W,
                      VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | CS |
                          VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | R | VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferCopy rb{0, 0, 4};
    vkCmdCopyBuffer(cmd, w.cull.buffer, fs.readback.buffer, 1, &rb);
    fs.world = true;
    vk_mark(cmd, "отсечение");

    // 2. Visibility buffer: номер треугольника и глубина
    vk_image_barrier(cmd, w.id.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    auto ca = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    ca.imageView = w.id.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color.uint32[0] = 0;  // пусто — небо
    auto da = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    da.imageView = g_vk.depth.view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // по ней потом рисуются отладочные отрезки и точки
    da.clearValue.depthStencil = {0.0f, 0};
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
    PushConsts pc{frame, 0, 0};
    if (ninst) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w.pipe_vis_mesh);
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdBindIndexBuffer(cmd, g_vk.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirectCount(cmd, w.draws.buffer, 0, w.cull.buffer, 0, VIS_MAX_CLUSTERS,
                                      sizeof(VkDrawIndexedIndirectCommand));
    }
    const TerrainGpu &tg = g_vk.terrain;
    if (tg.on && f.ground) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w.pipe_vis_ground);
        vkCmdBindIndexBuffer(cmd, tg.index.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (i32 L = 0; L < CLIP_LEVELS; ++L) {
            PushConsts pl{frame, (u32)L, 0};
            vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pl, &pl);
            u32 first = L == 0 ? tg.first_full : tg.first_ring[tg.ring[L]];
            u32 count = L == 0 ? tg.count_full : tg.count_ring;
            vkCmdDrawIndexed(cmd, count, 1, first, 0, 0);
        }
    }
    vkCmdEndRendering(cmd);
    vk_mark(cmd, "visibility");

    // 3. Освещение раз на ячейку → HDR
    vk_image_barrier(cmd, w.id.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, CS, R);
    vk_image_barrier(cmd, w.hdr.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, CS, W);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &w.set, 0, nullptr);
    dispatch(cmd, w.pipe_light, frame, (g_vk.extent.width + 7) / 8, (g_vk.extent.height + 7) / 8);
    vk_image_barrier(cmd, w.hdr.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, CS, W,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, R);
    vk_mark(cmd, "освещение");

    // 4. Стиль в картинку swapchain, поверх — отладочные отрезки и точки по глубине кадра мира
    vk_image_barrier(cmd, g_vk.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    auto sa = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    sa.imageView = g_vk.views[img];
    sa.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sa.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // стиль закрывает весь кадр
    sa.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ri.pColorAttachments = &sa;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, w.pipe_style);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.layout, 0, 1, &w.set, 0, nullptr);
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    if (nlines) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_lines);
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, nlines, 1, 0, 0);
    }
    if (npoints) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_points);
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, 6, npoints, 0, 0);
    }
    vkCmdEndRendering(cmd);
    vk_mark(cmd, "стиль");
    return true;
}

u32 render_vis_clusters() { return g_vk.world.vis_clusters; }
