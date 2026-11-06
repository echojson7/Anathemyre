// Небо (docs/sky.md): таблицы атмосферы в буферах видеопамяти и compute-конвейеры, которые их считают.
// Пропускание и многократное рассеяние зависят только от параметров воздуха — пересчёт при их смене;
// вид неба от камеры (солнце и луна) и свет неба на плоскость (гармоники, экспозиция) — каждый кадр
#include "vk_state.h"
#include "../core/log.h"

// Размеры — как в data/shaders/atmosphere.glsl
static constexpr u32 TRANS_W = 256, TRANS_H = 64, MS_N = 32;
static constexpr u32 SKY_W = 192, SKY_H = 128, MOON_W = 96, MOON_H = 64;
static constexpr u32 SKY_OUT = 8;

static VkPipeline make_compute(const char *name) {
    VkShaderModule m = vk_load_shader(name);
    if (!m) return VK_NULL_HANDLE;
    auto ci = vk_info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = m;
    ci.stage.pName = "main";
    ci.layout = g_vk.layout;
    VkPipeline p = VK_NULL_HANDLE;
    vk_check(vkCreateComputePipelines(g_vk.device, VK_NULL_HANDLE, 1, &ci, nullptr, &p), name);
    vkDestroyShaderModule(g_vk.device, m, nullptr);
    return p;
}

bool vk_sky_init() {
    SkyGpu &s = g_vk.sky;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkDeviceSize v4 = 16;
    if (!vk_create_buffer(s.trans, TRANS_W * TRANS_H * v4, ssbo, true) ||
        !vk_create_buffer(s.ms, MS_N * MS_N * v4, ssbo, true) || !vk_create_buffer(s.sun, SKY_W * SKY_H * v4, ssbo, true) ||
        !vk_create_buffer(s.moon, MOON_W * MOON_H * v4, ssbo, true) || !vk_create_buffer(s.out, SKY_OUT * v4, ssbo, true))
        return false;
    s.pipe_trans = make_compute("sky_trans.comp");
    s.pipe_ms = make_compute("sky_ms.comp");
    s.pipe_view = make_compute("sky_view.comp");
    s.pipe_sum = make_compute("sky_sum.comp");
    s.luts_ready = false;
    return s.pipe_trans && s.pipe_ms && s.pipe_view && s.pipe_sum;
}

void vk_sky_shutdown() {
    SkyGpu &s = g_vk.sky;
    VkPipeline ps[] = {s.pipe_trans, s.pipe_ms, s.pipe_view, s.pipe_sum};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    GpuBuffer *bs[] = {&s.trans, &s.ms, &s.sun, &s.moon, &s.out};
    for (GpuBuffer *b : bs) vk_destroy_buffer(*b);
    s = SkyGpu{};
}

// Параметры воздуха (без светил, звёзд и экспозиции): по ним считаются таблицы пропускания и рассеяния
static bool same_air(const RenderSky &a, const RenderSky &b) {
    const usize from = offsetof(RenderSky, rayleigh), to = offsetof(RenderSky, star);
    return memcmp((const u8 *)&a + from, (const u8 *)&b + from, to - from) == 0;
}

static RenderSky g_pending_sky;  // параметры этого кадра (vk_sky_fill → vk_sky_record)

void vk_sky_fill(FrameGpu &g, const RenderFrame &f) {
    const SkyGpu &s = g_vk.sky;
    g.sky_trans = s.trans.address;
    g.sky_ms = s.ms.address;
    g.sky_sun = s.sun.address;
    g.sky_moon = s.moon.address;
    g.sky_out = s.out.address;
    g.sky = f.sky;
    g_pending_sky = f.sky;
}

// Запись шейдером → чтение следующими compute и фрагментами
static void storage_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src, VkAccessFlags2 src_access,
                            VkPipelineStageFlags2 dst, VkAccessFlags2 dst_access) {
    auto b = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    b.srcStageMask = src;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst;
    b.dstAccessMask = dst_access;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

static void dispatch(VkCommandBuffer cmd, VkPipeline p, VkDeviceAddress frame, u32 a, u32 gx, u32 gy) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    PushConsts pc{frame, a, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, gx, gy, 1);
}

void vk_sky_record(VkCommandBuffer cmd, VkDeviceAddress frame) {
    SkyGpu &s = g_vk.sky;
    const VkPipelineStageFlags2 CS = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    const VkPipelineStageFlags2 FS = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const VkAccessFlags2 W = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, R = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    // Прошлый кадр мог ещё читать таблицы во фрагментах: запись — после него (те же буферы на все кадры)
    storage_barrier(cmd, FS | CS, R | W, CS, R | W);
    if (!s.luts_ready || !same_air(s.last, g_pending_sky)) {
        dispatch(cmd, s.pipe_trans, frame, 0, TRANS_W / 8, TRANS_H / 8);
        storage_barrier(cmd, CS, W, CS, R);
        dispatch(cmd, s.pipe_ms, frame, 0, MS_N / 8, MS_N / 8);
        storage_barrier(cmd, CS, W, CS, R);
        s.last = g_pending_sky;
        s.luts_ready = true;
    }
    dispatch(cmd, s.pipe_view, frame, 0, SKY_W / 8, SKY_H / 8);
    dispatch(cmd, s.pipe_view, frame, 1, MOON_W / 8, MOON_H / 8);
    storage_barrier(cmd, CS, W, CS, R);
    dispatch(cmd, s.pipe_sum, frame, 0, 1, 1);
    storage_barrier(cmd, CS, W, CS | FS, R);
}
