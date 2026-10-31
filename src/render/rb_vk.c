// Слой рендера на Vulkan 1.3: dynamic rendering, synchronization2, push descriptors.
// Окно — своя картинка RGBA8 + глубина D32F. Кадр в ней лежит как в GL (строка 0 внизу картинки),
// поэтому координаты областей, ножниц и gl_FragCoord совпадают с GL без пересчёта;
// переворот один — blit в swapchain в конце кадра.
// Нарочно просто (быстрее — этап 5): все картинки живут в раскладке GENERAL, между проходами, копиями
// и вычислениями — полный барьер памяти. Шейдеры — офлайн SPIR-V (tools/spv); какие привязки читает
// конвейер, видно из его SPIR-V, перед вызовом они уходят push-дескрипторами.
// Память — свои блоки по 64 МБ; буфер RB_BUF_DYNAMIC видим с CPU и пишется напрямую.
// Загрузки (rb_buf_write, rb_tex_write) — через кольцо слота в буфере команд кадра: встают перед
// следующими командами, как в GL. Буфер команд начинается сам при первой команде, даже вне кадра.
#include "rb_backend.h"
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



#define MAX_SWAP_IMAGES 8
#define MAX_BUFS 4096
#define MAX_TEXS 256
#define MAX_PIPES 256
#define MAX_LAYOUTS 32
#define MAX_BINDS 64             // номера привязок 0..63
#define MAX_VARIANTS 4           // конвейер под разные форматы целей прохода
#define MAX_INPUTS 16
#define TEX_VIEWS 16
#define TRASH_MAX 512
#define MAX_BLOCKS 64
#define BLOCK_RANGES 512
#define BLOCK_BYTES ((VkDeviceSize)64 << 20)
#define DEDICATED_BYTES ((VkDeviceSize)32 << 20)
#define STAGING_BYTES ((VkDeviceSize)16 << 20)
#define ZERO_BYTES 256           // поток нулей; с 128 — значения отсутствующих входов (0, 0, 0, 1)
#define CONST_STREAM RB_MAX_STREAMS
#define ALL_LAYERS 0xFFFF
#define TIMER_MAX 256
#define WIN_COLOR_FMT VK_FORMAT_R8G8B8A8_UNORM
#define WIN_DEPTH_FMT VK_FORMAT_D32_SFLOAT

// Кусок памяти: из блока (block — номер с 1) или своё выделение (block 0)
typedef struct Alloc {
    VkDeviceMemory mem;
    VkDeviceSize   off, size;
    u32            block;
    u8            *map;      // видимая с CPU
} Alloc;

typedef struct Range {
    VkDeviceSize off, size;
} Range;

// Блок памяти со списком свободных кусков по возрастанию. Картинки и буферы — в разных блоках
typedef struct Block {
    VkDeviceMemory mem;
    u32            type;
    bool           image;
    u32            n;
    Range          free[BLOCK_RANGES];
} Block;

typedef struct VBuf {
    VkBuffer buf;
    Alloc    a;
    size_t   size;
    bool     dynamic;
} VBuf;

typedef struct TexView {
    VkImageView view;
    u16         mip, layer;   // layer ALL_LAYERS — все слои (запись в картинку)
} TexView;

typedef struct VTex {
    VkImage            image;
    VkImageView        view;   // все мипы и слои: выборка
    Alloc              a;
    RbTexDesc          d;
    VkFormat           fmt;
    VkImageAspectFlags aspect;
    TexView            views[TEX_VIEWS];
    u32                nviews;
} VTex;

// Что лежит на привязке
enum { DT_NONE, DT_UBO, DT_SSBO, DT_TEX, DT_IMAGE };

// Набор привязок конвейера: тип на каждом номере
typedef struct Layout {
    u8                    type[MAX_BINDS];
    u64                   mask;
    VkDescriptorSetLayout set;
    VkPipelineLayout      pl;
} Layout;

typedef struct Variant {
    VkPipeline pipe;
    VkFormat   color[RB_MAX_COLORS];
    u32        ncolor;
    VkFormat   depth;
} Variant;

// Что шейдер берёт из SPIR-V: привязки и входы вершин
typedef struct Reflect {
    u8  type[MAX_BINDS];
    u64 used;                   // привязки, которые точка входа читает
    u32 inputs;                 // location входов вершинного шейдера
    u8  input_kind[MAX_INPUTS]; // 0 float, 1 uint, 2 int
} Reflect;

typedef struct Pipe {
    bool           used, compute, warned;
    RbPipeDesc     d;           // только состояние: строки имён после создания не читаются (кроме name)
    VkShaderModule vs, fs;
    u32            layout;
    u64            used_mask;
    u32            inputs;
    u8             input_kind[MAX_INPUTS];
    u32            streams;     // потоки вершин из раскладки
    Variant        var[MAX_VARIANTS];
    u32            nvar;
    VkPipeline     cpipe;
} Pipe;

typedef struct Bind {
    u8     kind;
    u8     smp;
    u32    h;       // RbBuf или RbTex
    u32    mip;
    size_t off, size;
} Bind;

enum { TR_PIPE, TR_BUF, TR_IMAGE, TR_VIEW, TR_MEM };

// Освобождённое: удаляется, когда слот снова свободен (GPU мог ещё читать)
typedef struct Trash {
    u8 kind;
    union {
        VkPipeline  pipe;
        VkBuffer    buf;
        VkImage     image;
        VkImageView view;
    } h;
    Alloc a;
} Trash;

typedef struct Frame {
    VkCommandPool   pool;
    VkCommandBuffer cmd;
    VkFence         done;      // работа слота отработала на GPU
    VkSemaphore     acquired;  // картинка swapchain получена
    Trash           trash[TRASH_MAX];
    u32             trash_n;
    VBuf            staging;   // кольцо загрузок слота
    VkDeviceSize    staging_used;
    u32             tmask[TIMER_MAX / 32];   // какие метки времени записаны в этом слоте
} Frame;

static struct {
    Arena   *scratch;
    char     data_dir[256];
    bool     validation;
    u32      validation_errors;
    bool     dying;

    VkInstance               instance;
    VkDebugUtilsMessengerEXT messenger;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         gpu;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties mem_props;
    VkDevice                 device;
    VkQueue                  queue;
    u32                      queue_family;
    RbCaps                   caps;
    VkFormat                 d24;           // RB_FMT_D24 на этом устройстве
    u32                      max_push_desc;
    bool                     null_desc;     // пустые дескрипторы (VK_EXT_robustness2)
    u64                      ts_mask;

    VkSwapchainKHR swapchain;
    VkFormat       swap_fmt;
    VkExtent2D     swap_extent;
    u32            swap_count;
    VkImage        swap_images[MAX_SWAP_IMAGES];
    VkSemaphore    swap_ready[MAX_SWAP_IMAGES];  // кадр готов к показу: своя на каждую картинку
    VkImageLayout  swap_layout[MAX_SWAP_IMAGES];
    bool           swap_dirty;                   // пересобрать перед следующим показом
    RbPresent      present_want, present_have;

    VTex    win_color, win_depth;   // окно: сюда рисуют проходы без целей
    i32     win_w, win_h;

    Frame   frames[RB_FRAMES];
    u32     slot;
    bool    in_frame;
    VkCommandBuffer cmd;            // записываемый буфер команд, NULL — не начат

    Block   blocks[MAX_BLOCKS];
    u32     nblocks;
    VBuf    buf[MAX_BUFS];
    VTex    tex[MAX_TEXS];
    Pipe    pipe[MAX_PIPES];
    Layout  lay[MAX_LAYOUTS];
    u32     nlay;
    VkSampler smp[RB_SMP_COUNT];
    VBuf    zero;

    // Проход
    bool     in_pass, need_barrier;
    RbPass   pass;
    i32      pass_w, pass_h;
    VkFormat pass_color[RB_MAX_COLORS];
    u32      pass_ncolor;
    VkFormat pass_depth;
    VkViewport vp;
    VkRect2D   sc;

    // Привязки и что из них уже отдано буферу команд; [0] — графика, [1] — вычисления
    RbPipe   cur;
    Bind     bind[MAX_BINDS];
    u64      dirty[2];
    u32      cur_layout[2];
    bool     push_dirty[2];
    VkPipeline bound[2];
    u8       push[RB_PUSH_BYTES];
    RbBuf    vb[RB_MAX_STREAMS];
    size_t   vb_off[RB_MAX_STREAMS];
    VkBuffer vb_cur[CONST_STREAM + 1];
    VkDeviceSize vb_cur_off[CONST_STREAM + 1], vb_cur_stride[CONST_STREAM + 1];
    RbBuf    ib;
    size_t   ib_off;
    bool     ib16;
    VkBuffer ib_cur;
    VkDeviceSize ib_cur_off;
    bool     ib_cur16;

    // Метки времени: на слот свой кусок пула, результаты забираются, когда слот снова свободен
    VkQueryPool qpool;
    u32      tsets, tmarks;
    u64      tval[TIMER_MAX];
    bool     tready[TIMER_MAX];
} g_vk;

#define VK_OK(call) vk_ok((call), #call)

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

// Устройство подходит: 1.3, dynamic rendering, sync2, swapchain, push descriptors, depth clip control, очередь графики с показом
static i32 score_gpu(VkPhysicalDevice gpu, u32 *family)
{
    VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    vkGetPhysicalDeviceProperties2(gpu, &p2);
    const VkPhysicalDeviceProperties p = p2.properties;
    if (p.apiVersion < VK_API_VERSION_1_3) return -1;

    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 f = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f13 };
    vkGetPhysicalDeviceFeatures2(gpu, &f);
    if (!f13.dynamicRendering || !f13.synchronization2 || !f.features.multiDrawIndirect ||
        !f.features.drawIndirectFirstInstance || !f.features.depthClamp)
        return -1;

    VkExtensionProperties ext[512];
    u32 n = ARRAY_COUNT(ext);
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, ext);
    if (!has_ext(ext, n, VK_KHR_SWAPCHAIN_EXTENSION_NAME) || !has_ext(ext, n, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) ||
        !has_ext(ext, n, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME))
        return -1;
    // Глубина как в GL (отсечение по [-1, 1]) и gl_BaseInstance (номер экземпляра как в GL)
    VkPhysicalDeviceDepthClipControlFeaturesEXT clip = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT };
    VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &clip };
    VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f11 };
    vkGetPhysicalDeviceFeatures2(gpu, &f2);
    if (!clip.depthClipControl || !f11.shaderDrawParameters) return -1;

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
        log_error("Vulkan: нет устройства с 1.3, dynamic rendering, sync2, push descriptors, depth clip control и draw parameters");
        return false;
    }
    vkGetPhysicalDeviceProperties(g_vk.gpu, &g_vk.props);
    vkGetPhysicalDeviceMemoryProperties(g_vk.gpu, &g_vk.mem_props);

    VkExtensionProperties ext[512];
    u32 n_ext = ARRAY_COUNT(ext);
    vkEnumerateDeviceExtensionProperties(g_vk.gpu, NULL, &n_ext, ext);
    const char *want[8] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    u32 n_want = 2;

    // Что есть у устройства
    VkPhysicalDeviceVulkan13Features a13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features a12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &a13 };
    VkPhysicalDeviceVulkan11Features a11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &a12 };
    VkPhysicalDeviceFeatures2 a = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &a11 };
    VkPhysicalDeviceRobustness2FeaturesEXT rob = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    bool rob_ext = has_ext(ext, n_ext, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    if (rob_ext) a13.pNext = &rob;
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
        .shaderDrawParameters = 1,
    };
    VkPhysicalDeviceFeatures2 on = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &e11 };
    on.features.multiDrawIndirect = 1;
    on.features.drawIndirectFirstInstance = 1;
    on.features.depthClamp = 1;
    on.features.depthBiasClamp = a.features.depthBiasClamp;
    on.features.samplerAnisotropy = a.features.samplerAnisotropy;
    on.features.fragmentStoresAndAtomics = a.features.fragmentStoresAndAtomics;
    on.features.shaderStorageImageReadWithoutFormat = a.features.shaderStorageImageReadWithoutFormat;
    on.features.shaderStorageImageWriteWithoutFormat = a.features.shaderStorageImageWriteWithoutFormat;
    on.features.independentBlend = a.features.independentBlend;
    // Глубина как в GL: отсечение по [-1, 1] без правки шейдеров
    e13.pNext = &e_clip;
    want[n_want++] = VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME;
    // Пустая привязка читается нулями, как текстурный блок GL без текстуры (шейдер читает её не на каждой ветке)
    VkPhysicalDeviceRobustness2FeaturesEXT e_rob = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .nullDescriptor = 1 };
    g_vk.null_desc = rob_ext && rob.nullDescriptor;
    if (g_vk.null_desc) {
        e_clip.pNext = &e_rob;
        want[n_want++] = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
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
    VkPhysicalDevicePushDescriptorPropertiesKHR pd = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &pd };
    vkGetPhysicalDeviceProperties2(g_vk.gpu, &p2);
    g_vk.max_push_desc = pd.maxPushDescriptors;
    VkQueueFamilyProperties q[32];
    u32 nq = ARRAY_COUNT(q);
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.gpu, &nq, q);
    u32 bits = q[g_vk.queue_family].timestampValidBits;
    g_vk.ts_mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    // Глубина 24 бита для теней: если её нельзя рисовать и читать с фильтром — 32 бита
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_vk.gpu, VK_FORMAT_X8_D24_UNORM_PACK32, &fp);
    VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    g_vk.d24 = (fp.optimalTilingFeatures & need) == need ? VK_FORMAT_X8_D24_UNORM_PACK32 : VK_FORMAT_D32_SFLOAT;
    return true;
}

static u32 find_memory(u32 type_bits, VkMemoryPropertyFlags want)
{
    for (u32 i = 0; i < g_vk.mem_props.memoryTypeCount; i++)
        if ((type_bits & (1u << i)) && (g_vk.mem_props.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}


// --- Память

// Свободный кусок нужного размера и выравнивания: первый подходящий
static bool block_take(Block *b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *off)
{
    for (u32 i = 0; i < b->n; i++) {
        Range *r = &b->free[i];
        VkDeviceSize o = (r->off + align - 1) / align * align, end = r->off + r->size;
        if (o + size > end) continue;
        VkDeviceSize head = o - r->off, tail = end - (o + size);
        if (head && tail) {
            if (b->n == BLOCK_RANGES) continue;
            memmove(&b->free[i + 2], &b->free[i + 1], (b->n - i - 1) * sizeof(Range));
            b->free[i].size = head;
            b->free[i + 1] = (Range){ o + size, tail };
            b->n++;
        } else if (head) {
            r->size = head;
        } else if (tail) {
            r->off = o + size;
            r->size = tail;
        } else {
            memmove(&b->free[i], &b->free[i + 1], (b->n - i - 1) * sizeof(Range));
            b->n--;
        }
        *off = o;
        return true;
    }
    return false;
}

// Вернуть кусок и склеить с соседями
static void block_give(Block *b, VkDeviceSize off, VkDeviceSize size)
{
    u32 i = 0;
    while (i < b->n && b->free[i].off < off) i++;
    bool prev = i > 0 && b->free[i - 1].off + b->free[i - 1].size == off;
    bool next = i < b->n && off + size == b->free[i].off;
    if (prev && next) {
        b->free[i - 1].size += size + b->free[i].size;
        memmove(&b->free[i], &b->free[i + 1], (b->n - i - 1) * sizeof(Range));
        b->n--;
    } else if (prev) {
        b->free[i - 1].size += size;
    } else if (next) {
        b->free[i].off = off;
        b->free[i].size += size;
    } else if (b->n < BLOCK_RANGES) {
        memmove(&b->free[i + 1], &b->free[i], (b->n - i) * sizeof(Range));
        b->free[i] = (Range){ off, size };
        b->n++;
    } else {
        log_error("Vulkan: блок памяти раздроблен, кусок %llu Б потерян до выхода", (unsigned long long)size);
    }
}

static bool mem_dedicated(VkMemoryRequirements req, u32 type, bool host, Alloc *a)
{
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type };
    if (vkAllocateMemory(g_vk.device, &ai, NULL, &a->mem) != VK_SUCCESS) return false;
    a->size = req.size;
    if (host && !VK_OK(vkMapMemory(g_vk.device, a->mem, 0, VK_WHOLE_SIZE, 0, (void **)&a->map))) {
        vkFreeMemory(g_vk.device, a->mem, NULL);
        a->mem = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// host — видимая с CPU и отображённая (сначала в памяти видеокарты, если такая есть)
static bool mem_alloc(VkMemoryRequirements req, bool host, bool image, Alloc *a)
{
    memset(a, 0, sizeof *a);
    const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (host) {
        u32 t = find_memory(req.memoryTypeBits, hv | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (t != UINT32_MAX && mem_dedicated(req, t, true, a)) return true;
        t = find_memory(req.memoryTypeBits, hv);
        if (t != UINT32_MAX && mem_dedicated(req, t, true, a)) return true;
        log_error("Vulkan: нет памяти, видимой с CPU (%llu Б)", (unsigned long long)req.size);
        return false;
    }
    u32 type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) type = find_memory(req.memoryTypeBits, 0);
    if (type == UINT32_MAX) return false;
    if (req.size < DEDICATED_BYTES) {
        for (u32 i = 0; i < g_vk.nblocks; i++) {
            Block *b = &g_vk.blocks[i];
            if (b->type != type || b->image != image || !block_take(b, req.size, req.alignment, &a->off)) continue;
            a->mem = b->mem;
            a->size = req.size;
            a->block = i + 1;
            return true;
        }
        if (g_vk.nblocks < MAX_BLOCKS) {
            Block *b = &g_vk.blocks[g_vk.nblocks];
            VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = BLOCK_BYTES, .memoryTypeIndex = type };
            if (vkAllocateMemory(g_vk.device, &ai, NULL, &b->mem) == VK_SUCCESS) {
                b->type = type;
                b->image = image;
                b->n = 1;
                b->free[0] = (Range){ 0, BLOCK_BYTES };
                g_vk.nblocks++;
                if (block_take(b, req.size, req.alignment, &a->off)) {
                    a->mem = b->mem;
                    a->size = req.size;
                    a->block = g_vk.nblocks;
                    return true;
                }
            }
        }
    }
    if (mem_dedicated(req, type, false, a)) return true;
    log_error("Vulkan: нет памяти видеокарты (%llu Б)", (unsigned long long)req.size);
    return false;
}

static void mem_free(Alloc *a)
{
    if (a->block) block_give(&g_vk.blocks[a->block - 1], a->off, a->size);
    else if (a->mem) vkFreeMemory(g_vk.device, a->mem, NULL);
    memset(a, 0, sizeof *a);
}

static void trash_free(Trash *t)
{
    switch (t->kind) {
    case TR_PIPE: vkDestroyPipeline(g_vk.device, t->h.pipe, NULL); break;
    case TR_BUF: vkDestroyBuffer(g_vk.device, t->h.buf, NULL); break;
    case TR_IMAGE: vkDestroyImage(g_vk.device, t->h.image, NULL); break;
    case TR_VIEW: vkDestroyImageView(g_vk.device, t->h.view, NULL); break;
    default: break;
    }
    mem_free(&t->a);
}

static void empty_trash(Frame *f)
{
    for (u32 i = 0; i < f->trash_n; i++) trash_free(&f->trash[i]);
    f->trash_n = 0;
}

// --- Буфер команд

static void submit(VkSemaphore wait, VkSemaphore signal, VkFence fence);
static void timers_collect(Frame *f);
static void flush_wait(void);

// После нового буфера команд в нём ничего не привязано
static void state_dirty(void)
{
    g_vk.bound[0] = g_vk.bound[1] = VK_NULL_HANDLE;
    g_vk.cur_layout[0] = g_vk.cur_layout[1] = UINT32_MAX;
    g_vk.push_dirty[0] = g_vk.push_dirty[1] = true;
    memset(g_vk.vb_cur, 0, sizeof g_vk.vb_cur);
    g_vk.ib_cur = VK_NULL_HANDLE;
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
    state_dirty();
}

// Начать запись в слот, если не начата: слот свободен, когда его прошлая работа отработала на GPU
static void ensure_cmd(void)
{
    if (g_vk.cmd) return;
    Frame *f = &g_vk.frames[g_vk.slot];
    vkWaitForFences(g_vk.device, 1, &f->done, 1, UINT64_MAX);
    empty_trash(f);
    timers_collect(f);
    f->staging_used = 0;
    cmd_begin();
    if (g_vk.qpool) {
        u32 n = g_vk.tsets * g_vk.tmarks;
        vkCmdResetQueryPool(g_vk.cmd, g_vk.qpool, g_vk.slot * n, n);
    }
}

static void trash_add(u8 kind, void *handle, const Alloc *a)
{
    Trash t = { .kind = kind };
    if (a) t.a = *a;
    switch (kind) {
    case TR_PIPE: t.h.pipe = handle; break;
    case TR_BUF: t.h.buf = handle; break;
    case TR_IMAGE: t.h.image = handle; break;
    case TR_VIEW: t.h.view = handle; break;
    default: break;
    }
    if (g_vk.dying) {
        trash_free(&t);
        return;
    }
    ensure_cmd();
    Frame *f = &g_vk.frames[g_vk.slot];
    if (f->trash_n == TRASH_MAX) flush_wait();
    f->trash[f->trash_n++] = t;
}

// Полный барьер памяти: всё записанное раньше видно всему, что дальше
static void full_barrier(void)
{
    VkMemoryBarrier2 mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
    };
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb };
    vkCmdPipelineBarrier2(g_vk.cmd, &dep);
    g_vk.need_barrier = false;
}

// Перед новой работой GPU: прошлая (проход, копия, вычисление) должна дописаться
static void sync(void)
{
    if (g_vk.need_barrier) full_barrier();
}

static void begin_rendering(bool load);

// Копии и вычисления — вне прохода: проход приостанавливается и потом продолжается с загрузкой целей
static bool leave_pass(void)
{
    if (!g_vk.in_pass) return false;
    vkCmdEndRendering(g_vk.cmd);
    g_vk.in_pass = false;
    g_vk.need_barrier = true;
    return true;
}

static void back_to_pass(bool was)
{
    if (!was) return;
    sync();
    begin_rendering(true);
    g_vk.in_pass = true;
    vkCmdSetViewport(g_vk.cmd, 0, 1, &g_vk.vp);
    vkCmdSetScissor(g_vk.cmd, 0, 1, &g_vk.sc);
}

// Отправить записанное и дождаться GPU; запись продолжается в том же слоте (переполнение, чтение кадра)
static void flush_wait(void)
{
    if (!g_vk.cmd) return;
    bool was = leave_pass();
    Frame *f = &g_vk.frames[g_vk.slot];
    vkResetFences(g_vk.device, 1, &f->done);
    submit(VK_NULL_HANDLE, VK_NULL_HANDLE, f->done);
    vkWaitForFences(g_vk.device, 1, &f->done, 1, UINT64_MAX);
    empty_trash(f);
    f->staging_used = 0;
    cmd_begin();
    back_to_pass(was);
}

// Место в кольце загрузок слота (size не больше STAGING_BYTES)
static u8 *staging_take(VkDeviceSize size, VkDeviceSize *off)
{
    ensure_cmd();
    Frame *f = &g_vk.frames[g_vk.slot];
    VkDeviceSize o = (f->staging_used + 15) & ~(VkDeviceSize)15;
    if (o + size > STAGING_BYTES) {
        flush_wait();
        o = 0;
    }
    f->staging_used = o + size;
    *off = o;
    return f->staging.a.map + o;
}

// --- Буферы

static bool vbuf_create(VBuf *b, size_t size, bool host)
{
    memset(b, 0, sizeof *b);
    VkBufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = MAX(size, 16),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    if (!VK_OK(vkCreateBuffer(g_vk.device, &ci, NULL, &b->buf))) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.device, b->buf, &req);
    if (!mem_alloc(req, host, false, &b->a)) {
        vkDestroyBuffer(g_vk.device, b->buf, NULL);
        b->buf = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(g_vk.device, b->buf, b->a.mem, b->a.off);
    b->size = size;
    b->dynamic = host;
    return true;
}

static void vbuf_destroy_now(VBuf *b)
{
    if (b->buf) vkDestroyBuffer(g_vk.device, b->buf, NULL);
    mem_free(&b->a);
    memset(b, 0, sizeof *b);
}

RbBuf rb_buf_make(size_t size, u32 usage, const void *data)
{
    for (u32 i = 1; i < MAX_BUFS; i++) {
        VBuf *b = &g_vk.buf[i];
        if (b->buf) continue;
        if (!vbuf_create(b, size, (usage & RB_BUF_DYNAMIC) != 0)) return 0;
        if (data) {
            rb_buf_write(i, 0, size, data);
        } else if (b->dynamic) {
            memset(b->a.map, 0, size);
        } else {
            // Без данных — нули, как дают драйверы GL
            ensure_cmd();
            bool was = leave_pass();
            sync();
            vkCmdFillBuffer(g_vk.cmd, b->buf, 0, VK_WHOLE_SIZE, 0);
            g_vk.need_barrier = true;
            back_to_pass(was);
        }
        return i;
    }
    log_error("RB: буферов больше %u", MAX_BUFS);
    return 0;
}

void rb_buf_write(RbBuf h, size_t offset, size_t size, const void *data)
{
    if (!h || !size) return;
    VBuf *b = &g_vk.buf[h];
    if (b->dynamic) {
        memcpy(b->a.map + offset, data, size);
        return;
    }
    ensure_cmd();
    bool was = leave_pass();
    sync();
    const u8 *src = data;
    while (size) {
        VkDeviceSize n = MIN((VkDeviceSize)size, STAGING_BYTES / 2), so;
        u8 *dst = staging_take(n, &so);
        memcpy(dst, src, (size_t)n);
        VkBufferCopy c = { so, offset, n };
        vkCmdCopyBuffer(g_vk.cmd, g_vk.frames[g_vk.slot].staging.buf, b->buf, 1, &c);
        src += n;
        offset += (size_t)n;
        size -= (size_t)n;
    }
    g_vk.need_barrier = true;
    back_to_pass(was);
}

void rb_buf_free(RbBuf *h)
{
    if (!*h) return;
    VBuf *b = &g_vk.buf[*h];
    for (u32 i = 0; i < MAX_BINDS; i++)
        if ((g_vk.bind[i].kind == DT_UBO || g_vk.bind[i].kind == DT_SSBO) && g_vk.bind[i].h == *h) g_vk.bind[i].kind = DT_NONE;
    for (u32 s = 0; s < RB_MAX_STREAMS; s++)
        if (g_vk.vb[s] == *h) g_vk.vb[s] = 0;
    if (g_vk.ib == *h) g_vk.ib = 0;
    trash_add(TR_BUF, b->buf, &b->a);
    memset(b, 0, sizeof *b);
    *h = 0;
}

// --- Текстуры

static VkFormat tex_fmt(RbFmt f)
{
    switch (f) {
    case RB_FMT_R8: return VK_FORMAT_R8_UNORM;
    case RB_FMT_R16: return VK_FORMAT_R16_UNORM;
    case RB_FMT_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case RB_FMT_R8UI: return VK_FORMAT_R8_UINT;
    case RB_FMT_RG16UI: return VK_FORMAT_R16G16_UINT;
    case RB_FMT_R32UI: return VK_FORMAT_R32_UINT;
    case RB_FMT_R16F: return VK_FORMAT_R16_SFLOAT;
    case RB_FMT_RG16F: return VK_FORMAT_R16G16_SFLOAT;
    case RB_FMT_RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RB_FMT_R11G11B10F: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case RB_FMT_R32F: return VK_FORMAT_R32_SFLOAT;
    case RB_FMT_D24: return g_vk.d24;
    case RB_FMT_D32F: return VK_FORMAT_D32_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

static const u32 k_bpp[RB_FMT_COUNT] = {
    [RB_FMT_R8] = 1, [RB_FMT_R16] = 2, [RB_FMT_RGBA8] = 4, [RB_FMT_R8UI] = 1, [RB_FMT_RG16UI] = 4, [RB_FMT_R32UI] = 4,
    [RB_FMT_R16F] = 2, [RB_FMT_RG16F] = 4, [RB_FMT_RGBA16F] = 8, [RB_FMT_R11G11B10F] = 4, [RB_FMT_R32F] = 4,
    [RB_FMT_D24] = 4, [RB_FMT_D32F] = 4,
};

static u32 tex_layers(const VTex *t) { return t->d.kind == RB_TEX_2D_ARRAY ? (u32)t->d.d : 1; }

// Перевод из UNDEFINED в GENERAL, где картинка живёт всегда
static void image_to_general(VkImage image, VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier2 b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
    };
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b };
    vkCmdPipelineBarrier2(g_vk.cmd, &dep);
}

static bool tex_init(VTex *t, const RbTexDesc *d)
{
    memset(t, 0, sizeof *t);
    t->d = *d;
    if (t->d.d < 1) t->d.d = 1;
    if (t->d.mips < 1) t->d.mips = 1;
    t->fmt = tex_fmt(d->fmt);
    bool depth = d->fmt == RB_FMT_D24 || d->fmt == RB_FMT_D32F;
    t->aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (d->usage & RB_TEX_TARGET)
        usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (d->usage & RB_TEX_STORAGE) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    bool is3d = d->kind == RB_TEX_3D;
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        // Слой 3D-текстуры как цель — через вид 2D
        .flags = is3d && (d->usage & RB_TEX_TARGET) ? VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT : 0,
        .imageType = is3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
        .format = t->fmt,
        .extent = { (u32)MAX(d->w, 1), (u32)MAX(d->h, 1), is3d ? (u32)t->d.d : 1 },
        .mipLevels = t->d.mips,
        .arrayLayers = tex_layers(t),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VK_OK(vkCreateImage(g_vk.device, &ci, NULL, &t->image))) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vk.device, t->image, &req);
    if (!mem_alloc(req, false, true, &t->a)) {
        vkDestroyImage(g_vk.device, t->image, NULL);
        t->image = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(g_vk.device, t->image, t->a.mem, t->a.off);
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = is3d ? VK_IMAGE_VIEW_TYPE_3D : d->kind == RB_TEX_2D_ARRAY ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format = t->fmt,
        .subresourceRange = { t->aspect, 0, t->d.mips, 0, tex_layers(t) },
    };
    if (!VK_OK(vkCreateImageView(g_vk.device, &vi, NULL, &t->view))) return false;
    // GENERAL и очистка: нули, глубина 1 (у GL содержимое не задано, драйверы дают нули)
    ensure_cmd();
    bool was = leave_pass();
    sync();
    image_to_general(t->image, t->aspect);
    VkImageSubresourceRange all = { t->aspect, 0, t->d.mips, 0, tex_layers(t) };
    if (depth) {
        VkClearDepthStencilValue one = { 1.0f, 0 };
        vkCmdClearDepthStencilImage(g_vk.cmd, t->image, VK_IMAGE_LAYOUT_GENERAL, &one, 1, &all);
    } else {
        VkClearColorValue zero;
        memset(&zero, 0, sizeof zero);
        vkCmdClearColorImage(g_vk.cmd, t->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
    }
    g_vk.need_barrier = true;
    back_to_pass(was);
    return true;
}

// Сразу (картинка не в работе) или в корзину слота
static void tex_release(VTex *t, bool now)
{
    for (u32 i = 0; i < t->nviews; i++) {
        if (now) vkDestroyImageView(g_vk.device, t->views[i].view, NULL);
        else trash_add(TR_VIEW, t->views[i].view, NULL);
    }
    if (now) {
        if (t->view) vkDestroyImageView(g_vk.device, t->view, NULL);
        if (t->image) vkDestroyImage(g_vk.device, t->image, NULL);
        mem_free(&t->a);
    } else {
        if (t->view) trash_add(TR_VIEW, t->view, NULL);
        if (t->image) trash_add(TR_IMAGE, t->image, &t->a);
    }
    memset(t, 0, sizeof *t);
}

// Вид одного мипа: слоя layer (цель прохода) или всех слоёв (ALL_LAYERS, запись в картинку)
static VkImageView tex_view(VTex *t, u32 mip, u32 layer)
{
    for (u32 i = 0; i < t->nviews; i++)
        if (t->views[i].mip == mip && t->views[i].layer == layer) return t->views[i].view;
    if (t->nviews == TEX_VIEWS) {
        log_error("Vulkan: у текстуры видов больше %u", TEX_VIEWS);
        return t->view;
    }
    VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
    u32 base = 0, count = 1;
    if (layer == ALL_LAYERS) {
        if (t->d.kind == RB_TEX_3D) type = VK_IMAGE_VIEW_TYPE_3D;
        else if (t->d.kind == RB_TEX_2D_ARRAY) type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        count = tex_layers(t);
    } else {
        base = layer;
    }
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = type,
        .format = t->fmt,
        .subresourceRange = { t->aspect, mip, 1, base, count },
    };
    VkImageView v = VK_NULL_HANDLE;
    if (!VK_OK(vkCreateImageView(g_vk.device, &vi, NULL, &v))) return t->view;
    t->views[t->nviews++] = (TexView){ v, (u16)mip, (u16)layer };
    return v;
}

RbTex rb_tex_make(const RbTexDesc *d)
{
    for (u32 i = 1; i < MAX_TEXS; i++) {
        if (g_vk.tex[i].image) continue;
        if (!tex_init(&g_vk.tex[i], d)) {
            tex_release(&g_vk.tex[i], true);
            return 0;
        }
        return i;
    }
    log_error("RB: текстур больше %u", MAX_TEXS);
    return 0;
}

void rb_tex_write(RbTex h, u32 mip, i32 x, i32 y, i32 z, i32 w, i32 hh, i32 d, const void *data, u32 row_len)
{
    if (!h || w <= 0 || hh <= 0 || d <= 0) return;
    VTex *t = &g_vk.tex[h];
    u32 bpp = k_bpp[t->d.fmt], rl = row_len ? row_len : (u32)w;
    VkDeviceSize bytes = ((VkDeviceSize)((u32)d * (u32)hh - 1) * rl + (u32)w) * bpp;
    // Больше половины кольца — по слоям, потом по полосам строк
    if (bytes > STAGING_BYTES / 2) {
        const u8 *p = data;
        if (d > 1) {
            for (i32 k = 0; k < d; k++)
                rb_tex_write(h, mip, x, y, z + k, w, hh, 1, p + (size_t)k * rl * (u32)hh * bpp, rl);
        } else {
            i32 band = (i32)MAX((STAGING_BYTES / 2) / ((VkDeviceSize)rl * bpp), 1);
            for (i32 r = 0; r < hh; r += band)
                rb_tex_write(h, mip, x, y + r, z, w, MIN(band, hh - r), 1, p + (size_t)r * rl * bpp, rl);
        }
        return;
    }
    ensure_cmd();
    bool was = leave_pass();
    sync();
    VkDeviceSize so;
    u8 *dst = staging_take(bytes, &so);
    memcpy(dst, data, (size_t)bytes);
    bool arr = t->d.kind == RB_TEX_2D_ARRAY, is3d = t->d.kind == RB_TEX_3D;
    VkBufferImageCopy c = {
        .bufferOffset = so,
        .bufferRowLength = rl,
        .bufferImageHeight = (u32)hh,
        .imageSubresource = { t->aspect, mip, arr ? (u32)z : 0, arr ? (u32)d : 1 },
        .imageOffset = { x, y, is3d ? z : 0 },
        .imageExtent = { (u32)w, (u32)hh, is3d ? (u32)d : 1 },
    };
    vkCmdCopyBufferToImage(g_vk.cmd, g_vk.frames[g_vk.slot].staging.buf, t->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    g_vk.need_barrier = true;
    back_to_pass(was);
}

void rb_tex_free(RbTex *h)
{
    if (!*h) return;
    for (u32 i = 0; i < MAX_BINDS; i++)
        if ((g_vk.bind[i].kind == DT_TEX || g_vk.bind[i].kind == DT_IMAGE) && g_vk.bind[i].h == *h) g_vk.bind[i].kind = DT_NONE;
    tex_release(&g_vk.tex[*h], false);
    *h = 0;
}

void rb_tex_size(RbTex h, i32 *w, i32 *hh)
{
    *w = h ? g_vk.tex[h].d.w : g_vk.win_w;
    *hh = h ? g_vk.tex[h].d.h : g_vk.win_h;
}

static bool window_targets(i32 w, i32 h)
{
    w = MAX(w, 1);
    h = MAX(h, 1);
    if (g_vk.win_color.image && g_vk.win_color.d.w == w && g_vk.win_color.d.h == h) return true;
    vkDeviceWaitIdle(g_vk.device);
    tex_release(&g_vk.win_color, true);
    tex_release(&g_vk.win_depth, true);
    return tex_init(&g_vk.win_color, &(RbTexDesc){ .fmt = RB_FMT_RGBA8, .w = w, .h = h, .usage = RB_TEX_TARGET }) &&
           tex_init(&g_vk.win_depth, &(RbTexDesc){ .fmt = RB_FMT_D32F, .w = w, .h = h, .usage = RB_TEX_TARGET });
}


// --- Swapchain

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
    // Без ожидания: IMMEDIATE, нет — MAILBOX (тоже не ждёт), нет и его — FIFO
    for (u32 pass = 0; pass < 2 && g_vk.present_have == RB_PRESENT_FIFO && g_vk.present_want != RB_PRESENT_FIFO; pass++) {
        RbPresent want = pass == 0 ? g_vk.present_want : RB_PRESENT_MAILBOX;
        for (u32 i = 0; i < nm; i++) {
            if (modes[i] == present_mode_vk(want)) {
                mode = modes[i];
                g_vk.present_have = want;
            }
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


// --- Шейдеры и конвейеры

enum { SPV_DECORATE = 71, SPV_ENTRY = 15, SPV_INT = 21, SPV_FLOAT = 22, SPV_VECTOR = 23, SPV_IMAGE = 25,
       SPV_SAMPLED_IMAGE = 27, SPV_ARRAY = 28, SPV_RUNTIME_ARRAY = 29, SPV_STRUCT = 30, SPV_POINTER = 32, SPV_VARIABLE = 59 };
enum { DEC_BLOCK = 2, DEC_BUFFER_BLOCK = 3, DEC_BUILTIN = 11, DEC_LOCATION = 30, DEC_BINDING = 33 };
enum { SC_UNIFORM_CONSTANT = 0, SC_INPUT = 1, SC_UNIFORM = 2, SC_STORAGE_BUFFER = 12 };
enum { F_BLOCK = 1, F_BUFFER_BLOCK = 2, F_BUILTIN = 4, F_IFACE = 8 };

// Привязки и входы из SPIR-V: типы, переменные, декорации. Точка входа (SPIR-V 1.4+) перечисляет все
// глобальные переменные, которые читает: по ней видно, какие привязки шейдеру нужны на самом деле
static bool reflect(const u32 *w, size_t n, Reflect *r)
{
    if (n < 5 || w[0] != 0x07230203u) return false;
    u32 bound = w[3];
    bool iface_all = w[1] < 0x00010400u;
    // На id: опкод типа, два аргумента, привязка, location, флаги
    u32 *op = arena_push(g_vk.scratch, (size_t)bound * 5 * sizeof(u32), 4);
    u8 *fl = arena_push(g_vk.scratch, bound, 1);
    if (!op || !fl) return false;
    memset(op, 0xFF, (size_t)bound * 5 * sizeof(u32));
    memset(fl, 0, bound);
    u32 *a1 = op + bound, *a2 = op + 2 * (size_t)bound, *bind = op + 3 * (size_t)bound, *loc = op + 4 * (size_t)bound;
    for (size_t i = 5; i < n;) {
        u32 len = w[i] >> 16, code = w[i] & 0xFFFF;
        if (!len || i + len > n) return false;
        const u32 *x = w + i + 1;
        if (code == SPV_ENTRY && len >= 4) {
            // модель, id, имя (строка до нуля), интерфейс
            size_t k = 2;
            while (k < (size_t)len - 1 && ((x[k] >> 24) & 0xFF) != 0) k++;
            for (k++; k < (size_t)len - 1; k++)
                if (x[k] < bound) fl[x[k]] |= F_IFACE;
        } else if (code == SPV_DECORATE && len >= 3 && x[0] < bound) {
            if (x[1] == DEC_BLOCK) fl[x[0]] |= F_BLOCK;
            else if (x[1] == DEC_BUFFER_BLOCK) fl[x[0]] |= F_BUFFER_BLOCK;
            else if (x[1] == DEC_BUILTIN) fl[x[0]] |= F_BUILTIN;
            else if (x[1] == DEC_LOCATION && len >= 4) loc[x[0]] = x[2];
            else if (x[1] == DEC_BINDING && len >= 4) bind[x[0]] = x[2];
        } else if ((code == SPV_INT || code == SPV_FLOAT || code == SPV_VECTOR || code == SPV_IMAGE ||
                    code == SPV_SAMPLED_IMAGE || code == SPV_ARRAY || code == SPV_RUNTIME_ARRAY || code == SPV_STRUCT ||
                    code == SPV_POINTER) && len >= 2 && x[0] < bound) {
            u32 id = x[0];
            op[id] = code;
            if (code == SPV_INT) a1[id] = len >= 4 && x[2] ? 2 : 1;   // знаковый — 2
            else if (code == SPV_FLOAT) a1[id] = 0;
            else if (code == SPV_VECTOR || code == SPV_SAMPLED_IMAGE || code == SPV_ARRAY || code == SPV_RUNTIME_ARRAY)
                a1[id] = len >= 3 ? x[1] : 0;
            else if (code == SPV_IMAGE) a1[id] = len >= 8 ? x[6] : 0;   // 1 — выборка, 2 — запись
            else if (code == SPV_POINTER && len >= 4) {
                a1[id] = x[1];
                a2[id] = x[2];
            }
        } else if (code == SPV_VARIABLE && len >= 4 && x[1] < bound && x[0] < bound) {
            u32 id = x[1], sc = x[2], t = a2[x[0]];
            // Сквозь массивы к типу элемента
            for (u32 guard = 0; t < bound && (op[t] == SPV_ARRAY || op[t] == SPV_RUNTIME_ARRAY) && guard < 8; guard++) t = a1[t];
            if (t >= bound) {
                i += len;
                continue;
            }
            if (bind[id] != UINT32_MAX) {
                u32 b = bind[id];
                u8 type = DT_NONE;
                if (sc == SC_STORAGE_BUFFER) type = DT_SSBO;
                else if (sc == SC_UNIFORM) type = (fl[t] & F_BUFFER_BLOCK) ? DT_SSBO : DT_UBO;
                else if (sc == SC_UNIFORM_CONSTANT && op[t] == SPV_SAMPLED_IMAGE) type = DT_TEX;
                else if (sc == SC_UNIFORM_CONSTANT && op[t] == SPV_IMAGE && a1[t] == 2) type = DT_IMAGE;
                if (b >= MAX_BINDS || type == DT_NONE) {
                    log_error("Vulkan: привязка %u в SPIR-V не поддержана", b);
                    return false;
                }
                r->type[b] = type;
                if (iface_all || (fl[id] & F_IFACE)) r->used |= 1ull << b;
            } else if (sc == SC_INPUT && loc[id] < MAX_INPUTS && !(fl[id] & F_BUILTIN)) {
                u32 c = op[t] == SPV_VECTOR ? a1[t] : t;
                r->inputs |= 1u << loc[id];
                r->input_kind[loc[id]] = c < bound && op[c] != UINT32_MAX ? (u8)a1[c] : 0;
            }
        }
        i += len;
    }
    return true;
}

static const char *const k_stage_name[] = { "вершинный", "фрагментный", "вычислительный" };

// Модуль стадии по рецепту: data/spv/<имя>.spv, собранный tools/spv/build.sh
static VkShaderModule load_stage(const RbPipeDesc *d, RbStage stage, Reflect *r)
{
    char line[512], name[256], path[1024];
    if (!rb_spv_recipe(d, stage, line, sizeof line, name, sizeof name)) {
        log_error("Vulkan: рецепт шейдера %s не помещается", d->name ? d->name : "?");
        return VK_NULL_HANDLE;
    }
    snprintf(path, sizeof path, "%s/spv/%s", g_vk.data_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("Vulkan: нет %s (%s шейдер «%s»). Строка в tools/spv/list.txt: %s, затем ./tools/spv/build.sh",
                  path, k_stage_name[stage], d->name ? d->name : "?", line);
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
    if (ok && reflect(code, (size_t)size / 4, r)) {
        VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (size_t)size, .pCode = code };
        VK_OK(vkCreateShaderModule(g_vk.device, &ci, NULL, &m));
    } else {
        log_error("Vulkan: шейдер %s не прочитан", path);
    }
    g_vk.scratch->used = mark;
    return m;
}

// Набор привязок с такими типами: общий у всех конвейеров с ним (push-дескрипторы не сбрасываются)
static u32 layout_get(const u8 *type)
{
    for (u32 i = 0; i < g_vk.nlay; i++)
        if (!memcmp(g_vk.lay[i].type, type, MAX_BINDS)) return i;
    if (g_vk.nlay == MAX_LAYOUTS) {
        log_error("Vulkan: наборов привязок больше %u", MAX_LAYOUTS);
        return UINT32_MAX;
    }
    static const VkDescriptorType k_dt[] = {
        [DT_UBO] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, [DT_SSBO] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        [DT_TEX] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, [DT_IMAGE] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    };
    VkDescriptorSetLayoutBinding b[MAX_BINDS];
    u32 n = 0;
    Layout *l = &g_vk.lay[g_vk.nlay];
    memset(l, 0, sizeof *l);
    for (u32 i = 0; i < MAX_BINDS; i++) {
        if (!type[i]) continue;
        b[n++] = (VkDescriptorSetLayoutBinding){ i, k_dt[type[i]], 1, VK_SHADER_STAGE_ALL, NULL };
        l->mask |= 1ull << i;
    }
    if (n > g_vk.max_push_desc) {
        log_error("Vulkan: привязок %u, устройство даёт push-дескрипторов %u", n, g_vk.max_push_desc);
        return UINT32_MAX;
    }
    VkDescriptorSetLayoutCreateInfo dl = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
        .bindingCount = n,
        .pBindings = b,
    };
    if (!VK_OK(vkCreateDescriptorSetLayout(g_vk.device, &dl, NULL, &l->set))) return UINT32_MAX;
    VkPushConstantRange pc = { VK_SHADER_STAGE_ALL, 0, RB_PUSH_BYTES };
    VkPipelineLayoutCreateInfo pl = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &l->set,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc,
    };
    if (!VK_OK(vkCreatePipelineLayout(g_vk.device, &pl, NULL, &l->pl))) {
        vkDestroyDescriptorSetLayout(g_vk.device, l->set, NULL);
        return UINT32_MAX;
    }
    memcpy(l->type, type, MAX_BINDS);
    return g_vk.nlay++;
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

// Форматы целей, под которые объявлен конвейер: окно — RB_FMT_NONE и в цвете, и в глубине
static void desc_formats(const RbPipeDesc *d, VkFormat *col, u32 *n, VkFormat *depth)
{
    *n = 0;
    if (d->color[0] == RB_FMT_NONE && d->depth_fmt == RB_FMT_NONE) {
        col[(*n)++] = WIN_COLOR_FMT;
        *depth = WIN_DEPTH_FMT;
        return;
    }
    for (u32 c = 0; c < RB_MAX_COLORS && d->color[c] != RB_FMT_NONE; c++) col[(*n)++] = tex_fmt(d->color[c]);
    *depth = d->depth_fmt != RB_FMT_NONE ? tex_fmt(d->depth_fmt) : VK_FORMAT_UNDEFINED;
}

// Графический конвейер под форматы целей прохода. Конвейер GL не знает целей, поэтому здесь
// по варианту на набор форматов: первый — по объявленным в RbPipeDesc, остальные — при первом вызове
static VkPipeline variant_get(Pipe *p, const VkFormat *col, u32 ncol, VkFormat depth)
{
    for (u32 i = 0; i < p->nvar; i++) {
        const Variant *v = &p->var[i];
        if (v->ncolor == ncol && v->depth == depth && !memcmp(v->color, col, ncol * sizeof(VkFormat))) return v->pipe;
    }
    if (p->nvar == MAX_VARIANTS) {
        if (!p->warned) log_error("Vulkan: у конвейера %s вариантов целей больше %u", p->d.name ? p->d.name : "?", MAX_VARIANTS);
        p->warned = true;
        return VK_NULL_HANDLE;
    }
    const RbPipeDesc *d = &p->d;
    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = p->vs, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = p->fs, .pName = "main" },
    };
    // Атрибуты раскладки; вход шейдера без атрибута читает (0, 0, 0, 1) из потока констант, как GL
    VkVertexInputAttributeDescription attrs[MAX_INPUTS + RB_MAX_ATTRS];
    VkVertexInputBindingDescription binds[CONST_STREAM + 1];
    u32 na = 0, nb = 0, covered = 0;
    for (u32 i = 0; i < d->layout.attrs; i++) {
        const RbAttr *a = &d->layout.attr[i];
        attrs[na++] = (VkVertexInputAttributeDescription){ a->location, a->stream, attr_fmt(a->fmt), a->offset };
        covered |= 1u << a->location;
    }
    for (u32 s = 0; s < RB_MAX_STREAMS; s++)
        if (p->streams & (1u << s))
            binds[nb++] = (VkVertexInputBindingDescription){
                s, d->layout.stride[s], d->layout.per_instance[s] ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX };
    u32 missing = p->inputs & ~covered;
    if (missing) {
        static const VkFormat k_const_fmt[] = { VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_SINT };
        for (u32 l = 0; l < MAX_INPUTS; l++)
            if (missing & (1u << l)) {
                u32 k = MIN(p->input_kind[l], 2u);
                attrs[na++] = (VkVertexInputAttributeDescription){ l, CONST_STREAM, k_const_fmt[k], 128 + 16 * (k ? 1 : 0) };
            }
        binds[nb++] = (VkVertexInputBindingDescription){ CONST_STREAM, 0, VK_VERTEX_INPUT_RATE_VERTEX };
    }
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = nb,
        .pVertexBindingDescriptions = binds,
        .vertexAttributeDescriptionCount = na,
        .pVertexAttributeDescriptions = attrs,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    // Отсечение по глубине как в GL, [-1, 1]: шейдеры не меняются
    VkPipelineViewportDepthClipControlCreateInfoEXT clip = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT,
        .negativeOneToOne = 1,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = &clip,
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
        .depthTestEnable = d->depth != RB_DEPTH_OFF && depth != VK_FORMAT_UNDEFINED,
        .depthWriteEnable = d->depth_write && depth != VK_FORMAT_UNDEFINED,
        .depthCompareOp = d->depth == RB_DEPTH_LEQUAL ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
    };
    VkPipelineColorBlendAttachmentState blends[RB_MAX_COLORS];
    for (u32 c = 0; c < ncol; c++) blends[c] = blend_state(d->blend, d->no_color);
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = ncol,
        .pAttachments = blends,
    };
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE };
    VkPipelineDynamicStateCreateInfo dy = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = ARRAY_COUNT(dyn),   // шаг потока задаётся при привязке: поток нулей идёт с шагом 0
        .pDynamicStates = dyn,
    };
    VkPipelineRenderingCreateInfo rend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = ncol,
        .pColorAttachmentFormats = col,
        .depthAttachmentFormat = depth,
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
        .layout = g_vk.lay[p->layout].pl,
    };
    VkPipeline pipe = VK_NULL_HANDLE;
    if (!VK_OK(vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &pipe))) return VK_NULL_HANDLE;
    Variant *v = &p->var[p->nvar++];
    memset(v, 0, sizeof *v);
    v->pipe = pipe;
    memcpy(v->color, col, ncol * sizeof(VkFormat));
    v->ncolor = ncol;
    v->depth = depth;
    return pipe;
}

static void pipe_release(Pipe *p)
{
    for (u32 i = 0; i < p->nvar; i++) trash_add(TR_PIPE, p->var[i].pipe, NULL);
    if (p->cpipe) trash_add(TR_PIPE, p->cpipe, NULL);
    // Модули нужны только при создании конвейеров
    if (p->vs) vkDestroyShaderModule(g_vk.device, p->vs, NULL);
    if (p->fs) vkDestroyShaderModule(g_vk.device, p->fs, NULL);
    memset(p, 0, sizeof *p);
}

RbPipe rb_vk_pipe_make(const RbPipeDesc *d)
{
    u32 slot = 0;
    for (u32 i = 1; i < MAX_PIPES && !slot; i++)
        if (!g_vk.pipe[i].used) slot = i;
    if (!slot) {
        log_error("RB: конвейеров больше %u", MAX_PIPES);
        return 0;
    }
    Pipe *p = &g_vk.pipe[slot];
    memset(p, 0, sizeof *p);
    p->used = true;
    p->compute = d->cs[0] != NULL;
    p->d = *d;
    Reflect r[2];
    memset(r, 0, sizeof r);
    bool ok;
    if (p->compute) {
        p->vs = load_stage(d, RB_STAGE_COMP, &r[0]);
        ok = p->vs != VK_NULL_HANDLE;
    } else {
        p->vs = load_stage(d, RB_STAGE_VERT, &r[0]);
        p->fs = load_stage(d, RB_STAGE_FRAG, &r[1]);
        ok = p->vs && p->fs;
    }
    u8 type[MAX_BINDS];
    for (u32 b = 0; ok && b < MAX_BINDS; b++) {
        type[b] = r[0].type[b] ? r[0].type[b] : r[1].type[b];
        if (r[0].type[b] && r[1].type[b] && r[0].type[b] != r[1].type[b]) {
            log_error("Vulkan: %s: привязка %u разного типа в стадиях", d->name ? d->name : "?", b);
            ok = false;
        }
    }
    if (ok) {
        p->layout = layout_get(type);
        ok = p->layout != UINT32_MAX;
    }
    p->used_mask = r[0].used | r[1].used;
    p->inputs = r[0].inputs;
    memcpy(p->input_kind, r[0].input_kind, sizeof p->input_kind);
    for (u32 i = 0; i < d->layout.attrs; i++) p->streams |= 1u << d->layout.attr[i].stream;
    if (ok && p->compute) {
        VkComputePipelineCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                       .module = p->vs, .pName = "main" },
            .layout = g_vk.lay[p->layout].pl,
        };
        ok = VK_OK(vkCreateComputePipelines(g_vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &p->cpipe));
    } else if (ok) {
        // Вариант под объявленные цели собирается сразу: ошибка видна при загрузке, а не посреди кадра
        VkFormat col[RB_MAX_COLORS], depth;
        u32 n;
        desc_formats(d, col, &n, &depth);
        ok = variant_get(p, col, n, depth) != VK_NULL_HANDLE;
    }
    if (!ok) {
        pipe_release(p);
        return 0;
    }
    return slot;
}

void rb_pipe_free(RbPipe *h)
{
    if (!*h || *h >= MAX_PIPES || !g_vk.pipe[*h].used) return;
    pipe_release(&g_vk.pipe[*h]);
    if (g_vk.cur == *h) g_vk.cur = 0;
    *h = 0;
}

void rb_bind_pipe(RbPipe h)
{
    g_vk.cur = h < MAX_PIPES && g_vk.pipe[h].used ? h : 0;
}

void rb_shaders_changed(void)
{
    // .spv читаются заново при каждом rb_pipe_make: кешировать нечего
}

// --- Проходы

static void begin_rendering(bool load)
{
    const RbPass *p = &g_vk.pass;
    VkRenderingAttachmentInfo ca[RB_MAX_COLORS], da;
    u32 n = 0;
    bool window = !p->color[0].tex && !p->depth.tex, has_depth = window || p->depth.tex;
    for (u32 c = 0; c < (window ? 1u : RB_MAX_COLORS); c++) {
        if (!window && !p->color[c].tex) break;
        VkImageView v = window ? g_vk.win_color.view : tex_view(&g_vk.tex[p->color[c].tex], p->color[c].mip, p->color[c].layer);
        ca[n++] = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = v,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = p->clear_color && !load ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue.color.float32 = { p->clear[0], p->clear[1], p->clear[2], p->clear[3] },
        };
    }
    if (has_depth) {
        VkImageView v = window ? g_vk.win_depth.view : tex_view(&g_vk.tex[p->depth.tex], p->depth.mip, p->depth.layer);
        da = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = v,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = p->clear_depth && !load ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue.depthStencil = { 1.0f, 0 },
        };
    }
    VkRenderingInfo ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { { 0, 0 }, { (u32)g_vk.pass_w, (u32)g_vk.pass_h } },
        .layerCount = 1,
        .colorAttachmentCount = n,
        .pColorAttachments = ca,
        .pDepthAttachment = has_depth ? &da : NULL,
    };
    vkCmdBeginRendering(g_vk.cmd, &ri);
}

void rb_pass_begin(const RbPass *p)
{
    ensure_cmd();
    if (g_vk.in_pass) rb_pass_end();
    g_vk.pass = *p;
    bool window = !p->color[0].tex && !p->depth.tex;
    g_vk.pass_ncolor = 0;
    if (window) {
        g_vk.pass_color[g_vk.pass_ncolor++] = WIN_COLOR_FMT;
        g_vk.pass_depth = WIN_DEPTH_FMT;
        g_vk.pass_w = g_vk.win_color.d.w;
        g_vk.pass_h = g_vk.win_color.d.h;
    } else {
        for (u32 c = 0; c < RB_MAX_COLORS && p->color[c].tex; c++) g_vk.pass_color[g_vk.pass_ncolor++] = g_vk.tex[p->color[c].tex].fmt;
        g_vk.pass_depth = p->depth.tex ? g_vk.tex[p->depth.tex].fmt : VK_FORMAT_UNDEFINED;
        const RbAttach *a = p->color[0].tex ? &p->color[0] : &p->depth;
        g_vk.pass_w = MAX(g_vk.tex[a->tex].d.w >> a->mip, 1);
        g_vk.pass_h = MAX(g_vk.tex[a->tex].d.h >> a->mip, 1);
    }
    sync();
    begin_rendering(false);
    g_vk.in_pass = true;
    rb_viewport(0, 0, g_vk.pass_w, g_vk.pass_h);
    rb_scissor_off();
}

void rb_pass_end(void)
{
    leave_pass();
}

// Строки в памяти совпадают с GL, поэтому y снизу — это просто номер строки
void rb_viewport(i32 x, i32 y, i32 w, i32 h)
{
    g_vk.vp = (VkViewport){ (f32)x, (f32)y, (f32)MAX(w, 1), (f32)MAX(h, 1), 0.0f, 1.0f };
    if (g_vk.in_pass) vkCmdSetViewport(g_vk.cmd, 0, 1, &g_vk.vp);
}

void rb_scissor(i32 x, i32 y, i32 w, i32 h)
{
    i32 x0 = MAX(x, 0), y0 = MAX(y, 0);
    g_vk.sc = (VkRect2D){ { x0, y0 }, { (u32)MAX(x + w - x0, 0), (u32)MAX(y + h - y0, 0) } };
    if (g_vk.in_pass) vkCmdSetScissor(g_vk.cmd, 0, 1, &g_vk.sc);
}

void rb_scissor_off(void)
{
    rb_scissor(0, 0, g_vk.pass_w, g_vk.pass_h);
}

void rb_clear_rect(i32 x, i32 y, i32 w, i32 h, const f32 *color, bool depth)
{
    if (!g_vk.in_pass) return;
    VkClearAttachment a[RB_MAX_COLORS + 1];
    u32 n = 0;
    if (color)
        for (u32 c = 0; c < g_vk.pass_ncolor; c++)
            a[n++] = (VkClearAttachment){ VK_IMAGE_ASPECT_COLOR_BIT, c, { .color.float32 = { color[0], color[1], color[2], color[3] } } };
    if (depth && g_vk.pass_depth != VK_FORMAT_UNDEFINED)
        a[n++] = (VkClearAttachment){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } };
    // Прямоугольник обрезается по цели, как glClear с ножницами
    i32 x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + w, g_vk.pass_w), y1 = MIN(y + h, g_vk.pass_h);
    if (!n || x1 <= x0 || y1 <= y0) return;
    VkClearRect r = { { { x0, y0 }, { (u32)(x1 - x0), (u32)(y1 - y0) } }, 0, 1 };
    vkCmdClearAttachments(g_vk.cmd, n, a, 1, &r);
}

// --- Привязки

static void bind_set(u32 b, const Bind *v)
{
    if (b >= MAX_BINDS) {
        log_error("Vulkan: привязка %u больше %u", b, MAX_BINDS - 1);
        return;
    }
    Bind *o = &g_vk.bind[b];
    if (o->kind == v->kind && o->h == v->h && o->off == v->off && o->size == v->size && o->smp == v->smp && o->mip == v->mip) return;
    *o = *v;
    g_vk.dirty[0] |= 1ull << b;
    g_vk.dirty[1] |= 1ull << b;
}

void rb_bind_vertex(u32 stream, RbBuf b, size_t offset)
{
    g_vk.vb[stream] = b;
    g_vk.vb_off[stream] = offset;
}

void rb_bind_index(RbBuf b, size_t offset, bool idx16)
{
    g_vk.ib = b;
    g_vk.ib_off = offset;
    g_vk.ib16 = idx16;
}

void rb_bind_ubo(u32 binding, RbBuf b, size_t offset, size_t size)
{
    bind_set(binding, &(Bind){ .kind = DT_UBO, .h = b, .off = offset, .size = size });
}

void rb_bind_ssbo(u32 binding, RbBuf b, size_t offset, size_t size)
{
    bind_set(binding, &(Bind){ .kind = DT_SSBO, .h = b, .off = offset, .size = size });
}

void rb_bind_tex(u32 binding, RbTex t, RbSampler s)
{
    bind_set(binding, &(Bind){ .kind = DT_TEX, .h = t, .smp = (u8)s });
}

void rb_bind_image(u32 binding, RbTex t, u32 mip)
{
    bind_set(binding, &(Bind){ .kind = DT_IMAGE, .h = t, .mip = mip });
}

void rb_push(u32 offset, u32 size, const void *data)
{
    if (offset + size > RB_PUSH_BYTES) return;
    memcpy(g_vk.push + offset, data, size);
    g_vk.push_dirty[0] = g_vk.push_dirty[1] = true;
}

// Дескрипторы и константы конвейера: bp 0 — графика, 1 — вычисления. false — чего-то не хватает
static bool prep_desc(Pipe *p, u32 bp)
{
    const Layout *l = &g_vk.lay[p->layout];
    bool new_layout = g_vk.cur_layout[bp] != p->layout;
    // Что шейдер читает, должно быть привязано подходящим типом; нет — пустой дескриптор (нули), а без
    // robustness2 вызов пропускается
    u64 empty = 0;
    for (u64 m = p->used_mask; m; m &= m - 1) {
        u32 b = (u32)__builtin_ctzll(m);
        const Bind *v = &g_vk.bind[b];
        bool ok = v->kind == l->type[b] && v->h &&
                  ((v->kind == DT_UBO || v->kind == DT_SSBO) ? g_vk.buf[v->h].buf != VK_NULL_HANDLE : g_vk.tex[v->h].image != VK_NULL_HANDLE);
        if (ok) continue;
        if (!g_vk.null_desc) {
            if (!p->warned) log_error("Vulkan: конвейер %s: привязка %u не задана, вызов пропущен", p->d.name ? p->d.name : "?", b);
            p->warned = true;
            return false;
        }
        empty |= 1ull << b;
    }
    VkPipelineBindPoint point = bp ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    // Пустые уходят каждый раз: привязка могла опустеть (освобождена) без отметки
    u64 todo = ((new_layout ? l->mask : g_vk.dirty[bp]) | empty) & l->mask;
    if (todo) {
        VkWriteDescriptorSet w[MAX_BINDS];
        VkDescriptorBufferInfo bi[MAX_BINDS];
        VkDescriptorImageInfo ii[MAX_BINDS];
        u32 n = 0;
        for (u64 m = todo; m; m &= m - 1) {
            u32 b = (u32)__builtin_ctzll(m);
            const Bind *v = &g_vk.bind[b];
            static const VkDescriptorType k_dt[] = {
                [DT_UBO] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, [DT_SSBO] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                [DT_TEX] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, [DT_IMAGE] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            };
            w[n] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b, .descriptorCount = 1,
                                           .descriptorType = k_dt[l->type[b]] };
            if (empty & (1ull << b)) {
                bi[n] = (VkDescriptorBufferInfo){ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };
                ii[n] = (VkDescriptorImageInfo){ g_vk.smp[0], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL };
                if (l->type[b] == DT_UBO || l->type[b] == DT_SSBO) w[n].pBufferInfo = &bi[n];
                else w[n].pImageInfo = &ii[n];
                n++;
                continue;
            }
            if (v->kind != l->type[b] || !v->h) continue;   // шейдер не читает
            if (v->kind == DT_UBO || v->kind == DT_SSBO) {
                const VBuf *buf = &g_vk.buf[v->h];
                if (!buf->buf) continue;
                VkDeviceSize range = v->size ? v->size : buf->size - v->off;
                if (v->kind == DT_UBO) range = MIN(range, (VkDeviceSize)g_vk.props.limits.maxUniformBufferRange);
                bi[n] = (VkDescriptorBufferInfo){ buf->buf, v->off, range };
                w[n].descriptorType = v->kind == DT_UBO ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[n].pBufferInfo = &bi[n];
            } else {
                VTex *t = &g_vk.tex[v->h];
                if (!t->image) continue;
                if (v->kind == DT_TEX) {
                    ii[n] = (VkDescriptorImageInfo){ g_vk.smp[v->smp], t->view, VK_IMAGE_LAYOUT_GENERAL };
                    w[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                } else {
                    ii[n] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, tex_view(t, v->mip, ALL_LAYERS), VK_IMAGE_LAYOUT_GENERAL };
                    w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                }
                w[n].pImageInfo = &ii[n];
            }
            n++;
        }
        if (n) vkCmdPushDescriptorSetKHR(g_vk.cmd, point, l->pl, 0, n, w);
    }
    g_vk.dirty[bp] = 0;
    if (new_layout || g_vk.push_dirty[bp]) {
        vkCmdPushConstants(g_vk.cmd, l->pl, VK_SHADER_STAGE_ALL, 0, RB_PUSH_BYTES, g_vk.push);
        g_vk.push_dirty[bp] = false;
    }
    g_vk.cur_layout[bp] = p->layout;
    return true;
}

// Перед вызовом рисования: конвейер под цели прохода, дескрипторы, константы, потоки вершин
static bool prep_draw(bool indexed)
{
    if (!g_vk.in_pass || !g_vk.cur) return false;
    Pipe *p = &g_vk.pipe[g_vk.cur];
    if (p->compute) return false;
    VkPipeline vp = variant_get(p, g_vk.pass_color, g_vk.pass_ncolor, g_vk.pass_depth);
    if (!vp) return false;
    if (vp != g_vk.bound[0]) {
        vkCmdBindPipeline(g_vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vp);
        g_vk.bound[0] = vp;
    }
    if (!prep_desc(p, 0)) return false;
    // Потоки: все сразу одной командой, если что-то сменилось. Нет буфера — поток нулей с шагом 0
    VkBuffer vb[CONST_STREAM + 1];
    VkDeviceSize off[CONST_STREAM + 1], stride[CONST_STREAM + 1];
    bool change = false;
    for (u32 s = 0; s <= CONST_STREAM; s++) {
        RbBuf b = s < RB_MAX_STREAMS && (p->streams & (1u << s)) ? g_vk.vb[s] : 0;
        vb[s] = b ? g_vk.buf[b].buf : g_vk.zero.buf;
        off[s] = b ? g_vk.vb_off[s] : 0;
        stride[s] = b ? p->d.layout.stride[s] : 0;
        change |= vb[s] != g_vk.vb_cur[s] || off[s] != g_vk.vb_cur_off[s] || stride[s] != g_vk.vb_cur_stride[s];
    }
    if (change) {
        vkCmdBindVertexBuffers2(g_vk.cmd, 0, CONST_STREAM + 1, vb, off, NULL, stride);
        memcpy(g_vk.vb_cur, vb, sizeof vb);
        memcpy(g_vk.vb_cur_off, off, sizeof off);
        memcpy(g_vk.vb_cur_stride, stride, sizeof stride);
    }
    if (indexed) {
        if (!g_vk.ib) return false;
        VkBuffer ib = g_vk.buf[g_vk.ib].buf;
        if (ib != g_vk.ib_cur || g_vk.ib_off != g_vk.ib_cur_off || g_vk.ib16 != g_vk.ib_cur16) {
            vkCmdBindIndexBuffer(g_vk.cmd, ib, g_vk.ib_off, g_vk.ib16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            g_vk.ib_cur = ib;
            g_vk.ib_cur_off = g_vk.ib_off;
            g_vk.ib_cur16 = g_vk.ib16;
        }
    }
    return true;
}

// --- Вызовы

void rb_draw(u32 verts, u32 first, u32 instances)
{
    if (prep_draw(false)) vkCmdDraw(g_vk.cmd, verts, MAX(instances, 1), first, 0);
}

void rb_draw_indexed(u32 count, u32 first_index, i32 base_vertex, u32 instances, u32 base_instance)
{
    if (prep_draw(true)) vkCmdDrawIndexed(g_vk.cmd, count, MAX(instances, 1), first_index, base_vertex, base_instance);
}

void rb_draw_indexed_multi(const u32 *counts, const u32 *first_indices, u32 n)
{
    if (!n || !prep_draw(true)) return;
    for (u32 i = 0; i < n; i++) vkCmdDrawIndexed(g_vk.cmd, counts[i], 1, first_indices[i], 0, 0);
}

void rb_draw_indexed_indirect(RbBuf b, size_t offset, u32 n)
{
    if (!n || !b || !prep_draw(true)) return;
    vkCmdDrawIndexedIndirect(g_vk.cmd, g_vk.buf[b].buf, offset, n, 20);
}

// Вычисление — вне прохода, с барьерами до и после: посчитанное видно всем следующим чтениям
static bool prep_dispatch(bool *was)
{
    Pipe *p = &g_vk.pipe[g_vk.cur];
    if (!g_vk.cur || !p->compute) return false;
    ensure_cmd();
    *was = leave_pass();
    sync();
    if (p->cpipe != g_vk.bound[1]) {
        vkCmdBindPipeline(g_vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->cpipe);
        g_vk.bound[1] = p->cpipe;
    }
    if (!prep_desc(p, 1)) {
        back_to_pass(*was);
        return false;
    }
    return true;
}

void rb_dispatch(u32 x, u32 y, u32 z)
{
    bool was;
    if (!prep_dispatch(&was)) return;
    vkCmdDispatch(g_vk.cmd, x, y, z);
    g_vk.need_barrier = true;
    back_to_pass(was);
}

void rb_dispatch_indirect(RbBuf b, size_t offset)
{
    bool was;
    if (!b || !prep_dispatch(&was)) return;
    vkCmdDispatchIndirect(g_vk.cmd, g_vk.buf[b].buf, offset);
    g_vk.need_barrier = true;
    back_to_pass(was);
}


// --- Метки времени

bool rb_timer_init(u32 sets, u32 marks)
{
    if (!sets || !marks || sets * marks > TIMER_MAX) return false;
    VkQueryPoolCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = RB_FRAMES * sets * marks,
    };
    if (!VK_OK(vkCreateQueryPool(g_vk.device, &ci, NULL, &g_vk.qpool))) return false;
    g_vk.tsets = sets;
    g_vk.tmarks = marks;
    // Сброс всего пула; дальше каждый слот сбрасывает свой кусок в начале записи
    ensure_cmd();
    bool was = leave_pass();
    vkCmdResetQueryPool(g_vk.cmd, g_vk.qpool, 0, ci.queryCount);
    back_to_pass(was);
    return true;
}

void rb_timer_mark(u32 set, u32 mark)
{
    if (!g_vk.qpool || set >= g_vk.tsets || mark >= g_vk.tmarks) return;
    ensure_cmd();
    u32 i = set * g_vk.tmarks + mark;
    Frame *f = &g_vk.frames[g_vk.slot];
    if (f->tmask[i / 32] & (1u << (i % 32))) return;   // уже записана в этом слоте: второй раз без сброса нельзя
    vkCmdWriteTimestamp2(g_vk.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, g_vk.qpool, g_vk.slot * g_vk.tsets * g_vk.tmarks + i);
    f->tmask[i / 32] |= 1u << (i % 32);
    g_vk.tready[i] = false;
}

// Слот снова свободен: его метки посчитаны, забираем без ожидания
static void timers_collect(Frame *f)
{
    if (!g_vk.qpool) return;
    u32 slot = (u32)(f - g_vk.frames), n = g_vk.tsets * g_vk.tmarks;
    for (u32 i = 0; i < n; i++) {
        if (!(f->tmask[i / 32] & (1u << (i % 32)))) continue;
        u64 r[2] = { 0, 0 };
        VkResult res = vkGetQueryPoolResults(g_vk.device, g_vk.qpool, slot * n + i, 1, sizeof r, r, sizeof r,
                                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (res == VK_SUCCESS && r[1]) {
            g_vk.tval[i] = (u64)((f64)(r[0] & g_vk.ts_mask) * (f64)g_vk.props.limits.timestampPeriod);
            g_vk.tready[i] = true;
        }
    }
    memset(f->tmask, 0, sizeof f->tmask);
}

bool rb_timer_ready(u32 set, u32 mark)
{
    u32 i = set * g_vk.tmarks + mark;
    return i < TIMER_MAX && g_vk.tready[i];
}

u64 rb_timer_get(u32 set, u32 mark)
{
    u32 i = set * g_vk.tmarks + mark;
    return i < TIMER_MAX ? g_vk.tval[i] : 0;
}

// --- Кадр

void rb_frame_begin(i32 win_w, i32 win_h)
{
    g_vk.win_w = MAX(win_w, 1);
    g_vk.win_h = MAX(win_h, 1);
    window_targets(g_vk.win_w, g_vk.win_h);
    ensure_cmd();
    g_vk.in_frame = true;
}

u32 rb_frame_slot(void)
{
    return g_vk.slot;
}

// Перевод картинки swapchain между раскладками
static void swap_barrier(VkImage image, VkImageLayout from, VkImageLayout to)
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
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b };
    vkCmdPipelineBarrier2(g_vk.cmd, &dep);
}

void rb_frame_end(void)
{
    ensure_cmd();
    leave_pass();
    sync();
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
        swap_barrier(dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // Переворот: строка 0 кадра (низ картинки, как в GL) уходит в нижнюю строку окна
        i32 sw = g_vk.win_color.d.w, sh = g_vk.win_color.d.h;
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
            .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .dstImage = dst,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &region,
            .filter = VK_FILTER_NEAREST,
        };
        vkCmdBlitImage2(g_vk.cmd, &bi);
        swap_barrier(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        // Окно дальше снова пишется проходами: перенос должен дочитать его раньше
        g_vk.need_barrier = true;
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
    if (g_vk.swapchain && (g_vk.swap_extent.width != (u32)g_vk.win_color.d.w || g_vk.swap_extent.height != (u32)g_vk.win_color.d.h))
        g_vk.swap_dirty = true;
    g_vk.cmd = VK_NULL_HANDLE;
    g_vk.in_frame = false;
    g_vk.slot = (g_vk.slot + 1) % RB_FRAMES;
}

void rb_read_window(u8 *rgb, i32 w, i32 h)
{
    ensure_cmd();
    bool was = leave_pass();
    sync();
    w = MIN(w, g_vk.win_color.d.w);
    h = MIN(h, g_vk.win_color.d.h);
    VBuf rb;
    if (w <= 0 || h <= 0 || !vbuf_create(&rb, (size_t)w * (size_t)h * 4, true)) {
        back_to_pass(was);
        return;
    }
    VkBufferImageCopy copy = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { (u32)w, (u32)h, 1 },
    };
    vkCmdCopyImageToBuffer(g_vk.cmd, g_vk.win_color.image, VK_IMAGE_LAYOUT_GENERAL, rb.buf, 1, &copy);
    VkMemoryBarrier2 mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
    };
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb };
    vkCmdPipelineBarrier2(g_vk.cmd, &dep);
    // Кадр ещё не отправлен: дописываем копию, ждём GPU и продолжаем запись в том же слоте
    flush_wait();
    // В памяти строка 0 — низ картинки, как и просят: RGB снизу вверх
    const u8 *src = rb.a.map;
    for (i32 i = 0; i < w * h; i++) {
        rgb[i * 3 + 0] = src[i * 4 + 0];
        rgb[i * 3 + 1] = src[i * 4 + 1];
        rgb[i * 3 + 2] = src[i * 4 + 2];
    }
    vbuf_destroy_now(&rb);
    back_to_pass(was);
}

// --- Начало и конец

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
        if (!vbuf_create(&f->staging, STAGING_BYTES, true)) return false;
    }
    return true;
}

static bool create_samplers(void)
{
    static const struct { VkFilter filter; VkSamplerAddressMode wrap; bool cmp; } k[RB_SMP_COUNT] = {
        [RB_SMP_NEAREST] = { VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false },
        [RB_SMP_LINEAR] = { VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false },
        [RB_SMP_NEAREST_REPEAT] = { VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, false },
        [RB_SMP_LINEAR_REPEAT] = { VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, false },
        [RB_SMP_SHADOW] = { VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true },
    };
    for (u32 i = 0; i < RB_SMP_COUNT; i++) {
        // Как GL_NEAREST / GL_LINEAR без мипов: всегда нулевой мип вида
        VkSamplerCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = k[i].filter,
            .minFilter = k[i].filter,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = k[i].wrap,
            .addressModeV = k[i].wrap,
            .addressModeW = k[i].wrap,
            .compareEnable = k[i].cmp,
            .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .minLod = 0.0f,
            .maxLod = 0.0f,
        };
        if (!VK_OK(vkCreateSampler(g_vk.device, &ci, NULL, &g_vk.smp[i]))) return false;
    }
    return true;
}

// Поток нулей (сетка без весов) и с 128 — (0, 0, 0, 1) для входов шейдера без атрибута, float и int
static bool create_zero(void)
{
    if (!vbuf_create(&g_vk.zero, ZERO_BYTES, false)) return false;
    u8 data[ZERO_BYTES];
    memset(data, 0, sizeof data);
    f32 one_f = 1.0f;
    u32 one_u = 1;
    memcpy(data + 128 + 12, &one_f, 4);
    memcpy(data + 144 + 12, &one_u, 4);
    VkDeviceSize so;
    u8 *dst = staging_take(sizeof data, &so);
    memcpy(dst, data, sizeof data);
    VkBufferCopy c = { so, 0, sizeof data };
    vkCmdCopyBuffer(g_vk.cmd, g_vk.frames[g_vk.slot].staging.buf, g_vk.zero.buf, 1, &c);
    g_vk.need_barrier = true;
    return true;
}

bool rb_init(Arena *scratch, const char *data_dir)
{
    memset(&g_vk, 0, sizeof g_vk);
    g_vk.scratch = scratch;
    snprintf(g_vk.data_dir, sizeof g_vk.data_dir, "%s", data_dir);
    PlatformProc loader = platform_vk_loader();
    if (!loader) {
        log_error("Vulkan: в системе нет загрузчика (libvulkan / vulkan-1.dll)");
        return false;
    }
    memcpy(&vkGetInstanceProcAddr, &loader, sizeof loader);
    i32 w, h;
    platform_window_size(&w, &h);
    g_vk.win_w = MAX(w, 1);
    g_vk.win_h = MAX(h, 1);
    if (!create_instance() || !create_surface() || !create_device() || !create_frames() || !create_samplers() ||
        !create_zero() || !window_targets(g_vk.win_w, g_vk.win_h)) {
        rb_shutdown();
        return false;
    }
    swapchain_build();
    log_info("Vulkan: %s, API %u.%u.%u, validation %s", g_vk.props.deviceName, VK_API_VERSION_MAJOR(g_vk.props.apiVersion),
             VK_API_VERSION_MINOR(g_vk.props.apiVersion), VK_API_VERSION_PATCH(g_vk.props.apiVersion),
             g_vk.validation ? "вкл" : "выкл");
    return true;
}

void rb_shutdown(void)
{
    if (g_vk.device) {
        // Записанное, но не отправленное — отправить, чтобы буфер команд закончился честно
        if (g_vk.cmd) flush_wait();
        vkDeviceWaitIdle(g_vk.device);
        g_vk.dying = true;
        for (u32 i = 0; i < RB_FRAMES; i++) empty_trash(&g_vk.frames[i]);
        for (u32 i = 1; i < MAX_PIPES; i++)
            if (g_vk.pipe[i].used) pipe_release(&g_vk.pipe[i]);
        for (u32 i = 1; i < MAX_TEXS; i++)
            if (g_vk.tex[i].image) tex_release(&g_vk.tex[i], true);
        for (u32 i = 1; i < MAX_BUFS; i++)
            if (g_vk.buf[i].buf) vbuf_destroy_now(&g_vk.buf[i]);
        vbuf_destroy_now(&g_vk.zero);
        tex_release(&g_vk.win_color, true);
        tex_release(&g_vk.win_depth, true);
        for (u32 i = 0; i < g_vk.nlay; i++) {
            vkDestroyPipelineLayout(g_vk.device, g_vk.lay[i].pl, NULL);
            vkDestroyDescriptorSetLayout(g_vk.device, g_vk.lay[i].set, NULL);
        }
        for (u32 i = 0; i < RB_SMP_COUNT; i++)
            if (g_vk.smp[i]) vkDestroySampler(g_vk.device, g_vk.smp[i], NULL);
        if (g_vk.qpool) vkDestroyQueryPool(g_vk.device, g_vk.qpool, NULL);
        for (u32 i = 0; i < RB_FRAMES; i++) {
            Frame *f = &g_vk.frames[i];
            vbuf_destroy_now(&f->staging);
            if (f->acquired) vkDestroySemaphore(g_vk.device, f->acquired, NULL);
            if (f->done) vkDestroyFence(g_vk.device, f->done, NULL);
            if (f->pool) vkDestroyCommandPool(g_vk.device, f->pool, NULL);
        }
        for (u32 i = 0; i < g_vk.nblocks; i++) vkFreeMemory(g_vk.device, g_vk.blocks[i].mem, NULL);
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
