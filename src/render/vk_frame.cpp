// Конвейеры и кадр: общая раскладка, графические конвейеры, отправка и показ кадра; проходы кадра мира —
// vk_world.cpp. Обратная глубина D32F до бесконечности, координаты относительно камеры. Вершины шейдер читает
// сам по адресу буфера (без входа вершин конвейера, docs/frame.md)
#include "vk_graph.h"
#include "../asset/mesh.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>

// name — один из списка через запятую
static bool name_listed(const char *list, const char *name) {
    if (!list) return false;
    usize n = strlen(name);
    for (const char *p = list; *p;) {
        const char *e = strchr(p, ',');
        usize len = e ? (usize)(e - p) : strlen(p);
        if (len == n && !memcmp(p, name, n)) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

VkShaderModule vk_load_shader(const char *name) {
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
        // Проверка на GPU — у каждого шейдера, кроме названных в gpu_validation_skip (docs/graph.md, «Проверки»)
        VkValidationFeatureEnableEXT gpuav = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
        auto vf = vk_info<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
        vf.enabledValidationFeatureCount = 1;
        vf.pEnabledValidationFeatures = &gpuav;
        if (g_vk.cfg.gpu_validation && !name_listed(g_vk.cfg.gpu_validation_skip, name)) ci.pNext = &vf;
        vk_check(vkCreateShaderModule(g_vk.device, &ci, nullptr, &m), path);
    }
    platform_free(mem);
    return m;
}

VkPipeline vk_make_pipeline(const PipeDesc &d) {
    VkShaderModule vs = vk_load_shader(d.vs);
    VkShaderModule fs = vk_load_shader(d.fs);
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
        dss.depthTestEnable = d.depth_test ? VK_TRUE : VK_FALSE;
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
        VkFormat color = d.color != VK_FORMAT_UNDEFINED ? d.color : g_vk.format;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color;
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
        vk_check(vkCreateGraphicsPipelines(g_vk.device, g_vk.pipeline_cache, 1, &pi, nullptr, &p), d.vs);
    }
    if (vs) vkDestroyShaderModule(g_vk.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(g_vk.device, fs, nullptr);
    return p;
}

bool vk_create_pipelines() {
    // Общая раскладка: push-константы и набор картинок кадра мира (visibility buffer, HDR)
    if (!vk_world_layout()) return false;
    VkPushConstantRange pc{PUSH_STAGES, 0, sizeof(PushConsts)};
    auto li = vk_info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    li.setLayoutCount = 1;
    li.pSetLayouts = &g_vk.world.set_layout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (!vk_check(vkCreatePipelineLayout(g_vk.device, &li, nullptr, &g_vk.layout), "vkCreatePipelineLayout")) return false;
    PipeDesc points{"points.vert", "color.frag"};
    points.blend = true;
    points.depth_write = false;
    PipeDesc lines{"lines.vert", "color.frag", VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, false};
    g_vk.pipe_points = vk_make_pipeline(points);
    g_vk.pipe_lines = vk_make_pipeline(lines);
    return g_vk.pipe_points && g_vk.pipe_lines && vk_sky_init() &&
           vk_horizon_init() && vk_world_init() && vk_froxel_init() && vk_ui_init();
}

void vk_destroy_pipelines() {
    vk_ui_shutdown();
    vk_world_shutdown();
    vk_froxel_shutdown();
    vk_sky_shutdown();
    vk_horizon_shutdown();
    VkPipeline ps[] = {g_vk.pipe_points, g_vk.pipe_lines};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    if (g_vk.layout) vkDestroyPipelineLayout(g_vk.device, g_vk.layout, nullptr);
    g_vk.pipe_points = g_vk.pipe_lines = VK_NULL_HANDLE;
    g_vk.layout = VK_NULL_HANDLE;
}

// Метка времени на границе проходов: всё записанное до неё закончилось. Первая метка кадра — начало
void vk_mark(VkCommandBuffer cmd, const char *name) {
    if (!g_vk.queries) return;
    FrameSlot &fs = g_vk.frames[g_vk.frame_index];
    if (fs.marks >= GPU_MAX_MARKS) return;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, g_vk.queries,
                         g_vk.frame_index * GPU_MAX_MARKS + fs.marks);
    fs.mark_names[fs.marks++] = name;
}

// Метки времени прошлого использования слота: время кадра мира и его проходов на GPU
static void read_gpu_time(FrameSlot &fs, u32 slot) {
    if (fs.marks < 2 || !g_vk.queries) { fs.marks = 0; return; }
    u64 t[GPU_MAX_MARKS] = {};
    VkResult r = vkGetQueryPoolResults(g_vk.device, g_vk.queries, slot * GPU_MAX_MARKS, fs.marks, sizeof t, t,
                                       sizeof(u64), VK_QUERY_RESULT_64_BIT);
    if (r == VK_SUCCESS) {
        const f64 k = (f64)g_vk.timestamp_period / 1e6;
        g_vk.gpu_ms = t[fs.marks - 1] >= t[0] ? (f64)(t[fs.marks - 1] - t[0]) * k : 0.0;
        g_vk.pass_count = fs.marks - 1;
        for (u32 i = 1; i < fs.marks; ++i) {
            g_vk.pass_names[i - 1] = fs.mark_names[i];
            g_vk.pass_ms[i - 1] = t[i] >= t[i - 1] ? (f64)(t[i] - t[i - 1]) * k : 0.0;
        }
    }
    fs.marks = 0;
}

u32 render_gpu_passes(const char **names, f64 *ms, u32 cap) {
    u32 n = g_vk.pass_count < cap ? g_vk.pass_count : cap;
    for (u32 i = 0; i < n; ++i) {
        names[i] = g_vk.pass_names[i];
        ms[i] = g_vk.pass_ms[i];
    }
    return n;
}

static void fill_frame(FrameGpu &g, const RenderFrame &f, FrameSlot &fs) {
    g = FrameGpu{};
    g.vertices = g_vk.vertices.address;
    g.materials = g_vk.materials.address;
    g.lines = fs.lines.address;
    g.points = fs.points.address;
    g.stats = g_vk.diag.stats.address;
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
    vk_sky_fill(g, f);
    vk_horizon_fill(g, f);
    vk_world_fill(g, f, fs);
    vk_froxel_fill(g, f, fs);
}

// Снимок: копия картинки кадра в буфер (после интерфейса, до показа)
static u32 g_capture_img = 0;

static void record_capture(VkCommandBuffer cmd, void *) {
    VkImage img = g_vk.images[g_capture_img];
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {g_vk.extent.width, g_vk.extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_vk.capture.buffer, 1, &c);
}

static void capture_pass(u32 img) {
    VkDeviceSize bytes = (VkDeviceSize)g_vk.extent.width * g_vk.extent.height * 4;
    if (g_vk.capture.size < bytes) {
        vk_destroy_buffer(g_vk.capture);
        if (!vk_create_buffer(g_vk.capture, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) return;
        vk_buffer_name(g_vk.capture, "снимок кадра");
        rg_reset(g_vk.rg_capture);
    }
    g_capture_img = img;
    rg_pass("снимок", nullptr, record_capture, nullptr);
    rg_use(g_vk.rg_swap, RG_IMG_COPY_READ);
    rg_use(g_vk.rg_capture, RG_COPY_WRITE);
    g_vk.capture_extent = g_vk.extent;
    g_vk.capture_ready = true;
}

bool render_frame(const RenderFrame &f) {
    // Части кадра (docs/frame_time.md): отметка после каждой, время между отметками — в свою часть
    g_vk.cpu = RenderCpuTimes{};
    u64 t = platform_time_ns();
    auto lap = [&t](f64 &part) {
        u64 n = platform_time_ns();
        part += (f64)(n - t) / 1e6;
        t = n;
    };
    if (g_vk.swapchain_dirty) {
        i32 w = 0, h = 0;
        platform_window_size(&w, &h);
        if (w == 0 || h == 0) return true;  // окно свёрнуто
        vkDeviceWaitIdle(g_vk.device);
        if (!vk_create_swapchain()) return false;
    }
    u32 slot = g_vk.frame_index;
    FrameSlot &fs = g_vk.frames[slot];
    lap(g_vk.cpu.prepare);
    if (!vk_check(vkWaitForFences(g_vk.device, 1, &fs.done, VK_TRUE, UINT64_MAX), "vkWaitForFences")) return false;
    lap(g_vk.cpu.fence);
    read_gpu_time(fs, slot);
    vk_stats_read(fs);
    vk_diag_frame(g_vk.frames_drawn);
    vk_geometry_collect();
    lap(g_vk.cpu.prepare);

    u32 img = 0;
    VkResult r = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX, fs.acquired, VK_NULL_HANDLE, &img);
    lap(g_vk.cpu.acquire);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { g_vk.swapchain_dirty = true; return true; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return vk_check(r, "vkAcquireNextImageKHR");
    vkResetFences(g_vk.device, 1, &fs.done);

    vk_overflow("экземпляры", f.instances.count, RENDER_MAX_INSTANCES);
    vk_overflow("точки", f.points.count, RENDER_MAX_POINTS);
    vk_overflow("вершины отрезков", f.lines.count, RENDER_MAX_LINE_VERTICES);
    vk_overflow("декали", f.decals.count, RENDER_MAX_DECALS);
    vk_overflow("огни", f.lights.count, RENDER_MAX_LIGHTS);
    FrameGpu &g = *(FrameGpu *)fs.frame.mapped;
    fill_frame(g, f, fs);

    u32 npoints = f.points.count < RENDER_MAX_POINTS ? (u32)f.points.count : RENDER_MAX_POINTS;
    u32 nlines = f.lines.count < RENDER_MAX_LINE_VERTICES ? (u32)f.lines.count & ~1u : RENDER_MAX_LINE_VERTICES;
    if (npoints) memcpy(fs.points.mapped, f.points.data, npoints * sizeof(RenderPoint));
    if (nlines) memcpy(fs.lines.mapped, f.lines.data, nlines * sizeof(RenderLineVertex));
    u32 ndecals = f.decals.count < RENDER_MAX_DECALS ? (u32)f.decals.count : RENDER_MAX_DECALS;
    if (ndecals) memcpy(fs.decals.mapped, f.decals.data, ndecals * sizeof(RenderDecal));
    g.decals = fs.decals.address;
    g.decal_count = ndecals;

    lap(g_vk.cpu.prepare);

    // Кадр в графе (docs/graph.md): проходы по порядку, барьеры и метки ставит граф
    rg_begin();
    rg_import_swapchain(g_vk.rg_swap, g_vk.images[img]);
    vk_stats_clear_pass();
    // Таблицы неба и froxels — до кадра: их читают земля, сетки (дымка, свет неба), небо и освещение
    vk_sky_passes(fs.frame.address);
    vk_froxel_passes(fs);
    // Карта горизонта (docs/horizon.md): только в первом кадре новой земли
    vk_horizon_passes(fs.frame.address);
    if (!vk_world_passes(fs, f, img, npoints, nlines)) {
        log_error("кадр мира: нет картинок");
        return false;
    }
    vk_stats_copy_pass(fs);
    // Интерфейс — после замеров GPU (таймер меряет кадр мира) и до снимка: снимок с интерфейсом
    vk_ui_pass(img);
    if (g_vk.capture_next) {
        g_vk.capture_next = false;
        capture_pass(img);
    }
    rg_pass("показ", nullptr, nullptr, nullptr);
    rg_use(g_vk.rg_swap, RG_PRESENT);

    vkResetCommandPool(g_vk.device, fs.pool, 0);
    VkCommandBuffer cmd = fs.cmd;
    auto bi = vk_info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    if (g_vk.queries) vkCmdResetQueryPool(cmd, g_vk.queries, slot * GPU_MAX_MARKS, GPU_MAX_MARKS);
    fs.marks = 0;
    rg_execute(cmd);
    vkEndCommandBuffer(cmd);
    lap(g_vk.cpu.record);

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
    fs.stats_pending = true;
    lap(g_vk.cpu.submit);

    auto pi = vk_info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_vk.rendered[img];
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_vk.swapchain;
    pi.pImageIndices = &img;
    r = vkQueuePresentKHR(g_vk.queue, &pi);
    lap(g_vk.cpu.present);
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
