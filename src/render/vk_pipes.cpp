// Сборка конвейеров при запуске (docs/startup.md): шейдеры читаются по порядку, а драйвер компилирует
// конвейеры пачкой в нескольких потоках. Заявки ставят vk_make_compute и vk_make_pipeline (в *_init модулей),
// vk_pipelines_build собирает все и раскладывает по местам. Конвейеры те же, что при сборке по одному:
// порядок сборки на кадр не влияет. Кеш конвейеров (vk_pipecache.cpp) общий: драйвер синхронизирует его сам
#include "vk_state.h"
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

VkShaderModule vk_load_shader(const char *name) {
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
    u64 ns;  // время сборки
};
constexpr u32 MAX_PIPE_JOBS = 64;
constexpr u32 MAX_PIPE_THREADS = 8;
}  // namespace

static PipeJob g_pipe_jobs[MAX_PIPE_JOBS];
static u32 g_pipe_njobs = 0;
static bool g_pipe_overflow = false;
static std::atomic<u32> g_pipe_next{0};

static VkPipeline build_graphics(const PipeDesc &d, VkShaderModule vs, VkShaderModule fs) {
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
        cb.attachmentCount = d.no_attach ? 0 : 1;
        cb.pAttachments = &ba;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        auto ds = vk_info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dyn;
        auto ri = vk_info<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        VkFormat color = d.color != VK_FORMAT_UNDEFINED ? d.color : g_vk.format;
        ri.colorAttachmentCount = d.no_attach ? 0 : 1;
        ri.pColorAttachmentFormats = &color;
        ri.depthAttachmentFormat = d.no_attach ? VK_FORMAT_UNDEFINED : DEPTH_FORMAT;
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
        vk_check(vkCreateGraphicsPipelines(g_vk.device, g_vk.pipeline_cache, 1, &pi, nullptr, &p), d.vs);
    }
    return p;
}

static VkPipeline build_compute(PipeJob &j) {
    auto ci = vk_info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = j.cs;
    ci.stage.pName = "main";
    ci.stage.pSpecializationInfo = j.has_spec ? &j.spec : nullptr;
    ci.layout = g_vk.layout;
    VkPipeline p = VK_NULL_HANDLE;
    vk_check(vkCreateComputePipelines(g_vk.device, g_vk.pipeline_cache, 1, &ci, nullptr, &p), j.name);
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
    j = PipeJob{};
    j.out = out;
    j.name = name;
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
    }
    j->cs = vk_load_shader(name);
    return j->cs != VK_NULL_HANDLE;
}

bool vk_make_pipeline(VkPipeline *out, const PipeDesc &d) {
    PipeJob *j = job_new(out, d.vs);
    if (!j) return false;
    j->graphics = true;
    j->d = d;
    j->vs = vk_load_shader(d.vs);
    j->fs = vk_load_shader(d.fs);
    return j->vs && j->fs;
}

static void pipe_worker(void *) {
    for (;;) {
        u32 i = g_pipe_next.fetch_add(1, std::memory_order_relaxed);
        if (i >= g_pipe_njobs) return;
        PipeJob &j = g_pipe_jobs[i];
        u64 t0 = platform_time_ns();
        VkPipeline p = j.graphics ? build_graphics(j.d, j.vs, j.fs) : build_compute(j);
        j.ns = platform_time_ns() - t0;
        *j.out = p;
    }
}

bool vk_pipelines_build(u32 *count, u32 *threads, const char **slowest, f64 *slowest_ms) {
    // Потоков — по ядрам, кроме главного (он тоже собирает), не больше MAX_PIPE_THREADS. Создание мира в своём
    // потоке идёт параллельно (run_window.cpp), поэтому одно ядро оставлено ему
    u32 cores = platform_cpu_cores();
    u32 extra = cores > 2 ? cores - 2 : 0;
    if (extra > MAX_PIPE_THREADS - 1) extra = MAX_PIPE_THREADS - 1;
    if (extra + 1 > g_pipe_njobs) extra = g_pipe_njobs ? g_pipe_njobs - 1 : 0;
    g_pipe_next.store(0, std::memory_order_relaxed);
    PlatformThread *ts[MAX_PIPE_THREADS] = {};
    u32 started = 0;
    for (u32 k = 0; k < extra; ++k)
        if ((ts[started] = platform_thread_start("pipelines", pipe_worker, nullptr))) ++started;
    pipe_worker(nullptr);
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
    if (count) *count = g_pipe_njobs;
    if (threads) *threads = started + 1;
    if (slowest) *slowest = worst_name;
    if (slowest_ms) *slowest_ms = (f64)worst / 1e6;
    g_pipe_njobs = 0;
    g_pipe_overflow = false;
    return ok;
}
