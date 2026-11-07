// Кеш конвейеров на диске (docs/frame_time.md, «Кеш конвейеров»). Все конвейеры создаются при запуске, поэтому
// кеш ускоряет запуск, а не кадр: драйвер не компилирует шейдеры заново, если видеокарта и драйвер те же.
// Файл: свой заголовок (метка, размер, хеш) и данные vkGetPipelineCacheData как есть. Чужой, битый или
// обрезанный файл не отдаётся драйверу: кеш начинается пустым и переписывается
#include "vk_state.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include <string.h>

namespace {
struct CacheFile {
    char magic[4];  // "APC1"
    u32 bytes;      // данных драйвера после заголовка
    u64 hash;       // hash_bytes данных
};
static_assert(sizeof(CacheFile) == 16, "CacheFile — 16 байт");
constexpr usize MAX_CACHE_BYTES = 64u << 20;
}  // namespace

static u64 g_loaded_hash = 0;  // хеш данных, прочитанных с диска (0 — не читали)

// Данные драйвера подходят этой видеокарте: заголовок VkPipelineCacheHeaderVersionOne (32 байта)
static bool matches_device(const u8 *data, usize size, const char **why) {
    VkPipelineCacheHeaderVersionOne h;
    if (size < sizeof h) { *why = "короткий"; return false; }
    memcpy(&h, data, sizeof h);
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(g_vk.gpu, &p);
    if (h.headerSize < sizeof h || h.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) { *why = "другой формат"; return false; }
    if (h.vendorID != p.vendorID || h.deviceID != p.deviceID) { *why = "другая видеокарта"; return false; }
    if (memcmp(h.pipelineCacheUUID, p.pipelineCacheUUID, VK_UUID_SIZE)) { *why = "другой драйвер"; return false; }
    return true;
}

bool vk_pipeline_cache_open() {
    const char *path = g_vk.cfg.pipeline_cache_path;
    // GPU-AV меняет шейдеры: такой кеш не читается и не пишется
    if (g_vk.cfg.gpu_validation) path = nullptr;
    const u8 *init = nullptr;
    usize init_size = 0;
    void *mem = nullptr;
    const char *state = "новый";
    if (path) {
        mem = platform_alloc(MAX_CACHE_BYTES + 64);
        Arena a;
        arena_init(a, mem, MAX_CACHE_BYTES + 64, "кеш конвейеров");
        u8 *data = nullptr;
        usize size = 0;
        if (platform_read_file(path, a, &data, &size)) {
            CacheFile cf;
            const char *why = nullptr;
            if (size < sizeof cf) why = "короткий";
            else {
                memcpy(&cf, data, sizeof cf);
                if (memcmp(cf.magic, "APC1", 4)) why = "чужой файл";
                else if (cf.bytes != size - sizeof cf) why = "обрезан";
                else if (hash_bytes(data + sizeof cf, cf.bytes) != cf.hash) why = "битый";
                else if (matches_device(data + sizeof cf, cf.bytes, &why)) {
                    init = data + sizeof cf;
                    init_size = cf.bytes;
                    g_loaded_hash = cf.hash;
                }
            }
            state = why ? why : "с диска";
        }
    }
    auto ci = vk_info<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    ci.initialDataSize = init_size;
    ci.pInitialData = init;
    VkResult r = vkCreatePipelineCache(g_vk.device, &ci, nullptr, &g_vk.pipeline_cache);
    if (r != VK_SUCCESS && init) {
        // Драйвер не принял данные — начать с пустого
        state = "не принят драйвером";
        g_loaded_hash = 0;
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        r = vkCreatePipelineCache(g_vk.device, &ci, nullptr, &g_vk.pipeline_cache);
    }
    if (mem) platform_free(mem);
    if (!vk_check(r, "vkCreatePipelineCache")) return false;
    if (path) log_info("кеш конвейеров: %s (%.1f КБ)", state, (f64)init_size / 1024.0);
    return true;
}

// После создания всех конвейеров: записать, если кеш изменился
void vk_pipeline_cache_save() {
    const char *path = g_vk.cfg.pipeline_cache_path;
    if (!g_vk.pipeline_cache || !path || g_vk.cfg.gpu_validation) return;
    usize size = 0;
    if (vkGetPipelineCacheData(g_vk.device, g_vk.pipeline_cache, &size, nullptr) != VK_SUCCESS || !size ||
        size > MAX_CACHE_BYTES)
        return;
    u8 *mem = (u8 *)platform_alloc(sizeof(CacheFile) + size);
    bool ok = vkGetPipelineCacheData(g_vk.device, g_vk.pipeline_cache, &size, mem + sizeof(CacheFile)) == VK_SUCCESS;
    if (ok) {
        CacheFile cf;
        memcpy(cf.magic, "APC1", 4);
        cf.bytes = (u32)size;
        cf.hash = hash_bytes(mem + sizeof cf, size);
        memcpy(mem, &cf, sizeof cf);
        if (cf.hash != g_loaded_hash) {
            if (platform_write_file(path, mem, sizeof cf + size))
                log_info("кеш конвейеров записан: %.1f КБ", (f64)size / 1024.0);
            else log_warn("кеш конвейеров не записан: %s", path);
        }
    }
    platform_free(mem);
}

void vk_pipeline_cache_close() {
    if (g_vk.pipeline_cache) vkDestroyPipelineCache(g_vk.device, g_vk.pipeline_cache, nullptr);
    g_vk.pipeline_cache = VK_NULL_HANDLE;
    g_loaded_hash = 0;
}
