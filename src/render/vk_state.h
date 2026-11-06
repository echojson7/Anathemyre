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
    u64 pad0;
    i32 cam_cx, cam_cz;
    f32 cam_x, cam_y, cam_z;
    f32 tan_half_fov;
    f32 right[4], up[4], forward[4];   // .w не используется
    f32 proj_x, proj_y, near_m, pixel_angle;  // pixel_angle — 2·tan(fov/2)/высота: метров на пиксель на 1 м глубины
    f32 cell_min, cell_pixels;
    u32 pixel, ground_material;
    f32 sun_dir[4], sun_color[4], sky_color[4];
    f32 viewport[2], ndc_per_pixel[2];
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
    bool timed = false;   // в этом слоте записаны метки времени
};

struct MeshSlot {
    VmaVirtualAllocation vtx = VK_NULL_HANDLE, idx = VK_NULL_HANDLE;
    u32 first_vertex = 0, vertex_count = 0;
    u32 first_index = 0, index_count = 0;
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
void vk_destroy_pipelines();
// Геометрия и материалы (vk_geometry.cpp)
bool vk_geometry_init();
void vk_geometry_shutdown();
// Освободить место сеток, чьи кадры GPU уже дорисовал
void vk_geometry_collect();
// Разовая команда копирования: записать, отправить и дождаться
bool vk_one_shot(void (*record)(VkCommandBuffer, void *), void *user);
