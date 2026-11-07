// Интерфейс поверх кадра (docs/ui.md): последний проход по готовой картинке swapchain, без глубины.
// Прямоугольники и буквы — экземпляры по 6 вершин, данные шейдер читает сам по адресам (как всё в кадре).
// Проход не зависит от прохода мира: этап 5 меняет кадр под ним, этот файл не трогая
#include "vk_graph.h"
#include "../core/log.h"

constexpr u32 UI_MAX_QUADS = 16384;

// Push-константы ui.vert/ui.frag
struct UiPush {
    VkDeviceAddress quads, atlas;
    f32 screen[2];
    u32 atlas_w, pad;
};

static struct {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    GpuBuffer atlas;  // атлас букв R8, по 4 в слове
    u32 atlas_w = 0;
    GpuBuffer quads[FRAMES_IN_FLIGHT];
    u32 count = 0;    // прямоугольников в следующем кадре
} g_ui;

static VkPipeline make_ui_pipeline() {
    VkShaderModule vs = vk_load_shader("ui.vert");
    VkShaderModule fs = vk_load_shader("ui.frag");
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
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        auto vp = vk_info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        auto rs = vk_info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        auto ms = vk_info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        auto dss = vk_info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                            VK_COLOR_COMPONENT_A_BIT;
        ba.blendEnable = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.alphaBlendOp = VK_BLEND_OP_ADD;
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
        pi.pDepthStencilState = &dss;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &ds;
        pi.layout = g_ui.layout;
        vk_check(vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &pi, nullptr, &p), "ui.vert");
    }
    if (vs) vkDestroyShaderModule(g_vk.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(g_vk.device, fs, nullptr);
    return p;
}

bool vk_ui_init() {
    VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UiPush)};
    auto li = vk_info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (!vk_check(vkCreatePipelineLayout(g_vk.device, &li, nullptr, &g_ui.layout), "vkCreatePipelineLayout ui"))
        return false;
    g_ui.pipe = make_ui_pipeline();
    if (!g_ui.pipe) return false;
    for (GpuBuffer &b : g_ui.quads)
        if (!vk_create_buffer(b, UI_MAX_QUADS * sizeof(RenderUiQuad), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
    return true;
}

void vk_ui_shutdown() {
    if (g_ui.pipe) vkDestroyPipeline(g_vk.device, g_ui.pipe, nullptr);
    if (g_ui.layout) vkDestroyPipelineLayout(g_vk.device, g_ui.layout, nullptr);
    for (GpuBuffer &b : g_ui.quads) vk_destroy_buffer(b);
    vk_destroy_buffer(g_ui.atlas);
    g_ui.pipe = VK_NULL_HANDLE;
    g_ui.layout = VK_NULL_HANDLE;
    g_ui.count = 0;
    g_ui.atlas_w = 0;
}

struct AtlasUpload {
    VkBuffer staging;
    VkDeviceSize bytes;
};

static void record_atlas_upload(VkCommandBuffer cmd, void *user) {
    AtlasUpload &u = *(AtlasUpload *)user;
    VkBufferCopy c{0, 0, u.bytes};
    vkCmdCopyBuffer(cmd, u.staging, g_ui.atlas.buffer, 1, &c);
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

bool render_ui_font(const u8 *atlas, u32 w, u32 h) {
    if (!g_ui.pipe || w % 4 != 0) return false;
    VkDeviceSize bytes = (VkDeviceSize)w * h;
    vkDeviceWaitIdle(g_vk.device);
    vk_destroy_buffer(g_ui.atlas);
    if (!vk_create_buffer(g_ui.atlas, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
        return false;
    GpuBuffer staging;
    if (!vk_create_buffer(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return false;
    memcpy(staging.mapped, atlas, bytes);
    vmaFlushAllocation(g_vk.vma, staging.alloc, 0, VK_WHOLE_SIZE);
    AtlasUpload u{staging.buffer, bytes};
    bool ok = vk_one_shot(record_atlas_upload, &u);
    vk_destroy_buffer(staging);
    if (ok) g_ui.atlas_w = w;
    return ok;
}

static u32 g_ui_img = 0, g_ui_n = 0;
static usize g_ui_want = 0;  // сколько прямоугольников просили (render_ui_set)

void render_ui_set(Span<const RenderUiQuad> quads) {
    u32 n = quads.count < UI_MAX_QUADS ? (u32)quads.count : UI_MAX_QUADS;
    g_ui_want = quads.count;
    if (n) memcpy(g_ui.quads[g_vk.frame_index].mapped, quads.data, n * sizeof(RenderUiQuad));
    g_ui.count = n;
}


static void record_ui(VkCommandBuffer cmd, void *) {
    u32 img = g_ui_img, n = g_ui_n;
    auto ca = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    ca.imageView = g_vk.views[img];
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_ui.pipe);
    UiPush pc{g_ui.quads[g_vk.frame_index].address, g_ui.atlas.address,
              {(f32)g_vk.extent.width, (f32)g_vk.extent.height}, g_ui.atlas_w, 0};
    vkCmdPushConstants(cmd, g_ui.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    vkCmdDraw(cmd, 6, n, 0, 0);
    vkCmdEndRendering(cmd);
}

void vk_ui_pass(u32 img) {
    u32 n = g_ui.count;
    g_ui.count = 0;  // кадр без render_ui_set — без интерфейса
    vk_overflow("прямоугольники интерфейса", g_ui_want, UI_MAX_QUADS);
    g_ui_want = 0;
    if (!n || !g_ui.pipe || !g_ui.atlas_w) return;
    g_ui_img = img;
    g_ui_n = n;
    rg_pass("интерфейс", nullptr, record_ui, nullptr);
    rg_use(g_vk.rg_swap, RG_COLOR);
}
