// Сглаживание краёв (docs/aa.md): TAA — дрожание точки пикселя по кадрам, векторы движения (их пишет освещение) и
// смешение с историей прошлого кадра; FXAA — по краям одного кадра; выкл — копия. Один проход после освещения,
// до стиля: читает HDR кадра, пишет одну из двух историй, стиль читает её же. Здесь же — дрожание и прошлый вид
// камеры в данные кадра
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/mathf.h"

// Дрожание: точки Халтона (2, 3), 8 фаз — края ровные уже через полсекунды, а цикл короче, чем заметен глазу
constexpr u32 AA_PHASES = 8;
// Камера сдвинулась дальше — история не годна (телепорт, смена места в отладке)
constexpr f32 AA_JUMP_M = 30.0f;

static f32 halton(u32 i, u32 base) {
    f32 f = 1.0f, r = 0.0f;
    while (i > 0) {
        f /= (f32)base;
        r += f * (f32)(i % base);
        i /= base;
    }
    return r;
}

bool vk_aa_init() {
    AaGpu &a = g_vk.aa;
    a.rg_motion = rg_image("сглаживание: векторы движения", VK_IMAGE_ASPECT_COLOR_BIT);
    a.rg_hist[0] = rg_image("сглаживание: история 0", VK_IMAGE_ASPECT_COLOR_BIT);
    a.rg_hist[1] = rg_image("сглаживание: история 1", VK_IMAGE_ASPECT_COLOR_BIT);
    return vk_make_compute(&a.pipe, "aa.comp");
}

static void aa_destroy_image(GpuImage &im) {
    if (im.view) vkDestroyImageView(g_vk.device, im.view, nullptr);
    if (im.image) vmaDestroyImage(g_vk.vma, im.image, im.alloc);
    im = GpuImage{};
}

void vk_aa_shutdown() {
    AaGpu &a = g_vk.aa;
    if (a.pipe) vkDestroyPipeline(g_vk.device, a.pipe, nullptr);
    aa_destroy_image(a.motion);
    aa_destroy_image(a.hist[0]);
    aa_destroy_image(a.hist[1]);
    a = AaGpu{};
}

static bool aa_create_image(GpuImage &im, VkImageUsageFlags usage, const char *name) {
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ii.extent = {g_vk.extent.width, g_vk.extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &im.image, &im.alloc, nullptr), "vmaCreateImage сглаживания")) return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &im.view), "vkCreateImageView сглаживания")) return false;
    vk_image_name(im.image, name);
    return true;
}

// Картинки под размер кадра (зовёт vk_world при смене размера, GPU уже стоит) и места 11–13 набора кадра мира:
// 11 — векторы движения (запись освещением, чтение сглаживанием), 12 — истории для выборки, 13 — они же для записи
bool vk_aa_images(VkDescriptorSet set) {
    AaGpu &a = g_vk.aa;
    aa_destroy_image(a.motion);
    aa_destroy_image(a.hist[0]);
    aa_destroy_image(a.hist[1]);
    const VkImageUsageFlags st = VK_IMAGE_USAGE_STORAGE_BIT;
    if (!aa_create_image(a.motion, st, "сглаживание: векторы движения") ||
        !aa_create_image(a.hist[0], st | VK_IMAGE_USAGE_SAMPLED_BIT, "сглаживание: история 0") ||
        !aa_create_image(a.hist[1], st | VK_IMAGE_USAGE_SAMPLED_BIT, "сглаживание: история 1"))
        return false;
    rg_bind_image(a.rg_motion, a.motion.image);
    rg_bind_image(a.rg_hist[0], a.hist[0].image);
    rg_bind_image(a.rg_hist[1], a.hist[1].image);
    a.valid = false;
    VkDescriptorImageInfo mi{VK_NULL_HANDLE, a.motion.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo hi[2] = {{VK_NULL_HANDLE, a.hist[0].view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, a.hist[1].view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[3] = {};
    for (u32 i = 0; i < 3; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = set;
        wr[i].dstBinding = 11 + i;
    }
    wr[0].descriptorCount = 1;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    wr[0].pImageInfo = &mi;
    wr[1].descriptorCount = 2;
    wr[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr[1].pImageInfo = hi;
    wr[2].descriptorCount = 2;
    wr[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    wr[2].pImageInfo = hi;
    vkUpdateDescriptorSets(g_vk.device, 3, wr, 0, nullptr);
    return true;
}

void vk_aa_fill(FrameGpu &g, const RenderFrame &f) {
    AaGpu &a = g_vk.aa;
    const RenderView &v = f.view;
    u32 mode = f.style.aa < RENDER_AAS ? f.style.aa : RENDER_AA_TAA;
    if (++a.frame == 0) a.frame = 1;  // 0 в прошлом экземпляре — «не годен»
    // Сдвиг камеры с прошлого кадра: целые чанки отдельно, потом доли
    f32 d[3] = {0, 0, 0};
    if (a.have_prev) {
        d[0] = (f32)(v.cx - a.cx) * 256.0f + (v.x - a.x);
        d[1] = v.y - a.y;
        d[2] = (f32)(v.cz - a.cz) * 256.0f + (v.z - a.z);
    }
    bool jump = f_abs(d[0]) + f_abs(d[1]) + f_abs(d[2]) > AA_JUMP_M;
    // Картинки кадра мира пересоздадутся в этом кадре (новый размер) — истории нет
    bool resized = g_vk.world.extent.width != g_vk.extent.width || g_vk.world.extent.height != g_vk.extent.height;
    // Скачок часов: история — свет прошлого часа, рамка соседей сводит его не за один кадр (белая пелена в тенях)
    bool hist = a.valid && a.have_prev && !jump && !resized && !f.style.clock_cut && mode == a.mode && mode == RENDER_AA_TAA;
    a.cur ^= 1u;
    g.aa_mode = mode;
    g.aa_frame = a.frame;
    g.aa_hist = hist ? 1u : 0u;
    if (mode == RENDER_AA_TAA) {
        u32 k = a.frame % AA_PHASES + 1;
        g.jitter[0] = halton(k, 2) - 0.5f;
        g.jitter[1] = halton(k, 3) - 0.5f;
    }
    // Прошлый вид; без прошлого — этот же (векторы — только от движения людей)
    const f32 *pr = a.have_prev ? a.right : v.right, *pu = a.have_prev ? a.up : v.up, *pf = a.have_prev ? a.forward : v.forward;
    for (u32 i = 0; i < 3; ++i) {
        g.prev_right[i] = pr[i];
        g.prev_up[i] = pu[i];
        g.prev_forward[i] = pf[i];
        g.prev_delta[i] = d[i];
    }
    g.prev_proj[0] = a.have_prev ? a.proj[0] : g.proj_x;
    g.prev_proj[1] = a.have_prev ? a.proj[1] : g.proj_y;
    a.have_prev = true;
    a.cx = v.cx;
    a.cz = v.cz;
    a.x = v.x;
    a.y = v.y;
    a.z = v.z;
    for (u32 i = 0; i < 3; ++i) {
        a.right[i] = v.right[i];
        a.up[i] = v.up[i];
        a.forward[i] = v.forward[i];
    }
    a.proj[0] = g.proj_x;
    a.proj[1] = g.proj_y;
    a.mode = mode;
    a.valid = true;  // со следующего кадра (до смены размера картинок)
}

static void record_aa(VkCommandBuffer cmd, void *user) {
    VkDeviceAddress frame = *(const VkDeviceAddress *)user;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.aa.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    PushConsts pc{frame, g_vk.aa.cur, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, (g_vk.extent.width + 7) / 8, (g_vk.extent.height + 7) / 8, 1);
}

void vk_aa_pass(const VkDeviceAddress *frame) {
    const AaGpu &a = g_vk.aa;
    rg_pass("сглаживание", "сглаживание", record_aa, (void *)frame);
    rg_use(g_vk.world.rg_hdr, RG_IMG_CS_READ);
    rg_use(a.rg_motion, RG_IMG_CS_READ);
    rg_use(a.rg_hist[a.cur ^ 1u], RG_IMG_CS_SAMPLE);
    rg_use(a.rg_hist[a.cur], RG_IMG_CS_WRITE, RG_DISCARD);
}

void vk_aa_light_uses(bool first) { rg_use(g_vk.aa.rg_motion, RG_IMG_CS_WRITE, first ? RG_DISCARD : 0); }
void vk_aa_style_reads() { rg_use(g_vk.aa.rg_hist[g_vk.aa.cur], RG_IMG_FS_READ); }
u32 vk_aa_cur() { return g_vk.aa.cur; }
