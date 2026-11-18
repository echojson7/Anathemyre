// Воздух (шаг 5б, docs/air.md): пыль у земли и в залах светится — лучи солнца в проёмах и за предметами, свет
// огней и непрямой свет зондов в пыли. Клетки — плитки AIR_TILE пикселей × AIR_NZ слоёв до AIR_FAR по расстоянию
// вдоль луча (air_cells.comp: пыль, огни с тенью из их карт, зонды, видимость солнца по земле и облакам; заодно
// заявки страниц теней для лучей солнца), сумма вдоль луча (air_sum.comp). Солнце — лучом на пиксель половины кадра
// с тенью из страниц (air_march.comp) и размытием по глубине (air_blur.comp). С кадром складывает освещение
// (light_cells.comp, air_apply). Расстояния половины кадра и дальний край плиток воздуха пишет разбор плиток
#include "vk_graph.h"
#include "../core/log.h"
#include <math.h>

bool vk_air_init() {
    AirGpu &a = g_vk.air;
    a.rg_sum = rg_image("воздух: сумма", VK_IMAGE_ASPECT_COLOR_BIT);
    a.rg_sun = rg_image("воздух: клетки солнца", VK_IMAGE_ASPECT_COLOR_BIT);
    a.rg_depth = rg_buffer("воздух: расстояния");
    a.rg_ray = rg_buffer("воздух: свет солнца");
    a.rg_tmp = rg_buffer("воздух: размытие");
    a.rg_tmax = rg_buffer("воздух: край плиток");
    for (GpuBuffer &b : a.boxes)
        if (!vk_create_buffer(b, RENDER_MAX_AIR_BOXES * sizeof(RenderAirBox), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return false;
    bool ok = vk_make_compute(&a.pipe_cells, "air_cells.comp");
    ok = vk_make_compute(&a.pipe_sum, "air_sum.comp") && ok;
    ok = vk_make_compute(&a.pipe_march, "air_march.comp") && ok;
    return vk_make_compute(&a.pipe_blur, "air_blur.comp") && ok;
}

static void air_destroy_image(GpuImage &im) {
    if (im.view) vkDestroyImageView(g_vk.device, im.view, nullptr);
    if (im.image) vmaDestroyImage(g_vk.vma, im.image, im.alloc);
    im = GpuImage{};
}

static void air_free() {
    AirGpu &a = g_vk.air;
    air_destroy_image(a.sum);
    air_destroy_image(a.sun);
    vk_destroy_buffer(a.depth);
    vk_destroy_buffer(a.ray);
    vk_destroy_buffer(a.tmp);
    vk_destroy_buffer(a.tmax);
}

void vk_air_shutdown() {
    AirGpu &a = g_vk.air;
    VkPipeline ps[] = {a.pipe_cells, a.pipe_sum, a.pipe_march, a.pipe_blur};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    air_free();
    for (GpuBuffer &b : a.boxes) vk_destroy_buffer(b);
    a = AirGpu{};
}

static bool air_image(GpuImage &im, u32 nx, u32 ny, const char *name) {
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_3D;
    ii.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ii.extent = {nx, ny, AIR_NZ};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &im.image, &im.alloc, nullptr), "vmaCreateImage воздуха")) return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
    vi.format = ii.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &im.view), "vkCreateImageView воздуха")) return false;
    vk_image_name(im.image, name);
    return true;
}

// Картинки и буферы под размер кадра (зовёт vk_world при смене размера, GPU уже стоит) и места 18–21 набора кадра
// мира: 18, 19 — сумма и клетки солнца для записи, 20, 21 — они же для выборки (трилинейно, по краю)
bool vk_air_images(VkDescriptorSet set) {
    AirGpu &a = g_vk.air;
    air_free();
    u32 w = g_vk.extent.width, h = g_vk.extent.height;
    a.nx = (w + AIR_TILE - 1) / AIR_TILE;
    a.ny = (h + AIR_TILE - 1) / AIR_TILE;
    a.w2 = (w + 1) / 2;
    a.h2 = (h + 1) / 2;
    if (!a.nx || !a.ny) return false;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkDeviceSize half = (VkDeviceSize)a.w2 * a.h2;
    if (!air_image(a.sum, a.nx, a.ny, "воздух: сумма") || !air_image(a.sun, a.nx, a.ny, "воздух: клетки солнца") ||
        !vk_create_buffer(a.depth, half * 4, ssbo, true) || !vk_create_buffer(a.ray, half * 8, ssbo, true) ||
        !vk_create_buffer(a.tmp, half * 8, ssbo, true) ||
        !vk_create_buffer(a.tmax, (VkDeviceSize)a.nx * a.ny * 4, ssbo | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
        return false;
    vk_buffer_name(a.depth, "воздух: расстояния");
    vk_buffer_name(a.ray, "воздух: свет солнца");
    vk_buffer_name(a.tmp, "воздух: размытие");
    vk_buffer_name(a.tmax, "воздух: край плиток");
    rg_bind_image(a.rg_sum, a.sum.image);
    rg_bind_image(a.rg_sun, a.sun.image);
    RgRes bufs[] = {a.rg_depth, a.rg_ray, a.rg_tmp, a.rg_tmax};
    for (RgRes r : bufs) rg_reset(r);
    VkDescriptorImageInfo ii[2] = {{VK_NULL_HANDLE, a.sum.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, a.sun.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[4] = {};
    for (u32 i = 0; i < 4; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = set;
        wr[i].dstBinding = 18 + i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[i].pImageInfo = &ii[i & 1];
    }
    vkUpdateDescriptorSets(g_vk.device, 4, wr, 0, nullptr);
    return true;
}

// Воздух кадра: выкл — по F3 и ключу, на «Низко», без пыли и коробок, без картинок. Шагов луча солнца — по ступени
void vk_air_fill(FrameGpu &g, const RenderFrame &f, bool images) {
    AirGpu &a = g_vk.air;
    u32 q = f.style.light_quality;
    u32 nb = f.air.boxes.count < RENDER_MAX_AIR_BOXES ? (u32)f.air.boxes.count : RENDER_MAX_AIR_BOXES;
    a.on = images && f.style.air && a.sum.image && q != RENDER_LIGHT_LOW && (f.air.dust > 0.0f || nb > 0);
    g.air_on = a.on ? 1u : 0u;
    if (!a.on) return;
    GpuBuffer &bb = a.boxes[g_vk.frame_index];
    if (nb) {
        memcpy(bb.mapped, f.air.boxes.data, nb * sizeof(RenderAirBox));
        vmaFlushAllocation(g_vk.vma, bb.alloc, 0, VK_WHOLE_SIZE);
    }
    g.air_boxes = bb.address;
    g.air_depth = a.depth.address;
    g.air_ray = a.ray.address;
    g.air_tmp = a.tmp.address;
    g.air_tmax = a.tmax.address;
    g.air_boxes_n = nb;
    g.air_steps = q == RENDER_LIGHT_MEDIUM ? 16u : q == RENDER_LIGHT_HIGH ? 24u : 32u;
    g.air_w2 = a.w2;
    g.air_h2 = a.h2;
    g.air_nx = a.nx;
    g.air_ny = a.ny;
    g.air_far = AIR_FAR;
    g.air_lnk = logf(1.0f + AIR_FAR / AIR_A);
    g.air_dust = f.air.dust;
    g.air_dust_h = f.air.dust_h > 1.0f ? f.air.dust_h : 1.0f;
    for (u32 k = 0; k < 3; ++k) g.air_tint[k] = f.air.tint[k];
    g.air_g = f.air.g;
    g.air_iso = f.air.iso;
}

bool vk_air_on() { return g_vk.air.on; }

// Дальний край плиток воздуха — с нуля (максимум по пикселям пишет разбор плиток); в проходе обнуления плиток
void vk_air_clear(VkCommandBuffer cmd) {
    const AirGpu &a = g_vk.air;
    if (a.on) vkCmdFillBuffer(cmd, a.tmax.buffer, 0, VK_WHOLE_SIZE, 0);
}
void vk_air_clear_uses() {
    if (g_vk.air.on) rg_use(g_vk.air.rg_tmax, RG_COPY_WRITE);
}
void vk_air_tiles_uses() {
    const AirGpu &a = g_vk.air;
    if (!a.on) return;
    rg_use(a.rg_depth, RG_CS_WRITE);
    rg_use(a.rg_tmax, RG_CS_WRITE);
}

static void air_dispatch(VkCommandBuffer cmd, VkPipeline p, VkDeviceAddress frame, u32 gx, u32 gy, u32 gz) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    PushConsts pc{frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, gx, gy, gz);
}
static void air_record_cells(VkCommandBuffer cmd, void *user) {
    const AirGpu &a = g_vk.air;
    air_dispatch(cmd, a.pipe_cells, *(const VkDeviceAddress *)user, (a.nx + 3) / 4, (a.ny + 3) / 4, AIR_NZ / 4);
}
static void air_record_sum(VkCommandBuffer cmd, void *user) {
    const AirGpu &a = g_vk.air;
    air_dispatch(cmd, a.pipe_sum, *(const VkDeviceAddress *)user, (a.nx + 7) / 8, (a.ny + 7) / 8, 1);
}
static void air_record_march(VkCommandBuffer cmd, void *user) {
    const AirGpu &a = g_vk.air;
    air_dispatch(cmd, a.pipe_march, *(const VkDeviceAddress *)user, (a.w2 + 7) / 8, (a.h2 + 7) / 8, 1);
}
static void air_record_blur(VkCommandBuffer cmd, void *user) {
    const AirGpu &a = g_vk.air;
    air_dispatch(cmd, a.pipe_blur, *(const VkDeviceAddress *)user, (a.w2 + 7) / 8, (a.h2 + 7) / 8, 1);
}

// Клетки и сумма: после разбора плиток (дальний край плиток) и до выдачи страниц теней (заявки лучей солнца)
void vk_air_cells_passes(const VkDeviceAddress *frame) {
    const AirGpu &a = g_vk.air;
    if (!a.on) return;
    rg_pass("воздух: клетки", "воздух", air_record_cells, (void *)frame);
    rg_use(a.rg_tmax, RG_CS_READ);
    rg_use(a.rg_sum, RG_IMG_CS_WRITE, RG_DISCARD);
    rg_use(a.rg_sun, RG_IMG_CS_WRITE, RG_DISCARD);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_shadow_tiles_uses();
    vk_sky_reads(RG_CS_READ);
    vk_froxel_reads(RG_CS_READ);
    vk_horizon_reads(RG_CS_READ);
    vk_sdf_reads();
    vk_lsm_reads();
    vk_gi_reads();
    rg_pass("воздух: сумма", "воздух", air_record_sum, (void *)frame);
    rg_use(a.rg_sum, RG_IMG_CS_READ);
    rg_use(a.rg_sum, RG_IMG_CS_WRITE);
    rg_use(a.rg_sun, RG_IMG_CS_READ);
    rg_use(a.rg_sun, RG_IMG_CS_WRITE);
}

// Свет солнца лучами и размытие: после отрисовки страниц теней этого кадра, до освещения
void vk_air_march_passes(const VkDeviceAddress *frame) {
    const AirGpu &a = g_vk.air;
    if (!a.on) return;
    rg_pass("воздух: лучи солнца", "воздух", air_record_march, (void *)frame);
    rg_use(a.rg_sun, RG_IMG_CS_SAMPLE);
    rg_use(a.rg_depth, RG_CS_READ);
    rg_use(a.rg_ray, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_sky_reads(RG_CS_READ);
    vk_shadow_reads();
    rg_pass("воздух: размытие", "воздух", air_record_blur, (void *)frame);
    rg_use(a.rg_ray, RG_CS_READ);
    rg_use(a.rg_depth, RG_CS_READ);
    rg_use(a.rg_tmp, RG_CS_WRITE);
}

// Что читает освещение: сумма клеток, размытый свет солнца и расстояния половины кадра (сложение с кадром)
void vk_air_reads() {
    const AirGpu &a = g_vk.air;
    if (!a.on) return;
    rg_use(a.rg_sum, RG_IMG_CS_SAMPLE);
    rg_use(a.rg_tmp, RG_CS_READ);
    rg_use(a.rg_depth, RG_CS_READ);
}
