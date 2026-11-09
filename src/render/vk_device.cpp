// Vulkan: instance, устройство, VMA, swapchain. Через vk-bootstrap, функции — volk
#include "vk_graph.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include "vk-bootstrap/VkBootstrap.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>

VkState g_vk;
u32 g_vk_validation_errors = 0;

bool vk_check(VkResult r, const char *what) {
    if (r == VK_SUCCESS) return true;
    log_error("Vulkan: %s → %d", what, (int)r);
    if (r == VK_ERROR_DEVICE_LOST) vk_device_lost();
    return false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL on_debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                               VkDebugUtilsMessageTypeFlagsEXT,
                                               const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        g_vk_validation_errors++;
        log_error("слой проверки: %s", data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        log_warn("слой проверки: %s", data->pMessage);
    }
    return VK_FALSE;
}

static bool create_device() {
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)platform_vk_get_instance_proc_addr();
    if (!gipa) { log_error("нет vkGetInstanceProcAddr"); return false; }
    volkInitializeCustom(gipa);

    vkb::InstanceBuilder ib(gipa);
    ib.set_app_name("Anathemyre").set_engine_name("anathemyre").require_api_version(1, 3, 0);
    u32 ext_count = 0;
    const char *const *ext = platform_vk_instance_extensions(&ext_count);
    for (u32 i = 0; i < ext_count; ++i) ib.enable_extension(ext[i]);
    if (g_vk.cfg.validation || g_vk.cfg.gpu_validation) {
        ib.request_validation_layers(true)
            .set_debug_callback(on_debug)
            .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
        // Проверка на GPU (чтение и запись мимо буферов по адресу) медленная и с синхронизацией не сочетается:
        // это отдельный запуск (docs/graph.md, «Проверки»)
        if (g_vk.cfg.gpu_validation) {
            // Шейдеры выбирает vk_load_shader (структурой проверок у модуля): кроме тех, что lavapipe не тянет
#ifdef _WIN32
            _putenv_s("VK_KHRONOS_VALIDATION_SELECT_INSTRUMENTED_SHADERS", "1");
#else
            setenv("VK_KHRONOS_VALIDATION_SELECT_INSTRUMENTED_SHADERS", "1", 1);
#endif
            ib.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
        }
        else ib.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
    }
    auto inst = ib.build();
    if (!inst) {
        log_error("Vulkan 1.3 недоступен: %s", inst.error().message().c_str());
        return false;
    }
    vkb::Instance vi = inst.value();
    g_vk.instance = vi.instance;
    g_vk.messenger = vi.debug_messenger;
    volkLoadInstanceOnly(g_vk.instance);
    if ((g_vk.cfg.validation || g_vk.cfg.gpu_validation) && !g_vk.messenger) log_warn("слои проверки Vulkan не найдены, иду без них");

    g_vk.surface = (VkSurfaceKHR)platform_vk_create_surface(g_vk.instance);
    if (!g_vk.surface) return false;

    // Требуемые возможности перечислены явно (docs/stack.md): на неподходящей карте — понятная ошибка
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.bufferDeviceAddress = VK_TRUE;
    f12.timelineSemaphore = VK_TRUE;
    f12.descriptorIndexing = VK_TRUE;
    f12.runtimeDescriptorArray = VK_TRUE;
    f12.descriptorBindingPartiallyBound = VK_TRUE;
    f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    f12.scalarBlockLayout = VK_TRUE;
    f12.drawIndirectCount = VK_TRUE;  // кадр мира: вызовы видимых кластеров
    // gl_PrimitiveID во фрагменте visibility buffer требует возможности геометрического шейдера
    VkPhysicalDeviceFeatures f10{};
    f10.geometryShader = VK_TRUE;

    vkb::PhysicalDeviceSelector sel(vi);
    auto pick = sel.set_surface(g_vk.surface)
                    .set_minimum_version(1, 3)
                    .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                    .set_required_features_13(f13)
                    .set_required_features_12(f12)
                    .set_required_features(f10)
                    .select();
    if (!pick) {
        log_error("нет видеокарты с Vulkan 1.3 и нужными возможностями: %s", pick.error().message().c_str());
        for (const std::string &why : pick.detailed_failure_reasons()) log_error("  %s", why.c_str());
        return false;
    }
    vkb::PhysicalDevice pd = pick.value();
    g_vk.gpu = pd.physical_device;
    snprintf(g_vk.gpu_name, sizeof g_vk.gpu_name, "%s", pd.properties.deviceName);
    g_vk.timestamp_period = pd.properties.limits.timestampPeriod;

    // Разбор сбоя GPU (vk_diag.cpp): что есть у драйвера. Метки NV или AMD — одно из двух
    g_vk.diag.nv_checkpoints = pd.enable_extension_if_present(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    if (!g_vk.diag.nv_checkpoints) g_vk.diag.amd_marker = pd.enable_extension_if_present(VK_AMD_BUFFER_MARKER_EXTENSION_NAME);
    VkPhysicalDeviceFaultFeaturesEXT fault{};
    fault.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
    fault.deviceFault = VK_TRUE;
    g_vk.diag.device_fault = pd.enable_extension_if_present(VK_EXT_DEVICE_FAULT_EXTENSION_NAME) &&
                             pd.enable_extension_features_if_present(fault);

    auto dev = vkb::DeviceBuilder(pd).build();
    if (!dev) {
        log_error("не создалось устройство Vulkan: %s", dev.error().message().c_str());
        return false;
    }
    vkb::Device vd = dev.value();
    g_vk.device = vd.device;
    volkLoadDevice(g_vk.device);
    auto q = vd.get_queue(vkb::QueueType::graphics);
    auto qi = vd.get_queue_index(vkb::QueueType::graphics);
    if (!q || !qi) { log_error("нет очереди графики"); return false; }
    g_vk.queue = q.value();
    g_vk.queue_family = qi.value();
    // Метки времени GPU: у очереди должны быть значащие биты
    u32 nfam = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.gpu, &nfam, nullptr);
    VkQueueFamilyProperties fam[16];
    if (nfam > 16) nfam = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.gpu, &nfam, fam);
    if (g_vk.queue_family >= nfam || fam[g_vk.queue_family].timestampValidBits == 0) g_vk.timestamp_period = 0;

    VmaVulkanFunctions fns{};
    fns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    fns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ai{};
    ai.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    ai.physicalDevice = g_vk.gpu;
    ai.device = g_vk.device;
    ai.instance = g_vk.instance;
    ai.vulkanApiVersion = VK_API_VERSION_1_3;
    ai.pVulkanFunctions = &fns;
    return vk_check(vmaCreateAllocator(&ai, &g_vk.vma), "vmaCreateAllocator");
}

static void destroy_depth() {
    if (g_vk.depth.view) vkDestroyImageView(g_vk.device, g_vk.depth.view, nullptr);
    if (g_vk.depth.image) vmaDestroyImage(g_vk.vma, g_vk.depth.image, g_vk.depth.alloc);
    g_vk.depth = GpuImage{};
    g_vk.depth_extent = {};
}

// Глубина D32F под размер swapchain: обратная (ближе — больше), очистка нулём
static bool create_depth() {
    if (g_vk.depth.image && g_vk.depth_extent.width == g_vk.extent.width && g_vk.depth_extent.height == g_vk.extent.height)
        return true;
    destroy_depth();
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = DEPTH_FORMAT;
    ii.extent = {g_vk.extent.width, g_vk.extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &g_vk.depth.image, &g_vk.depth.alloc, nullptr), "глубина"))
        return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = g_vk.depth.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = DEPTH_FORMAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &g_vk.depth.view), "вид глубины")) return false;
    g_vk.depth_extent = g_vk.extent;
    if (!g_vk.rg_depth) g_vk.rg_depth = rg_image("глубина", VK_IMAGE_ASPECT_DEPTH_BIT);
    rg_bind_image(g_vk.rg_depth, g_vk.depth.image);
    vk_image_name(g_vk.depth.image, "глубина");
    return true;
}

bool vk_create_swapchain() {
    i32 w = 0, h = 0;
    platform_window_size(&w, &h);
    vkb::SwapchainBuilder sb(g_vk.gpu, g_vk.device, g_vk.surface, g_vk.queue_family, g_vk.queue_family);
    sb.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format({VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(g_vk.cfg.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR)
        .add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
        .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent((u32)w, (u32)h)
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        .set_old_swapchain(g_vk.swapchain);
    auto sc = sb.build();
    VkSwapchainKHR old = g_vk.swapchain;
    if (!sc) {
        log_error("не создался swapchain: %s", sc.error().message().c_str());
        return false;
    }
    // Старый удаляется после создания нового (его передали как old_swapchain)
    g_vk.swapchain = VK_NULL_HANDLE;
    for (u32 i = 0; i < g_vk.image_count; ++i) vkDestroyImageView(g_vk.device, g_vk.views[i], nullptr);
    if (old) vkDestroySwapchainKHR(g_vk.device, old, nullptr);

    vkb::Swapchain s = sc.value();
    auto imgs = s.get_images();
    auto views = s.get_image_views();
    if (!imgs || !views || imgs.value().size() > MAX_SWAPCHAIN_IMAGES) {
        log_error("swapchain: не получить картинки");
        return false;
    }
    g_vk.swapchain = s.swapchain;
    g_vk.format = s.image_format;
    g_vk.extent = s.extent;
    g_vk.image_count = (u32)imgs.value().size();
    for (u32 i = 0; i < g_vk.image_count; ++i) {
        g_vk.images[i] = imgs.value()[i];
        g_vk.views[i] = views.value()[i];
    }
    if (!create_depth()) return false;
    auto si = vk_info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    for (; g_vk.rendered_count < g_vk.image_count; g_vk.rendered_count++)
        if (!vk_check(vkCreateSemaphore(g_vk.device, &si, nullptr, &g_vk.rendered[g_vk.rendered_count]), "vkCreateSemaphore"))
            return false;
    g_vk.swapchain_dirty = false;
    return true;
}

void vk_destroy_swapchain() {
    destroy_depth();
    for (u32 i = 0; i < g_vk.image_count; ++i) vkDestroyImageView(g_vk.device, g_vk.views[i], nullptr);
    for (u32 i = 0; i < g_vk.rendered_count; ++i) vkDestroySemaphore(g_vk.device, g_vk.rendered[i], nullptr);
    g_vk.image_count = 0;
    g_vk.rendered_count = 0;
    if (g_vk.swapchain) vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, nullptr);
    g_vk.swapchain = VK_NULL_HANDLE;
}

void render_set_vsync(bool on) {
    if (g_vk.cfg.vsync == on) return;
    g_vk.cfg.vsync = on;
    g_vk.swapchain_dirty = true;
}

bool vk_create_buffer(GpuBuffer &b, VkDeviceSize size, VkBufferUsageFlags usage, bool device_local) {
    auto bi = vk_info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bi.size = size;
    bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = device_local ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE : VMA_MEMORY_USAGE_AUTO;
    // Видимый процессору: запись подряд (кадр), а приёмник копии (снимок) читается — тогда кешируемая память
    if (!device_local)
        ci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | ((usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                                                           ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                                           : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    VmaAllocationInfo info{};
    if (!vk_check(vmaCreateBuffer(g_vk.vma, &bi, &ci, &b.buffer, &b.alloc, &info), "vmaCreateBuffer")) return false;
    b.mapped = info.pMappedData;
    b.size = size;
    auto ai = vk_info<VkBufferDeviceAddressInfo>(VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
    ai.buffer = b.buffer;
    b.address = vkGetBufferDeviceAddress(g_vk.device, &ai);
    vk_buffer_track(b);
    return device_local || b.mapped != nullptr;
}

void vk_destroy_buffer(GpuBuffer &b) {
    if (b.buffer) {
        vk_buffer_untrack(b);
        vmaDestroyBuffer(g_vk.vma, b.buffer, b.alloc);
    }
    b = GpuBuffer{};
}

static bool create_frames() {
    auto pi = vk_info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pi.queueFamilyIndex = g_vk.queue_family;
    auto fi = vk_info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    auto si = vk_info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    for (FrameSlot &f : g_vk.frames) {
        if (!vk_check(vkCreateCommandPool(g_vk.device, &pi, nullptr, &f.pool), "vkCreateCommandPool")) return false;
        auto ai = vk_info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (!vk_check(vkAllocateCommandBuffers(g_vk.device, &ai, &f.cmd), "vkAllocateCommandBuffers")) return false;
        if (!vk_check(vkCreateFence(g_vk.device, &fi, nullptr, &f.done), "vkCreateFence")) return false;
        if (!vk_check(vkCreateSemaphore(g_vk.device, &si, nullptr, &f.acquired), "vkCreateSemaphore")) return false;
        const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (!vk_create_buffer(f.frame, sizeof(FrameGpu), ssbo)) return false;
        if (!vk_create_buffer(f.points, RENDER_MAX_POINTS * sizeof(RenderPoint), ssbo)) return false;
        if (!vk_create_buffer(f.lines, RENDER_MAX_LINE_VERTICES * sizeof(RenderLineVertex), ssbo)) return false;
        if (!vk_create_buffer(f.decals, RENDER_MAX_DECALS * sizeof(RenderDecal), ssbo)) return false;
    }
    if (g_vk.timestamp_period > 0) {
        auto qi = vk_info<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = GPU_MAX_MARKS * FRAMES_IN_FLIGHT;
        if (!vk_check(vkCreateQueryPool(g_vk.device, &qi, nullptr, &g_vk.queries), "vkCreateQueryPool")) return false;
    }
    return true;
}

// Сброс состояния на месте. Не `g_vk = VkState{}`: это временный объект на стеке, а VkState ~1 МБ — ровно
// стек главного потока Windows (с 6а: переполнение стека при старте, на Linux стек 8 МБ и не видно)
static void vk_state_reset() { ::new (&g_vk) VkState{}; }

bool render_init(const RenderConfig &cfg) {
    vk_state_reset();
    g_vk.cfg = cfg;
    if (!create_device()) return false;
    if (!vk_create_swapchain()) return false;
    if (!create_frames()) return false;
    g_vk.rg_swap = rg_image("картинка кадра", VK_IMAGE_ASPECT_COLOR_BIT);
    g_vk.rg_capture = rg_buffer("снимок кадра");
    if (!vk_diag_init()) return false;
    if (!vk_geometry_init()) return false;
    if (!vk_pipeline_cache_open()) return false;
    u64 t0 = platform_time_ns();
    if (!vk_create_pipelines()) return false;
    f64 pipes_ms = (f64)(platform_time_ns() - t0) / 1e6;
    vk_pipeline_cache_save();
    log_info("Vulkan: %s, кадр %ux%u, %s, конвейеры за %.0f мс", g_vk.gpu_name, g_vk.extent.width, g_vk.extent.height,
             cfg.vsync ? "vsync" : "без vsync", pipes_ms);
    return true;
}

void render_shutdown() {
    if (!g_vk.instance) return;
    if (g_vk.device) {
        vkDeviceWaitIdle(g_vk.device);
        vk_destroy_pipelines();
        vk_pipeline_cache_close();
        vk_terrain_shutdown();
        vk_geometry_shutdown();
        vk_destroy_buffer(g_vk.capture);
        vk_diag_shutdown();
        if (g_vk.queries) vkDestroyQueryPool(g_vk.device, g_vk.queries, nullptr);
        for (FrameSlot &f : g_vk.frames) {
            vk_destroy_buffer(f.frame);
            vk_destroy_buffer(f.points);
            vk_destroy_buffer(f.lines);
            vk_destroy_buffer(f.decals);
            if (f.acquired) vkDestroySemaphore(g_vk.device, f.acquired, nullptr);
            if (f.done) vkDestroyFence(g_vk.device, f.done, nullptr);
            if (f.pool) vkDestroyCommandPool(g_vk.device, f.pool, nullptr);
        }
        vk_destroy_swapchain();
        if (g_vk.vma) vmaDestroyAllocator(g_vk.vma);
        vkDestroyDevice(g_vk.device, nullptr);
    }
    if (g_vk.surface) vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, nullptr);
    if (g_vk.messenger) vkDestroyDebugUtilsMessengerEXT(g_vk.instance, g_vk.messenger, nullptr);
    vkDestroyInstance(g_vk.instance, nullptr);
    vk_state_reset();
    rg_clear();
}

u32 render_validation_errors() { return g_vk_validation_errors; }
f64 render_gpu_ms() { return g_vk.gpu_ms; }
RenderCpuTimes render_cpu_times() { return g_vk.cpu; }
const char *render_device_name() { return g_vk.gpu_name; }
