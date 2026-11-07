// Froxels (этап 5б, docs/frame.md «Froxels»): клетки 64×64 пикселя кадра × FROXEL_Z слоёв глубины (первый — до
// FROXEL_NEAR, дальше степенями до FROXEL_FAR, последний — до бесконечности). На клетку — списки огней и декалей,
// чьи шары её задевают (froxel.comp). Огни — вход света пользователя (light.glsl), декали читает decal.glsl.
// Буфер среды (туман и пыль на клетку) — место для света пользователя, каркас кладёт туда нули
#include "vk_state.h"
#include "../core/log.h"

bool vk_froxel_init() {
    for (FrameSlot &fs : g_vk.frames)
        if (!vk_create_buffer(fs.lights, RENDER_MAX_LIGHTS * sizeof(RenderPointLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return false;
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

void vk_froxel_record(VkCommandBuffer cmd, const FrameSlot &fs) {
    if (!g_vk.froxel_cap) return;
    const FrameGpu &g = *(const FrameGpu *)fs.frame.mapped;
    if (!g.froxel_on) return;
    u32 cells = g.froxel_x * g.froxel_y * g.froxel_z;
    // Прошлый кадр мог ещё читать списки (освещение, forward) и писать их и среду
    vk_memory_barrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(cmd, g_vk.media.buffer, 0, (VkDeviceSize)cells * 16, 0);  // среды пока нет
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.pipe_froxel);
    PushConsts pc{fs.frame.address, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, (cells + 63) / 64, 1, 1);
    vk_memory_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}
