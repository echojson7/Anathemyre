// Диагностика GPU (docs/graph.md, «Сбой GPU» и «Переполнения»):
// — метки проходов для разбора «потери устройства»: VK_NV_device_diagnostic_checkpoints или VK_AMD_buffer_marker,
//   что есть у драйвера; при сбое журнал получает последний начатый и последний законченный проход;
// — VK_EXT_device_fault: причина и адрес сбоя от драйвера, адрес ищется среди живых буферов по их именам;
// — счётчики GPU: шейдеры пишут, сколько не влезло (STAT_* в common.glsl), кадр копирует их в буфер слота,
//   процессор читает после забора кадра — вместе с переполнениями на стороне процессора это render_overflows()
#include "vk_graph.h"
#include "../core/log.h"
#include <stdio.h>
#include <string.h>

namespace {

// Имена проходов последних кадров: метка — номер кадра (младшие биты), номер прохода, начало или конец
constexpr u32 RING = 4;
const char *g_names[RING][RG_MAX_PASSES];
u64 g_ring_frame[RING];
u64 g_frame = 0;

// Живые буферы с адресами: для разбора адреса сбоя
struct BufEntry {
    VkBuffer buffer;
    VkDeviceAddress address;
    VkDeviceSize size;
    const char *name;
};
constexpr u32 MAX_BUFS = 256;
BufEntry g_bufs[MAX_BUFS];
u32 g_nbufs = 0;

constexpr u32 MAX_OVERFLOWS = 16;
RenderOverflow g_cpu_over[MAX_OVERFLOWS], g_gpu_over[MAX_OVERFLOWS];
u32 g_ncpu = 0, g_ngpu = 0;
bool g_lost_reported = false;

u32 encode(u64 frame, u32 pass, bool end) { return (u32)((frame & 0x1FFFFF) << 11) | (pass << 1) | (end ? 1u : 0u); }

const char *decode(u32 v, u64 *frame, bool *end) {
    u32 pass = (v >> 1) & 0x3FF;
    u64 fr = v >> 11;
    *end = (v & 1) != 0;
    for (u32 k = 0; k < RING; ++k)
        if ((g_ring_frame[k] & 0x1FFFFF) == fr && pass < RG_MAX_PASSES) {
            *frame = g_ring_frame[k];
            return g_names[k][pass] ? g_names[k][pass] : "?";
        }
    *frame = fr;
    return "?";
}

void log_marker(const char *what, u32 v) {
    u64 fr;
    bool end;
    const char *name = decode(v, &fr, &end);
    log_error("  %s: кадр %llu, проход «%s» (%s)", what, (unsigned long long)fr, name, end ? "закончен" : "начат");
}

void add_over(RenderOverflow *list, u32 &n, const char *what, u64 want, u64 cap) {
    if (n < MAX_OVERFLOWS) list[n++] = RenderOverflow{what, want, cap};
}

}  // namespace

bool vk_diag_init() {
    DiagGpu &d = g_vk.diag;
    if (d.amd_marker) {
        if (!vk_create_buffer(d.markers, 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) return false;
        vk_buffer_name(d.markers, "метки сбоя");
        memset(d.markers.mapped, 0, 64);
    }
    if (!vk_create_buffer(d.stats, STAT_COUNT * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true))
        return false;
    vk_buffer_name(d.stats, "счётчики GPU");
    d.rg_stats = rg_buffer("счётчики GPU");
    for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        FrameSlot &fs = g_vk.frames[i];
        if (!vk_create_buffer(fs.readback, READBACK_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) return false;
        vk_buffer_name(fs.readback, "чтение кадра");
        memset(fs.readback.mapped, 0, READBACK_BYTES);
        fs.rg_readback = rg_buffer("чтение кадра");
    }
    const char *how = d.nv_checkpoints ? "метки NV" : d.amd_marker ? "метки AMD" : "без меток проходов";
    log_info("разбор сбоя GPU: %s%s", how, d.device_fault ? ", сведения драйвера (device_fault)" : "");
    return true;
}

void vk_diag_shutdown() {
    DiagGpu &d = g_vk.diag;
    vk_destroy_buffer(d.markers);
    vk_destroy_buffer(d.stats);
    for (FrameSlot &fs : g_vk.frames) vk_destroy_buffer(fs.readback);
}

void vk_diag_frame(u64 frame) {
    g_frame = frame;
    g_ring_frame[frame % RING] = frame;
    for (u32 i = 0; i < RG_MAX_PASSES; ++i) g_names[frame % RING][i] = nullptr;
    g_ncpu = 0;
}

void vk_diag_begin(VkCommandBuffer cmd, u32 pass, const char *name) {
    g_names[g_frame % RING][pass] = name;
    const DiagGpu &d = g_vk.diag;
    u32 v = encode(g_frame, pass, false);
    if (d.nv_checkpoints) vkCmdSetCheckpointNV(cmd, (const void *)(uintptr_t)v);
    else if (d.amd_marker) vkCmdWriteBufferMarker2AMD(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, d.markers.buffer, 0, v);
}

void vk_diag_end(VkCommandBuffer cmd, u32 pass) {
    const DiagGpu &d = g_vk.diag;
    u32 v = encode(g_frame, pass, true);
    if (d.nv_checkpoints) vkCmdSetCheckpointNV(cmd, (const void *)(uintptr_t)v);
    else if (d.amd_marker) vkCmdWriteBufferMarker2AMD(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, d.markers.buffer, 4, v);
}

// Буфер, в который попадает адрес; nullptr — ни в один живой
static const BufEntry *find_buffer(VkDeviceAddress a) {
    for (u32 i = 0; i < g_nbufs; ++i)
        if (a >= g_bufs[i].address && a < g_bufs[i].address + g_bufs[i].size) return &g_bufs[i];
    return nullptr;
}

static void report_address(VkDeviceAddress a) {
    const BufEntry *b = find_buffer(a);
    if (b) log_error("    это буфер «%s», смещение %llu из %llu байт", b->name ? b->name : "без имени",
                     (unsigned long long)(a - b->address), (unsigned long long)b->size);
    else log_error("    ни в одном живом буфере (за концом или освобождён)");
}

void vk_device_lost() {
    if (g_lost_reported) return;
    g_lost_reported = true;
    const DiagGpu &d = g_vk.diag;
    log_error("GPU: потеря устройства (VK_ERROR_DEVICE_LOST), кадр %llu", (unsigned long long)g_frame);
    if (d.nv_checkpoints) {
        u32 n = 0;
        vkGetQueueCheckpointDataNV(g_vk.queue, &n, nullptr);
        VkCheckpointDataNV cp[16];
        if (n > 16) n = 16;
        for (u32 i = 0; i < n; ++i) cp[i] = vk_info<VkCheckpointDataNV>(VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV);
        vkGetQueueCheckpointDataNV(g_vk.queue, &n, cp);
        for (u32 i = 0; i < n; ++i) {
            char what[64];
            snprintf(what, sizeof what, "метка на стадии 0x%x", (unsigned)cp[i].stage);
            log_marker(what, (u32)(uintptr_t)cp[i].pCheckpointMarker);
        }
        if (!n) log_error("  меток NV нет");
    } else if (d.amd_marker && d.markers.mapped) {
        vmaInvalidateAllocation(g_vk.vma, d.markers.alloc, 0, VK_WHOLE_SIZE);
        const u32 *m = (const u32 *)d.markers.mapped;
        log_marker("последний начатый", m[0]);
        log_marker("последний законченный", m[1]);
    } else {
        log_error("  меток проходов нет: драйвер не даёт ни NV checkpoints, ни AMD buffer marker");
    }
    if (d.device_fault) {
        auto c = vk_info<VkDeviceFaultCountsEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT);
        if (vkGetDeviceFaultInfoEXT(g_vk.device, &c, nullptr) != VK_SUCCESS) return;
        VkDeviceFaultAddressInfoEXT addr[16];
        VkDeviceFaultVendorInfoEXT vend[16];
        c.addressInfoCount = c.addressInfoCount < 16 ? c.addressInfoCount : 16;
        c.vendorInfoCount = c.vendorInfoCount < 16 ? c.vendorInfoCount : 16;
        c.vendorBinarySize = 0;
        auto info = vk_info<VkDeviceFaultInfoEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT);
        info.pAddressInfos = addr;
        info.pVendorInfos = vend;
        VkResult r = vkGetDeviceFaultInfoEXT(g_vk.device, &c, &info);
        if (r != VK_SUCCESS && r != VK_INCOMPLETE) return;
        log_error("  драйвер: %s", info.description);
        static const char *const KINDS[] = {"нет", "чтение мимо памяти", "запись мимо памяти", "исполнение мимо",
                                            "адрес команды", "сбой команды", "ошибка команды"};
        for (u32 i = 0; i < c.addressInfoCount; ++i) {
            u32 k = (u32)addr[i].addressType;
            log_error("  адрес 0x%llx ± %llu: %s", (unsigned long long)addr[i].reportedAddress,
                      (unsigned long long)addr[i].addressPrecision, k < 7 ? KINDS[k] : "?");
            report_address(addr[i].reportedAddress);
        }
        for (u32 i = 0; i < c.vendorInfoCount; ++i)
            log_error("  драйвер: %s (код %llu, данные %llu)", vend[i].description,
                      (unsigned long long)vend[i].vendorFaultCode, (unsigned long long)vend[i].vendorFaultData);
    }
}

void vk_buffer_track(const GpuBuffer &b) {
    if (!b.address || g_nbufs >= MAX_BUFS) return;
    g_bufs[g_nbufs++] = BufEntry{b.buffer, b.address, b.size, nullptr};
}

void vk_buffer_untrack(const GpuBuffer &b) {
    for (u32 i = 0; i < g_nbufs; ++i)
        if (g_bufs[i].buffer == b.buffer) {
            g_bufs[i] = g_bufs[--g_nbufs];
            return;
        }
}

void vk_buffer_name(const GpuBuffer &b, const char *name) {
    for (u32 i = 0; i < g_nbufs; ++i)
        if (g_bufs[i].buffer == b.buffer) g_bufs[i].name = name;
    // Имя и в сообщениях слоёв проверки
    if (g_vk.messenger && vkSetDebugUtilsObjectNameEXT) {
        auto ni = vk_info<VkDebugUtilsObjectNameInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
        ni.objectType = VK_OBJECT_TYPE_BUFFER;
        ni.objectHandle = (u64)b.buffer;
        ni.pObjectName = name;
        vkSetDebugUtilsObjectNameEXT(g_vk.device, &ni);
    }
}

void vk_image_name(VkImage img, const char *name) {
    if (!g_vk.messenger || !vkSetDebugUtilsObjectNameEXT || !img) return;
    auto ni = vk_info<VkDebugUtilsObjectNameInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
    ni.objectType = VK_OBJECT_TYPE_IMAGE;
    ni.objectHandle = (u64)img;
    ni.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(g_vk.device, &ni);
}

// Счётчики GPU: обнулить в начале кадра, скопировать в буфер слота в конце
static void record_stats_clear(VkCommandBuffer cmd, void *) {
    vkCmdFillBuffer(cmd, g_vk.diag.stats.buffer, 0, STAT_COUNT * 4, 0);
}

static void record_stats_copy(VkCommandBuffer cmd, void *user) {
    const FrameSlot &fs = *(const FrameSlot *)user;
    VkBufferCopy c{0, READBACK_STATS, STAT_COUNT * 4};
    vkCmdCopyBuffer(cmd, g_vk.diag.stats.buffer, fs.readback.buffer, 1, &c);
    VkBufferCopy d{0, 0, 4};  // число вызовов отсечения — видимых кластеров
    vkCmdCopyBuffer(cmd, g_vk.world.cull.buffer, fs.readback.buffer, 1, &d);
    VkBufferCopy e{SKY_OUT_EYE * 16, READBACK_EYE, 32};  // глаз: экспозиция и освещённость (F3)
    vkCmdCopyBuffer(cmd, g_vk.sky.out.buffer, fs.readback.buffer, 1, &e);
}

void vk_stats_clear_pass() {
    rg_pass("счётчики: обнулить", nullptr, record_stats_clear, nullptr);
    rg_use(g_vk.diag.rg_stats, RG_COPY_WRITE);
}

void vk_stats_copy_pass(FrameSlot &fs) {
    rg_pass("счётчики: в процессор", nullptr, record_stats_copy, &fs);
    rg_use(g_vk.diag.rg_stats, RG_COPY_READ);
    rg_use(g_vk.world.rg_cull, RG_COPY_READ);
    rg_use(g_vk.sky.rg_out, RG_COPY_READ);
    rg_use(fs.rg_readback, RG_COPY_WRITE);
}

void vk_stats_read(FrameSlot &fs) {
    if (!fs.stats_pending) return;
    fs.stats_pending = false;
    vmaInvalidateAllocation(g_vk.vma, fs.readback.alloc, 0, VK_WHOLE_SIZE);
    const u32 *rb = (const u32 *)fs.readback.mapped;
    const u32 *st = rb + READBACK_STATS / 4;
    g_ngpu = 0;
    g_vk.world.vis_clusters = rb[0];
    if (st[STAT_VIS_WANT] > VIS_MAX_CLUSTERS) add_over(g_gpu_over, g_ngpu, "видимые кластеры", st[STAT_VIS_WANT], VIS_MAX_CLUSTERS);
    memcpy(g_vk.world.work, st, sizeof g_vk.world.work);
    memcpy(g_vk.world.eye, rb + READBACK_EYE / 4, sizeof g_vk.world.eye);
    vk_gcache_read(st);
    vk_shadow_read(st, fs.sm_frame);
    g_vk.world.work_valid = true;
    if (st[STAT_FROXEL_LIGHTS]) add_over(g_gpu_over, g_ngpu, "froxels: клеток, где огни не влезли", st[STAT_FROXEL_LIGHTS], 0);
    if (st[STAT_FROXEL_DECALS]) add_over(g_gpu_over, g_ngpu, "froxels: клеток, где декали не влезли", st[STAT_FROXEL_DECALS], 0);
}

void vk_overflow(const char *what, u64 want, u64 cap) {
    if (want > cap) add_over(g_cpu_over, g_ncpu, what, want, cap);
}

bool render_work(RenderWork &out) {
    const WorldGpu &w = g_vk.world;
    out = RenderWork{};
    if (!w.work_valid) return false;
    static_assert(RENDER_TILE_KINDS == TILE_KINDS, "виды плиток");
    for (u32 k = 0; k < TILE_KINDS; ++k) out.tiles[k] = w.work[STAT_TILES + k];
    out.px_sky = w.work[STAT_PX_SKY];
    out.px_ground = w.work[STAT_PX_GROUND];
    out.px_mesh = w.work[STAT_PX_MESH];
    out.surf_ground = w.work[STAT_SURF_GROUND];
    out.surf_mesh = w.work[STAT_SURF_MESH];
    out.shadow_taps = w.work[STAT_SHADOW_TAPS];
    out.gc_used = w.work[STAT_GC_USED];
    out.gc_baked = w.work[STAT_GC_BAKED];
    out.gc_over = w.work[STAT_GC_OVER];
    out.gc_direct = w.work[STAT_GC_PX_DIRECT];
    out.skin_verts = w.work[STAT_SKIN_VERTS];
    out.sm_used = w.work[STAT_SM_USED];
    out.sm_new = w.work[STAT_SM_NEW];
    out.sm_over = w.work[STAT_SM_OVER];
    out.sm_miss = w.work[STAT_SM_MISS];
    out.sm_draws = w.work[STAT_SM_DRAWS];
    out.sm_taps = w.work[STAT_SM_TAPS];
    out.sm_wait = w.work[STAT_SM_WAIT];
    out.sdf_taps = w.work[STAT_SDF_TAPS];
    out.sdf_tris = w.work[STAT_SDF_TRIS];
    out.sdf_bricks = w.work[STAT_SDF_BRICKS];
    out.sm_blend = vk_shadow_blend();
    out.gc_off = g_vk.gcache.off;
    return true;
}

bool render_light(RenderLightInfo &out) {
    const WorldGpu &w = g_vk.world;
    out = RenderLightInfo{};
    if (!w.work_valid) return false;
    out.exposure = w.eye[0];
    out.lux = w.eye[1];
    out.ev100 = w.eye[2];
    out.lux_sky = w.eye[3];
    out.lux_key = w.eye[4];
    out.ev100_goal = w.eye[5];
    out.lux_eye = w.eye[6];
    return true;
}

u32 render_overflows(RenderOverflow *out, u32 cap) {
    u32 n = 0;
    for (u32 i = 0; i < g_ncpu && n < cap; ++i) out[n++] = g_cpu_over[i];
    for (u32 i = 0; i < g_ngpu && n < cap; ++i) out[n++] = g_gpu_over[i];
    return n;
}
