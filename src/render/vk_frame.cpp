// Конвейеры и кадр: очистка, треугольник, точки и отрезки. Вершины шейдер читает сам по адресу буфера
// (без входа вершин конвейера), как потом visibility buffer (docs/stack.md)
#include "vk_state.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include <stdio.h>

// Раскладка — как layout(push_constant) в data/shaders/*.vert
struct PushConsts {
    VkDeviceAddress data;
    f32 meters_to_ndc[2];
    f32 ndc_per_pixel[2];
};

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

static VkPipeline make_pipeline(const char *vs_name, const char *fs_name, VkPrimitiveTopology topo, bool blend) {
    VkShaderModule vs = load_shader(vs_name);
    VkShaderModule fs = load_shader(fs_name);
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
        ia.topology = topo;
        auto vp = vk_info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        auto rs = vk_info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        auto ms = vk_info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                            VK_COLOR_COMPONENT_A_BIT;
        if (blend) {
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
        auto pi = vk_info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        pi.pNext = &ri;
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &ds;
        pi.layout = g_vk.layout;
        vk_check(vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p), vs_name);
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
    g_vk.pipe_triangle = make_pipeline("triangle.vert", "color.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false);
    g_vk.pipe_points = make_pipeline("points.vert", "color.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true);
    g_vk.pipe_lines = make_pipeline("lines.vert", "color.frag", VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true);
    return g_vk.pipe_triangle && g_vk.pipe_points && g_vk.pipe_lines;
}

void vk_destroy_pipelines() {
    VkPipeline ps[] = {g_vk.pipe_triangle, g_vk.pipe_points, g_vk.pipe_lines};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    if (g_vk.layout) vkDestroyPipelineLayout(g_vk.device, g_vk.layout, nullptr);
    g_vk.pipe_triangle = g_vk.pipe_points = g_vk.pipe_lines = VK_NULL_HANDLE;
    g_vk.layout = VK_NULL_HANDLE;
}

static void image_barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
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
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
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

bool render_frame(const RenderFrame &f) {
    if (g_vk.swapchain_dirty) {
        i32 w = 0, h = 0;
        platform_window_size(&w, &h);
        if (w == 0 || h == 0) return true;  // окно свёрнуто
        vkDeviceWaitIdle(g_vk.device);
        if (!vk_create_swapchain()) return false;
    }
    FrameSlot &fs = g_vk.frames[g_vk.frame_index];
    if (!vk_check(vkWaitForFences(g_vk.device, 1, &fs.done, VK_TRUE, UINT64_MAX), "vkWaitForFences")) return false;

    u32 img = 0;
    VkResult r = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX, fs.acquired, VK_NULL_HANDLE, &img);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { g_vk.swapchain_dirty = true; return true; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return vk_check(r, "vkAcquireNextImageKHR");
    vkResetFences(g_vk.device, 1, &fs.done);

    u32 npoints = f.points.count < RENDER_MAX_POINTS ? (u32)f.points.count : RENDER_MAX_POINTS;
    u32 nlines = f.lines.count < RENDER_MAX_LINE_VERTICES ? (u32)f.lines.count & ~1u : RENDER_MAX_LINE_VERTICES;
    if (npoints) memcpy(fs.points.mapped, f.points.data, npoints * sizeof(RenderPoint));
    if (nlines) memcpy(fs.lines.mapped, f.lines.data, nlines * sizeof(RenderLineVertex));

    vkResetCommandPool(g_vk.device, fs.pool, 0);
    VkCommandBuffer cmd = fs.cmd;
    auto bi = vk_info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    image_barrier(cmd, g_vk.images[img], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    auto ca = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    ca.imageView = g_vk.views[img];
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color = {{srgb_to_linear(f.clear[0]), srgb_to_linear(f.clear[1]), srgb_to_linear(f.clear[2]), 1.0f}};
    auto ri = vk_info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = {{0, 0}, g_vk.extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRendering(cmd, &ri);

    VkViewport vp{0, 0, (f32)g_vk.extent.width, (f32)g_vk.extent.height, 0, 1};
    VkRect2D sc{{0, 0}, g_vk.extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    PushConsts pc{};
    pc.meters_to_ndc[0] = f.meters_to_ndc[0];
    pc.meters_to_ndc[1] = f.meters_to_ndc[1];
    pc.ndc_per_pixel[0] = 2.0f / (f32)g_vk.extent.width;
    pc.ndc_per_pixel[1] = 2.0f / (f32)g_vk.extent.height;
    VkShaderStageFlags pcs = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    if (f.triangle) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_triangle);
        vkCmdPushConstants(cmd, g_vk.layout, pcs, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    if (nlines) {
        pc.data = fs.lines.address;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_lines);
        vkCmdPushConstants(cmd, g_vk.layout, pcs, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, nlines, 1, 0, 0);
    }
    if (npoints) {
        pc.data = fs.points.address;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe_points);
        vkCmdPushConstants(cmd, g_vk.layout, pcs, 0, sizeof pc, &pc);
        vkCmdDraw(cmd, 6, npoints, 0, 0);
    }
    vkCmdEndRendering(cmd);

    image_barrier(cmd, g_vk.images[img], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
    vkEndCommandBuffer(cmd);

    auto wait = vk_info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    wait.semaphore = fs.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto signal = vk_info<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    signal.semaphore = g_vk.rendered[img];
    signal.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
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
    r = vkQueuePresentKHR(g_vk.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) g_vk.swapchain_dirty = true;
    else if (r != VK_SUCCESS) return vk_check(r, "vkQueuePresentKHR");
    if (g_vk.extent.width == 0) g_vk.swapchain_dirty = true;

    g_vk.frame_index = (g_vk.frame_index + 1) % FRAMES_IN_FLIGHT;
    return true;
}

// Окно поменяло размер: swapchain пересоздаётся перед следующим кадром
void render_resized() { g_vk.swapchain_dirty = true; }
