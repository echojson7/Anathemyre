// Регистры и занятость compute-шейдеров без видеокарты (docs/perf_arch.md): собирает конвейер драйвером RADV
// на «пустом» устройстве (RADV_FORCE_FAMILY) и печатает статистику компилятора — VGPR, SGPR, сброс регистров в
// память, размер кода, LDS и волн на SIMD. Раскладка — как общая раскладка игры (vk_world_layout, PushConsts).
// Запуск: shader_stats файл.spv [константа 0] [константа 1] … (см. tools/shader_stats/check.sh)
#define VK_NO_PROTOTYPES
#include "volk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *p = malloc(*size);
    if (fread(p, 1, *size, f) != *size) { free(p); p = NULL; }
    fclose(f);
    return p;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "shader_stats файл.spv [spec0 spec1 ...]\n"); return 2; }
    if (volkInitialize() != VK_SUCCESS) { fprintf(stderr, "нет Vulkan\n"); return 1; }
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ii = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    VkInstance inst;
    if (vkCreateInstance(&ii, NULL, &inst) != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance\n"); return 1; }
    volkLoadInstance(inst);
    uint32_t n = 1;
    VkPhysicalDevice pd;
    vkEnumeratePhysicalDevices(inst, &n, &pd);
    if (n == 0) { fprintf(stderr, "нет устройства\n"); return 1; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);

    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.bufferDeviceAddress = VK_TRUE;
    f12.scalarBlockLayout = VK_TRUE;
    f12.shaderInt8 = VK_TRUE;
    f12.storageBuffer8BitAccess = VK_TRUE;
    f12.shaderFloat16 = VK_TRUE;
    f12.descriptorIndexing = VK_TRUE;
    f12.runtimeDescriptorArray = VK_TRUE;
    f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    f12.descriptorBindingPartiallyBound = VK_TRUE;
    f12.vulkanMemoryModel = VK_TRUE;
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.pNext = &f12;
    f13.maintenance4 = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR fexe = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
    fexe.pNext = &f13;
    fexe.pipelineExecutableInfo = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &fexe;
    f2.features.shaderInt64 = VK_TRUE;
    f2.features.shaderInt16 = VK_TRUE;
    f2.features.shaderImageGatherExtended = VK_TRUE;
    f2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    f2.features.shaderStorageImageReadWithoutFormat = VK_TRUE;
    f2.features.samplerAnisotropy = VK_TRUE;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    const char *ext[] = {VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME};
    VkDeviceCreateInfo di = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext = &f2;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = ext;
    VkDevice dev;
    if (vkCreateDevice(pd, &di, NULL, &dev) != VK_SUCCESS) { fprintf(stderr, "vkCreateDevice\n"); return 1; }
    volkLoadDevice(dev);

    // Общая раскладка игры: 24 привязки набора кадра мира + push-константы (адрес кадра, два слова)
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    VkSampler smp, cmp;
    vkCreateSampler(dev, &si, NULL, &smp);
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    vkCreateSampler(dev, &si, NULL, &cmp);
    VkSampler smp2[2] = {smp, smp};
    VkDescriptorSetLayoutBinding b[24];
    memset(b, 0, sizeof b);
    for (uint32_t i = 0; i < 24; ++i) {
        int sampled = i == 2 || i == 4 || i == 5 || i == 9 || i == 10 || i == 12 || i == 14 || i == 15 || i == 20 || i == 21 || i == 23;
        b[i].binding = i;
        b[i].descriptorType = sampled ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = i == 12 || i == 13 ? 2 : 1;
        b[i].stageFlags = VK_SHADER_STAGE_ALL;
        b[i].pImmutableSamplers = sampled ? (i == 12 ? smp2 : i == 14 ? &cmp : &smp) : NULL;
    }
    b[16].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    b[16].descriptorCount = 256;
    b[17].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    b[17].pImmutableSamplers = &smp;
    VkDescriptorSetLayoutCreateInfo sli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.bindingCount = 24;
    sli.pBindings = b;
    VkDescriptorSetLayout sl;
    vkCreateDescriptorSetLayout(dev, &sli, NULL, &sl);
    VkPushConstantRange pcr = {VK_SHADER_STAGE_ALL, 0, 16};
    VkPipelineLayoutCreateInfo li = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &sl;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pcr;
    VkPipelineLayout layout;
    vkCreatePipelineLayout(dev, &li, NULL, &layout);

    size_t size;
    void *code = read_file(argv[1], &size);
    if (!code) { fprintf(stderr, "нет файла %s\n", argv[1]); return 1; }
    VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = size;
    mi.pCode = code;
    VkShaderModule mod;
    if (vkCreateShaderModule(dev, &mi, NULL, &mod) != VK_SUCCESS) { fprintf(stderr, "модуль\n"); return 1; }
    // Константы специализации по порядку: constant_id 0, 1, … (TILE_KIND, LIGHT_DEBUG у освещения)
    uint32_t spec[8] = {0};
    VkSpecializationMapEntry me[8];
    uint32_t nspec = 0;
    for (int a = 2; a < argc && nspec < 8; ++a, ++nspec) {
        spec[nspec] = (uint32_t)atoi(argv[a]);
        me[nspec].constantID = nspec;
        me[nspec].offset = nspec * 4;
        me[nspec].size = 4;
    }
    VkSpecializationInfo spi = {nspec, me, nspec * 4, spec};
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = mod;
    ci.stage.pName = "main";
    ci.stage.pSpecializationInfo = nspec ? &spi : NULL;
    ci.layout = layout;
    VkPipeline pipe;
    if (vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &pipe) != VK_SUCCESS) {
        fprintf(stderr, "конвейер не собран\n");
        return 1;
    }
    VkPipelineInfoKHR pinfo = {VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
    pinfo.pipeline = pipe;
    uint32_t ne = 0;
    vkGetPipelineExecutablePropertiesKHR(dev, &pinfo, &ne, NULL);
    VkPipelineExecutablePropertiesKHR ep[4];
    for (uint32_t i = 0; i < 4; ++i) { memset(&ep[i], 0, sizeof ep[i]); ep[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR; }
    if (ne > 4) ne = 4;
    vkGetPipelineExecutablePropertiesKHR(dev, &pinfo, &ne, ep);
    printf("%s", props.deviceName);
    for (uint32_t e = 0; e < ne; ++e) {
        VkPipelineExecutableInfoKHR ei = {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
        ei.pipeline = pipe;
        ei.executableIndex = e;
        uint32_t ns = 0;
        vkGetPipelineExecutableStatisticsKHR(dev, &ei, &ns, NULL);
        VkPipelineExecutableStatisticKHR st[64];
        if (ns > 64) ns = 64;
        for (uint32_t i = 0; i < ns; ++i) { memset(&st[i], 0, sizeof st[i]); st[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR; }
        vkGetPipelineExecutableStatisticsKHR(dev, &ei, &ns, st);
        for (uint32_t i = 0; i < ns; ++i) {
            printf(" | %s=", st[i].name);
            switch (st[i].format) {
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: printf("%u", st[i].value.b32); break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: printf("%lld", (long long)st[i].value.i64); break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: printf("%llu", (unsigned long long)st[i].value.u64); break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: printf("%.2f", st[i].value.f64); break;
            default: break;
            }
        }
    }
    printf("\n");
    return 0;
}
