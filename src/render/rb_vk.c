// Слой рендера на Vulkan 1.3: dynamic rendering, synchronization2, push descriptors.
// Этап 2: instance, устройство, поверхность X11/Win32, swapchain, кадры в полёте, проход в окно,
// очистка, конвейер из .spv. Текстуры, буферы и привязки — этап 3.
// Окно — своя картинка RGBA8 + глубина D32F. Кадр в ней лежит как в GL (строка 0 внизу картинки),
// поэтому координаты областей, ножниц и gl_FragCoord совпадают с GL без пересчёта;
// переворот один — blit в swapchain в конце кадра.
#include "rb_vk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"
#include "../platform/platform.h"
#include "vk.h"

#define X(name) static PFN_##name name;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
VK_GLOBAL_FUNCS(X)
VK_INSTANCE_FUNCS(X)
VK_DEVICE_FUNCS(X)
#undef X

#define VK_OK(call) vk_ok((call), #call)

#define MAX_SWAP_IMAGES 8
#define MAX_PIPES 256
#define WIN_COLOR_FMT VK_FORMAT_R8G8B8A8_UNORM
#define WIN_DEPTH_FMT VK_FORMAT_D32_SFLOAT

// Картинка с памятью и текущей раскладкой (барьеры ставит слой)
typedef struct VkTex {
    VkImage        image;
    VkImageView    view;
    VkDeviceMemory mem;
    VkImageLayout  layout;
    VkImageAspectFlags aspect;
    i32            w, h;
} VkTex;

#define FRAME_TRASH 64

typedef struct Frame {
    VkCommandPool   pool;
    VkCommandBuffer cmd;
    VkFence         done;      // кадр слота отработал на GPU
    VkSemaphore     acquired;  // картинка swapchain получена
    // Освобождённое в этом слоте: удаляется, когда слот снова свободен (GPU мог ещё читать)
    VkPipeline      trash_pipes[FRAME_TRASH];
    u32             trash_n;
} Frame;

typedef struct Pipe {
    VkPipeline pipe;
    bool       used;
} Pipe;

static struct {
    Arena   *scratch;
    char     data_dir[256];
    bool     validation;
    u32      validation_errors;

    VkInstance               instance;
    VkDebugUtilsMessengerEXT messenger;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         gpu;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties mem_props;
    VkDevice                 device;
    VkQueue                  queue;
    u32                      queue_family;
    bool                     have_depth_clip_control;
    RbCaps                   caps;

    VkSwapchainKHR swapchain;
    VkFormat       swap_fmt;
    VkExtent2D     swap_extent;
    u32            swap_count;
    VkImage        swap_images[MAX_SWAP_IMAGES];
    VkSemaphore    swap_ready[MAX_SWAP_IMAGES];  // кадр готов к показу: своя на каждую картинку
    VkImageLayout  swap_layout[MAX_SWAP_IMAGES];
    bool           swap_dirty;                   // пересобрать перед следующим показом
    RbPresent      present_want, present_have;

    VkTex   win_color, win_depth;   // окно: сюда рисуют проходы без целей

    Frame   frames[RB_FRAMES];
    u32     slot;
    bool    in_frame, in_pass;
    VkCommandBuffer cmd;            // текущий буфер команд кадра
    i32     pass_w, pass_h;         // размер целей текущего прохода

    VkDescriptorSetLayout set_layout;   // пустой набор push-дескрипторов: привязки — этап 3
    VkPipelineLayout      pipe_layout;
    Pipe    pipes[MAX_PIPES];
    u8      push[RB_PUSH_BYTES];
    bool    push_dirty;
    RbPipe  bound;
} g_vk;

static bool vk_ok(VkResult r, const char *what)
{
    if (r == VK_SUCCESS) return true;
    log_error("Vulkan: %s → %d", what, (int)r);
    return false;
}

static PFN_vkVoidFunction inst_proc(const char *name)
{
    return vkGetInstanceProcAddr(g_vk.instance, name);
}

// Указатели функций приводятся через memcpy, как и в платформенном слое
#define LOAD(name, getter, missing)                                           \
    do {                                                                      \
        PFN_vkVoidFunction f_ = getter(#name);                                \
        memcpy(&name, &f_, sizeof f_);                                        \
        if (!name) missing;                                                   \
    } while (0)

static PFN_vkVoidFunction dev_proc(const char *name)
{
    return vkGetDeviceProcAddr(g_vk.device, name);
}

static VkBool32 VKAPI_PTR debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT type,
                                   const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)type;
    (void)user;
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        g_vk.validation_errors++;
        log_error("Vulkan validation: %s", data->pMessage);
    } else {
        log_info("Vulkan validation: %s", data->pMessage);
    }
    return 0;
}

static bool has_ext(const VkExtensionProperties *ext, u32 n, const char *name)
{
    for (u32 i = 0; i < n; i++)
        if (strcmp(ext[i].extensionName, name) == 0) return true;
    return false;
}

static bool create_instance(void)
{
    LOAD(vkEnumerateInstanceVersion, inst_proc, return false);
#define X(name) LOAD(name, inst_proc, { log_error("Vulkan: нет %s", #name); return false; });
    VK_GLOBAL_FUNCS(X)
#undef X
    u32 ver = 0;
    vkEnumerateInstanceVersion(&ver);
    if (ver < VK_API_VERSION_1_3) {
        log_error("Vulkan: загрузчик %u.%u, нужен 1.3", VK_API_VERSION_MAJOR(ver), VK_API_VERSION_MINOR(ver));
        return false;
    }

    VkExtensionProperties ext[256];
    u32 n_ext = ARRAY_COUNT(ext);
    vkEnumerateInstanceExtensionProperties(NULL, &n_ext, ext);
    const char *want[4];
    u32 n_want = 0;
    want[n_want++] = VK_KHR_SURFACE_EXTENSION_NAME;
#ifdef _WIN32
    want[n_want++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    want[n_want++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
    for (u32 i = 0; i < n_want; i++) {
        if (!has_ext(ext, n_ext, want[i])) {
            log_error("Vulkan: нет расширения %s", want[i]);
            return false;
        }
    }

    // Validation: в отладочной сборке, в релизе по ANA_VK_VALIDATION=1. Слоя нет — идём без него
    const char *env = getenv("ANA_VK_VALIDATION");
#ifdef NDEBUG
    bool validation = env && env[0] == '1';
#else
    bool validation = !(env && env[0] == '0');
#endif
    const char *layer = "VK_LAYER_KHRONOS_validation";
    if (validation) {
        VkLayerProperties layers[64];
        u32 n_layers = ARRAY_COUNT(layers);
        vkEnumerateInstanceLayerProperties(&n_layers, layers);
        bool found = false;
        for (u32 i = 0; i < n_layers; i++) found |= strcmp(layers[i].layerName, layer) == 0;
        if (!found || !has_ext(ext, n_ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            log_info("Vulkan: слоя validation нет, проверки выключены");
            validation = false;
        } else {
            want[n_want++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }
    }
    g_vk.validation = validation;

    VkDebugUtilsMessengerCreateInfoEXT dbg = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_cb,
    };
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "anathemyre",
        .pEngineName = "anathemyre",
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validation ? &dbg : NULL,   // сообщения и при создании instance
        .pApplicationInfo = &app,
        .enabledLayerCount = validation ? 1 : 0,
        .ppEnabledLayerNames = &layer,
        .enabledExtensionCount = n_want,
        .ppEnabledExtensionNames = want,
    };
    if (!VK_OK(vkCreateInstance(&ci, NULL, &g_vk.instance))) return false;

#define X(name) LOAD(name, inst_proc, (void)0);
    VK_INSTANCE_FUNCS(X)
#undef X
    if (validation && vkCreateDebugUtilsMessengerEXT)
        VK_OK(vkCreateDebugUtilsMessengerEXT(g_vk.instance, &dbg, NULL, &g_vk.messenger));
    return true;
}

static bool create_surface(void)
{
    PlatformNative nat = platform_native();
#ifdef _WIN32
    if (!vkCreateWin32SurfaceKHR) return false;
    VkWin32SurfaceCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = (HINSTANCE)nat.display,
        .hwnd = (HWND)(uintptr_t)nat.window,
    };
    return VK_OK(vkCreateWin32SurfaceKHR(g_vk.instance, &ci, NULL, &g_vk.surface));
#else
    if (!vkCreateXlibSurfaceKHR) return false;
    VkXlibSurfaceCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = (Display *)nat.display,
        .window = (Window)nat.window,
    };
    return VK_OK(vkCreateXlibSurfaceKHR(g_vk.instance, &ci, NULL, &g_vk.surface));
#endif
}

// Устройство подходит: 1.3, dynamic rendering, sync2, swapchain, push descriptors, очередь графики с показом
static i32 score_gpu(VkPhysicalDevice gpu, u32 *family)
{
    VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    vkGetPhysicalDeviceProperties2(gpu, &p2);
    const VkPhysicalDeviceProperties p = p2.properties;
    if (p.apiVersion < VK_API_VERSION_1_3) return -1;

    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 f = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f13 };
    vkGetPhysicalDeviceFeatures2(gpu, &f);
    if (!f13.dynamicRendering || !f13.synchronization2 || !f.features.multiDrawIndirect) return -1;

    VkExtensionProperties ext[512];
    u32 n = ARRAY_COUNT(ext);
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, ext);
    if (!has_ext(ext, n, VK_KHR_SWAPCHAIN_EXTENSION_NAME) || !has_ext(ext, n, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
        return -1;

    VkQueueFamilyProperties q[32];
    u32 nq = ARRAY_COUNT(q);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, q);
    for (u32 i = 0; i < nq; i++) {
        VkBool32 present = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, g_vk.surface, &present);
        if ((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (q[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && present &&
            q[i].timestampValidBits > 0) {
            *family = i;
            switch (p.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
            default: return 1;   // процессор (lavapipe)
            }
        }
    }
    return -1;
}

static bool optional_fn(const char *name)
{
    return strcmp(name, "vkCmdSetFragmentShadingRateKHR") == 0 || strstr(name, "DebugUtils") != NULL;
}

static bool create_device(void)
{
    VkPhysicalDevice gpus[16];
    u32 n = ARRAY_COUNT(gpus);
    vkEnumeratePhysicalDevices(g_vk.instance, &n, gpus);
    i32 best = -1;
    for (u32 i = 0; i < n; i++) {
        u32 fam = 0;
        i32 s = score_gpu(gpus[i], &fam);
        if (s > best) {
            best = s;
            g_vk.gpu = gpus[i];
            g_vk.queue_family = fam;
        }
    }
    if (best < 0) {
        log_error("Vulkan: нет устройства с 1.3, dynamic rendering, sync2 и push descriptors");
        return false;
    }
    vkGetPhysicalDeviceProperties(g_vk.gpu, &g_vk.props);
    vkGetPhysicalDeviceMemoryProperties(g_vk.gpu, &g_vk.mem_props);

    VkExtensionProperties ext[512];
    u32 n_ext = ARRAY_COUNT(ext);
    vkEnumerateDeviceExtensionProperties(g_vk.gpu, NULL, &n_ext, ext);
    const char *want[4] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    u32 n_want = 2;

    // Что есть у устройства
    VkPhysicalDeviceDepthClipControlFeaturesEXT clip = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT };
    VkPhysicalDeviceVulkan13Features a13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features a12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &a13 };
    VkPhysicalDeviceVulkan11Features a11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &a12 };
    VkPhysicalDeviceFeatures2 a = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &a11 };
    bool clip_ext = has_ext(ext, n_ext, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
    if (clip_ext) a13.pNext = &clip;
    vkGetPhysicalDeviceFeatures2(g_vk.gpu, &a);

    // Что включаем: обязательное проверено в score_gpu, остальное — если есть
    VkPhysicalDeviceDepthClipControlFeaturesEXT e_clip = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT, .depthClipControl = 1 };
    VkPhysicalDeviceVulkan13Features e13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = 1,
        .synchronization2 = 1,
        .maintenance4 = a13.maintenance4,
    };
    VkPhysicalDeviceVulkan12Features e12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &e13,
        .drawIndirectCount = a12.drawIndirectCount,
        .hostQueryReset = a12.hostQueryReset,
        .scalarBlockLayout = a12.scalarBlockLayout,
    };
    // gl_DrawID и gl_BaseInstance для людей одним вызовом (MultiDrawIndirect)
    VkPhysicalDeviceVulkan11Features e11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &e12,
        .shaderDrawParameters = a11.shaderDrawParameters,
    };
    VkPhysicalDeviceFeatures2 on = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &e11 };
    on.features.multiDrawIndirect = 1;
    on.features.drawIndirectFirstInstance = a.features.drawIndirectFirstInstance;
    on.features.depthClamp = a.features.depthClamp;
    on.features.depthBiasClamp = a.features.depthBiasClamp;
    on.features.samplerAnisotropy = a.features.samplerAnisotropy;
    on.features.fragmentStoresAndAtomics = a.features.fragmentStoresAndAtomics;
    on.features.shaderStorageImageReadWithoutFormat = a.features.shaderStorageImageReadWithoutFormat;
    on.features.shaderStorageImageWriteWithoutFormat = a.features.shaderStorageImageWriteWithoutFormat;
    on.features.independentBlend = a.features.independentBlend;
    // Глубина как в GL: отсечение по [-1, 1] без правки шейдеров
    g_vk.have_depth_clip_control = clip_ext && clip.depthClipControl;
    if (g_vk.have_depth_clip_control) {
        e13.pNext = &e_clip;
        want[n_want++] = VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME;
    }

    f32 prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = g_vk.queue_family,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &on,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
        .enabledExtensionCount = n_want,
        .ppEnabledExtensionNames = want,
    };
    if (!VK_OK(vkCreateDevice(g_vk.gpu, &ci, NULL, &g_vk.device))) return false;
    // Функции расширений, которых может не быть, грузятся без проверки
#define X(name) LOAD(name, dev_proc, if (!optional_fn(#name)) { log_error("Vulkan: нет %s", #name); return false; });
    VK_DEVICE_FUNCS(X)
#undef X
    vkGetDeviceQueue(g_vk.device, g_vk.queue_family, 0, &g_vk.queue);

    g_vk.caps.ubo_align = (u32)g_vk.props.limits.minUniformBufferOffsetAlignment;
    g_vk.caps.ssbo_align = (u32)g_vk.props.limits.minStorageBufferOffsetAlignment;
    g_vk.caps.vrs = false;   // этап 5
    return true;
}

static u32 find_memory(u32 type_bits, VkMemoryPropertyFlags want)
{
    for (u32 i = 0; i < g_vk.mem_props.memoryTypeCount; i++)
        if ((type_bits & (1u << i)) && (g_vk.mem_props.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

// Пока по выделению на картинку; свой распределитель кусками — этап 3
static bool tex_create(VkTex *t, VkFormat fmt, i32 w, i32 h, VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fmt,
        .extent = { (u32)w, (u32)h, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VK_OK(vkCreateImage(g_vk.device, &ci, NULL, &t->image))) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vk.device, t->image, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = find_memory(req.memoryTypeBits, 0);
    if (!VK_OK(vkAllocateMemory(g_vk.device, &ai, NULL, &t->mem))) return false;
    vkBindImageMemory(g_vk.device, t->image, t->mem, 0);
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .subresourceRange = { aspect, 0, 1, 0, 1 },
    };
    if (!VK_OK(vkCreateImageView(g_vk.device, &vi, NULL, &t->view))) return false;
    t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    t->aspect = aspect;
    t->w = w;
    t->h = h;
    return true;
}

static void tex_destroy(VkTex *t)
{
    if (t->view) vkDestroyImageView(g_vk.device, t->view, NULL);
    if (t->image) vkDestroyImage(g_vk.device, t->image, NULL);
    if (t->mem) vkFreeMemory(g_vk.device, t->mem, NULL);
    memset(t, 0, sizeof *t);
}

// Перевод картинки в раскладку: ждём все прошлые записи и чтения, это проще и честно для этапа 2
static void barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier2 b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { aspect, 0, 1, 0, 1 },
    };
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b };
    vkCmdPipelineBarrier2(cmd, &dep);
}

static void tex_layout(VkTex *t, VkImageLayout to)
{
    if (t->layout == to) return;
    barrier(g_vk.cmd, t->image, t->aspect, t->layout, to);
    t->layout = to;
}

static bool window_targets(i32 w, i32 h)
{
    w = MAX(w, 1);
    h = MAX(h, 1);
    if (g_vk.win_color.image && g_vk.win_color.w == w && g_vk.win_color.h == h) return true;
    vkDeviceWaitIdle(g_vk.device);
    tex_destroy(&g_vk.win_color);
    tex_destroy(&g_vk.win_depth);
    return tex_create(&g_vk.win_color, WIN_COLOR_FMT, w, h,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT) &&
           tex_create(&g_vk.win_depth, WIN_DEPTH_FMT, w, h, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_IMAGE_ASPECT_DEPTH_BIT);
}

static VkPresentModeKHR present_mode_vk(RbPresent m)
{
    switch (m) {
    case RB_PRESENT_MAILBOX: return VK_PRESENT_MODE_MAILBOX_KHR;
    case RB_PRESENT_IMMEDIATE: return VK_PRESENT_MODE_IMMEDIATE_KHR;
    default: return VK_PRESENT_MODE_FIFO_KHR;
    }
}

static void swapchain_destroy_images(void)
{
    for (u32 i = 0; i < g_vk.swap_count; i++) {
        if (g_vk.swap_ready[i]) vkDestroySemaphore(g_vk.device, g_vk.swap_ready[i], NULL);
        g_vk.swap_ready[i] = VK_NULL_HANDLE;
    }
    g_vk.swap_count = 0;
}

// Пересобрать swapchain под окно. false — окно свёрнуто (размер 0) или ошибка: кадр не показываем
static bool swapchain_build(void)
{
    vkDeviceWaitIdle(g_vk.device);
    VkSurfaceCapabilitiesKHR sc;
    if (!VK_OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.gpu, g_vk.surface, &sc))) return false;
    VkExtent2D ext = sc.currentExtent;
    if (ext.width == UINT32_MAX) {
        i32 w, h;
        platform_window_size(&w, &h);
        ext.width = (u32)CLAMP(w, (i32)sc.minImageExtent.width, (i32)sc.maxImageExtent.width);
        ext.height = (u32)CLAMP(h, (i32)sc.minImageExtent.height, (i32)sc.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0) return false;

    if (!g_vk.swap_fmt) {
        VkSurfaceFormatKHR fmts[64];
        u32 n = ARRAY_COUNT(fmts);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.gpu, g_vk.surface, &n, fmts);
        // Кадр уже в гамме экрана (как окно GL без sRGB): нужен UNORM, иначе гамма применится дважды
        g_vk.swap_fmt = n ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
        for (u32 i = 0; i < n; i++) {
            if ((fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) &&
                fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                g_vk.swap_fmt = fmts[i].format;
                break;
            }
        }
    }

    VkPresentModeKHR modes[16];
    u32 nm = ARRAY_COUNT(modes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk.gpu, g_vk.surface, &nm, modes);
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    g_vk.present_have = RB_PRESENT_FIFO;
    for (u32 i = 0; i < nm; i++) {
        if (modes[i] == present_mode_vk(g_vk.present_want)) {
            mode = modes[i];
            g_vk.present_have = g_vk.present_want;
        }
    }

    // Кадров в полёте три: картинок swapchain столько же, если драйвер даёт
    u32 count = MAX(sc.minImageCount, (u32)RB_FRAMES);
    if (sc.maxImageCount) count = MIN(count, sc.maxImageCount);
    VkSwapchainKHR old = g_vk.swapchain;
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_vk.surface,
        .minImageCount = count,
        .imageFormat = g_vk.swap_fmt,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = ext,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = sc.currentTransform,
        .compositeAlpha = (sc.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                              ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                              : (VkCompositeAlphaFlagBitsKHR)(sc.supportedCompositeAlpha & (0u - sc.supportedCompositeAlpha)),
        .presentMode = mode,
        .clipped = 1,
        .oldSwapchain = old,
    };
    if (!(sc.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        log_error("Vulkan: swapchain не принимает копирование");
        return false;
    }
    VkResult r = vkCreateSwapchainKHR(g_vk.device, &ci, NULL, &g_vk.swapchain);
    if (old) vkDestroySwapchainKHR(g_vk.device, old, NULL);
    swapchain_destroy_images();
    if (!VK_OK(r)) {
        g_vk.swapchain = VK_NULL_HANDLE;
        return false;
    }
    g_vk.swap_extent = ext;
    g_vk.swap_count = MAX_SWAP_IMAGES;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.swap_count, g_vk.swap_images);
    VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (u32 i = 0; i < g_vk.swap_count; i++) {
        vkCreateSemaphore(g_vk.device, &si, NULL, &g_vk.swap_ready[i]);
        g_vk.swap_layout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    g_vk.swap_dirty = false;
    log_info("Vulkan: swapchain %ux%u, картинок %u, показ %s", ext.width, ext.height, g_vk.swap_count,
             mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE" : "FIFO");
    return true;
}

static bool create_frames(void)
{
    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = g_vk.queue_family,
    };
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (u32 i = 0; i < RB_FRAMES; i++) {
        Frame *f = &g_vk.frames[i];
        if (!VK_OK(vkCreateCommandPool(g_vk.device, &pci, NULL, &f->pool))) return false;
        VkCommandBufferAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = f->pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (!VK_OK(vkAllocateCommandBuffers(g_vk.device, &ai, &f->cmd))) return false;
        if (!VK_OK(vkCreateFence(g_vk.device, &fci, NULL, &f->done))) return false;
        if (!VK_OK(vkCreateSemaphore(g_vk.device, &sci, NULL, &f->acquired))) return false;
    }
    return true;
}

static void empty_trash(Frame *f)
{
    for (u32 i = 0; i < f->trash_n; i++) vkDestroyPipeline(g_vk.device, f->trash_pipes[i], NULL);
    f->trash_n = 0;
}

static bool create_layout(void)
{
    VkDescriptorSetLayoutCreateInfo dl = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
    };
    if (!VK_OK(vkCreateDescriptorSetLayout(g_vk.device, &dl, NULL, &g_vk.set_layout))) return false;
    VkPushConstantRange pc = { VK_SHADER_STAGE_ALL, 0, RB_PUSH_BYTES };
    VkPipelineLayoutCreateInfo pl = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &g_vk.set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc,
    };
    return VK_OK(vkCreatePipelineLayout(g_vk.device, &pl, NULL, &g_vk.pipe_layout));
}

bool rb_init(Arena *scratch, const char *data_dir)
{
    memset(&g_vk, 0, sizeof g_vk);
    g_vk.scratch = scratch;
    snprintf(g_vk.data_dir, sizeof g_vk.data_dir, "%s", data_dir);
    PlatformProc loader = platform_vk_loader();
    if (!loader) return false;
    memcpy(&vkGetInstanceProcAddr, &loader, sizeof loader);
    if (!create_instance() || !create_surface() || !create_device() || !create_frames() || !create_layout()) {
        rb_shutdown();
        return false;
    }
    i32 w, h;
    platform_window_size(&w, &h);
    if (!window_targets(w, h)) {
        rb_shutdown();
        return false;
    }
    swapchain_build();
    log_info("Vulkan: %s, API %u.%u.%u, validation %s", g_vk.props.deviceName, VK_API_VERSION_MAJOR(g_vk.props.apiVersion),
             VK_API_VERSION_MINOR(g_vk.props.apiVersion), VK_API_VERSION_PATCH(g_vk.props.apiVersion),
             g_vk.validation ? "вкл" : "выкл");
    return true;
}

void rb_shaders_changed(void)
{
}

void rb_shutdown(void)
{
    if (g_vk.device) {
        vkDeviceWaitIdle(g_vk.device);
        for (u32 i = 1; i < MAX_PIPES; i++) {
            RbPipe p = i;
            rb_pipe_free(&p);
        }
        if (g_vk.pipe_layout) vkDestroyPipelineLayout(g_vk.device, g_vk.pipe_layout, NULL);
        if (g_vk.set_layout) vkDestroyDescriptorSetLayout(g_vk.device, g_vk.set_layout, NULL);
        for (u32 i = 0; i < RB_FRAMES; i++) {
            Frame *f = &g_vk.frames[i];
            empty_trash(f);
            if (f->acquired) vkDestroySemaphore(g_vk.device, f->acquired, NULL);
            if (f->done) vkDestroyFence(g_vk.device, f->done, NULL);
            if (f->pool) vkDestroyCommandPool(g_vk.device, f->pool, NULL);
        }
        tex_destroy(&g_vk.win_color);
        tex_destroy(&g_vk.win_depth);
        swapchain_destroy_images();
        if (g_vk.swapchain) vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, NULL);
        vkDestroyDevice(g_vk.device, NULL);
    }
    if (g_vk.instance) {
        if (g_vk.surface) vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, NULL);
        if (g_vk.messenger) vkDestroyDebugUtilsMessengerEXT(g_vk.instance, g_vk.messenger, NULL);
        vkDestroyInstance(g_vk.instance, NULL);
    }
    u32 errors = g_vk.validation_errors;
    memset(&g_vk, 0, sizeof g_vk);
    g_vk.validation_errors = errors;   // для отчёта после выхода
}

const RbCaps *rb_caps(void)
{
    return &g_vk.caps;
}

void rb_vk_set_present(RbPresent mode)
{
    if (mode == g_vk.present_want) return;
    g_vk.present_want = mode;
    g_vk.swap_dirty = true;
}

RbPresent rb_vk_present(void)
{
    return g_vk.present_have;
}

const char *rb_vk_device_name(void)
{
    return g_vk.props.deviceName;
}

u32 rb_vk_validation_errors(void)
{
    return g_vk.validation_errors;
}

static void cmd_begin(void)
{
    Frame *f = &g_vk.frames[g_vk.slot];
    vkResetCommandPool(g_vk.device, f->pool, 0);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(f->cmd, &bi);
    g_vk.cmd = f->cmd;
    g_vk.bound = 0;
    g_vk.push_dirty = true;
}

void rb_frame_begin(i32 win_w, i32 win_h)
{
    Frame *f = &g_vk.frames[g_vk.slot];
    // Слот свободен, когда его кадр RB_FRAMES назад отработал: тогда и кольца динамических буферов можно писать
    vkWaitForFences(g_vk.device, 1, &f->done, 1, UINT64_MAX);
    empty_trash(f);
    window_targets(win_w, win_h);
    cmd_begin();
    g_vk.in_frame = true;
}

u32 rb_frame_slot(void)
{
    return g_vk.slot;
}

// Отправить записанное; wait — семафор получения картинки, signal — готовность к показу
static void submit(VkSemaphore wait, VkSemaphore signal, VkFence fence)
{
    vkEndCommandBuffer(g_vk.cmd);
    VkCommandBufferSubmitInfo cb = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = g_vk.cmd };
    VkSemaphoreSubmitInfo w = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = wait,
        .stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,   // рисование не ждёт картинку, ждёт только перенос
    };
    VkSemaphoreSubmitInfo s = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = signal,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkSubmitInfo2 si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = wait ? 1 : 0,
        .pWaitSemaphoreInfos = &w,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cb,
        .signalSemaphoreInfoCount = signal ? 1 : 0,
        .pSignalSemaphoreInfos = &s,
    };
    VK_OK(vkQueueSubmit2(g_vk.queue, 1, &si, fence));
}

void rb_frame_end(void)
{
    if (g_vk.in_pass) rb_pass_end();
    Frame *f = &g_vk.frames[g_vk.slot];
    vkResetFences(g_vk.device, 1, &f->done);

    // Картинку swapchain берём в самом конце: рисование уже записано и не ждёт её
    u32 idx = UINT32_MAX;
    if (g_vk.swap_dirty || !g_vk.swapchain) swapchain_build();
    if (g_vk.swapchain) {
        VkResult r = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX, f->acquired, VK_NULL_HANDLE, &idx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            g_vk.swap_dirty = true;
            idx = UINT32_MAX;
        } else if (r == VK_SUBOPTIMAL_KHR) {
            g_vk.swap_dirty = true;   // картинка получена, покажем её и пересоберём к следующему кадру
        } else if (r != VK_SUCCESS) {
            VK_OK(r);
            idx = UINT32_MAX;
        }
    }

    if (idx == UINT32_MAX) {
        // Окно свёрнуто или swapchain устарел: кадр считается, но не показывается
        submit(VK_NULL_HANDLE, VK_NULL_HANDLE, f->done);
    } else {
        VkImage dst = g_vk.swap_images[idx];
        tex_layout(&g_vk.win_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        barrier(g_vk.cmd, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // Переворот: строка 0 кадра (низ картинки, как в GL) уходит в нижнюю строку окна
        i32 sw = g_vk.win_color.w, sh = g_vk.win_color.h;
        VkImageBlit2 region = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets = { { 0, sh, 0 }, { sw, 0, 1 } },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = { { 0, 0, 0 }, { (i32)g_vk.swap_extent.width, (i32)g_vk.swap_extent.height, 1 } },
        };
        VkBlitImageInfo2 bi = {
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = g_vk.win_color.image,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstImage = dst,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &region,
            .filter = VK_FILTER_NEAREST,
        };
        vkCmdBlitImage2(g_vk.cmd, &bi);
        barrier(g_vk.cmd, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        submit(f->acquired, g_vk.swap_ready[idx], f->done);

        VkPresentInfoKHR pi = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &g_vk.swap_ready[idx],
            .swapchainCount = 1,
            .pSwapchains = &g_vk.swapchain,
            .pImageIndices = &idx,
        };
        VkResult r = vkQueuePresentKHR(g_vk.queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) g_vk.swap_dirty = true;
        else VK_OK(r);
    }
    if (g_vk.swapchain && (g_vk.swap_extent.width != (u32)g_vk.win_color.w || g_vk.swap_extent.height != (u32)g_vk.win_color.h))
        g_vk.swap_dirty = true;
    g_vk.cmd = VK_NULL_HANDLE;
    g_vk.in_frame = false;
    g_vk.slot = (g_vk.slot + 1) % RB_FRAMES;
}

void rb_pass_begin(const RbPass *p)
{
    if (g_vk.in_pass) rb_pass_end();
    // Этап 2: только окно; цели-текстуры придут с rb_tex_make
    VkTex *color = &g_vk.win_color, *depth = &g_vk.win_depth;
    tex_layout(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    tex_layout(depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo ca = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = color->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = p->clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color.float32 = { p->clear[0], p->clear[1], p->clear[2], p->clear[3] },
    };
    VkRenderingAttachmentInfo da = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth->view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = p->clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.depthStencil = { 1.0f, 0 },
    };
    VkRenderingInfo ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { { 0, 0 }, { (u32)color->w, (u32)color->h } },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &ca,
        .pDepthAttachment = &da,
    };
    vkCmdBeginRendering(g_vk.cmd, &ri);
    g_vk.in_pass = true;
    g_vk.pass_w = color->w;
    g_vk.pass_h = color->h;
    rb_viewport(0, 0, color->w, color->h);
    rb_scissor_off();
}

void rb_pass_end(void)
{
    if (!g_vk.in_pass) return;
    vkCmdEndRendering(g_vk.cmd);
    g_vk.in_pass = false;
}

// Строки в памяти совпадают с GL, поэтому y снизу — это просто номер строки
void rb_viewport(i32 x, i32 y, i32 w, i32 h)
{
    VkViewport v = { (f32)x, (f32)y, (f32)w, (f32)h, 0.0f, 1.0f };
    vkCmdSetViewport(g_vk.cmd, 0, 1, &v);
}

void rb_scissor(i32 x, i32 y, i32 w, i32 h)
{
    VkRect2D r = { { MAX(x, 0), MAX(y, 0) }, { (u32)MAX(w, 0), (u32)MAX(h, 0) } };
    vkCmdSetScissor(g_vk.cmd, 0, 1, &r);
}

void rb_scissor_off(void)
{
    rb_scissor(0, 0, g_vk.pass_w, g_vk.pass_h);
}

void rb_clear_rect(i32 x, i32 y, i32 w, i32 h, const f32 *color, bool depth)
{
    VkClearAttachment a[2];
    u32 n = 0;
    if (color) a[n++] = (VkClearAttachment){ VK_IMAGE_ASPECT_COLOR_BIT, 0, { .color.float32 = { color[0], color[1], color[2], color[3] } } };
    if (depth) a[n++] = (VkClearAttachment){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } };
    // Прямоугольник обрезается по цели, как glClear с ножницами
    i32 x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + w, g_vk.pass_w), y1 = MIN(y + h, g_vk.pass_h);
    if (!n || x1 <= x0 || y1 <= y0) return;
    VkClearRect r = { { { x0, y0 }, { (u32)(x1 - x0), (u32)(y1 - y0) } }, 0, 1 };
    vkCmdClearAttachments(g_vk.cmd, n, a, 1, &r);
}

static VkShaderModule load_spv(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_vk.data_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("Vulkan: нет шейдера %s", path);
        return VK_NULL_HANDLE;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t mark = g_vk.scratch->used;
    u32 *code = size > 0 ? arena_push(g_vk.scratch, (size_t)size, 4) : NULL;
    bool ok = code && fread(code, 1, (size_t)size, f) == (size_t)size && size % 4 == 0;
    fclose(f);
    VkShaderModule m = VK_NULL_HANDLE;
    if (ok) {
        VkShaderModuleCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = (size_t)size,
            .pCode = code,
        };
        VK_OK(vkCreateShaderModule(g_vk.device, &ci, NULL, &m));
    } else {
        log_error("Vulkan: шейдер %s не прочитан", path);
    }
    g_vk.scratch->used = mark;
    return m;
}

static VkFormat attr_fmt(u8 f)
{
    switch (f) {
    case RB_ATTR_F32x2: return VK_FORMAT_R32G32_SFLOAT;
    case RB_ATTR_F32x3: return VK_FORMAT_R32G32B32_SFLOAT;
    case RB_ATTR_UN8x4: return VK_FORMAT_R8G8B8A8_UNORM;
    case RB_ATTR_U8x4: return VK_FORMAT_R8G8B8A8_UINT;
    default: return VK_FORMAT_R32G32B32A32_UINT;
    }
}

static VkPipelineColorBlendAttachmentState blend_state(RbBlend b, bool no_color)
{
    VkPipelineColorBlendAttachmentState s = {
        .colorWriteMask = no_color ? 0 : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT),
    };
    if (b == RB_BLEND_NONE) return s;
    s.blendEnable = 1;
    s.colorBlendOp = s.alphaBlendOp = VK_BLEND_OP_ADD;
    switch (b) {
    case RB_BLEND_ALPHA:
        s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case RB_BLEND_PREMUL:
        s.srcColorBlendFactor = s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        s.dstColorBlendFactor = s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    default:
        s.srcColorBlendFactor = s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        s.dstColorBlendFactor = s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }
    return s;
}

RbPipe rb_vk_pipe_spv(const RbPipeDesc *d, const char *vs_spv, const char *fs_spv)
{
    RbPipe slot = 0;
    for (u32 i = 1; i < MAX_PIPES && !slot; i++)
        if (!g_vk.pipes[i].used) slot = i;
    if (!slot) {
        log_error("Vulkan: конвейеров больше %d", MAX_PIPES);
        return 0;
    }
    VkShaderModule vs = load_spv(vs_spv), fs = load_spv(fs_spv);
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vs && fs) {
        VkPipelineShaderStageCreateInfo st[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" },
        };
        VkVertexInputAttributeDescription attrs[RB_MAX_ATTRS];
        VkVertexInputBindingDescription binds[RB_MAX_STREAMS];
        u32 n_binds = 0;
        for (u32 i = 0; i < d->layout.attrs; i++) {
            const RbAttr *a = &d->layout.attr[i];
            attrs[i] = (VkVertexInputAttributeDescription){ a->location, a->stream, attr_fmt(a->fmt), a->offset };
        }
        for (u32 s = 0; s < RB_MAX_STREAMS; s++) {
            if (!d->layout.stride[s]) continue;
            binds[n_binds++] = (VkVertexInputBindingDescription){
                s, d->layout.stride[s], d->layout.per_instance[s] ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX };
        }
        VkPipelineVertexInputStateCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = n_binds,
            .pVertexBindingDescriptions = binds,
            .vertexAttributeDescriptionCount = d->layout.attrs,
            .pVertexAttributeDescriptions = attrs,
        };
        VkPipelineInputAssemblyStateCreateInfo ia = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };
        // Отсечение по глубине как в GL, [-1, 1]; без расширения шейдеры пересчитают z сами (этап 3)
        VkPipelineViewportDepthClipControlCreateInfoEXT clip = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT,
            .negativeOneToOne = 1,
        };
        VkPipelineViewportStateCreateInfo vp = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = g_vk.have_depth_clip_control ? &clip : NULL,
            .viewportCount = 1,
            .scissorCount = 1,
        };
        // Обход граней: строки в памяти как в GL, но знак площади в Vulkan обратный — передом считается CW
        VkPipelineRasterizationStateCreateInfo rs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = d->depth_clamp,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = d->cull == RB_CULL_BACK ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = d->bias_slope != 0.0f || d->bias_const != 0.0f,
            .depthBiasConstantFactor = d->bias_const,
            .depthBiasSlopeFactor = d->bias_slope,
            .lineWidth = 1.0f,
        };
        VkPipelineMultisampleStateCreateInfo ms = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        VkPipelineDepthStencilStateCreateInfo ds = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = d->depth != RB_DEPTH_OFF,
            .depthWriteEnable = d->depth_write,
            .depthCompareOp = d->depth == RB_DEPTH_LEQUAL ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
        };
        VkFormat color_fmt[RB_MAX_COLORS];
        VkPipelineColorBlendAttachmentState blends[RB_MAX_COLORS];
        u32 n_colors = 0;
        VkFormat depth_fmt = VK_FORMAT_UNDEFINED;
        if (d->color[0] == RB_FMT_NONE && d->depth_fmt == RB_FMT_NONE) {
            color_fmt[n_colors] = WIN_COLOR_FMT;
            blends[n_colors++] = blend_state(d->blend, d->no_color);
            depth_fmt = WIN_DEPTH_FMT;
        } else {
            log_error("Vulkan: цели-текстуры конвейера %s — этап 3", d->name ? d->name : "?");
        }
        VkPipelineColorBlendStateCreateInfo cb = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = n_colors,
            .pAttachments = blends,
        };
        VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dy = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = ARRAY_COUNT(dyn),
            .pDynamicStates = dyn,
        };
        VkPipelineRenderingCreateInfo rend = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = n_colors,
            .pColorAttachmentFormats = color_fmt,
            .depthAttachmentFormat = depth_fmt,
        };
        VkGraphicsPipelineCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rend,
            .stageCount = 2,
            .pStages = st,
            .pVertexInputState = &vi,
            .pInputAssemblyState = &ia,
            .pViewportState = &vp,
            .pRasterizationState = &rs,
            .pMultisampleState = &ms,
            .pDepthStencilState = &ds,
            .pColorBlendState = &cb,
            .pDynamicState = &dy,
            .layout = g_vk.pipe_layout,
        };
        if (n_colors) VK_OK(vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &pipe));
    }
    if (vs) vkDestroyShaderModule(g_vk.device, vs, NULL);
    if (fs) vkDestroyShaderModule(g_vk.device, fs, NULL);
    if (!pipe) return 0;
    g_vk.pipes[slot] = (Pipe){ .pipe = pipe, .used = true };
    return slot;
}

void rb_pipe_free(RbPipe *p)
{
    if (!*p || *p >= MAX_PIPES || !g_vk.pipes[*p].used) return;
    // Кадры в полёте могут ещё рисовать им: удалим, когда слот освободится
    Frame *f = &g_vk.frames[g_vk.slot];
    if (f->trash_n == FRAME_TRASH) {
        vkDeviceWaitIdle(g_vk.device);
        empty_trash(f);
    }
    f->trash_pipes[f->trash_n++] = g_vk.pipes[*p].pipe;
    if (g_vk.bound == *p) g_vk.bound = 0;
    g_vk.pipes[*p] = (Pipe){ 0 };
    *p = 0;
}

void rb_bind_pipe(RbPipe p)
{
    if (p == g_vk.bound || !p || p >= MAX_PIPES || !g_vk.pipes[p].used) return;
    vkCmdBindPipeline(g_vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipes[p].pipe);
    g_vk.bound = p;
}

void rb_push(u32 offset, u32 size, const void *data)
{
    if (offset + size > RB_PUSH_BYTES) return;
    memcpy(g_vk.push + offset, data, size);
    g_vk.push_dirty = true;
}

// Константы уходят все разом перед вызовом: в GL они тоже живут до перезаписи
static void flush_push(void)
{
    if (!g_vk.push_dirty) return;
    vkCmdPushConstants(g_vk.cmd, g_vk.pipe_layout, VK_SHADER_STAGE_ALL, 0, RB_PUSH_BYTES, g_vk.push);
    g_vk.push_dirty = false;
}

void rb_draw(u32 verts, u32 first, u32 instances)
{
    flush_push();
    vkCmdDraw(g_vk.cmd, verts, MAX(instances, 1), first, 0);
}

void rb_read_window(u8 *rgb, i32 w, i32 h)
{
    // Кадр ещё не отправлен: дописываем копию, ждём GPU и продолжаем кадр в том же слоте
    bool was_frame = g_vk.in_frame;
    if (!was_frame) {
        vkWaitForFences(g_vk.device, 1, &g_vk.frames[g_vk.slot].done, 1, UINT64_MAX);
        cmd_begin();
    }
    rb_pass_end();
    w = MIN(w, g_vk.win_color.w);
    h = MIN(h, g_vk.win_color.h);
    VkDeviceSize size = (VkDeviceSize)w * (VkDeviceSize)h * 4;
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VkBuffer buf;
    if (!VK_OK(vkCreateBuffer(g_vk.device, &bci, NULL, &buf))) return;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.device, buf, &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VkDeviceMemory mem;
    if (!VK_OK(vkAllocateMemory(g_vk.device, &ai, NULL, &mem))) {
        vkDestroyBuffer(g_vk.device, buf, NULL);
        return;
    }
    vkBindBufferMemory(g_vk.device, buf, mem, 0);

    tex_layout(&g_vk.win_color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { (u32)w, (u32)h, 1 },
    };
    vkCmdCopyImageToBuffer(g_vk.cmd, g_vk.win_color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &copy);
    VkMemoryBarrier2 mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
    };
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb };
    vkCmdPipelineBarrier2(g_vk.cmd, &dep);

    Frame *f = &g_vk.frames[g_vk.slot];
    vkResetFences(g_vk.device, 1, &f->done);
    submit(VK_NULL_HANDLE, VK_NULL_HANDLE, f->done);
    vkWaitForFences(g_vk.device, 1, &f->done, 1, UINT64_MAX);

    void *map = NULL;
    if (VK_OK(vkMapMemory(g_vk.device, mem, 0, size, 0, &map))) {
        // В памяти строка 0 — низ картинки, как и просят: RGB снизу вверх
        const u8 *src = map;
        for (i32 i = 0; i < w * h; i++) {
            rgb[i * 3 + 0] = src[i * 4 + 0];
            rgb[i * 3 + 1] = src[i * 4 + 1];
            rgb[i * 3 + 2] = src[i * 4 + 2];
        }
        vkUnmapMemory(g_vk.device, mem);
    }
    vkDestroyBuffer(g_vk.device, buf, NULL);
    vkFreeMemory(g_vk.device, mem, NULL);
    if (was_frame) cmd_begin();
    else g_vk.cmd = VK_NULL_HANDLE;
}
