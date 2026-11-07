// Froxels (этап 5б, docs/frame.md «Froxels»): клетки 64×64 пикселя кадра × FROXEL_Z слоёв глубины (первый — до
// FROXEL_NEAR, дальше степенями до FROXEL_FAR, последний — до бесконечности). На клетку — списки огней и декалей,
// чьи шары её задевают (froxel.comp). Огни — вход света пользователя (light.glsl), декали читает decal.glsl.
// Буфер среды (туман и пыль на клетку) — место для света пользователя, каркас кладёт туда нули
#include "vk_graph.h"
#include "../core/log.h"

bool vk_froxel_init() {
    for (FrameSlot &fs : g_vk.frames)
        if (!vk_create_buffer(fs.lights, RENDER_MAX_LIGHTS * sizeof(RenderPointLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return false;
    g_vk.rg_froxels = rg_buffer("froxels: списки");
    g_vk.rg_media = rg_buffer("froxels: среда");
    g_vk.pipe_froxel = vk_make_compute("froxel.comp");
    return g_vk.pipe_froxel != VK_NULL_HANDLE;
}

void vk_froxel_shutdown() {
    if (g_vk.pipe_froxel) vkDestroyPipeline(g_vk.device, g_vk.pipe_froxel, nullptr);
    g_vk.pipe_froxel = VK_NULL_HANDLE;
    for (FrameSlot &fs : g_vk.frames) vk_destroy_buffer(fs.lights);
    vk_destroy_buffer(g_vk.froxels);
    vk_destroy_buffer(g_vk.media);
    g_vk.froxel_cap = 0;
}

// Буферы клеток под размер кадра: растут, не сжимаются
static bool ensure_cells(u32 cells) {
    if (cells <= g_vk.froxel_cap) return true;
    vkDeviceWaitIdle(g_vk.device);
    vk_destroy_buffer(g_vk.froxels);
    vk_destroy_buffer(g_vk.media);
    const VkBufferUsageFlags u = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!vk_create_buffer(g_vk.froxels, (VkDeviceSize)cells * FROXEL_STRIDE * 4, u, true) ||
        !vk_create_buffer(g_vk.media, (VkDeviceSize)cells * 16, u, true)) {
        g_vk.froxel_cap = 0;
        return false;
    }
    vk_buffer_name(g_vk.froxels, "froxels: списки");
    vk_buffer_name(g_vk.media, "froxels: среда");
    rg_reset(g_vk.rg_froxels);
    rg_reset(g_vk.rg_media);
    g_vk.froxel_cap = cells;
    return true;
}

void vk_froxel_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs) {
    u32 fx = (g_vk.extent.width + FROXEL_TILE - 1) / FROXEL_TILE, fy = (g_vk.extent.height + FROXEL_TILE - 1) / FROXEL_TILE;
    g.froxel_on = 0;
    if (!fx || !fy || !ensure_cells(fx * fy * FROXEL_Z)) return;
    u32 n = f.lights.count < RENDER_MAX_LIGHTS ? (u32)f.lights.count : RENDER_MAX_LIGHTS;
    if (n) {
        memcpy(fs.lights.mapped, f.lights.data, n * sizeof(RenderPointLight));
        vmaFlushAllocation(g_vk.vma, fs.lights.alloc, 0, VK_WHOLE_SIZE);
    }
    g.lights = fs.lights.address;
    g.light_count = n;
    g.froxels = g_vk.froxels.address;
    g.media = g_vk.media.address;
    g.froxel_x = fx;
    g.froxel_y = fy;
    g.froxel_z = FROXEL_Z;
    g.froxel_near = FROXEL_NEAR;
    g.froxel_far = FROXEL_FAR;
    g.froxel_on = 1;
    g.froxel_heat = f.style.froxel_heat ? 1u : 0u;
}

// Среда пока нули; списки — froxel.comp. Барьеры с прошлым кадром (освещение, forward читали) ставит граф
static void record_froxels(VkCommandBuffer cmd, void *user) {
    const FrameSlot &fs = *(const FrameSlot *)user;
    const FrameGpu &g = *(const FrameGpu *)fs.frame.mapped;
    u32 cells = g.froxel_x * g.froxel_y * g.froxel_z;
    vkCmdFillBuffer(cmd, g_vk.media.buffer, 0, (VkDeviceSize)cells * 16, 0);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.pipe_froxel);
    PushConsts pc{fs.frame.address, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, (cells + 63) / 64, 1, 1);
}

void vk_froxel_passes(const FrameSlot &fs) {
    if (!g_vk.froxel_cap || !((const FrameGpu *)fs.frame.mapped)->froxel_on) return;
    rg_pass("froxels", "froxels", record_froxels, (void *)&fs);
    rg_use(g_vk.rg_media, RG_COPY_WRITE);
    rg_use(g_vk.rg_froxels, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
}

void vk_froxel_reads(RgUse u) {
    rg_use(g_vk.rg_froxels, u);
    rg_use(g_vk.rg_media, u);
}
