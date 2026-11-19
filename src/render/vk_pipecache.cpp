// Кеш конвейеров на диске (docs/frame_time.md, «Кеш конвейеров»). Все конвейеры создаются при запуске, поэтому
// кеш ускоряет запуск, а не кадр: драйвер не компилирует шейдеры заново, если видеокарта, драйвер и шейдер те же.
// У каждого конвейера свой VkPipelineCache и своя запись в файле по ключу (шейдеры, специализация, состояние):
// файл хранит только конвейеры этой сборки и не растёт от правки к правке. Чужой, битый или обрезанный файл не
// отдаётся драйверу: все конвейеры собираются заново, а файл переписывается
#include "vk_state.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include <string.h>

namespace {
// Файл: заголовок, count записей {ключ, байт}, потом данные драйвера записей подряд в том же порядке
struct CacheFile {
    char magic[4];  // "APC2"
    u32 count;
    u64 hash;  // hash_bytes всего после заголовка
};
struct CacheEntry {
    u64 key;
    u64 bytes;
};
static_assert(sizeof(CacheFile) == 16 && sizeof(CacheEntry) == 16, "CacheFile и CacheEntry — по 16 байт");
constexpr usize MAX_CACHE_BYTES = 128u << 20;
constexpr u32 MAX_ENTRIES = 256;
}  // namespace

static bool g_active = false;    // файл открыт для этой сборки (между open и close)
static void *g_mem = nullptr;    // прочитанный файл
static const CacheEntry *g_entries = nullptr;
static const u8 *g_data[MAX_ENTRIES] = {};
static u32 g_count = 0;
static u64 g_loaded_hash = 0;    // хеш прочитанного (0 — не читали)

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

// Разобрать прочитанный файл; nullptr — подошёл (записи в g_entries/g_data), иначе причина
static const char *parse(const u8 *data, usize size) {
    CacheFile cf;
    if (size < sizeof cf) return "короткий";
    memcpy(&cf, data, sizeof cf);
    if (!memcmp(cf.magic, "APC1", 4)) return "старый формат";
    if (memcmp(cf.magic, "APC2", 4)) return "чужой файл";
    if (cf.count > MAX_ENTRIES || sizeof cf + (usize)cf.count * sizeof(CacheEntry) > size) return "обрезан";
    if (hash_bytes(data + sizeof cf, size - sizeof cf) != cf.hash) return "битый";
    const CacheEntry *e = (const CacheEntry *)(data + sizeof cf);
    usize off = sizeof cf + (usize)cf.count * sizeof(CacheEntry);
    for (u32 i = 0; i < cf.count; ++i) {
        if (e[i].bytes > size - off) return "обрезан";
        g_data[i] = data + off;
        off += (usize)e[i].bytes;
    }
    if (off != size) return "обрезан";
    const char *why = nullptr;
    for (u32 i = 0; i < cf.count; ++i)
        if (!matches_device(g_data[i], (usize)e[i].bytes, &why)) return why;
    g_entries = e;
    g_count = cf.count;
    g_loaded_hash = cf.hash;
    return nullptr;
}

bool vk_pipeline_cache_open() {
    vk_pipeline_cache_close();
    const char *path = g_vk.cfg.pipeline_cache_path;
    // GPU-AV меняет шейдеры: такой кеш не читается и не пишется
    if (!path || g_vk.cfg.gpu_validation) return true;
    g_active = true;
    g_mem = platform_alloc(MAX_CACHE_BYTES + 64);
    if (!g_mem) return true;
    Arena a;
    arena_init(a, g_mem, MAX_CACHE_BYTES + 64, "кеш конвейеров");
    u8 *data = nullptr;
    usize size = 0;
    const char *state = "новый";
    if (platform_read_file(path, a, &data, &size)) {
        const char *why = parse(data, size);
        state = why ? why : "с диска";
    }
    log_info("кеш конвейеров: %s (%u конвейеров, %.1f КБ)", state, g_count, (f64)size / 1024.0);
    return true;
}

bool vk_pipeline_cache_active() { return g_active; }

const void *vk_pipeline_cache_find(u64 key, usize *size) {
    for (u32 i = 0; i < g_count; ++i)
        if (g_entries[i].key == key) {
            *size = (usize)g_entries[i].bytes;
            return g_data[i];
        }
    *size = 0;
    return nullptr;
}

// После сборки всех конвейеров запуска: записать их данные (ключи уникальны), если что-то изменилось
void vk_pipeline_cache_store(const u64 *keys, const void *const *data, const usize *sizes, u32 n) {
    const char *path = g_vk.cfg.pipeline_cache_path;
    if (!g_active || !path || n > MAX_ENTRIES) return;
    usize total = sizeof(CacheFile) + (usize)n * sizeof(CacheEntry);
    for (u32 i = 0; i < n; ++i) total += sizes[i];
    if (total > MAX_CACHE_BYTES) {
        log_warn("кеш конвейеров не записан: %.1f МБ больше %u МБ", (f64)total / 1048576.0, (u32)(MAX_CACHE_BYTES >> 20));
        return;
    }
    u8 *mem = (u8 *)platform_alloc(total);
    if (!mem) return;
    CacheFile cf;
    memcpy(cf.magic, "APC2", 4);
    cf.count = n;
    CacheEntry *e = (CacheEntry *)(mem + sizeof cf);
    usize off = sizeof cf + (usize)n * sizeof(CacheEntry);
    for (u32 i = 0; i < n; ++i) {
        e[i] = {keys[i], (u64)sizes[i]};
        if (sizes[i]) memcpy(mem + off, data[i], sizes[i]);
        off += sizes[i];
    }
    cf.hash = hash_bytes(mem + sizeof cf, total - sizeof cf);
    memcpy(mem, &cf, sizeof cf);
    if (cf.hash != g_loaded_hash) {
        if (platform_write_file(path, mem, total)) log_info("кеш конвейеров записан: %u конвейеров, %.1f КБ", n, (f64)total / 1024.0);
        else log_warn("кеш конвейеров не записан: %s", path);
    }
    platform_free(mem);
}

void vk_pipeline_cache_close() {
    if (g_mem) platform_free(g_mem);
    g_mem = nullptr;
    g_entries = nullptr;
    g_count = 0;
    g_loaded_hash = 0;
    g_active = false;
}
