// Конвейеры и кадр forward (этапы 1–4): сетки экземплярами по ключу порядка, земля, отладочные отрезки и точки.
// Обратная глубина D32F до бесконечности, координаты относительно камеры. Вершины шейдер читает сам по
// адресу буфера (без входа вершин конвейера), как потом visibility buffer (docs/frame.md)
#include "vk_state.h"
#include "../asset/mesh.h"
#include "../core/log.h"
#include "../core/sort.h"
#include "../platform/platform.h"
#include <stdio.h>

// Раскладка — как layout(push_constant) в data/shaders/common.glsl
struct PushConsts {
    VkDeviceAddress frame;
    u32 a, b;
};

static RenderDraw g_sort_tmp[RENDER_MAX_INSTANCES];

static VkShaderModule load_shader(const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s%s.spv", g_vk.cfg.shader_dir, name);
    // Загрузка при старте: временная память, не игровой цикл
    usize cap = 1u << 20;
    void *mem = platform_alloc(cap);
    Arena a;
    arena_init(a, mem, cap, "shader");
    u8 *data = nullptr;
    usize size = 0;
    VkShaderModule m = VK_NULL_HANDLE;
    if (!platform_read_file(path, a, &data, &size) || size % 4 != 0) {
        log_error("нет шейдера %s (собери: make)", path);
    } else {
        auto ci = vk_info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        ci.codeSize = size;
        ci.pCode = (const u32 *)data;
        vk_check(vkCreateShaderModule(g_vk.device, &ci, nullptr, &m), path);
    }
    platform_free(mem);
    return m;
}

struct PipeDesc {
    const char *vs, *fs;
    VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool blend = false;
    bool depth_write = true;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
};

static VkPipeline make_pipeline(const PipeDesc &d) {
    VkShaderModule vs = load_shader(d.vs);
    VkShaderModule fs = load_shader(d.fs);
    VkPipeline p = VK_NULL_HANDLE;
    if (vs && fs) {
        VkPipelineShaderStageCreateInfo st[2] = {};
        st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        st[0].module = vs;
        st[0].pName = "main";
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        st[1].module = fs;
        st[1].pName = "main";
        auto vi = vk_info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        auto ia = vk_info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        ia.topology = d.topo;
        auto vp = vk_info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        auto rs = vk_info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = d.cull;
        // Грани .mesh — против часовой стрелки снаружи в осях мира (x восток, y вверх, z север). Вид смотрит
        // с правым = вверх × вперёд (восток справа при взгляде на север), y в NDC вниз — на экране они по часовой
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1.0f;
        auto ms = vk_info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        // Обратная глубина: ближе — больше, очистка нулём
        auto dss = vk_info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        dss.depthTestEnable = VK_TRUE;
        dss.depthWriteEnable = d.depth_write ? VK_TRUE : VK_FALSE;
        dss.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                            VK_COLOR_COMPONENT_A_BIT;
        if (d.blend) {
            ba.blendEnable = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        auto cb = vk_info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        cb.attachmentCount = 1;
        cb.pAttachments = &ba;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        auto ds = vk_info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dyn;
        auto ri = vk_info<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &g_vk.format;
        ri.depthAttachmentFormat = DEPTH_FORMAT;
        auto pi = vk_info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        pi.pNext = &ri;
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &dss;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &ds;
        pi.layout = g_vk.layout;
        vk_check(vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p), d.vs);
    }
    if (vs) vkDestroyShaderModule(g_vk.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(g_vk.device, fs, nullptr);
    return p;
}

bool vk_create_pipelines() {
    VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts)};
    auto li = vk_info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (!vk_check(vkCreatePipelineLayout(g_vk.device, &li, nullptr, &g_vk.layout), "vkCreatePipelineLayout")) return false;
    PipeDesc mesh{"mesh.vert", "mesh.frag"};
    mesh.cull = VK_CULL_MODE_BACK_BIT;
    PipeDesc ground{"ground.vert", "ground.frag"};
    ground.cull = VK_CULL_MODE_BACK_BIT;
    PipeDesc points{"points.vert", "color.frag"};
    points.blend = true;
    points.depth_write = false;
    PipeDesc lines{"lines.vert", "color.frag", VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, false};
    g_vk.pipe_mesh = make_pipeline(mesh);
    g_vk.pipe_ground = make_pipeline(ground);
    g_vk.pipe_points = make_pipeline(points);
    g_vk.pipe_lines = make_pipeline(lines);
    return g_vk.pipe_mesh && g_vk.pipe_ground && g_vk.pipe_points && g_vk.pipe_lines;
}

void vk_destroy_pipelines() {
    VkPipeline ps[] = {g_vk.pipe_mesh, g_vk.pipe_ground, g_vk.pipe_points, g_vk.pipe_lines};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    if (g_vk.layout) vkDestroyPipelineLayout(g_vk.device, g_vk.layout, nullptr);
    g_vk.pipe_mesh = g_vk.pipe_ground = g_vk.pipe_points = g_vk.pipe_lines = VK_NULL_HANDLE;
    g_vk.layout = VK_NULL_HANDLE;
}

static void image_barrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                          VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                          VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    auto b = vk_info<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

static f32 srgb_to_linear(f32 c) {
    // Цвет очистки задаётся в sRGB, а картинка swapchain — *_SRGB: в буфер идёт линейный.
    // Не шаг мира, но и libm не нужна: степень 2.1875 = 2 + 1/8 + 1/16 вместо 2.2 через корни
    f32 r8 = __builtin_sqrtf(__builtin_sqrtf(__builtin_sqrtf(c)));
    return c * c * r8 * __builtin_sqrtf(r8);
}

// Метки времени прошлого использования слота: время прохода forward на GPU
static void read_gpu_time(FrameSlot &fs, u32 slot) {
    if (!fs.timed || !g_vk.queries) return;
    u64 t[2] = {};
    VkResult r = vkGetQueryPoolResults(g_vk.device, g_vk.queries, slot * 2, 2, sizeof t, t, sizeof(u64),
                                       VK_QUERY_RESULT_64_BIT);
    if (r == VK_SUCCESS && t[1] >= t[0]) g_vk.gpu_ms = (f64)(t[1] - t[0]) * (f64)g_vk.timestamp_period / 1e6;
    fs.timed = false;
}

static void fill_frame(FrameGpu &g, const RenderFrame &f, const FrameSlot &fs) {
    g = FrameGpu{};
    g.vertices = g_vk.vertices.address;
    g.instances = fs.instances.address;
    g.materials = g_vk.materials.address;
    g.lines = fs.lines.address;
    g.points = fs.points.address;
    const RenderView &v = f.view;
    g.cam_cx = v.cx;
    g.cam_cz = v.cz;
    g.cam_x = v.x;
    g.cam_y = v.y;
    g.cam_z = v.z;
    g.tan_half_fov = v.tan_half_fov;
    for (u32 i = 0; i < 3; ++i) {
        g.right[i] = v.right[i];
        g.up[i] = v.up[i];
        g.forward[i] = v.forward[i];
        g.sun_dir[i] = f.light.sun_dir[i];
        g.sun_color[i] = f.light.sun_color[i];
        g.sky_color[i] = f.light.sky_color[i];
    }
    f32 w = (f32)g_vk.extent.width, h = (f32)g_vk.extent.height;
    f32 aspect = h > 0 ? w / h : 1.0f;
    g.proj_y = 1.0f / v.tan_half_fov;
    g.proj_x = g.proj_y / aspect;
    g.near_m = v.near_m;
    g.pixel_angle = 2.0f * v.tan_half_fov / (h > 0 ? h : 1.0f);
    g.cell_min = f.style.cell_min;
    g.cell_pixels = f.style.cell_pixels;
    g.pixel = f.style.pixel ? 1u : 0u;
    g.viewport[0] = w;
    g.viewport[1] = h;
    g.ndc_per_pixel[0] = 2.0f / (w > 0 ? w : 1.0f);
    g.ndc_per_pixel[1] = 2.0f / (h > 0 ? h : 1.0f);
    vk_terrain_fill(g, f);
}

// Копия картинки кадра в буфер снимка (до перевода в PRESENT)
static void record_capture(VkCommandBuffer cmd, VkImage img) {
    VkDeviceSize bytes = (VkDeviceSize)g_vk.extent.width * g_vk.extent.height * 4;
    if (g_vk.capture.size < bytes) {
        vk_destroy_buffer(g_vk.capture);
        if (!vk_create_buffer(g_vk.capture, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) return;
    }
    image_barrier(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {g_vk.extent.width, g_vk.extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_vk.capture.buffer, 1, &c);
    image_barrier(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    g_vk.capture_extent = g_vk.extent;
    g_vk.capture_ready = true;
}

bool render_frame(const RenderFrame &f) {
    if (g_vk.swapchain_dirty) {
        i32 w = 0, h = 0;
        platform_window_size(&w, &h);
        if (w == 0 || h == 0) return true;  // окно свёрнуто
        vkDeviceWaitIdle(g_vk.device);
        if (!vk_create_swapchain()) return false;
    }
    u32 slot = g_vk.frame_index;
    FrameSlot &fs = g_vk.frames[slot];
    g_vk.wait_ns = 0;
    u64 t0 = platform_time_ns();
    if (!vk_check(vkWaitForFences(g_vk.device, 1, &fs.done, VK_TRUE, UINT64_MAX), "vkWaitForFences")) return false;
    read_gpu_time(fs, slot);
    vk_geometry_collect();

    u32 img = 0;
    VkResult r = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX, fs.acquired, VK_NULL_HANDLE, &img);
    g_vk.wait_ns += platform_time_ns() - t0;
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { g_vk.swapchain_dirty = true; return true; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return vk_check(r, "vkAcquireNextImageKHR");
    vkResetFences(g_vk.device, 1, &fs.done);

    // Вызовы по ключу порядка; экземпляры пишутся в буфер кадра в том же порядке, подряд для одной сетки
    usize ndraws = f.draws.count < RENDER_MAX_INSTANCES ? f.draws.count : RENDER_MAX_INSTANCES;
    radix_sort_u64(f.draws.data, g_sort_tmp, ndraws, [](const RenderDraw &d) { return d.key; });
    RenderInstance *inst = (RenderInstance *)fs.instances.mapped;
    u32 ninst = 0;
    for (usize i = 0; i < ndraws; ++i) {
        u32 k = f.draws.data[i].instance;
        if (k < f.instances.count) inst[ninst++] = f.instances.data[k];
        else f.draws.data[i].key = ~0ull;  // плохой номер — вызов пропускается
    }
    FrameGpu &g = *(FrameGpu *)fs.frame.mapped;
    fill_frame(g, f, fs);

    u32 npoints = f.points.count < RENDER_MAX_POINTS ? (u32)f.points.count : RENDER_MAX_POINTS;
    u32 nlines = f.lines.count < RENDER_MAX_LINE_VERTICES ? (u32)f.lines.count & ~1u : RENDER_MAX_LINE_VERTICES;
    if (npoints) memcpy(fs.points.mapped, f.points.data, npoints * sizeof(RenderPoint));
    if (nlines) memcpy(fs.lines.mapped, f.lines.data, nlines * sizeof(RenderLineVertex));

    vkResetCommandPool(g_vk.device, fs.pool, 0);
    VkCommandBuffer cmd = fs.cmd;
    auto bi = vk_info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    if (g_vk.queries) {
        vkCmdResetQueryPool(cmd, g_vk.queries, slot * 2, 2);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, g_vk.queries, slot * 2);
    }

    image_barrier(cmd, g_vk.images[img], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    // Глубина прошлого кадра не нужна: из UNDEFINED, очистка нулём (обратная глубина: 0 — бесконечность)
    image_barrier(cmd, g_vk.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    auto ca = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    ca.imageView = g_vk.views[img];
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color = {{srgb_to_linear(f.clear[0]), srgb_to_linear(f.clear[1]), srgb_to_linear(f.clear[2]), 1.0f}};
    auto da = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    da.imageView = g_vk.depth.view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
    PushConsts pc{fs.frame.address, 0, 0};
    const VkShaderStageFlags pcs = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Сетки: подряд идущие вызовы одной сетки — один вызов с экземплярами
    if (ndraws) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_mesh);
        vkCmdPushConstants(cmd, g_vk.layout, pcs, 0, sizeof pc, &pc);
        vkCmdBindIndexBuffer(cmd, g_vk.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        u32 first = 0;
        for (usize i = 0; i < ndraws;) {
            u64 key = f.draws.data[i].key;
            usize j = i + 1;
            while (j < ndraws && render_key_mesh(f.draws.data[j].key) == render_key_mesh(key) &&
                   render_key_pipe(f.draws.data[j].key) == render_key_pipe(key))
                j++;
            u32 n = (u32)(j - i);
            u32 mesh = render_key_mesh(key);
            if (key != ~0ull && mesh && mesh < RENDER_MAX_MESHES && g_vk.meshes[mesh].used) {
                const MeshSlot &m = g_vk.meshes[mesh];
                vkCmdDrawIndexed(cmd, m.index_count, n, m.first_index, (i32)m.first_vertex, first);
            }
            if (key != ~0ull) first += n;
            i = j;
        }
    }
    // Земля clipmap'ом: кольцо — номер в push-константе, буфер индексов — по сдвигу дыры
    const TerrainGpu &tg = g_vk.terrain;
    if (tg.on && f.ground) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_ground);
        vkCmdBindIndexBuffer(cmd, tg.index.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (i32 L = 0; L < CLIP_LEVELS; ++L) {
            PushConsts pl{fs.frame.address, (u32)L, 0};
            vkCmdPushConstants(cmd, g_vk.layout, pcs, 0, sizeof pl, &pl);
            u32 first = L == 0 ? tg.first_full : tg.first_ring[tg.ring[L]];
            u32 count = L == 0 ? tg.count_full : tg.count_ring;
            vkCmdDrawIndexed(cmd, count, 1, first, 0, 0);
        }
    }
    if (nlines) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_lines);
        vkCmdPushConstants(cmd, g_vk.layout, pcs, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, nlines, 1, 0, 0);
    }
    if (npoints) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_points);
        vkCmdPushConstants(cmd, g_vk.layout, pcs, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, 6, npoints, 0, 0);
    }
    vkCmdEndRendering(cmd);
    if (g_vk.queries) {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, g_vk.queries, slot * 2 + 1);
        fs.timed = true;
    }
    if (g_vk.capture_next) {
        g_vk.capture_next = false;
        record_capture(cmd, g_vk.images[img]);
    }

    image_barrier(cmd, g_vk.images[img], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
    vkEndCommandBuffer(cmd);

    auto wait = vk_info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    wait.semaphore = fs.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto signal = vk_info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    signal.semaphore = g_vk.rendered[img];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    auto cbi = vk_info<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
    cbi.commandBuffer = cmd;
    auto si = vk_info<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    si.waitSemaphoreInfoCount = 1;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = &signal;
    if (!vk_check(vkQueueSubmit2(g_vk.queue, 1, &si, fs.done), "vkQueueSubmit2")) return false;

    auto pi = vk_info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_vk.rendered[img];
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_vk.swapchain;
    pi.pImageIndices = &img;
    t0 = platform_time_ns();
    r = vkQueuePresentKHR(g_vk.queue, &pi);
    g_vk.wait_ns += platform_time_ns() - t0;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) g_vk.swapchain_dirty = true;
    else if (r != VK_SUCCESS) return vk_check(r, "vkQueuePresentKHR");
    if (g_vk.extent.width == 0) g_vk.swapchain_dirty = true;

    g_vk.frame_index = (g_vk.frame_index + 1) % FRAMES_IN_FLIGHT;
    g_vk.frames_drawn++;
    return true;
}

// Окно поменяло размер: swapchain пересоздаётся перед следующим кадром
void render_resized() { g_vk.swapchain_dirty = true; }

void render_capture_next() { g_vk.capture_next = true; }

bool render_capture_take(Arena &arena, u8 **rgba, u32 *width, u32 *height) {
    if (!g_vk.capture_ready) return false;
    vkDeviceWaitIdle(g_vk.device);
    g_vk.capture_ready = false;
    u32 w = g_vk.capture_extent.width, h = g_vk.capture_extent.height;
    vmaInvalidateAllocation(g_vk.vma, g_vk.capture.alloc, 0, VK_WHOLE_SIZE);
    u8 *out = arena_push<u8>(arena, (usize)w * h * 4);
    const u8 *src = (const u8 *)g_vk.capture.mapped;
    bool bgr = g_vk.format == VK_FORMAT_B8G8R8A8_SRGB || g_vk.format == VK_FORMAT_B8G8R8A8_UNORM;
    for (usize i = 0; i < (usize)w * h; ++i) {
        out[i * 4 + 0] = src[i * 4 + (bgr ? 2 : 0)];
        out[i * 4 + 1] = src[i * 4 + 1];
        out[i * 4 + 2] = src[i * 4 + (bgr ? 0 : 2)];
        out[i * 4 + 3] = 255;
    }
    *rgba = out;
    *width = w;
    *height = h;
    return true;
}
