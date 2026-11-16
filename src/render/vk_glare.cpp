// Блеск (docs/glare.md): пирамида кадра после сглаживания — уровни вдвое мельче вниз, потом сложение обратно
// вверх (glare.comp); стиль подмешивает её долю к кадру до тонмаппинга. Один проход графа, внутри — свои
// барьеры между уровнями (каждый уровень читает соседний)
#include "vk_graph.h"
#include "../core/log.h"

static constexpr u32 GLARE_LEVELS = 7;  // уровень 0 — полкадра; 6 — 1/128 кадра (~1° на тексель при 1080p)

bool vk_glare_init() {
    GlareGpu &g = g_vk.glare;
    g.rg_buf = rg_buffer("блеск: пирамида");
    return vk_make_compute(&g.pipe, "glare.comp");
}

void vk_glare_shutdown() {
    GlareGpu &g = g_vk.glare;
    if (g.pipe) vkDestroyPipeline(g_vk.device, g.pipe, nullptr);
    vk_destroy_buffer(g.buf);
    g = GlareGpu{};
}

void vk_glare_fill(FrameGpu &fg, const RenderFrame &f) {
    GlareGpu &g = g_vk.glare;
    g.on = false;
    fg.gr_on = 0;
    u32 w = g_vk.extent.width, h = g_vk.extent.height;
    if (!g.pipe || f.sky.glare <= 0.0f || w < 16 || h < 16) return;
    if (!g.buf.buffer || w != g.w || h != g.h) {
        // Новый размер кадра: уровни заново. Редко — подождать кадры в полёте
        vkDeviceWaitIdle(g_vk.device);
        vk_destroy_buffer(g.buf);
        u32 lw = (w + 1) / 2, lh = (h + 1) / 2, at = 0;
        g.n = 0;
        for (u32 k = 0; k < GLARE_LEVELS && lw >= 2 && lh >= 2; ++k) {
            g.lv[k][0] = (i32)lw;
            g.lv[k][1] = (i32)lh;
            g.lv[k][2] = (i32)at;
            g.lv[k][3] = (i32)(at + lw * lh);
            at += 2 * lw * lh;
            lw = (lw + 1) / 2;
            lh = (lh + 1) / 2;
            g.n = k + 1;
        }
        if (!vk_create_buffer(g.buf, (VkDeviceSize)at * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) {
            g.w = g.h = 0;
            return;
        }
        vk_buffer_name(g.buf, "блеск: пирамида");
        rg_reset(g.rg_buf);
        g.w = w;
        g.h = h;
    }
    g.on = true;
    fg.gr_on = 1;
    fg.gr_buf = g.buf.address;
    fg.gr_n = g.n;
    for (u32 k = 0; k < 8; ++k)
        for (u32 i = 0; i < 4; ++i) fg.gr_lv[k][i] = k < g.n ? g.lv[k][i] : 0;
}

static void barrier(VkCommandBuffer cmd) {
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

static void record_glare(VkCommandBuffer cmd, void *user) {
    const GlareGpu &g = g_vk.glare;
    VkDeviceAddress frame = *(const VkDeviceAddress *)user;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    auto run = [&](u32 mode, u32 b, u32 k) {
        PushConsts pc{frame, mode, b};
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDispatch(cmd, ((u32)g.lv[k][0] + 7) / 8, ((u32)g.lv[k][1] + 7) / 8, 1);
    };
    run(0, vk_aa_cur(), 0);
    for (u32 k = 1; k < g.n; ++k) {
        barrier(cmd);
        run(1, k, k);
    }
    for (u32 k = g.n; k-- > 0;) {
        barrier(cmd);
        run(2, k, k);
    }
}

void vk_glare_pass(const VkDeviceAddress *frame) {
    const GlareGpu &g = g_vk.glare;
    if (!g.on) return;
    rg_pass("блеск", "блеск", record_glare, (void *)frame);
    rg_use(g_vk.aa.rg_hist[vk_aa_cur()], RG_IMG_CS_READ);
    rg_use(g.rg_buf, RG_CS_WRITE);
}

void vk_glare_style_reads() {
    if (g_vk.glare.on) rg_use(g_vk.glare.rg_buf, RG_FS_READ);
}
