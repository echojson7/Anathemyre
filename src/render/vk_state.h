// Внутреннее состояние рендера, общее для vk_device.cpp и vk_frame.cpp. Снаружи render/ не включать
#pragma once
#include "render.h"
#include "volk/volk.h"
#include "vma/vk_mem_alloc.h"

constexpr u32 FRAMES_IN_FLIGHT = 2;
constexpr u32 MAX_SWAPCHAIN_IMAGES = 8;

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    void *mapped = nullptr;
    VkDeviceAddress address = 0;
    VkDeviceSize size = 0;
};

struct FrameSlot {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence done = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    GpuBuffer points;
    GpuBuffer lines;
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

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    u32 image_count = 0;
    VkImage images[MAX_SWAPCHAIN_IMAGES] = {};
    VkImageView views[MAX_SWAPCHAIN_IMAGES] = {};
    VkSemaphore rendered[MAX_SWAPCHAIN_IMAGES] = {};  // на картинку: показ ждёт конца её отрисовки
    u32 rendered_count = 0;
    bool swapchain_dirty = false;

    FrameSlot frames[FRAMES_IN_FLIGHT];
    u32 frame_index = 0;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe_triangle = VK_NULL_HANDLE;
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

bool vk_check(VkResult r, const char *what);
bool vk_create_swapchain();
void vk_destroy_swapchain();
bool vk_create_buffer(GpuBuffer &b, VkDeviceSize size, VkBufferUsageFlags usage);
void vk_destroy_buffer(GpuBuffer &b);
bool vk_create_pipelines();
void vk_destroy_pipelines();
