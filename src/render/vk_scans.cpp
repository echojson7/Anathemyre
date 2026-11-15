// Сканы CC0 (docs/scans.md): две картинки-массива на все слои, выборка — в surface() по трём осям мира.
// Без пакета лежит заглушка 1 × 1 (набор кадра мира должен быть полным), FrameData.scans остаётся 0
#include "vk_graph.h"
#include "../core/log.h"

static bool scans_image(GpuImage &im, VkFormat fmt, u32 size, u32 mips, u32 layers, const char *name) {
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {size, size, 1};
    ii.mipLevels = mips;
    ii.arrayLayers = layers;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &im.image, &im.alloc, nullptr), name)) return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, layers};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &im.view), name)) return false;
    vk_image_name(im.image, name);
    return true;
}

static void scans_free(ScansGpu &s) {
    GpuImage *ims[] = {&s.albedo, &s.normal};
    for (GpuImage *im : ims) {
        if (im->view) vkDestroyImageView(g_vk.device, im->view, nullptr);
        if (im->image) vmaDestroyImage(g_vk.vma, im->image, im->alloc);
        *im = GpuImage{};
    }
    s.layers = 0;
}

struct ScansUpload {
    VkBuffer staging;
    VkImage images[2];
    VkDeviceSize array_bytes;
    u32 size, mips, layers;
};

static void record_scans_upload(VkCommandBuffer cmd, void *user) {
    ScansUpload &u = *(ScansUpload *)user;
    VkImageMemoryBarrier2 b[2] = {};
    for (u32 k = 0; k < 2; ++k) {
        b[k] = vk_info<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
        b[k].dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b[k].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b[k].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b[k].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b[k].srcQueueFamilyIndex = b[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[k].image = u.images[k];
        b[k].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, u.mips, 0, u.layers};
    }
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.imageMemoryBarrierCount = 2;
    di.pImageMemoryBarriers = b;
    vkCmdPipelineBarrier2(cmd, &di);
    for (u32 k = 0; k < 2; ++k) {
        VkDeviceSize off = k * u.array_bytes;
        for (u32 m = 0; m < u.mips; ++m) {
            u32 s = u.size >> m;
            VkBufferImageCopy c{};
            c.bufferOffset = off;
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, u.layers};
            c.imageExtent = {s, s, 1};
            vkCmdCopyBufferToImage(cmd, u.staging, u.images[k], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            off += (VkDeviceSize)s * s * 4 * u.layers;
        }
    }
    for (u32 k = 0; k < 2; ++k) {
        b[k].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b[k].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b[k].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b[k].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b[k].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b[k].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    vkCmdPipelineBarrier2(cmd, &di);
}

// Новые картинки в набор кадра мира: 4 — цвет, 5 — нормаль (сэмплер неизменный, vk_world_layout)
static void scans_bind() {
    VkDescriptorImageInfo ii[2] = {{VK_NULL_HANDLE, g_vk.scans.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                   {VK_NULL_HANDLE, g_vk.scans.normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet wr[2] = {};
    for (u32 i = 0; i < 2; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = g_vk.world.set;
        wr[i].dstBinding = 4 + i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(g_vk.device, 2, wr, 0, nullptr);
}

// Картинки нужного размера, загрузка из data (оба массива подряд), привязка. false — старое уже снято
static bool scans_load(u32 layers, u32 size, u32 mips, const u8 *albedo, const u8 *normal, usize array_bytes) {
    ScansGpu &s = g_vk.scans;
    scans_free(s);
    if (!scans_image(s.albedo, VK_FORMAT_R8G8B8A8_SRGB, size, mips, layers, "сканы: цвет") ||
        !scans_image(s.normal, VK_FORMAT_R8G8B8A8_UNORM, size, mips, layers, "сканы: нормаль"))
        return false;
    GpuBuffer staging;
    if (!vk_create_buffer(staging, 2 * (VkDeviceSize)array_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return false;
    memcpy(staging.mapped, albedo, array_bytes);
    memcpy((u8 *)staging.mapped + array_bytes, normal, array_bytes);
    vmaFlushAllocation(g_vk.vma, staging.alloc, 0, VK_WHOLE_SIZE);
    ScansUpload u{staging.buffer, {s.albedo.image, s.normal.image}, array_bytes, size, mips, layers};
    bool ok = vk_one_shot(record_scans_upload, &u);
    vk_destroy_buffer(staging);
    if (ok) scans_bind();
    return ok;
}

bool vk_scans_init() {
    // Заглушка: серый цвет, AO 1, нормаль «вверх», шероховатость 1 — не видна, пока FrameData.scans = 0
    const u8 a[4] = {128, 128, 128, 255}, n[4] = {128, 128, 255, 255};
    return scans_load(1, 1, 1, a, n, 4);
}

void vk_scans_shutdown() {
    VkSampler sm = g_vk.scans.sampler;  // сэмплер живёт с раскладкой набора (vk_world_shutdown)
    scans_free(g_vk.scans);
    g_vk.scans = ScansGpu{};
    g_vk.scans.sampler = sm;
}

bool render_set_scans(const RenderScans &r) {
    if (!g_vk.device || !r.layers) return false;
    vkDeviceWaitIdle(g_vk.device);
    bool ok = scans_load(r.layers, r.size, r.mips, r.albedo, r.normal, r.array_bytes);
    if (!ok) {
        log_error("сканы: не загрузились в видеопамять, материалы остаются узором");
        vk_scans_init();
        return false;
    }
    g_vk.scans.layers = r.layers;
    log_info("сканы: %u слоёв %u × %u, мипов %u, %.1f МБ видеопамяти", r.layers, r.size, r.size, r.mips,
             (f64)(2 * r.array_bytes) / (1 << 20));
    return true;
}
