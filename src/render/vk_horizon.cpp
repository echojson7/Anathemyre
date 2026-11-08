// Карта горизонта земли (docs/horizon.md): в узле сетки 8 м — угол горизонта и расстояние до заслонки в
// H_DIRS направлениях. От солнца и камеры не зависит, поэтому считается один раз на землю (horizon.comp) и
// дальше только читается: тень земли — 8 чтений на точку вместо луча по высотам
#include "vk_graph.h"
#include "../core/log.h"

constexpr u32 H_DIRS = 16;     // как в horizon.glsl
constexpr u32 H_ROWS = 64;     // строк узлов на вызов сборки

bool vk_horizon_init() {
    HorizonGpu &h = g_vk.horizon;
    h.pipe = vk_make_compute("horizon.comp");
    h.rg_map = rg_buffer("земля: горизонт");
    return h.pipe != VK_NULL_HANDLE;
}

void vk_horizon_shutdown() {
    HorizonGpu &h = g_vk.horizon;
    if (h.pipe) vkDestroyPipeline(g_vk.device, h.pipe, nullptr);
    h.pipe = VK_NULL_HANDLE;
}

// Новая земля: буфер под её сетку, высшая точка, сборка в ближайшем кадре. Буфер освобождает
// vk_terrain_shutdown (vk_horizon_free) — вместе с землёй
bool vk_horizon_terrain(const u16 *height, u32 nx, u32 nz) {
    HorizonGpu &h = g_vk.horizon;
    vk_horizon_free();
    h.nx = nx;
    h.nz = nz;
    u32 top = 0;
    for (usize i = 0; i < (usize)nx * nz; ++i) top = height[i] > top ? height[i] : top;
    h.h_max = (f32)top * 0.05f;
    VkDeviceSize bytes = (VkDeviceSize)nx * nz * (H_DIRS / 2) * 4;
    if (!vk_create_buffer(h.map, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) return false;
    vk_buffer_name(h.map, "земля: горизонт");
    rg_reset(h.rg_map);
    h.dirty = true;
    log_info("карта горизонта: %ux%u узлов, %u направлений, %.1f МБ", nx, nz, H_DIRS, (f64)bytes / 1048576.0);
    return true;
}

void vk_horizon_fill(FrameGpu &g, const RenderFrame &f) {
    const HorizonGpu &h = g_vk.horizon;
    g.h_map = h.map.address;
    g.h_on = g_vk.terrain.on && f.ground && h.map.buffer ? 1u : 0u;
    g.h_max = h.h_max;
}

static VkDeviceAddress g_h_frame;

static void record_build(VkCommandBuffer cmd, void *) {
    const HorizonGpu &h = g_vk.horizon;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, h.pipe);
    for (u32 row = 0; row < h.nz; row += H_ROWS) {
        PushConsts pc{g_h_frame, row, 0};
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdDispatch(cmd, (h.nx + 7) / 8, h.nz - row < H_ROWS ? h.nz - row : H_ROWS, 1);
    }
}

void vk_horizon_passes(VkDeviceAddress frame) {
    HorizonGpu &h = g_vk.horizon;
    if (!h.dirty || !h.map.buffer || !g_vk.terrain.on) return;
    g_h_frame = frame;
    rg_pass("земля: горизонт", "земля: горизонт", record_build, nullptr);
    rg_use(h.rg_map, RG_CS_WRITE);
    h.dirty = false;
}

void vk_horizon_reads(RgUse u) { rg_use(g_vk.horizon.rg_map, u); }

void vk_horizon_free() {
    HorizonGpu &h = g_vk.horizon;
    vk_destroy_buffer(h.map);
    h.nx = h.nz = 0;
    h.dirty = false;
}
