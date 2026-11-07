// Земля в рендере (docs/terrain.md): сетка высот и маски — в видеопамять один раз, буферы индексов колец
// clipmap'а строятся при загрузке, начала колец считаются по камере каждый кадр. Высоты считает вершинный
// шейдер (data/shaders/ground.vert) той же целой функцией, что ground_at() у шага мира
#include "vk_state.h"
#include "../core/log.h"
#include "../platform/platform.h"

static inline i32 floor_div(i32 a, i32 b) {
    i32 q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

struct TerrainUpload {
    VkBuffer staging;
    VkBufferCopy h, m, w, idx, m2, du;
};

static void record_terrain_upload(VkCommandBuffer cmd, void *user) {
    TerrainUpload &u = *(TerrainUpload *)user;
    TerrainGpu &t = g_vk.terrain;
    vkCmdCopyBuffer(cmd, u.staging, t.height.buffer, 1, &u.h);
    vkCmdCopyBuffer(cmd, u.staging, t.mask.buffer, 1, &u.m);
    vkCmdCopyBuffer(cmd, u.staging, t.water.buffer, 1, &u.w);
    vkCmdCopyBuffer(cmd, u.staging, t.index.buffer, 1, &u.idx);
    vkCmdCopyBuffer(cmd, u.staging, t.mask2.buffer, 1, &u.m2);
    vkCmdCopyBuffer(cmd, u.staging, t.dune.buffer, 1, &u.du);
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                      VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

struct FineUpload {
    VkBuffer src, dst;
    VkDeviceSize bytes;
};

static void record_fine_upload(VkCommandBuffer cmd, void *user) {
    FineUpload &u = *(FineUpload *)user;
    VkBufferCopy c{0, 0, u.bytes};
    vkCmdCopyBuffer(cmd, u.src, u.dst, 1, &c);
    auto mb = vk_info<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    auto di = vk_info<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

// Уровень k сглаженной сетки из k−1: шаг вдвое, фильтр (1 2 1)/4 по каждой оси, край — повтор крайнего узла.
// Дальние кольца берут высоты отсюда, а не точками сетки 8 м: иначе обрывы уступов пилятся зубцами
static void mip_down(const u16 *src, u32 sx, u32 sz, u16 *dst, u32 dx, u32 dz) {
    for (u32 j = 0; j < dz; ++j)
        for (u32 i = 0; i < dx; ++i) {
            u32 sum = 0;
            for (i32 b = -1; b <= 1; ++b)
                for (i32 a = -1; a <= 1; ++a) {
                    i32 x = (i32)(2 * i) + a, z = (i32)(2 * j) + b;
                    x = x < 0 ? 0 : (x >= (i32)sx ? (i32)sx - 1 : x);
                    z = z < 0 ? 0 : (z >= (i32)sz ? (i32)sz - 1 : z);
                    sum += (u32)src[(u32)z * sx + (u32)x] * (u32)((2 - (a < 0 ? -a : a)) * (2 - (b < 0 ? -b : b)));
                }
            dst[j * dx + i] = (u16)((sum + 8) / 16);
        }
}

// Квадраты сетки (2n)² без дыры [hx, hx+n)×[hz, hz+n) (hx < 0 — без дыры), по два треугольника против часовой
// стрелки сверху: (00, 01, 11), (00, 11, 10)
static u32 *write_grid(u32 *out, i32 hx, i32 hz) {
    const i32 n2 = 2 * CLIP_N, row = n2 + 1;
    for (i32 j = 0; j < n2; ++j)
        for (i32 i = 0; i < n2; ++i) {
            if (hx >= 0 && i >= hx && i < hx + CLIP_N && j >= hz && j < hz + CLIP_N) continue;
            u32 v00 = (u32)(j * row + i), v10 = v00 + 1, v01 = v00 + (u32)row, v11 = v01 + 1;
            *out++ = v00; *out++ = v01; *out++ = v11;
            *out++ = v00; *out++ = v11; *out++ = v10;
        }
    return out;
}

bool render_terrain_set(const RenderTerrain &d) {
    TerrainGpu &t = g_vk.terrain;
    vkDeviceWaitIdle(g_vk.device);
    vk_terrain_shutdown();
    t.desc = d;
    g_vk.gcache.terrain_gen++;  // тексели кеша земли — от старой земли
    // Сетка высот и её сглаженные уровни подряд, каждый с чётного номера u16 (шейдер читает пары из u32)
    u32 mip_x[TERRAIN_MIPS], mip_z[TERRAIN_MIPS];
    VkDeviceSize hcount = 0;
    for (i32 k = 0; k < TERRAIN_MIPS; ++k) {
        mip_x[k] = ((d.nx - 1) >> k) + 1;
        mip_z[k] = ((d.nz - 1) >> k) + 1;
        t.mip_off[k] = (i32)hcount;
        hcount += ((VkDeviceSize)mip_x[k] * mip_z[k] + 1) & ~(VkDeviceSize)1;
    }
    const usize nodes = (usize)d.nx * d.nz, cells = (usize)d.nmx * d.nmz;
    VkDeviceSize hbytes = hcount * 2;
    VkDeviceSize mbytes = (VkDeviceSize)cells * 4;
    VkDeviceSize wbytes = (((VkDeviceSize)cells + 1) / 2) * 4;
    VkDeviceSize dbytes = (((VkDeviceSize)nodes + 1) / 2) * 4;
    t.count_full = (u32)(4 * CLIP_N * CLIP_N * 6);
    t.count_ring = (u32)(3 * CLIP_N * CLIP_N * 6);
    VkDeviceSize ibytes = ((VkDeviceSize)t.count_full + 4ull * t.count_ring) * 4;
    const VkBufferUsageFlags dst = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!vk_create_buffer(t.height, hbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true) ||
        !vk_create_buffer(t.mask, mbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true) ||
        !vk_create_buffer(t.water, wbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true) ||
        !vk_create_buffer(t.index, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true) ||
        !vk_create_buffer(t.mask2, mbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true) ||
        !vk_create_buffer(t.dune, dbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true))
        return false;

    // Загрузка: временный буфер (только при загрузке, не в игровом цикле)
    GpuBuffer staging;
    const VkDeviceSize o_m = hbytes, o_w = o_m + mbytes, o_i = o_w + wbytes, o_m2 = o_i + ibytes, o_d = o_m2 + mbytes;
    if (!vk_create_buffer(staging, o_d + dbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return false;
    u8 *p = (u8 *)staging.mapped;
    memset(p, 0, o_d + dbytes);
    memcpy(p, d.height, nodes * sizeof(u16));
    {
        // Уровни считаются в обычной памяти: читать из отображённой памяти загрузки медленно
        u16 *tmp = (u16 *)platform_alloc((usize)hbytes);
        if (!tmp) { vk_destroy_buffer(staging); return false; }
        memcpy(tmp, d.height, nodes * sizeof(u16));
        for (i32 k = 1; k < TERRAIN_MIPS; ++k)
            mip_down(tmp + t.mip_off[k - 1], mip_x[k - 1], mip_z[k - 1], tmp + t.mip_off[k], mip_x[k], mip_z[k]);
        memcpy(p + (usize)t.mip_off[1] * 2, tmp + t.mip_off[1], (usize)(hcount - (VkDeviceSize)t.mip_off[1]) * 2);
        platform_free(tmp);
    }
    memcpy(p + o_m, d.mask, cells * sizeof(u32));
    memcpy(p + o_w, d.water, cells * sizeof(u16));
    memcpy(p + o_m2, d.mask2, cells * sizeof(u32));
    memcpy(p + o_d, d.dune, nodes * sizeof(u16));
    u32 *idx = (u32 *)(p + o_i);
    u32 *w = write_grid(idx, -1, 0);
    t.first_full = 0;
    for (u32 r = 0; r < 4; ++r) {
        t.first_ring[r] = (u32)(w - idx);
        w = write_grid(w, CLIP_N / 2 + (i32)(r & 1), CLIP_N / 2 + (i32)(r >> 1));
    }
    ANA_CHECK((VkDeviceSize)(w - idx) * 4 == ibytes);
    vmaFlushAllocation(g_vk.vma, staging.alloc, 0, VK_WHOLE_SIZE);
    // Карты дюн 2 м — своим буфером и своей загрузкой (≈52 МБ на пустыню)
    {
        VkDeviceSize fb = d.fine ? (((VkDeviceSize)d.fine_count + 1) / 2) * 4 : 4;
        if (!vk_create_buffer(t.fine, fb, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | dst, true)) {
            vk_destroy_buffer(staging);
            return false;
        }
        if (d.fine) {
            GpuBuffer fs;
            if (!vk_create_buffer(fs, fb, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
                vk_destroy_buffer(staging);
                return false;
            }
            memset(fs.mapped, 0, (usize)fb);
            memcpy(fs.mapped, d.fine, d.fine_count * sizeof(u16));
            vmaFlushAllocation(g_vk.vma, fs.alloc, 0, VK_WHOLE_SIZE);
            FineUpload fu{fs.buffer, t.fine.buffer, fb};
            bool fok = vk_one_shot(record_fine_upload, &fu);
            vk_destroy_buffer(fs);
            if (!fok) {
                vk_destroy_buffer(staging);
                return false;
            }
        }
    }
    TerrainUpload u{staging.buffer, {0, 0, hbytes},  {o_m, 0, mbytes},  {o_w, 0, wbytes},
                    {o_i, 0, ibytes},       {o_m2, 0, mbytes}, {o_d, 0, dbytes}};
    bool ok = vk_one_shot(record_terrain_upload, &u);
    vk_destroy_buffer(staging);
    t.on = ok && vk_horizon_terrain(d.height, d.nx, d.nz);
    if (ok)
        log_info("земля в видеопамяти: %.1f МБ сетка, маски, вода и дюны, %.1f МБ индексы колец",
                 (f64)(hbytes + 2 * mbytes + wbytes + dbytes) / 1048576.0,
                 (f64)ibytes / 1048576.0);
    return ok;
}

void vk_terrain_shutdown() {
    TerrainGpu &t = g_vk.terrain;
    vk_destroy_buffer(t.height);
    vk_destroy_buffer(t.mask);
    vk_destroy_buffer(t.water);
    vk_destroy_buffer(t.index);
    vk_destroy_buffer(t.mask2);
    vk_destroy_buffer(t.dune);
    vk_destroy_buffer(t.fine);
    vk_horizon_free();
    t = TerrainGpu{};
}

void vk_terrain_fill(FrameGpu &g, const RenderFrame &f) {
    TerrainGpu &t = g_vk.terrain;
    const RenderTerrain &d = t.desc;
    g.t_on = t.on && f.ground ? 1u : 0u;
    g.t_height = t.height.address;
    g.t_mask = t.mask.address;
    g.t_water = t.water.address;
    g.t_mask2 = t.mask2.address;
    g.t_dune = t.dune.address;
    g.t_chunk0 = d.chunk0;
    g.t_size_x = d.size_x;
    g.t_size_z = d.size_z;
    g.t_nx = (i32)d.nx;
    g.t_nz = (i32)d.nz;
    g.t_nmx = (i32)d.nmx;
    g.t_nmz = (i32)d.nmz;
    g.t_dune_seed = d.dune_seed;
    g.t_fine = t.fine.address;
    for (u32 k = 0; k < 4; ++k) g.t_fine_off[k] = d.fine ? d.fine_off[k] : -1;
    g.t_fine_n = (i32)d.fine_n;
    g.t_loc_m = d.loc_m > 0 ? d.loc_m : 1;
    for (i32 k = 0; k < 8; ++k) g.t_mip[k] = k < TERRAIN_MIPS ? t.mip_off[k] : 0;
    for (u32 k = 0; k < RENDER_TERRAIN_OCTAVES; ++k) {
        g.t_oct_lp[k] = d.oct_lp[k];
        g.t_oct_amp[k] = d.oct_amp[k];
        g.t_oct_seed[k] = d.oct_seed[k];
    }
    g.clip_n = CLIP_N;
    g.clip_levels = CLIP_LEVELS;
    g.clip_morph = CLIP_MORPH;
    for (u32 k = 0; k < 8; ++k) g.ground_layers[k] = k < RENDER_GROUND_LAYERS ? f.ground_layers[k] : 0;
    if (!t.on) return;
    // Камера в полуметрах от угла карты. Начало кольца — на решётке следующего кольца (шаг 2·2^L),
    // тогда край кольца ложится на вершины следующего, а дыра следующего сдвинута на n/2 или n/2+1
    const RenderView &v = f.view;
    i32 cam[2] = {(v.cx - d.chunk0) * 512 + (i32)(v.x * 2.0f), (v.cz - d.chunk0) * 512 + (i32)(v.z * 2.0f)};
    for (i32 L = 0; L < CLIP_LEVELS; ++L) {
        i32 sh = 1 << L;
        for (u32 a = 0; a < 2; ++a) t.origin[L][a] = (floor_div(cam[a], 2 * sh) - CLIP_N / 2) * 2 * sh;
        g.clip[L][0] = t.origin[L][0];
        g.clip[L][1] = t.origin[L][1];
        g.clip[L][2] = g.clip[L][3] = 0;
        if (L > 0) {
            i32 hx = (t.origin[L - 1][0] - t.origin[L][0]) / sh - CLIP_N / 2;
            i32 hz = (t.origin[L - 1][1] - t.origin[L][1]) / sh - CLIP_N / 2;
            ANA_ASSERT(hx >= 0 && hx <= 1 && hz >= 0 && hz <= 1);
            t.ring[L] = (u32)(hx | (hz << 1));
            g.clip[L][2] = hx;
            g.clip[L][3] = hz;
        }
        // Начало индексов кольца — для разбора visibility buffer (номер треугольника кольца → вершины)
        g.g_first[L] = L == 0 ? t.first_full : t.first_ring[t.ring[L]];
    }
    g.g_indices = t.index.address;
}
