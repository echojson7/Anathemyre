// Текстуры ассетов (docs/textures.md): картинки BC с мипами в таблице набора кадра мира (привязка 16 — массив
// картинок, 17 — один сэмплер: трилинейно, анизотропия 16, с заворотом). Шейдер берёт картинку по номеру из
// материала (baked.glsl). Место 0 и все пустые — заглушка 1 × 1, поэтому таблица всегда полная
#include "vk_state.h"
#include "../asset/tex.h"
#include "../core/log.h"

static VkFormat tex_vk_format(u32 f) {
    switch (f) {
    case TEX_BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
    case TEX_BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
    case TEX_BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
    case TEX_BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
    default: return VK_FORMAT_UNDEFINED;
    }
}

static bool tex_image(GpuImage &im, VkFormat fmt, u32 w, u32 h, u32 mips, const char *name) {
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {w, h, 1};
    ii.mipLevels = mips;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &im.image, &im.alloc, nullptr), name)) return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &im.view), name)) return false;
    vk_image_name(im.image, name);
    return true;
}

static void tex_free(GpuImage &im) {
    if (im.view) vkDestroyImageView(g_vk.device, im.view, nullptr);
    if (im.image) vmaDestroyImage(g_vk.vma, im.image, im.alloc);
    im = GpuImage{};
}

struct TexUpload {
    VkBuffer staging;
    VkImage image;
    u32 width, height, mips, block;
    const usize *mip_offset;
};

static void record_tex_upload(VkCommandBuffer cmd, void *user) {
    TexUpload &u = *(TexUpload *)user;
    auto b = vk_info<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = u.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, u.mips, 0, 1};
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
    for (u32 m = 0; m < u.mips; ++m) {
        VkBufferImageCopy c{};
        c.bufferOffset = u.mip_offset ? u.mip_offset[m] : 0;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
        c.imageExtent = {u.width >> m, u.height >> m, 1};
        vkCmdCopyBufferToImage(cmd, u.staging, u.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    }
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(cmd, &di);
}

static bool tex_upload(GpuImage &im, const u8 *data, usize bytes, u32 w, u32 h, u32 mips, const usize *mip_offset) {
    GpuBuffer staging;
    if (!vk_create_buffer(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return false;
    memcpy(staging.mapped, data, bytes);
    vmaFlushAllocation(g_vk.vma, staging.alloc, 0, VK_WHOLE_SIZE);
    TexUpload u{staging.buffer, im.image, w, h, mips, 0, mip_offset};
    bool ok = vk_one_shot(record_tex_upload, &u);
    vk_destroy_buffer(staging);
    return ok;
}

// Место slot таблицы → картинка (пустое место — заглушка)
static void tex_bind(u32 first, u32 n) {
    TexturesGpu &t = g_vk.textures;
    VkDescriptorImageInfo ii[RENDER_MAX_TEXTURES + 1];
    for (u32 i = 0; i < n; ++i) {
        const GpuImage &im = t.images[first + i].view ? t.images[first + i] : t.images[0];
        ii[i] = {VK_NULL_HANDLE, im.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    auto wr = vk_info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    wr.dstSet = g_vk.world.set;
    wr.dstBinding = 16;
    wr.dstArrayElement = first;
    wr.descriptorCount = n;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    wr.pImageInfo = ii;
    vkUpdateDescriptorSets(g_vk.device, 1, &wr, 0, nullptr);
}

bool vk_textures_init() {
    TexturesGpu &t = g_vk.textures;
    const u8 grey[4] = {128, 128, 255, 255};
    if (!tex_image(t.images[0], VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, "текстуры: заглушка") ||
        !tex_upload(t.images[0], grey, 4, 1, 1, 1, nullptr))
        return false;
    t.count = 0;
    t.bytes = 0;
    tex_bind(0, RENDER_MAX_TEXTURES + 1);
    return true;
}

void vk_textures_shutdown() {
    TexturesGpu &t = g_vk.textures;
    for (GpuImage &im : t.images) tex_free(im);
    t.count = 0;
    t.bytes = 0;
    // сэмплер живёт с раскладкой набора (vk_world_shutdown)
}

u32 render_texture_create(const RenderTexture &r) {
    TexturesGpu &t = g_vk.textures;
    VkFormat fmt = tex_vk_format(r.format);
    if (!g_vk.device || fmt == VK_FORMAT_UNDEFINED || !r.mips) return 0;
    if (t.count >= RENDER_MAX_TEXTURES) { log_error("текстур больше %u", RENDER_MAX_TEXTURES); return 0; }
    vkDeviceWaitIdle(g_vk.device);  // набор кадра мира меняется: прошлые кадры его больше не читают
    u32 slot = t.count + 1;
    GpuImage &im = t.images[slot];
    if (!tex_image(im, fmt, r.width, r.height, r.mips, "текстура ассета") ||
        !tex_upload(im, r.blocks, r.bytes, r.width, r.height, r.mips, r.mip_offset)) {
        tex_free(im);
        return 0;
    }
    t.count = slot;
    t.bytes += r.bytes;
    tex_bind(slot, 1);
    return slot;
}
