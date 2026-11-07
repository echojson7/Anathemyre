// Внутреннее состояние рендера, общее для файлов render/. Снаружи render/ не включать
#pragma once
#include "render.h"
#include "volk/volk.h"
#include "vma/vk_mem_alloc.h"

constexpr u32 FRAMES_IN_FLIGHT = 2;
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
    VkDeviceAddress vertices, instances, materials, lines, points;
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
};

// Небо (vk_sky.cpp, docs/sky.md): таблицы в буферах видеопамяти, считают их compute-шейдеры sky_*.comp
struct SkyGpu {
    GpuBuffer trans, ms, sun, moon, out;
    VkPipeline pipe_trans = VK_NULL_HANDLE, pipe_ms = VK_NULL_HANDLE, pipe_view = VK_NULL_HANDLE,
               pipe_sum = VK_NULL_HANDLE;
    bool luts_ready = false;
    RenderSky last{};  // параметры, по которым посчитаны таблицы пропускания и многократного рассеяния
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
    GpuBuffer instances;  // экземпляры в порядке ключей
    GpuBuffer points;
    GpuBuffer lines;
    GpuBuffer decals;
    bool timed = false;   // в этом слоте записаны метки времени
};

struct MeshSlot {
    VmaVirtualAllocation vtx = VK_NULL_HANDLE, idx = VK_NULL_HANDLE;
    u32 first_vertex = 0, vertex_count = 0;
    u32 first_index = 0, index_count = 0;
    u32 lod_count = 0;
    RenderLod lods[RENDER_MESH_MAX_LODS];  // от начала индексов сетки
    bool used = false;
    bool dying = false;  // ждёт освобождения в очереди
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
    u64 wait_ns = 0;  // ожидание в последнем render_frame: забор кадра, картинка swapchain, показ

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

    FrameSlot frames[FRAMES_IN_FLIGHT];
    u32 frame_index = 0;
    u64 frames_drawn = 0;

    // Общая геометрия и таблицы
    GpuBuffer vertices, indices, materials;
    VmaVirtualBlock vtx_block = VK_NULL_HANDLE, idx_block = VK_NULL_HANDLE;
    MeshSlot meshes[RENDER_MAX_MESHES];
    PendingFree pending[MAX_PENDING_FREES];
    u32 pending_count = 0;

    // Снимок кадра
    bool capture_next = false;
    bool capture_ready = false;
    GpuBuffer capture;
    VkExtent2D capture_extent = {};

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe_mesh = VK_NULL_HANDLE;
    VkPipeline pipe_ground = VK_NULL_HANDLE;
    VkPipeline pipe_points = VK_NULL_HANDLE;
    VkPipeline pipe_lines = VK_NULL_HANDLE;
    VkPipeline pipe_sky = VK_NULL_HANDLE;
    SkyGpu sky;

    TerrainGpu terrain;
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
bool vk_create_swapchain();
void vk_destroy_swapchain();
// Буфер, видимый процессору (запись каждый кадр) или только видеокарте (device_local)
bool vk_create_buffer(GpuBuffer &b, VkDeviceSize size, VkBufferUsageFlags usage, bool device_local = false);
void vk_destroy_buffer(GpuBuffer &b);
bool vk_create_pipelines();
// Шейдер из shader_dir (файл .spv); VK_NULL_HANDLE — нет файла или ошибка
VkShaderModule vk_load_shader(const char *name);
// Push-константы общей раскладки — как layout(push_constant) в data/shaders/common.glsl
struct PushConsts {
    VkDeviceAddress frame;
    u32 a, b;
};
// Стадии push-констант общей раскладки: все вызовы vkCmdPushConstants передают их целиком
constexpr VkShaderStageFlags PUSH_STAGES = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
// Небо (vk_sky.cpp): буферы и compute-конвейеры; таблицы в начале кадра (до прохода forward)
bool vk_sky_init();
void vk_sky_shutdown();
void vk_sky_fill(FrameGpu &g, const RenderFrame &f);
void vk_sky_record(VkCommandBuffer cmd, VkDeviceAddress frame);
void vk_destroy_pipelines();
// Интерфейс поверх кадра (vk_ui.cpp): свой конвейер без глубины, проход по картинке swapchain
bool vk_ui_init();
void vk_ui_shutdown();
void vk_ui_record(VkCommandBuffer cmd, u32 img);
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
