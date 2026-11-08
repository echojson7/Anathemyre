// Небо (docs/sky.md): таблицы атмосферы в буферах видеопамяти и compute-конвейеры, которые их считают.
// Пропускание и многократное рассеяние зависят только от параметров воздуха — пересчёт при их смене;
// вид неба от камеры (солнце и луна) и свет неба на плоскость (гармоники, экспозиция) — каждый кадр
#include "vk_graph.h"
#include "../core/log.h"

// Размеры — как в data/shaders/atmosphere.glsl
static constexpr u32 TRANS_W = 256, TRANS_H = 64, MS_N = 32;
static constexpr u32 SKY_W = 192, SKY_H = 128, MOON_W = 96, MOON_H = 64;
static constexpr u32 SKY_OUT = 8;

VkPipeline vk_make_compute(const char *name, const VkSpecializationInfo *spec) {
    VkShaderModule m = vk_load_shader(name);
    if (!m) return VK_NULL_HANDLE;
    auto ci = vk_info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = m;
    ci.stage.pName = "main";
    ci.stage.pSpecializationInfo = spec;
    ci.layout = g_vk.layout;
    VkPipeline p = VK_NULL_HANDLE;
    vk_check(vkCreateComputePipelines(g_vk.device, g_vk.pipeline_cache, 1, &ci, nullptr, &p), name);
    vkDestroyShaderModule(g_vk.device, m, nullptr);
    return p;
}

bool vk_sky_init() {
    SkyGpu &s = g_vk.sky;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkDeviceSize v4 = 16;
    if (!vk_create_buffer(s.trans, TRANS_W * TRANS_H * v4, ssbo, true) ||
        !vk_create_buffer(s.ms, MS_N * MS_N * v4, ssbo, true) || !vk_create_buffer(s.sun, SKY_W * SKY_H * v4, ssbo, true) ||
        !vk_create_buffer(s.moon, MOON_W * MOON_H * v4, ssbo, true) || !vk_create_buffer(s.out, SKY_OUT * v4, ssbo | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true))
        return false;
    const char *names[] = {"небо: пропускание", "небо: рассеяние", "небо: вид от солнца", "небо: вид от луны", "небо: свет"};
    GpuBuffer *bs[] = {&s.trans, &s.ms, &s.sun, &s.moon, &s.out};
    u8 *rs[] = {&s.rg_trans, &s.rg_ms, &s.rg_sun, &s.rg_moon, &s.rg_out};
    for (u32 i = 0; i < 5; ++i) {
        vk_buffer_name(*bs[i], names[i]);
        *rs[i] = rg_buffer(names[i]);
    }
    s.pipe_trans = vk_make_compute("sky_trans.comp");
    s.pipe_ms = vk_make_compute("sky_ms.comp");
    s.pipe_view = vk_make_compute("sky_view.comp");
    s.pipe_sum = vk_make_compute("sky_sum.comp");
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

static void dispatch(VkCommandBuffer cmd, VkPipeline p, VkDeviceAddress frame, u32 a, u32 gx, u32 gy) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    PushConsts pc{frame, a, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, gx, gy, 1);
}

// Проходы неба (docs/graph.md): барьеры между ними и с прошлым кадром (те же буферы на все кадры) ставит граф
static VkDeviceAddress g_frame_addr;

static void record_trans(VkCommandBuffer cmd, void *) { dispatch(cmd, g_vk.sky.pipe_trans, g_frame_addr, 0, TRANS_W / 8, TRANS_H / 8); }
static void record_ms(VkCommandBuffer cmd, void *) { dispatch(cmd, g_vk.sky.pipe_ms, g_frame_addr, 0, MS_N / 8, MS_N / 8); }
static void record_view(VkCommandBuffer cmd, void *) {
    dispatch(cmd, g_vk.sky.pipe_view, g_frame_addr, 0, SKY_W / 8, SKY_H / 8);
    dispatch(cmd, g_vk.sky.pipe_view, g_frame_addr, 1, MOON_W / 8, MOON_H / 8);
}
static void record_sum(VkCommandBuffer cmd, void *) { dispatch(cmd, g_vk.sky.pipe_sum, g_frame_addr, 0, 1, 1); }

void vk_sky_passes(VkDeviceAddress frame) {
    SkyGpu &s = g_vk.sky;
    g_frame_addr = frame;
    if (!s.luts_ready || !same_air(s.last, g_pending_sky)) {
        rg_pass("небо: пропускание", "небо", record_trans, nullptr);
        rg_use(s.rg_trans, RG_CS_WRITE);
        rg_pass("небо: рассеяние", "небо", record_ms, nullptr);
        rg_use(s.rg_trans, RG_CS_READ);
        rg_use(s.rg_ms, RG_CS_WRITE);
        s.last = g_pending_sky;
        s.luts_ready = true;
    }
    rg_pass("небо: вид", "небо", record_view, nullptr);
    rg_use(s.rg_trans, RG_CS_READ);
    rg_use(s.rg_ms, RG_CS_READ);
    rg_use(s.rg_sun, RG_CS_WRITE);
    rg_use(s.rg_moon, RG_CS_WRITE);
    rg_pass("небо: свет", "небо", record_sum, nullptr);
    vk_sky_reads(RG_CS_READ);
    rg_use(s.rg_out, RG_CS_WRITE);
}

void vk_sky_reads(RgUse u) {
    const SkyGpu &s = g_vk.sky;
    RgRes rs[] = {s.rg_trans, s.rg_ms, s.rg_sun, s.rg_moon, s.rg_out};
    for (RgRes r : rs) rg_use(r, u);
}
