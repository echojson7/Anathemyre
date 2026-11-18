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
// Карта собирается за CLOUD_PERIOD секунд при любой частоте кадров (17.11): фаза режется на CLOUD_BANDS полос по
// строкам, и кадр берёт столько полос, сколько задолжало время, но не больше CLOUD_RATE фаз (так было до этого:
// вся карта — за 64 кадра, на «Ультра» за 32). При 86 к/с «Ультра» прежде пересобирала карту за 0.37 с, хотя форма и
// свет облаков меняются за секунды, а сдвиг ветром показ переносит каждый кадр сам; теперь — за 1.07 с, как
// «Высоко» при 60 к/с, втрое дешевле на кадр. Ниже 60 к/с — как раньше
static constexpr u32 CLOUD_RATE[RENDER_LIGHT_QUALITIES] = {1, 1, 1, 2};  // предел фаз карты за кадр
static constexpr u32 CLOUD_BANDS = 8;
static constexpr u32 CLOUD_UNITS = CLOUD_PHASES * CLOUD_BANDS;
static constexpr f32 CLOUD_PERIOD = 64.0f / 60.0f;

// Шум — картинка RGBA8 CLOUD_N³ (8 МБ): выборка трилинейная по кругу текстурным блоком, одной инструкцией, а не
// восемь чтений буфера и смесь в шейдере (18.11). Места набора кадра мира: 22 — запись (cloud_noise.comp), 23 —
// выборка (сэмплер сканов: трилинейно, с заворотом, vk_world_layout)
static bool noise_image(CloudGpu &c) {
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_3D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {CLOUD_N, CLOUD_N, CLOUD_N};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &c.noise.image, &c.noise.alloc, nullptr), "vmaCreateImage шума облаков"))
        return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = c.noise.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
    vi.format = ii.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &c.noise.view), "vkCreateImageView шума облаков")) return false;
    vk_image_name(c.noise.image, "облака: шум");
    c.rg_noise = rg_image("облака: шум", VK_IMAGE_ASPECT_COLOR_BIT);
    rg_bind_image(c.rg_noise, c.noise.image);
    VkDescriptorImageInfo di{VK_NULL_HANDLE, c.noise.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr[2] = {};
    for (u32 i = 0; i < 2; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = g_vk.world.set;
        wr[i].dstBinding = 22 + i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[i].pImageInfo = &di;
    }
    vkUpdateDescriptorSets(g_vk.device, 2, wr, 0, nullptr);
    return true;
}

bool vk_clouds_init() {
    CloudGpu &c = g_vk.clouds;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    // Карта купола — по ступени света, при первом кадре с облаками (vk_clouds_fill): 14…100 МБ
    if (!noise_image(c) || !vk_create_buffer(c.shadow, (VkDeviceSize)CLOUD_SH_N * CLOUD_SH_N * 4, ssbo, true) ||
        !vk_create_buffer(c.info, 3 * 4 * 16, ssbo, true))
        return false;
    const char *names[] = {"облака: карта", "облака: тень", "облака: слоты"};
    GpuBuffer *bs[] = {&c.map, &c.shadow, &c.info};
    u8 *rs[] = {&c.rg_map, &c.rg_shadow, &c.rg_info};
    for (u32 i = 0; i < 3; ++i) {
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
    GpuBuffer *bs[] = {&c.map, &c.shadow, &c.info};
    for (GpuBuffer *b : bs) vk_destroy_buffer(*b);
    if (c.noise.view) vkDestroyImageView(g_vk.device, c.noise.view, nullptr);
    if (c.noise.image) vmaDestroyImage(g_vk.vma, c.noise.image, c.noise.alloc);
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
    u32 q = f.style.air_quality < RENDER_LIGHT_QUALITIES ? f.style.air_quality : RENDER_LIGHT_HIGH;
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
        c.unit = 0;
        c.unit_acc = 0.0f;
        c.job_slot = 0;
        c.job_unit = 0;
        c.job_n = CLOUD_UNITS;
        c.ready = true;
    } else {
        if (c.unit >= CLOUD_UNITS) {
            // Слот w собран: b уходит в a, w — в b, старый a строится заново
            u32 old_a = c.slot_a;
            c.slot_a = c.slot_b;
            c.slot_b = c.slot_w;
            c.slot_w = old_a == c.slot_a ? 3 - c.slot_a - c.slot_b : old_a;
            c.snap_a = c.snap_b;
            c.snap_b = c.snap_w;
            c.unit = 0;
        }
        if (c.unit == 0) c.snap_w = now;
        c.job_slot = c.slot_w;
        c.job_unit = c.unit;
        f32 dt = f.style.dt > 0.0f ? f.style.dt : 0.0f;
        u32 cap = CLOUD_RATE[q] * CLOUD_BANDS;
        c.unit_acc += dt * ((f32)CLOUD_UNITS / CLOUD_PERIOD);
        if (c.unit_acc > (f32)cap) c.unit_acc = (f32)cap;
        u32 n = (u32)c.unit_acc;
        c.unit_acc -= (f32)n;
        c.job_n = n < CLOUD_UNITS - c.unit ? n : CLOUD_UNITS - c.unit;
    }
    g.cl_on = 1;
    g.cl_size = c.size;
    g.cl_slot_a = c.slot_a;
    g.cl_slot_b = c.slot_b;
    g.cl_mix = full ? 1.0f : (f32)c.unit / (f32)CLOUD_UNITS;
    g.cl_steps = CLOUD_STEPS[q];
    // Средняя тень за окном карты тени: грубо по доле неба под облаками
    g.cl_mean_t = 1.0f - 0.55f * s.cloud_shape[2] - 0.08f * s.cloud_cirrus[1];
    snap_off(now, c.snap_a, g.cl_off_a);
    snap_off(now, c.snap_b, g.cl_off_b);
    const CloudSnap &bs = full ? now : c.snap_w;
    snap_vec(bs, s.alt0, g.cl_build);
    memcpy(g.cl_build_light, bs.light, sizeof g.cl_build_light);
    snap_vec(now, s.alt0, g.cl_cam);
    if (!full) c.unit += c.job_n;
}

static VkDeviceAddress g_cl_frame;

static void record_noise(VkCommandBuffer cmd, void *) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.clouds.pipe_noise);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    PushConsts pc{g_cl_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, CLOUD_N / 4, CLOUD_N / 4, CLOUD_N / 4);
}

// pc.a — слот | первая фаза слота << 8 | первая строка групп полосы << 16; pc.b — фаза: поток — блок 8×8 текселей,
// в нём один тексель фазы. Кусок — полоса строк групп одной фазы
static void record_map(VkCommandBuffer cmd, void *) {
    const CloudGpu &c = g_vk.clouds;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipe_map);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    u32 groups = (c.size / 8 + 7) / 8;
    for (u32 i = 0; i < c.job_n; ++i) {
        u32 u = c.job_unit + i, phase = u / CLOUD_BANDS, band = u % CLOUD_BANDS;
        u32 y0 = band * groups / CLOUD_BANDS, y1 = (band + 1) * groups / CLOUD_BANDS;
        if (y1 == y0) continue;
        PushConsts pc{g_cl_frame, c.job_slot | (phase == 0 ? 1u << 8 : 0u) | y0 << 16, phase};
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDispatch(cmd, groups, y1 - y0, 1);
    }
}

// pc.a — фаза: поток — блок 4×4 текселей карты тени
static void record_shadow(VkCommandBuffer cmd, void *) {
    CloudGpu &c = g_vk.clouds;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipe_shadow);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
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
        rg_use(c.rg_noise, RG_IMG_CS_WRITE, RG_DISCARD);
        c.noise_ready = true;
    }
    const SkyGpu &s = g_vk.sky;
    RgRes sky[] = {s.rg_trans, s.rg_ms, s.rg_sun, s.rg_moon};
    rg_pass("облака: карта", "облака", record_map, nullptr);
    for (RgRes r : sky) rg_use(r, RG_CS_READ);
    rg_use(c.rg_noise, RG_IMG_CS_SAMPLE);
    rg_use(c.rg_map, RG_CS_WRITE);
    rg_use(c.rg_info, RG_CS_WRITE);
    rg_pass("облака: тень", "облака", record_shadow, nullptr);
    rg_use(c.rg_noise, RG_IMG_CS_SAMPLE);
    rg_use(c.rg_shadow, RG_CS_WRITE);
}

void vk_clouds_reads(RgUse u) {
    const CloudGpu &c = g_vk.clouds;
    rg_use(c.rg_map, u);
    rg_use(c.rg_info, u);
    rg_use(c.rg_shadow, u);
}
