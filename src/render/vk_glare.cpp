// Блеск (docs/glare.md): пирамида кадра после сглаживания — уровни вдвое мельче вниз, потом сложение обратно
// вверх (glare.comp); стиль подмешивает её долю к кадру до тонмаппинга. Один проход графа, внутри — свои
// барьеры между уровнями (каждый уровень читает соседний). Там же одним вызовом — солнце в кадре (видимая доля
// диска) для короны и бликов объектива, и данные объектива для стиля (docs/lens.md)
#include "vk_graph.h"
#include "../core/log.h"

static constexpr u32 GLARE_LEVELS = 7;  // уровень 0 — полкадра; 6 — 1/128 кадра (~1° на тексель при 1080p)
static constexpr u32 LENS_SUN = 16;     // бит lens_on: солнце в кадре измерено (lens.glsl)

bool vk_glare_init() {
    GlareGpu &g = g_vk.glare;
    g.rg_buf = rg_buffer("блеск: пирамида");
    g.rg_sun = rg_buffer("блеск: солнце");
    if (!vk_create_buffer(g.sun, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) return false;
    vk_buffer_name(g.sun, "блеск: солнце");
    return vk_make_compute(&g.pipe, "glare.comp");
}

void vk_glare_shutdown() {
    GlareGpu &g = g_vk.glare;
    if (g.pipe) vkDestroyPipeline(g_vk.device, g.pipe, nullptr);
    vk_destroy_buffer(g.buf);
    vk_destroy_buffer(g.sun);
    g = GlareGpu{};
}

// Объектив (docs/lens.md): биты и силы; сила 0 — то же, что выключено. Солнце в кадре меряется, если его ждут
// корона (блеск) или блики
static void lens_fill(FrameGpu &fg, const RenderFrame &f, bool glare) {
    GlareGpu &g = g_vk.glare;
    const RenderLens &l = f.lens;
    u32 on = l.on;
    if (l.flare <= 0.0f) on &= ~(u32)RENDER_LENS_FLARE;
    if (l.ca <= 0.0f) on &= ~(u32)RENDER_LENS_CA;
    if (l.vignette <= 0.0f) on &= ~(u32)RENDER_LENS_VIGNETTE;
    if (l.grain <= 0.0f) on &= ~(u32)RENDER_LENS_GRAIN;
    g.sun_on = g.pipe && (glare || (on & RENDER_LENS_FLARE));
    if (g.sun_on) on |= LENS_SUN;
    fg.lens_sun = g.sun.address;
    fg.lens_on = on;
    fg.lens_frame = g.frame++;
    fg.lens_flare = l.flare;
    fg.lens_ca = l.ca;
    fg.lens_vignette = l.vignette;
    fg.lens_grain = l.grain;
}

void vk_glare_fill(FrameGpu &fg, const RenderFrame &f) {
    GlareGpu &g = g_vk.glare;
    g.on = false;
    fg.gr_on = 0;
    u32 w = g_vk.extent.width, h = g_vk.extent.height;
    bool want = g.pipe && f.sky.glare > 0.0f && w >= 16 && h >= 16;
    lens_fill(fg, f, want);
    if (!want) return;
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
    auto push = [&](u32 mode, u32 b) {
        PushConsts pc{frame, mode, b};
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    };
    auto run = [&](u32 mode, u32 b, u32 k) {
        push(mode, b);
        vkCmdDispatch(cmd, ((u32)g.lv[k][0] + 7) / 8, ((u32)g.lv[k][1] + 7) / 8, 1);
    };
    if (g.sun_on) {
        // Солнце в кадре: одна группа, 64 точки по диску (свой буфер — без барьера с пирамидой)
        push(3, vk_aa_cur());
        vkCmdDispatch(cmd, 1, 1, 1);
    }
    if (!g.on) return;
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
    if (!g.on && !g.sun_on) return;
    rg_pass("блеск", "блеск", record_glare, (void *)frame);
    rg_use(g_vk.aa.rg_hist[vk_aa_cur()], RG_IMG_CS_READ);
    if (g.on) rg_use(g.rg_buf, RG_CS_WRITE);
    if (g.sun_on) rg_use(g.rg_sun, RG_CS_WRITE);
}

void vk_glare_style_reads() {
    if (g_vk.glare.on) rg_use(g_vk.glare.rg_buf, RG_FS_READ);
    if (g_vk.glare.sun_on) rg_use(g_vk.glare.rg_sun, RG_FS_READ);
}
