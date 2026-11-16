// Облака (docs/clouds.md): объёмные кучевые и слой перистых считаются не на пиксель кадра, а в карту купола
// неба вокруг камеры — по частям, несколько фаз из CLOUD_PHASES за кадр (cloud_map.comp). Слотов три: a и b
// показаны с перетеканием, w строится; собран — слоты сдвигаются по кругу. Показ переносит слот к своему кадру
// по сносу ветром и сдвигу камеры (clouds.glsl), поэтому кадр облаков стоит одну выборку карты, а не луч.
// Тень облаков на землю — карта в осях облаков (cloud_shadow.comp), тоже по частям
#include "vk_graph.h"
#include "../core/log.h"
#include <math.h>
#include <string.h>

// Как в data/shaders/clouds.glsl
static constexpr u32 CLOUD_N = 128, CLOUD_SH_N = 512;
static constexpr u32 CLOUD_PHASES = 64, CLOUD_SH_PHASES = 16;
static constexpr u32 CLOUD_SIZES[RENDER_LIGHT_QUALITIES] = {768, 1024, 1536, 2048};
static constexpr u32 CLOUD_STEPS[RENDER_LIGHT_QUALITIES] = {64, 96, 128, 160};
static constexpr u32 CLOUD_RATE[RENDER_LIGHT_QUALITIES] = {1, 1, 1, 2};  // фаз карты за кадр: вся карта — за 64 или 32 кадра

bool vk_clouds_init() {
    CloudGpu &c = g_vk.clouds;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    // Карта купола — по ступени света, при первом кадре с облаками (vk_clouds_fill): 14…100 МБ
    if (!vk_create_buffer(c.noise, (VkDeviceSize)CLOUD_N * CLOUD_N * CLOUD_N * 4, ssbo, true) ||
        !vk_create_buffer(c.shadow, (VkDeviceSize)CLOUD_SH_N * CLOUD_SH_N * 4, ssbo, true) ||
        !vk_create_buffer(c.info, 3 * 4 * 16, ssbo, true))
        return false;
    const char *names[] = {"облака: карта", "облака: шум", "облака: тень", "облака: слоты"};
    GpuBuffer *bs[] = {&c.map, &c.noise, &c.shadow, &c.info};
    u8 *rs[] = {&c.rg_map, &c.rg_noise, &c.rg_shadow, &c.rg_info};
    for (u32 i = 0; i < 4; ++i) {
        if (bs[i]->buffer) vk_buffer_name(*bs[i], names[i]);
        *rs[i] = rg_buffer(names[i]);
    }
    bool ok = vk_make_compute(&c.pipe_noise, "cloud_noise.comp");
    ok = vk_make_compute(&c.pipe_map, "cloud_map.comp") && ok;
    ok = vk_make_compute(&c.pipe_shadow, "cloud_shadow.comp") && ok;
    c.noise_ready = c.ready = false;
    return ok;
}

void vk_clouds_shutdown() {
    CloudGpu &c = g_vk.clouds;
    VkPipeline ps[] = {c.pipe_noise, c.pipe_map, c.pipe_shadow};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    GpuBuffer *bs[] = {&c.map, &c.noise, &c.shadow, &c.info};
    for (GpuBuffer *b : bs) vk_destroy_buffer(*b);
    c = CloudGpu{};
}

// Снимок сборки → вектор для шейдера: камера в осях облаков (мир минус снос ветром), высота над морем, время формы
static void snap_vec(const CloudSnap &s, f32 alt0, f32 out[4]) {
    out[0] = (f32)(s.cam[0] - (f64)s.wind[0]);
    out[1] = (f32)s.cam[1] + alt0;
    out[2] = (f32)(s.cam[2] - (f64)s.wind[1]);
    out[3] = s.evolve;
}
// Перенос слота к кадру now: куда сместилась точка облака относительно камеры со сборки слота
static void snap_off(const CloudSnap &now, const CloudSnap &s, f32 out[4]) {
    out[0] = (f32)(now.cam[0] - s.cam[0]) - (now.wind[0] - s.wind[0]);
    out[1] = (f32)(now.cam[1] - s.cam[1]);
    out[2] = (f32)(now.cam[2] - s.cam[2]) - (now.wind[1] - s.wind[1]);
    out[3] = 0;
}

void vk_clouds_fill(FrameGpu &g, const RenderFrame &f) {
    CloudGpu &c = g_vk.clouds;
    g.cl_map = c.map.address;
    g.cl_noise = c.noise.address;
    g.cl_shadow = c.shadow.address;
    g.cl_info = c.info.address;
    const RenderSky &s = f.sky;
    c.job_on = false;
    g.cl_mean_t = 1.0f;
    bool on = c.pipe_map && (s.cloud_shape[2] > 0.0f || s.cloud_cirrus[1] > 0.0f);
    if (!on) {
        c.ready = false;
        return;
    }
    u32 q = f.style.light_quality < RENDER_LIGHT_QUALITIES ? f.style.light_quality : RENDER_LIGHT_HIGH;
    u32 size = CLOUD_SIZES[q];
    if (!c.map.buffer || size != c.size) {
        // Новая ступень света (или первые облака): карта другого размера. Редко — подождать кадры в полёте
        vkDeviceWaitIdle(g_vk.device);
        vk_destroy_buffer(c.map);
        c.ready = false;
        if (!vk_create_buffer(c.map, (VkDeviceSize)size * size * 3 * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) return;
        vk_buffer_name(c.map, "облака: карта");
        rg_reset(c.rg_map);
        g.cl_map = c.map.address;
    }
    const RenderView &v = f.view;
    CloudSnap now;
    now.cam[0] = (f64)v.cx * 256.0 + (f64)v.x;
    now.cam[1] = (f64)v.y;
    now.cam[2] = (f64)v.cz * 256.0 + (f64)v.z;
    now.wind[0] = s.cloud_wind[0];
    now.wind[1] = s.cloud_wind[1];
    now.evolve = s.cloud_wind[2];
    for (u32 k = 0; k < 3; ++k) now.light[k] = s.cloud_light[k];
    now.light[3] = s.cloud_e;

    // Всё заново одним кадром: первый кадр, смена ступени, скачок камеры или времени; скачок светила — только
    // когда кадр — свой час (снимок, --timelapse): в игре светило идёт плавно, а смена солнца на луну
    // расходится перетеканием слотов
    bool full = !c.ready || size != c.size;
    if (!full) {
        const CloudSnap &b = c.snap_b;
        f64 dx = now.cam[0] - b.cam[0], dy = now.cam[1] - b.cam[1], dz = now.cam[2] - b.cam[2];
        f32 dl = now.light[0] * b.light[0] + now.light[1] * b.light[1] + now.light[2] * b.light[2];
        full = dx * dx + dy * dy + dz * dz > 2000.0 * 2000.0 || fabsf(now.evolve - b.evolve) > 30.0f ||
               (f.style.eye_snap && (dl < 0.9998f || fabsf(now.light[3] - b.light[3]) > 0.01f * b.light[3]));
    }
    c.job_on = true;
    c.job_sh_full = full;
    if (full) {
        c.size = size;
        c.slot_a = c.slot_b = 0;
        c.slot_w = 1;
        c.snap_a = c.snap_b = c.snap_w = now;
        c.phase = 0;
        c.job_slot = 0;
        c.job_first = true;
        c.job_phase = 0;
        c.job_n = CLOUD_PHASES;
        c.ready = true;
    } else {
        if (c.phase >= CLOUD_PHASES) {
            // Слот w собран: b уходит в a, w — в b, старый a строится заново
            u32 old_a = c.slot_a;
            c.slot_a = c.slot_b;
            c.slot_b = c.slot_w;
            c.slot_w = old_a == c.slot_a ? 3 - c.slot_a - c.slot_b : old_a;
            c.snap_a = c.snap_b;
            c.snap_b = c.snap_w;
            c.phase = 0;
        }
        if (c.phase == 0) c.snap_w = now;
        c.job_slot = c.slot_w;
        c.job_first = c.phase == 0;
        c.job_phase = c.phase;
        c.job_n = CLOUD_RATE[q] < CLOUD_PHASES - c.phase ? CLOUD_RATE[q] : CLOUD_PHASES - c.phase;
    }
    g.cl_on = 1;
    g.cl_size = c.size;
    g.cl_slot_a = c.slot_a;
    g.cl_slot_b = c.slot_b;
    g.cl_mix = full ? 1.0f : (f32)c.phase / (f32)CLOUD_PHASES;
    g.cl_steps = CLOUD_STEPS[q];
    // Средняя тень за окном карты тени: грубо по доле неба под облаками
    g.cl_mean_t = 1.0f - 0.55f * s.cloud_shape[2] - 0.08f * s.cloud_cirrus[1];
    snap_off(now, c.snap_a, g.cl_off_a);
    snap_off(now, c.snap_b, g.cl_off_b);
    const CloudSnap &bs = full ? now : c.snap_w;
    snap_vec(bs, s.alt0, g.cl_build);
    memcpy(g.cl_build_light, bs.light, sizeof g.cl_build_light);
    snap_vec(now, s.alt0, g.cl_cam);
    if (!full) c.phase += c.job_n;
}

static VkDeviceAddress g_cl_frame;

static void record_noise(VkCommandBuffer cmd, void *) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.clouds.pipe_noise);
    PushConsts pc{g_cl_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, CLOUD_N / 4, CLOUD_N / 4, CLOUD_N / 4);
}

// pc.a — слот | первая фаза слота << 8; pc.b — фаза: поток — блок 8×8 текселей, в нём один тексель фазы
static void record_map(VkCommandBuffer cmd, void *) {
    const CloudGpu &c = g_vk.clouds;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipe_map);
    u32 groups = (c.size / 8 + 7) / 8;
    for (u32 i = 0; i < c.job_n; ++i) {
        PushConsts pc{g_cl_frame, c.job_slot | (c.job_first ? 1u << 8 : 0u), c.job_phase + i};
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDispatch(cmd, groups, groups, 1);
    }
}

// pc.a — фаза: поток — блок 4×4 текселей карты тени
static void record_shadow(VkCommandBuffer cmd, void *) {
    CloudGpu &c = g_vk.clouds;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipe_shadow);
    u32 n = c.job_sh_full ? CLOUD_SH_PHASES : 1;
    for (u32 i = 0; i < n; ++i) {
        PushConsts pc{g_cl_frame, c.sh_phase, 0};
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDispatch(cmd, CLOUD_SH_N / 4 / 8, CLOUD_SH_N / 4 / 8, 1);
        c.sh_phase = (c.sh_phase + 1) % CLOUD_SH_PHASES;
    }
}

void vk_clouds_passes(VkDeviceAddress frame) {
    CloudGpu &c = g_vk.clouds;
    if (!c.job_on) return;
    g_cl_frame = frame;
    if (!c.noise_ready) {
        rg_pass("облака: шум", "облака", record_noise, nullptr);
        rg_use(c.rg_noise, RG_CS_WRITE);
        c.noise_ready = true;
    }
    const SkyGpu &s = g_vk.sky;
    RgRes sky[] = {s.rg_trans, s.rg_ms, s.rg_sun, s.rg_moon};
    rg_pass("облака: карта", "облака", record_map, nullptr);
    for (RgRes r : sky) rg_use(r, RG_CS_READ);
    rg_use(c.rg_noise, RG_CS_READ);
    rg_use(c.rg_map, RG_CS_WRITE);
    rg_use(c.rg_info, RG_CS_WRITE);
    rg_pass("облака: тень", "облака", record_shadow, nullptr);
    rg_use(c.rg_noise, RG_CS_READ);
    rg_use(c.rg_shadow, RG_CS_WRITE);
}

void vk_clouds_reads(RgUse u) {
    const CloudGpu &c = g_vk.clouds;
    rg_use(c.rg_map, u);
    rg_use(c.rg_info, u);
    rg_use(c.rg_shadow, u);
}
