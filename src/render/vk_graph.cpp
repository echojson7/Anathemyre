// Граф кадра (vk_graph.h, docs/graph.md). Барьер считается по состоянию ресурса:
// — запись после чего угодно: ждать прошлую запись и всех читавших после неё (WAW и WAR);
// — чтение после записи: сделать запись видимой этой стадии и этому доступу, если ещё не сделана (RAW);
// — чтение после чтения: ничего, только запомнить стадию (для будущей записи);
// — смена раскладки картинки: свой барьер картинки, переход считается записью на стадии нового доступа.
// Буферы и картинки без смены раскладки сливаются в один барьер памяти на проход
#include "vk_graph.h"
#include "../core/log.h"
#include <string.h>

namespace {

struct UseInfo {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;  // для буферов не важна
    bool write;
};

constexpr VkPipelineStageFlags2 CS = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkAccessFlags2 SR = VK_ACCESS_2_SHADER_STORAGE_READ_BIT, SW = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
// Копия и заливка — ALL_TRANSFER: слой проверки синхронизации относит vkCmdFillBuffer к стадии копии
constexpr VkPipelineStageFlags2 XFER = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
constexpr VkPipelineStageFlags2 TESTS =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

const UseInfo USES[RG_USES] = {
    {CS, SR, VK_IMAGE_LAYOUT_UNDEFINED, false},                                               // RG_CS_READ
    {CS, SR | SW, VK_IMAGE_LAYOUT_UNDEFINED, true},                                           // RG_CS_WRITE
    {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, SR, VK_IMAGE_LAYOUT_UNDEFINED, false},            // RG_VS_READ
    {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, SR, VK_IMAGE_LAYOUT_UNDEFINED, false},          // RG_FS_READ
    {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, false},
    {XFER, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, false},                  // RG_COPY_READ
    {XFER, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, true},                  // RG_COPY_WRITE
    {CS, SR, VK_IMAGE_LAYOUT_GENERAL, false},                                                 // RG_IMG_CS_READ
    {CS, SW, VK_IMAGE_LAYOUT_GENERAL, true},                                                  // RG_IMG_CS_WRITE
    {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, SR, VK_IMAGE_LAYOUT_GENERAL, false},            // RG_IMG_FS_READ
    {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true},                                         // RG_COLOR
    {TESTS, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, true},                                         // RG_DEPTH
    {XFER, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false},       // RG_IMG_COPY_READ
    {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, false},        // RG_PRESENT
};
constexpr VkAccessFlags2 WRITE_BITS = SW | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

struct Res {
    const char *name = nullptr;
    bool image = false;
    VkImageAspectFlags aspect = 0;
    VkImage handle = VK_NULL_HANDLE;
    // Последняя запись (или смена раскладки) и кому она уже видна; стадии чтений после неё
    VkPipelineStageFlags2 wstage = 0;
    VkAccessFlags2 waccess = 0;
    VkPipelineStageFlags2 vis_stage = 0;
    VkAccessFlags2 vis_access = 0;
    VkPipelineStageFlags2 rstages = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Use {
    RgRes res;
    RgUse use;
    u32 flags;
};

struct Pass {
    const char *name, *timer;
    RgRecord rec;
    void *user;
    u32 first_use, use_count;
};

Res g_res[RG_MAX_RES];
u32 g_nres = 1;  // 0 — «нет ресурса»
Pass g_pass[RG_MAX_PASSES];
u32 g_npass = 0;
Use g_use[RG_MAX_PASSES * 4];
u32 g_nuse = 0;
u32 g_barriers = 0;
bool g_overflow_logged = false;

RgRes add(const char *name, bool image, VkImageAspectFlags aspect) {
    if (g_nres >= RG_MAX_RES) {
        log_error("граф кадра: ресурсов больше %u (%s)", RG_MAX_RES, name);
        return 0;
    }
    Res &r = g_res[g_nres];
    r = Res{};
    r.name = name;
    r.image = image;
    r.aspect = aspect;
    return (RgRes)g_nres++;
}

void forget(Res &r) {
    r.wstage = r.vis_stage = r.rstages = 0;
    r.waccess = r.vis_access = 0;
    r.layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

bool same(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }

}  // namespace

RgRes rg_buffer(const char *name) { return add(name, false, 0); }
RgRes rg_image(const char *name, VkImageAspectFlags aspect) { return add(name, true, aspect); }

void rg_bind_image(RgRes r, VkImage img) {
    if (!r) return;
    g_res[r].handle = img;
    forget(g_res[r]);
}

void rg_reset(RgRes r) {
    if (r) forget(g_res[r]);
}

void rg_import_swapchain(RgRes r, VkImage img) {
    if (!r) return;
    Res &s = g_res[r];
    s.handle = img;
    forget(s);
    // Семафор забора ждут на стадии вывода цвета: первый барьер картинки — от неё
    s.wstage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
}

void rg_clear() {
    g_nres = 1;
    g_npass = g_nuse = 0;
}

void rg_begin() {
    g_npass = g_nuse = 0;
}

void rg_pass(const char *name, const char *timer, RgRecord rec, void *user) {
    if (g_npass >= RG_MAX_PASSES) {
        if (!g_overflow_logged) log_error("граф кадра: проходов больше %u (%s)", RG_MAX_PASSES, name);
        g_overflow_logged = true;
        return;
    }
    g_pass[g_npass++] = Pass{name, timer, rec, user, g_nuse, 0};
}

void rg_use(RgRes r, RgUse u, u32 flags) {
    if (!r || !g_npass) return;
    Pass &p = g_pass[g_npass - 1];
    if (p.use_count >= RG_MAX_USES || g_nuse >= ANA_COUNTOF(g_use)) {
        log_error("граф кадра: у прохода %s слишком много ресурсов", p.name);
        return;
    }
    g_use[g_nuse++] = Use{r, u, flags};
    p.use_count++;
}

u32 rg_barriers() { return g_barriers; }

// Барьеры перед проходом: сливает буферы в один барьер памяти, картинкам со сменой раскладки — свой
static void pass_barriers(VkCommandBuffer cmd, const Pass &p) {
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    VkImageMemoryBarrier2 ib[RG_MAX_USES];
    u32 nib = 0;
    for (u32 k = 0; k < p.use_count; ++k) {
        const Use &u = g_use[p.first_use + k];
        Res &r = g_res[u.res];
        const UseInfo &info = USES[u.use];
        if (r.image && r.layout != info.layout) {
            VkImageMemoryBarrier2 &b = ib[nib++];
            b = vk_info<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
            b.srcStageMask = r.wstage | r.rstages;
            b.srcAccessMask = r.waccess;
            b.dstStageMask = info.stage;
            b.dstAccessMask = info.access;
            b.oldLayout = (u.flags & RG_DISCARD) ? VK_IMAGE_LAYOUT_UNDEFINED : r.layout;
            b.newLayout = info.layout;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = r.handle;
            b.subresourceRange = {r.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
            // Переход — запись на стадии нового доступа: следующим он виден только после барьера от неё
            r.layout = info.layout;
            r.wstage = info.stage;
            r.waccess = info.access & WRITE_BITS;
            r.vis_stage = info.write ? 0 : info.stage;
            r.vis_access = info.write ? 0 : info.access;
            r.rstages = 0;
            continue;
        }
        if (info.write) {
            VkPipelineStageFlags2 src = r.wstage | r.rstages;
            if (src) {
                mb.srcStageMask |= src;
                mb.srcAccessMask |= r.waccess;
                mb.dstStageMask |= info.stage;
                mb.dstAccessMask |= info.access;
            }
            r.wstage = info.stage;
            r.waccess = info.access & WRITE_BITS;
            r.vis_stage = 0;  // свою запись следующие не видят без барьера, даже на той же стадии
            r.vis_access = 0;
            r.rstages = 0;
        } else {
            bool seen = (info.stage & ~r.vis_stage) == 0 && (info.access & ~r.vis_access) == 0;
            if (r.wstage && !seen) {
                mb.srcStageMask |= r.wstage;
                mb.srcAccessMask |= r.waccess;
                mb.dstStageMask |= info.stage;
                mb.dstAccessMask |= info.access;
                r.vis_stage |= info.stage;
                r.vis_access |= info.access;
            }
            r.rstages |= info.stage;
        }
    }
    if (!mb.srcStageMask && !nib) return;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    if (mb.srcStageMask) {
        di.memoryBarrierCount = 1;
        di.pMemoryBarriers = &mb;
    }
    di.imageMemoryBarrierCount = nib;
    di.pImageMemoryBarriers = ib;
    vkCmdPipelineBarrier2(cmd, &di);
    g_barriers++;
}

void rg_execute(VkCommandBuffer cmd) {
    g_barriers = 0;
    vk_mark(cmd, nullptr);
    for (u32 i = 0; i < g_npass; ++i) {
        const Pass &p = g_pass[i];
        vk_diag_begin(cmd, i, p.name);
        pass_barriers(cmd, p);
        if (p.rec) p.rec(cmd, p.user);
        vk_diag_end(cmd, i);
        if (p.timer && (i + 1 == g_npass || !same(g_pass[i + 1].timer, p.timer))) vk_mark(cmd, p.timer);
    }
    // Состав кадра сменился (первый кадр, forward, пересчёт таблиц неба, снимок) — в журнал
    static u32 s_npass = 0, s_barriers = 0;
    if (g_npass != s_npass || g_barriers != s_barriers) {
        log_info("граф кадра: проходов %u, барьеров %u", g_npass, g_barriers);
        s_npass = g_npass;
        s_barriers = g_barriers;
    }
}
