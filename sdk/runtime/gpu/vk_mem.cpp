// The Vulkan Memory Allocator's code (VMA_IMPLEMENTATION), compiled once here; see vk_mem.h
#define VMA_IMPLEMENTATION
#include "gpu/vk_mem.h"

namespace gpu::vk {

VmaAllocator CreateAllocator(VkInstance instance, VkPhysicalDevice gpu, VkDevice device) {
    VmaVulkanFunctions f{};
    f.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    f.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    f.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    f.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    f.vkAllocateMemory = vkAllocateMemory;
    f.vkFreeMemory = vkFreeMemory;
    f.vkMapMemory = vkMapMemory;
    f.vkUnmapMemory = vkUnmapMemory;
    f.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    f.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    f.vkBindBufferMemory = vkBindBufferMemory;
    f.vkBindImageMemory = vkBindImageMemory;
    f.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    f.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    f.vkCreateBuffer = vkCreateBuffer;
    f.vkDestroyBuffer = vkDestroyBuffer;
    f.vkCreateImage = vkCreateImage;
    f.vkDestroyImage = vkDestroyImage;
    f.vkCmdCopyBuffer = vkCmdCopyBuffer;
    VmaAllocatorCreateInfo ci{};
    ci.physicalDevice = gpu;
    ci.device = device;
    ci.instance = instance;
    ci.vulkanApiVersion = VK_API_VERSION_1_0;
    ci.pVulkanFunctions = &f;
    VmaAllocator a = VK_NULL_HANDLE;
    if (vmaCreateAllocator(&ci, &a) != VK_SUCCESS) return VK_NULL_HANDLE;
    return a;
}

}  // namespace gpu::vk
