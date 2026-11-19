// Сборка конвейеров при запуске (docs/startup.md): шейдеры читаются по порядку, а драйвер компилирует
// конвейеры пачкой в нескольких потоках. Заявки ставят vk_make_compute и vk_make_pipeline (в *_init модулей),
// vk_pipelines_build собирает все и раскладывает по местам. Конвейеры те же, что при сборке по одному:
// порядок сборки на кадр не влияет. Кеш конвейеров (vk_pipecache.cpp): у каждой заявки свой VkPipelineCache
// из записи файла по ключу заявки, после сборки данные всех записываются в файл
#include "vk_state.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include <atomic>
#include <stdio.h>
#include <string.h>

// name — один из списка через запятую
static bool name_listed(const char *list, const char *name) {
    if (!list) return false;
    usize n = strlen(name);
    for (const char *p = list; *p;) {
        const char *e = strchr(p, ',');
        usize len = e ? (usize)(e - p) : strlen(p);
        if (len == n && !memcmp(p, name, n)) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

VkShaderModule vk_load_shader(const char *name, u64 *code_hash) {
    char path[1024];
    snprintf(path, sizeof path, "%s%s.spv", g_vk.cfg.shader_dir, name);
    // Загрузка при старте: временная память, не игровой цикл
    usize cap = 1u << 20;
    void *mem = platform_alloc(cap);
    Arena a;
    arena_init(a, mem, cap, "shader");
    u8 *data = nullptr;
    usize size = 0;
    VkShaderModule m = VK_NULL_HANDLE;
    if (!platform_read_file(path, a, &data, &size) || size % 4 != 0) {
        log_error("нет шейдера %s (собери: make)", path);
    } else {
        auto ci = vk_info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        ci.codeSize = size;
        ci.pCode = (const u32 *)data;
        if (code_hash) *code_hash = hash_bytes(data, size);
        // Проверка на GPU — у каждого шейдера, кроме названных в gpu_validation_skip (docs/graph.md, «Проверки»)
        VkValidationFeatureEnableEXT gpuav = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
        auto vf = vk_info<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
        vf.enabledValidationFeatureCount = 1;
        vf.pEnabledValidationFeatures = &gpuav;
        if (g_vk.cfg.gpu_validation && !name_listed(g_vk.cfg.gpu_validation_skip, name)) ci.pNext = &vf;
        vk_check(vkCreateShaderModule(g_vk.device, &ci, nullptr, &m), path);
    }
    platform_free(mem);
    return m;
}


namespace {
struct PipeJob {
    VkPipeline *out;
    const char *name;  // для журнала: шейдер (вершинный у графического)
    bool graphics;
    PipeDesc d;
    VkShaderModule vs, fs, cs;
    bool has_spec;
    VkSpecializationMapEntry spec_map[4];
    u8 spec_data[16];
    VkSpecializationInfo spec;
    u64 key;            // ключ записи в кеше: шейдеры, специализация, состояние (vk_pipecache.cpp)
    const void *init;   // данные драйвера из кеша (nullptr — нет записи)
    usize init_size;
    void *blob;         // данные драйвера после сборки (platform_alloc), для записи кеша
    usize blob_size;
    u64 ns;  // время сборки
    std::atomic<u64> t_start;  // когда взялась сборка (0 — не бралась): для журнала главного потока
    std::atomic<bool> done;
};
constexpr u32 MAX_PIPE_JOBS = 64;
constexpr u32 MAX_PIPE_THREADS = 8;
}  // namespace

static PipeJob g_pipe_jobs[MAX_PIPE_JOBS];
static u32 g_pipe_njobs = 0;
static bool g_pipe_overflow = false;
static std::atomic<u32> g_pipe_next{0};

static VkPipeline build_graphics(const PipeDesc &d, VkShaderModule vs, VkShaderModule fs, VkPipelineCache cache) {
    VkPipeline p = VK_NULL_HANDLE;
    {
        VkPipelineShaderStageCreateInfo st[2] = {};
        st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        st[0].module = vs;
        st[0].pName = "main";
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        st[1].module = fs;
        st[1].pName = "main";
        auto vi = vk_info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        auto ia = vk_info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        ia.topology = d.topo;
        auto vp = vk_info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        auto rs = vk_info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = d.cull;
        // Грани .mesh — против часовой стрелки снаружи в осях мира (x восток, y вверх, z север). Вид смотрит
        // с правым = вверх × вперёд (восток справа при взгляде на север), y в NDC вниз — на экране они по часовой
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1.0f;
        auto ms = vk_info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        // Обратная глубина: ближе — больше, очистка нулём
        auto dss = vk_info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        dss.depthTestEnable = d.depth_test ? VK_TRUE : VK_FALSE;
        dss.depthWriteEnable = d.depth_write ? VK_TRUE : VK_FALSE;
        dss.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                            VK_COLOR_COMPONENT_A_BIT;
        if (d.blend) {
            ba.blendEnable = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        auto cb = vk_info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        cb.attachmentCount = d.no_attach || d.depth_only ? 0 : 1;
        cb.pAttachments = &ba;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        auto ds = vk_info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dyn;
        auto ri = vk_info<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        VkFormat color = d.color != VK_FORMAT_UNDEFINED ? d.color : g_vk.format;
        ri.colorAttachmentCount = d.no_attach || d.depth_only ? 0 : 1;
        ri.pColorAttachmentFormats = &color;
        ri.depthAttachmentFormat = d.no_attach ? VK_FORMAT_UNDEFINED : d.depth != VK_FORMAT_UNDEFINED ? d.depth : DEPTH_FORMAT;
        auto pi = vk_info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        pi.pNext = &ri;
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &dss;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &ds;
        pi.layout = g_vk.layout;
        vk_check(vkCreateGraphicsPipelines(g_vk.device, cache, 1, &pi, nullptr, &p), d.vs);
    }
    return p;
}

static VkPipeline build_compute(PipeJob &j, VkPipelineCache cache) {
    auto ci = vk_info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = j.cs;
    ci.stage.pName = "main";
    ci.stage.pSpecializationInfo = j.has_spec ? &j.spec : nullptr;
    ci.layout = g_vk.layout;
    VkPipeline p = VK_NULL_HANDLE;
    vk_check(vkCreateComputePipelines(g_vk.device, cache, 1, &ci, nullptr, &p), j.name);
    return p;
}

static PipeJob *job_new(VkPipeline *out, const char *name) {
    *out = VK_NULL_HANDLE;
    if (g_pipe_njobs >= MAX_PIPE_JOBS) {
        if (!g_pipe_overflow) log_error("конвейеров больше %u (%s): подними MAX_PIPE_JOBS", MAX_PIPE_JOBS, name);
        g_pipe_overflow = true;
        return nullptr;
    }
    PipeJob &j = g_pipe_jobs[g_pipe_njobs++];
    j.out = out;
    j.name = name;
    j.graphics = false;
    j.d = PipeDesc{};
    j.vs = j.fs = j.cs = VK_NULL_HANDLE;
    j.has_spec = false;
    j.key = hash_str(name);
    j.init = nullptr;
    j.init_size = 0;
    j.blob = nullptr;
    j.blob_size = 0;
    j.ns = 0;
    j.t_start.store(0, std::memory_order_relaxed);
    j.done.store(false, std::memory_order_relaxed);
    return &j;
}

bool vk_make_compute(VkPipeline *out, const char *name, const VkSpecializationInfo *spec) {
    PipeJob *j = job_new(out, name);
    if (!j) return false;
    if (spec) {
        // Копия: заявку собирают позже, а данные специализации у вызывающего обычно на стеке
        if (spec->mapEntryCount > 4 || spec->dataSize > sizeof j->spec_data) {
            log_error("%s: специализация больше заявки", name);
            return false;
        }
        memcpy(j->spec_map, spec->pMapEntries, spec->mapEntryCount * sizeof(VkSpecializationMapEntry));
        memcpy(j->spec_data, spec->pData, spec->dataSize);
        j->spec = *spec;
        j->spec.pMapEntries = j->spec_map;
        j->spec.pData = j->spec_data;
        j->has_spec = true;
        j->key = hash_combine(j->key, hash_bytes(spec->pMapEntries, spec->mapEntryCount * sizeof(VkSpecializationMapEntry)));
        j->key = hash_combine(j->key, hash_bytes(spec->pData, spec->dataSize));
    }
    u64 code = 0;
    j->cs = vk_load_shader(name, &code);
    j->key = hash_combine(j->key, code);
    return j->cs != VK_NULL_HANDLE;
}

bool vk_make_pipeline(VkPipeline *out, const PipeDesc &d) {
    PipeJob *j = job_new(out, d.vs);
    if (!j) return false;
    j->graphics = true;
    j->d = d;
    u64 cv = 0, cf = 0;
    j->vs = vk_load_shader(d.vs, &cv);
    j->fs = vk_load_shader(d.fs, &cf);
    // Состояние — по полям (в PipeDesc указатели на имена, их адрес от запуска к запуску другой)
    const u64 st[] = {(u64)d.topo, (u64)d.blend, (u64)d.depth_write, (u64)d.cull, (u64)d.color, (u64)d.depth_test,
                      (u64)d.no_attach, (u64)d.depth_only, (u64)d.depth};
    j->key = hash_combine(hash_combine(hash_combine(j->key, hash_str(d.fs)), hash_combine(cv, cf)), hash_bytes(st, sizeof st));
    return j->vs && j->fs;
}

// Свой кеш заявки: из записи файла, если есть (драйвер её не принял — пустой). Без открытого файла — без кеша
static VkPipelineCache job_cache_begin(PipeJob &j) {
    if (!vk_pipeline_cache_active()) return VK_NULL_HANDLE;
    auto ci = vk_info<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    ci.initialDataSize = j.init_size;
    ci.pInitialData = j.init;
    VkPipelineCache c = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(g_vk.device, &ci, nullptr, &c) == VK_SUCCESS) return c;
    j.init = nullptr;
    j.init_size = 0;
    ci.initialDataSize = 0;
    ci.pInitialData = nullptr;
    return vkCreatePipelineCache(g_vk.device, &ci, nullptr, &c) == VK_SUCCESS ? c : VK_NULL_HANDLE;
}

// Данные кеша заявки после сборки — в blob (для файла), сам кеш больше не нужен
static void job_cache_end(PipeJob &j, VkPipelineCache c, VkPipeline p) {
    if (!c) return;
    usize size = 0;
    if (p && vkGetPipelineCacheData(g_vk.device, c, &size, nullptr) == VK_SUCCESS && size) {
        j.blob = platform_alloc(size);
        if (j.blob && vkGetPipelineCacheData(g_vk.device, c, &size, j.blob) == VK_SUCCESS) j.blob_size = size;
        else if (j.blob) {
            platform_free(j.blob);
            j.blob = nullptr;
        }
    }
    vkDestroyPipelineCache(g_vk.device, c, nullptr);
}

static void pipe_worker(void *) {
    for (;;) {
        u32 i = g_pipe_next.fetch_add(1, std::memory_order_relaxed);
        if (i >= g_pipe_njobs) return;
        PipeJob &j = g_pipe_jobs[i];
        u64 t0 = platform_time_ns();
        j.t_start.store(t0, std::memory_order_release);
        VkPipelineCache cache = job_cache_begin(j);
        VkPipeline p = j.graphics ? build_graphics(j.d, j.vs, j.fs, cache) : build_compute(j, cache);
        j.ns = platform_time_ns() - t0;
        job_cache_end(j, cache, p);
        *j.out = p;
        j.done.store(true, std::memory_order_release);
    }
}

// Главный поток ждёт сборщиков и раз в PIPE_REPORT_S пишет в журнал, что собрано и что собирается сколько.
// Без этого долгая компиляция драйвером (или его зависание) выглядит как чёрное окно без строк журнала
constexpr u32 PIPE_REPORT_S = 5;

static void pipe_watch(PlatformThread *const *ts, u32 started, u64 t_begin) {
    u64 next_report = t_begin + (u64)PIPE_REPORT_S * 1000000000ull;
    for (;;) {
        bool all = true;
        for (u32 k = 0; k < started; ++k) all = all && platform_thread_done(ts[k]);
        if (all) return;
        platform_sleep_ms(20);
        u64 now = platform_time_ns();
        if (now < next_report) continue;
        next_report = now + (u64)PIPE_REPORT_S * 1000000000ull;
        char busy[512];
        usize len = 0;
        busy[0] = 0;
        u32 ready = 0;
        for (u32 i = 0; i < g_pipe_njobs; ++i) {
            const PipeJob &j = g_pipe_jobs[i];
            if (j.done.load(std::memory_order_acquire)) { ++ready; continue; }
            u64 t = j.t_start.load(std::memory_order_acquire);
            if (!t || len + 64 >= sizeof busy) continue;
            int w = snprintf(busy + len, sizeof busy - len, "%s%s %.0f с", len ? ", " : "", j.name,
                             (f64)(now - t) / 1e9);
            if (w > 0) len += (usize)w;
        }
        log_info("конвейеры: готово %u из %u за %.0f с; собираются: %s", ready, g_pipe_njobs,
                 (f64)(now - t_begin) / 1e9, len ? busy : "—");
    }
}

bool vk_pipelines_build(u32 *count, u32 *threads, const char **slowest, f64 *slowest_ms) {
    // Сборщиков — по ядрам без одного (его берёт создание мира в своём потоке, run_window.cpp), не больше
    // MAX_PIPE_THREADS; --pipe-threads N задаёт число сам (1 — по одному). Главный поток не собирает, а следит
    // и пишет журнал (pipe_watch). Не создался ни один поток — главный собирает всё сам, как раньше
    u32 cores = platform_cpu_cores();
    u32 want = g_vk.cfg.pipe_threads ? g_vk.cfg.pipe_threads : cores > 1 ? cores - 1 : 1;
    if (want > MAX_PIPE_THREADS) want = MAX_PIPE_THREADS;
    if (want > g_pipe_njobs) want = g_pipe_njobs ? g_pipe_njobs : 1;
    g_pipe_next.store(0, std::memory_order_relaxed);
    // Ключи кеша: одинаковые заявки (те же шейдеры и состояние) различает номер такой же по порядку
    u64 base[MAX_PIPE_JOBS];
    for (u32 i = 0; i < g_pipe_njobs; ++i) {
        PipeJob &j = g_pipe_jobs[i];
        base[i] = j.key;
        u64 same = 0;
        for (u32 k = 0; k < i; ++k) same += base[k] == base[i];
        if (same) j.key = hash_combine(base[i], same);
        j.init = vk_pipeline_cache_find(j.key, &j.init_size);
    }
    PlatformThread *ts[MAX_PIPE_THREADS] = {};
    u32 started = 0;
    const u64 t_begin = platform_time_ns();
    log_info("конвейеры: собираю %u в %u потоках", g_pipe_njobs, want);
    for (u32 k = 0; k < want; ++k)
        if ((ts[started] = platform_thread_start("pipelines", pipe_worker, nullptr))) ++started;
    if (started) pipe_watch(ts, started, t_begin);
    pipe_worker(nullptr);  // недособранное, если потоков не было
    for (u32 k = 0; k < started; ++k) platform_thread_join(ts[k]);
    bool ok = !g_pipe_overflow;
    u64 worst = 0;
    const char *worst_name = "";
    for (u32 i = 0; i < g_pipe_njobs; ++i) {
        PipeJob &j = g_pipe_jobs[i];
        if (j.vs) vkDestroyShaderModule(g_vk.device, j.vs, nullptr);
        if (j.fs) vkDestroyShaderModule(g_vk.device, j.fs, nullptr);
        if (j.cs) vkDestroyShaderModule(g_vk.device, j.cs, nullptr);
        if (!*j.out) ok = false;
        if (j.ns > worst) {
            worst = j.ns;
            worst_name = j.name;
        }
    }
    // Долгие (больше секунды) — поимённо: у шейдера с видами (специализация) — с числом вида
    for (u32 i = 0; i < g_pipe_njobs; ++i) {
        const PipeJob &j = g_pipe_jobs[i];
        if (j.ns < 1000000000ull) continue;
        u32 sv = 0;
        if (j.has_spec && j.spec.dataSize >= sizeof sv) memcpy(&sv, j.spec_data, sizeof sv);
        // Был в кеше, а собирался долго — драйвер не взял данные (это и надо чинить, а не ждать)
        const char *miss = j.init ? " — был в кеше, драйвер собрал заново" : "";
        if (j.has_spec) log_info("  конвейер %s (вид %u): %.1f с%s", j.name, sv, (f64)j.ns / 1e9, miss);
        else log_info("  конвейер %s: %.1f с%s", j.name, (f64)j.ns / 1e9, miss);
    }
    // Кеш на диске: сколько взято из файла, и данные всех собранных — в файл (только конвейеры этой сборки)
    if (vk_pipeline_cache_active() && g_pipe_njobs) {
        u64 keys[MAX_PIPE_JOBS];
        const void *data[MAX_PIPE_JOBS];
        usize sizes[MAX_PIPE_JOBS];
        u32 n = 0, hits = 0;
        for (u32 i = 0; i < g_pipe_njobs; ++i) {
            const PipeJob &j = g_pipe_jobs[i];
            hits += j.init != nullptr;
            if (!j.blob) continue;
            keys[n] = j.key;
            data[n] = j.blob;
            sizes[n++] = j.blob_size;
        }
        log_info("кеш конвейеров: взято %u из %u%s", hits, g_pipe_njobs,
                 hits < g_pipe_njobs ? " (остальные — новые или изменённые шейдеры)" : "");
        if (ok) vk_pipeline_cache_store(keys, data, sizes, n);
    }
    for (u32 i = 0; i < g_pipe_njobs; ++i) {
        PipeJob &j = g_pipe_jobs[i];
        if (j.blob) platform_free(j.blob);
        j.blob = nullptr;
    }
    if (count) *count = g_pipe_njobs;
    if (threads) *threads = started ? started : 1;
    if (slowest) *slowest = worst_name;
    if (slowest_ms) *slowest_ms = (f64)worst / 1e6;
    g_pipe_njobs = 0;
    g_pipe_overflow = false;
    return ok;
}
