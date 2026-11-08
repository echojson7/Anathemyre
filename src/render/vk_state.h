// Внутреннее состояние рендера, общее для файлов render/. Снаружи render/ не включать
#pragma once
#include "render.h"
#include "volk/volk.h"
#include "vma/vk_mem_alloc.h"

constexpr u32 FRAMES_IN_FLIGHT = 2;
// GPU-таймеры: метки на границах проходов, до GPU_MAX_MARKS на кадр
constexpr u32 GPU_MAX_MARKS = RENDER_MAX_PASSES + 1;
constexpr u32 MAX_SWAPCHAIN_IMAGES = 8;
// Общие буферы сеток (docs/frame.md): вершины и индексы всех сеток, место выдаёт виртуальный блок VMA
constexpr VkDeviceSize GEOMETRY_VERTEX_BYTES = 64ull << 20;
constexpr VkDeviceSize GEOMETRY_INDEX_BYTES = 32ull << 20;
// Освобождённое место сетки отдаётся через столько кадров: GPU точно дорисовал всё, что её читало
constexpr u32 FREE_DELAY_FRAMES = FRAMES_IN_FLIGHT + 1;
constexpr u32 MAX_PENDING_FREES = 256;

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    void *mapped = nullptr;  // только у видимых процессору
    VkDeviceAddress address = 0;
    VkDeviceSize size = 0;
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    VkImageView view = VK_NULL_HANDLE;
};

// Данные кадра для шейдеров (FrameData в data/shaders/common.glsl), пишутся раз за кадр
struct FrameGpu {
    VkDeviceAddress vertices, materials, lines, points;
    VkDeviceAddress t_height, t_mask;
    VkDeviceAddress t_water, t_mask2, t_dune, t_fine;
    i32 cam_cx, cam_cz;
    f32 cam_x, cam_y, cam_z;
    f32 tan_half_fov;
    f32 right[4], up[4], forward[4];   // .w не используется
    f32 proj_x, proj_y, near_m, pixel_angle;  // pixel_angle — 2·tan(fov/2)/высота: метров на пиксель на 1 м глубины
    f32 cell_min, cell_pixels;
    u32 pixel, pad1;
    f32 sun_dir[4], sun_color[4];
    f32 viewport[2], ndc_per_pixel[2];
    i32 t_chunk0, t_size_x, t_size_z, t_nx, t_nz, t_nmx, t_nmz;
    u32 t_dune_seed;
    i32 t_fine_off[4], t_fine_n, t_loc_m;  // карты дюн 2 м (RenderTerrain::fine): начало локации, −1 — нет
    i32 t_oct_lp[5], t_oct_amp[5];
    u32 t_oct_seed[5], t_on;
    i32 clip_n, clip_levels, clip_morph, clip_pad;
    i32 clip[12][4];   // начало кольца в полуметрах от угла карты (x, z), дыра кольца (x, z)
    u32 ground_layers[8];  // материалы слоёв земли (RENDER_GROUND_LAYERS), остаток — 0
    i32 t_mip[8];      // начало уровня k сглаженной сетки высот (шаг 8·2^k м) в буфере высот, номера u16
    VkDeviceAddress sky_trans, sky_ms, sky_sun, sky_moon, sky_out;  // таблицы неба (vk_sky.cpp)
    RenderSky sky;
    VkDeviceAddress decals;  // RenderDecal подряд
    u32 decal_count, decal_pad;
    // Кадр мира (этап 5, vk_world.cpp): таблицы сеток и кластеров, экземпляры на GPU, отсечение, индексы
    VkDeviceAddress meshes, clusters, vis_instances, vis, cull, draws, indices, g_indices;
    u32 instance_count, vis_max;
    u32 g_first[12];  // начало индексов кольца земли L в этом кадре
    // Froxels (vk_froxel.cpp): огни кадра и списки огней и декалей по клеткам
    VkDeviceAddress lights, froxels, media;
    u32 light_count, froxel_x, froxel_y, froxel_z;
    f32 froxel_near, froxel_far;
    u32 froxel_on, froxel_heat;
    VkDeviceAddress g_verts;  // вершины колец земли кадра (ground_bake.comp), 32 байта на вершину
    VkDeviceAddress stats;    // счётчики GPU кадра (STAT_*), vk_diag.cpp
    VkDeviceAddress tiles;    // плитки по видам (Tiles в common.glsl, docs/tiles.md)
    u32 tile_cap, tile_pad;   // мест в списке одного вида
    // Кеш поверхности земли (vk_gcache.cpp, docs/gcache.md)
    VkDeviceAddress gc_table, gc_pages, gc_texels, gc_req, gc_cells;
    u32 gc_w, gc_pages_n, gc_req_cap, gc_frame;
    i32 gc_tpc0;
    u32 gc_on, gc_pad0, gc_pad1;
    // Карта горизонта земли (vk_horizon.cpp, docs/horizon.md)
    VkDeviceAddress h_map;
    u32 h_on;
    f32 h_max;
    // Скиннинг людей (vk_skin.cpp, docs/people.md): работы кадра и палитры костей
    VkDeviceAddress skin_jobs, bones;
    VkDeviceAddress vertices_w, clusters_w;  // те же общие вершины и кластеры — для записи (skin*.comp)
    u32 skin_count, skin_pad;
    // Свет (docs/light.md): время кадра, сразу ли привыкать глазу, тонмаппинг, качество света
    f32 dt;
    u32 eye_snap, tonemap, light_q;
};

// Счётчики GPU (Stats в common.glsl): номера — как STAT_* там
constexpr u32 STAT_VIS_WANT = 0;       // сколько видимых кластеров нашло отсечение (до обрезки по VIS_MAX_CLUSTERS)
constexpr u32 STAT_FROXEL_LIGHTS = 1;  // клеток froxels, где огни не влезли
constexpr u32 STAT_FROXEL_DECALS = 2;  // клеток froxels, где декали не влезли
// Счётчики работы освещения (docs/tiles.md)
constexpr u32 STAT_TILES = 3;          // плитки по видам, TILE_KINDS подряд
constexpr u32 STAT_PX_SKY = 8, STAT_PX_GROUND = 9, STAT_PX_MESH = 10;  // пиксели по видам
constexpr u32 STAT_SURF_GROUND = 11, STAT_SURF_MESH = 12;             // поверхности: surface() и свет раз на ячейку
constexpr u32 STAT_SHADOW_TAPS = 13;  // выборки карты горизонта (тень земли, docs/horizon.md)
// Кеш поверхности земли (docs/gcache.md): страницы, прочитанные кадром; запечённые; не получившие места;
// поверхности земли, посчитанные освещением мимо кеша (крутые склоны, нет места)
constexpr u32 STAT_GC_USED = 14, STAT_GC_BAKED = 15, STAT_GC_OVER = 16, STAT_GC_PX_DIRECT = 17;
constexpr u32 STAT_SKIN_VERTS = 18;  // вершин скинено (skin.comp)
constexpr u32 STAT_COUNT = 20;
// Виды плиток кадра мира (TILE_* в common.glsl): небо, земля из кеша (и небо), сетки (и небо), смесь земли и
// сеток, земля на пиксель (и небо)
constexpr u32 TILE_KINDS = 5;
constexpr u32 TILE_KIND_GROUND = 1, TILE_KIND_MIX = 3, TILE_KIND_GROUND_PX = 4;
constexpr u32 TILE_HEADER = TILE_KINDS * 16;  // косвенные dispatch видов перед списками
// Буфер чтения кадра: число видимых кластеров, с READBACK_STATS — счётчики, с READBACK_EYE — глаз (sky_out [4], [5])
constexpr VkDeviceSize READBACK_STATS = 16, READBACK_EYE = READBACK_STATS + STAT_COUNT * 4;
constexpr VkDeviceSize READBACK_BYTES = READBACK_EYE + 32;
constexpr u32 SKY_OUT_EYE = 4;  // sky_out: [4], [5] — экспозиция и освещённость (sky_sum.comp, common.glsl)

// Небо (vk_sky.cpp, docs/sky.md): таблицы в буферах видеопамяти, считают их compute-шейдеры sky_*.comp
struct SkyGpu {
    GpuBuffer trans, ms, sun, moon, out;
    VkPipeline pipe_trans = VK_NULL_HANDLE, pipe_ms = VK_NULL_HANDLE, pipe_view = VK_NULL_HANDLE,
               pipe_sum = VK_NULL_HANDLE;
    bool luts_ready = false;
    RenderSky last{};  // параметры, по которым посчитаны таблицы пропускания и многократного рассеяния
    u8 rg_trans = 0, rg_ms = 0, rg_sun = 0, rg_moon = 0, rg_out = 0;  // в графе кадра
};

// Земля clipmap'ом (vk_terrain.cpp): кольцо L — (2n+1)² вершин с шагом 0.5·2^L м. Кольцо 0 — сплошная сетка,
// у остальных дыра n×n под кольцо мельче сдвинута на n/2 или n/2+1 по каждой оси: 4 буфера индексов
constexpr i32 CLIP_N = 128;
constexpr i32 TERRAIN_MIPS = 6;  // уровни сглаженной сетки высот 8..256 м для дальних колец
constexpr i32 CLIP_LEVELS = 10;   // шаг до 256 м, сторона последнего кольца 65 км — вся карта от любой точки
constexpr i32 CLIP_MORPH = 32;    // ширина перехода к следующему кольцу, квадратов
struct TerrainGpu {
    bool on = false;
    GpuBuffer height, mask, water, index, mask2, dune, fine;
    u32 first_full = 0, count_full = 0;
    u32 first_ring[4] = {}, count_ring = 0;
    RenderTerrain desc{};
    i32 origin[CLIP_LEVELS][2] = {};
    u32 ring[CLIP_LEVELS] = {};  // какой из 4 буферов дыры у кольца в этом кадре
    i32 mip_off[TERRAIN_MIPS] = {};  // начало уровня сглаженной сетки в буфере высот, номера u16
};

struct FrameSlot {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence done = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    GpuBuffer frame;      // FrameGpu
    GpuBuffer points;
    GpuBuffer lines;
    GpuBuffer decals;
    GpuBuffer lights;       // огни кадра (RenderPointLight)
    GpuBuffer inst_upload;  // изменившиеся экземпляры кадра мира → постоянный буфер на GPU (копией)
    GpuBuffer readback;     // число видимых кластеров и счётчики GPU кадра (READBACK_*)
    GpuBuffer skin_jobs;    // работы скиннинга кадра (SkinJobGpu)
    GpuBuffer bones;        // палитры костей людей кадра (RenderBone)
    u8 rg_readback = 0;     // он же в графе кадра
    bool stats_pending = false;  // readback заполнит отправленный кадр
    u32 marks = 0;          // записано меток времени
    const char *mark_names[GPU_MAX_MARKS] = {};  // проход, который кончается меткой k (k ≥ 1)
};

// Кадр мира (vk_world.cpp): постоянные экземпляры, отсечение, visibility buffer, освещение в compute, стиль
struct WorldGpu {
    GpuBuffer inst;      // экземпляры на GPU, порядок — как в RenderFrame::instances
    GpuBuffer cull;      // CullData
    GpuBuffer draws;     // VkDrawIndexedIndirectCommand на видимый кластер
    GpuBuffer vis;       // видимый кластер: экземпляр, кластер
    GpuBuffer gverts;    // вершины колец земли кадра (ground_bake.comp)
    GpuBuffer tiles;     // плитки по видам: косвенные dispatch и списки (light_tiles.comp), под размер кадра
    u32 tile_cap = 0;    // плиток кадра — мест в списке одного вида
    u32 inst_count = 0;  // сколько экземпляров лежит в inst
    GpuImage id;         // visibility buffer R32_UINT
    GpuImage hdr;        // линейный цвет кадра RGBA16F (освещение → стиль)
    VkExtent2D extent = {};
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipeline pipe_vis_mesh = VK_NULL_HANDLE, pipe_vis_ground = VK_NULL_HANDLE, pipe_style = VK_NULL_HANDLE;
    VkPipeline pipe_cull_count = VK_NULL_HANDLE, pipe_cull_scan = VK_NULL_HANDLE, pipe_cull_write = VK_NULL_HANDLE;
    VkPipeline pipe_light[TILE_KINDS] = {}, pipe_tiles = VK_NULL_HANDLE, pipe_ground_bake = VK_NULL_HANDLE;
    u32 vis_clusters = 0;  // видимых кластеров в последнем прочитанном кадре
    u32 work[STAT_COUNT] = {};  // счётчики последнего прочитанного кадра мира (STAT_*)
    f32 eye[8] = {};            // глаз того же кадра: sky_out [4], [5] (render_light)
    bool work_valid = false;
    // Ресурсы графа кадра (vk_graph.h)
    u8 rg_inst = 0, rg_cull = 0, rg_draws = 0, rg_vis = 0, rg_gverts = 0, rg_id = 0, rg_hdr = 0, rg_tiles = 0;
};

// Кеш поверхности земли (vk_gcache.cpp, docs/gcache.md): виртуальная текстура страницами 8×8 текселей по
// уровням ячейки. Окно страниц уровня вокруг камеры — gc_w × gc_w записей, пул физических страниц — pages_n
constexpr u32 GC_LEVELS = 13;   // как GC_LEVELS в gcache.glsl
constexpr u32 GC_PAGE_TEXELS = 64;
constexpr u32 GC_TEXEL_BYTES = 16;  // два варианта следа пикселя по 8 байт
struct GcacheGpu {
    GpuBuffer table, pages, texels, req, cells;
    u32 w = 0, pages_n = 0, req_cap = 0, frame = 0;
    i32 tpc0 = 0;
    bool reset = true;       // очистить окна и страницы в начале следующего кадра мира
    bool on = false;         // кеш в этом кадре (иначе вся земля — на пиксель в «смеси»)
    const char *off = nullptr;  // почему кеш выключен (F3, журнал); nullptr — включён или земли нет
    u32 grow = 0;            // заявок без места в прочитанном кадре: пул растёт перед следующим кадром
    u32 materials_gen = 0, terrain_gen = 0, seen_materials = ~0u, seen_terrain = ~0u;
    f32 cell_min = 0, cell_pixels = 0;
    u32 layers[8] = {};
    VkPipeline pipe_scan = VK_NULL_HANDLE, pipe_args = VK_NULL_HANDLE, pipe_bake = VK_NULL_HANDLE;
    u8 rg_table = 0, rg_pages = 0, rg_texels = 0, rg_req = 0, rg_cells = 0;
};

// Карта горизонта земли (vk_horizon.cpp, docs/horizon.md): H_DIRS/2 слов u32 на узел сетки 8 м. Собирается
// одним проходом в первом кадре после загрузки земли и дальше только читается
struct HorizonGpu {
    GpuBuffer map;
    VkPipeline pipe = VK_NULL_HANDLE;
    u8 rg_map = 0;
    bool dirty = false;  // земля новая — собрать в этом кадре
    u32 nx = 0, nz = 0;
    f32 h_max = 0;
};

// Диагностика GPU (vk_diag.cpp): что умеет драйвер для разбора сбоя, буфер меток AMD, счётчики GPU
struct DiagGpu {
    bool nv_checkpoints = false, amd_marker = false, device_fault = false;
    GpuBuffer markers;  // метки AMD: последний начатый и последний законченный проход
    GpuBuffer stats;    // счётчики GPU (STAT_COUNT u32)
    u8 rg_stats = 0;
};

// Кадр мира (этап 5, docs/frame.md «Кадр мира»): таблицы для отсечения на GPU. Раскладки — MeshInfo и Cluster
// в data/shaders/common.glsl
constexpr u32 GEOMETRY_MAX_CLUSTERS = 1u << 18;  // кластеров всех сеток
constexpr u32 VIS_MAX_CLUSTERS = 1u << 18;       // видимых кластеров за кадр
struct MeshGpu {
    u32 first_vertex, first_index, lod_count, pad;
    f32 sphere[4];  // сфера сетки в её осях
    struct Lod {
        u32 first_cluster, cluster_count;  // номера в общей таблице кластеров
        f32 error;
        u32 pad;
    } lods[8];
};
static_assert(sizeof(MeshGpu) == 160, "MeshGpu — как MeshInfo в common.glsl");
struct ClusterGpu {
    u32 first_index, tri_count;  // от начала индексов сетки
    f32 sphere[4];
    u32 pad[2];
};
static_assert(sizeof(ClusterGpu) == 32, "ClusterGpu — как Cluster в common.glsl");
// Счёт отсечения (CullData в common.glsl): число вызовов, на экземпляр — число кластеров с LOD и начало
constexpr VkDeviceSize CULL_BYTES = 16 + 2ull * RENDER_MAX_INSTANCES * sizeof(u32);

struct MeshSlot {
    VmaVirtualAllocation vtx = VK_NULL_HANDLE, idx = VK_NULL_HANDLE, cls = VK_NULL_HANDLE;
    u32 skin = 0;  // копия человека (render_skinned_create): номер источника скиннинга; индексы — источника
    u32 first_vertex = 0, vertex_count = 0;
    u32 first_index = 0, index_count = 0;
    u32 first_cluster = 0, cluster_count = 0;
    u32 lod_count = 0;
    RenderLod lods[RENDER_MESH_MAX_LODS];  // от начала индексов сетки
    bool used = false;
    bool dying = false;  // ждёт освобождения в очереди
};

// Скиннинг (vk_skin.cpp, docs/people.md «Скиннинг»): данные источников в одном буфере видеопамяти (только
// растёт, до render_shutdown), работы кадра — в буфере слота. Раскладка SkinJobGpu — SkinJob в skin.comp
constexpr VkDeviceSize SKIN_DATA_BYTES = 32ull << 20;
struct SkinSource {
    u32 mesh = 0, vertex_count = 0, bone_count = 0, morph_count = 0;
    VkDeviceAddress skin = 0, morphs = 0;  // слова весов (2 на вершину) и морфов (3 на вершину на морф)
    f32 step[RENDER_MAX_MORPHS] = {};
    u32 lod_vertices[RENDER_MESH_MAX_LODS] = {};
};
struct SkinJobGpu {
    u32 src_vertex, dst_vertex, vertex_count, first_bone;
    VkDeviceAddress skin, morphs;
    u32 morph_count, first_cluster, cluster_count, first_index;
    f32 morph[RENDER_MAX_MORPHS];  // веса морфов
    f32 step[RENDER_MAX_MORPHS];   // шаг морфа, м на единицу сдвига
};
static_assert(sizeof(SkinJobGpu) == 112, "SkinJobGpu — как SkinJob в common.glsl");
struct SkinGpu {
    GpuBuffer data;
    VkDeviceSize used = 0;
    SkinSource src[RENDER_MAX_SKINS + 1];  // 0 — нет
    u32 count = 0;
    VkPipeline pipe_skin = VK_NULL_HANDLE, pipe_bounds = VK_NULL_HANDLE;
    u32 jobs = 0, max_verts = 0, max_clusters = 0;  // этого кадра
    u32 skinned_verts = 0;                          // вершин скинено в этом кадре (CPU)
    u8 lod_min[RENDER_MAX_INSTANCES] = {};          // LOD экземпляра не тоньше (RenderInstance::lod_min)
    u32 touched[RENDER_MAX_SKINNED] = {};           // экземпляры с lod_min этого кадра (обнулить в следующем)
    u32 touched_count = 0;
    u8 rg_vertices = 0, rg_clusters = 0;            // общие буферы вершин и кластеров в графе кадра
};
struct PendingFree {
    u32 mesh;
    u64 frame;  // освободить, когда кадров нарисовано не меньше
};

struct VkState {
    RenderConfig cfg;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    u32 queue_family = 0;
    VmaAllocator vma = nullptr;
    char gpu_name[256] = {};
    f32 timestamp_period = 0;  // нс на тик; 0 — меток времени нет
    VkQueryPool queries = VK_NULL_HANDLE;
    f64 gpu_ms = 0;
    RenderCpuTimes cpu{};  // части последнего render_frame
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    u32 image_count = 0;
    VkImage images[MAX_SWAPCHAIN_IMAGES] = {};
    VkImageView views[MAX_SWAPCHAIN_IMAGES] = {};
    VkSemaphore rendered[MAX_SWAPCHAIN_IMAGES] = {};  // на картинку: показ ждёт конца её отрисовки
    u32 rendered_count = 0;
    bool swapchain_dirty = false;
    GpuImage depth;  // D32F, обратная глубина
    VkExtent2D depth_extent = {};
    u8 rg_depth = 0, rg_swap = 0;  // глубина и картинка swapchain кадра в графе

    FrameSlot frames[FRAMES_IN_FLIGHT];
    u32 frame_index = 0;
    u64 frames_drawn = 0;

    // Общая геометрия и таблицы
    GpuBuffer vertices, indices, materials;
    GpuBuffer mesh_table;  // MeshGpu на номер сетки (видим процессору, пишется при создании сетки)
    GpuBuffer clusters;    // ClusterGpu всех сеток
    VmaVirtualBlock vtx_block = VK_NULL_HANDLE, idx_block = VK_NULL_HANDLE, cls_block = VK_NULL_HANDLE;
    MeshSlot meshes[RENDER_MAX_MESHES];
    PendingFree pending[MAX_PENDING_FREES];
    u32 pending_count = 0;

    // Снимок кадра
    bool capture_next = false;
    bool capture_ready = false;
    GpuBuffer capture;
    u8 rg_capture = 0;
    VkExtent2D capture_extent = {};

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe_points = VK_NULL_HANDLE;
    VkPipeline pipe_lines = VK_NULL_HANDLE;
    SkyGpu sky;

    TerrainGpu terrain;
    WorldGpu world;
    GcacheGpu gcache;
    HorizonGpu horizon;
    // Froxels (vk_froxel.cpp): клетки 64×64 пикселя × FROXEL_Z слоёв глубины
    GpuBuffer froxels;     // списки на клетку, FROXEL_STRIDE u32
    GpuBuffer media;       // среда на клетку (туман и пыль, vec4) — место для света пользователя, пока нули
    u32 froxel_cap = 0;    // на сколько клеток выделены буферы
    VkPipeline pipe_froxel = VK_NULL_HANDLE;
    u8 rg_froxels = 0, rg_media = 0;
    DiagGpu diag;
    SkinGpu skin;
    // Время проходов на GPU за прошлый кадр слота (render_gpu_passes)
    u32 pass_count = 0;
    const char *pass_names[GPU_MAX_MARKS] = {};
    f64 pass_ms[GPU_MAX_MARKS] = {};
};

extern VkState g_vk;

// Структура Vulkan с sType, остальное нулями
template <typename T>
inline T vk_info(VkStructureType type) {
    T v{};
    v.sType = type;
    return v;
}
extern u32 g_vk_validation_errors;

constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

bool vk_check(VkResult r, const char *what);
// Диагностика GPU (vk_diag.cpp, docs/graph.md «Сбой GPU», «Переполнения»)
bool vk_diag_init();
void vk_diag_shutdown();
void vk_diag_frame(u64 frame);  // начало записи кадра: новый круг меток проходов, переполнения процессора с нуля
void vk_diag_begin(VkCommandBuffer cmd, u32 pass, const char *name);
void vk_diag_end(VkCommandBuffer cmd, u32 pass);
void vk_device_lost();  // в журнал: последние проходы и сведения драйвера (раз за запуск)
// Живые буферы и их имена — для адреса сбоя и сообщений слоёв проверки
void vk_buffer_track(const GpuBuffer &b);
void vk_buffer_untrack(const GpuBuffer &b);
void vk_buffer_name(const GpuBuffer &b, const char *name);
void vk_image_name(VkImage img, const char *name);
// Переполнение на стороне процессора в этом кадре (want > cap — в список)
void vk_overflow(const char *what, u64 want, u64 cap);
bool vk_create_swapchain();
void vk_destroy_swapchain();
// Буфер, видимый процессору (запись каждый кадр) или только видеокарте (device_local)
bool vk_create_buffer(GpuBuffer &b, VkDeviceSize size, VkBufferUsageFlags usage, bool device_local = false);
void vk_destroy_buffer(GpuBuffer &b);
bool vk_create_pipelines();
// Кеш конвейеров на диске (vk_pipecache.cpp): открыть до создания конвейеров, записать после, закрыть при выходе
bool vk_pipeline_cache_open();
void vk_pipeline_cache_save();
void vk_pipeline_cache_close();
// Шейдер из shader_dir (файл .spv); VK_NULL_HANDLE — нет файла или ошибка
VkShaderModule vk_load_shader(const char *name);
// Push-константы общей раскладки — как layout(push_constant) в data/shaders/common.glsl
struct PushConsts {
    VkDeviceAddress frame;
    u32 a, b;
};
// Стадии push-констант общей раскладки: все вызовы vkCmdPushConstants передают их целиком
constexpr VkShaderStageFlags PUSH_STAGES = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
// Небо (vk_sky.cpp): буферы и compute-конвейеры; проходы кадра — в vk_graph.h
bool vk_sky_init();
void vk_sky_shutdown();
void vk_sky_fill(FrameGpu &g, const RenderFrame &f);
void vk_destroy_pipelines();
// Compute-конвейер общей раскладки из шейдера name (.spv); spec — константы специализации или nullptr.
// VK_NULL_HANDLE — ошибка
VkPipeline vk_make_compute(const char *name, const VkSpecializationInfo *spec = nullptr);
// Метка времени GPU после прохода name (первая метка кадра — без имени); ставит граф (rg_execute)
void vk_mark(VkCommandBuffer cmd, const char *name);
// Графический конвейер общей раскладки
struct PipeDesc {
    const char *vs, *fs;
    VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool blend = false;
    bool depth_write = true;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    VkFormat color = VK_FORMAT_UNDEFINED;  // UNDEFINED — формат swapchain
    bool depth_test = true;
};
VkPipeline vk_make_pipeline(const PipeDesc &d);
// Кадр мира (vk_world.cpp): набор картинок для шейдеров (до раскладки конвейеров), конвейеры, кадр
bool vk_world_layout();
bool vk_world_init();
void vk_world_shutdown();
void vk_world_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs);
// Кеш поверхности земли (vk_gcache.cpp, docs/gcache.md): конвейеры; буферы под кадр и сброс при смене того, из
// чего считаются тексели (images — картинки кадра мира есть); прочитанные счётчики кадра (рост пула)
// Карта горизонта (vk_horizon.cpp, docs/horizon.md): конвейер сборки; буфер под землю (render_terrain_set);
// адрес и готовность в кадр
bool vk_horizon_init();
void vk_horizon_shutdown();
bool vk_horizon_terrain(const u16 *height, u32 nx, u32 nz);
void vk_horizon_fill(FrameGpu &g, const RenderFrame &f);
void vk_horizon_free();
bool vk_gcache_init();
void vk_gcache_shutdown();
void vk_gcache_fill(FrameGpu &g, const RenderFrame &f, bool images);
void vk_gcache_read(const u32 *stats);
// Froxels (vk_froxel.cpp, docs/frame.md «Froxels»): сетка клеток, огни и декали по клеткам
constexpr u32 FROXEL_TILE = 64, FROXEL_Z = 32, FROXEL_STRIDE = 96;
constexpr f32 FROXEL_NEAR = 1.0f, FROXEL_FAR = 512.0f;
bool vk_froxel_init();
void vk_froxel_shutdown();
void vk_froxel_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs);
// Скиннинг людей (vk_skin.cpp): данные и конвейеры; работы кадра и LOD людей (до vk_world_fill)
bool vk_skin_init();
void vk_skin_shutdown();
void vk_skin_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs);
// Интерфейс поверх кадра (vk_ui.cpp): свой конвейер без глубины; проход — в vk_graph.h
bool vk_ui_init();
void vk_ui_shutdown();
// Геометрия и материалы (vk_geometry.cpp)
bool vk_geometry_init();
void vk_geometry_shutdown();
// Освободить место сеток, чьи кадры GPU уже дорисовал
void vk_geometry_collect();
// Земля (vk_terrain.cpp): начала колец по камере и поля кадра; освобождение
void vk_terrain_fill(FrameGpu &g, const RenderFrame &f);
void vk_terrain_shutdown();
// Разовая команда копирования: записать, отправить и дождаться
bool vk_one_shot(void (*record)(VkCommandBuffer, void *), void *user);
