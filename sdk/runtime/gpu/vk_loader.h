// Vulkan without an SDK or import library: vulkan-1.dll (the driver's loader, part of every Vulkan driver) is
// opened at run time and its functions looked up. Nothing is linked and nothing ships; no Vulkan, no backend.
#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifdef _WIN32
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif
#include <vulkan/vulkan.h>

namespace gpu::vk {

// the functions, by level: global (before an instance), instance, device
#define GPU_VK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties) X(vkEnumerateInstanceLayerProperties) \
    X(vkEnumerateInstanceVersion)
#define GPU_VK_INSTANCE(X)                                                                                              \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                                 \
    X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceFeatures2)                    \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties)                                  \
    X(vkGetPhysicalDeviceFormatProperties) X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice)                    \
    X(vkGetDeviceProcAddr) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR)                               \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) GPU_VK_INSTANCE_WIN32(X)
#ifdef _WIN32
#define GPU_VK_INSTANCE_WIN32(X) X(vkCreateWin32SurfaceKHR)
#else
#define GPU_VK_INSTANCE_WIN32(X)
#endif
#define GPU_VK_DEVICE(X)                                                                                                \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle)                      \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR)                \
    X(vkQueuePresentKHR) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory)                            \
    X(vkFlushMappedMemoryRanges) X(vkInvalidateMappedMemoryRanges) X(vkCreateBuffer) X(vkDestroyBuffer)                 \
    X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkCreateImage) X(vkDestroyImage)                           \
    X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView)                     \
    X(vkCreateSampler) X(vkDestroySampler) X(vkCreateShaderModule) X(vkDestroyShaderModule)                             \
    X(vkCreatePipelineCache) X(vkDestroyPipelineCache) X(vkGetPipelineCacheData) X(vkCreateGraphicsPipelines)           \
    X(vkDestroyPipeline) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateDescriptorSetLayout)            \
    X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool)       \
    X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateCommandPool) X(vkDestroyCommandPool)                \
    X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer)                     \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkWaitSemaphores) X(vkGetSemaphoreCounterValue)                        \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkCreateQueryPool)                         \
    X(vkDestroyQueryPool) X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetBlendConstants)         \
    X(vkCmdSetStencilReference) X(vkCmdBindDescriptorSets) X(vkCmdBindIndexBuffer) X(vkCmdBindVertexBuffers)            \
    X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdCopyBuffer) X(vkCmdCopyImage) X(vkCmdBlitImage)                             \
    X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage)          \
    X(vkCmdClearAttachments) X(vkCmdResolveImage) X(vkCmdPipelineBarrier) X(vkCmdBeginQuery) X(vkCmdEndQuery)           \
    X(vkCmdResetQueryPool) X(vkCmdCopyQueryPoolResults) X(vkCmdPushConstants) X(vkCmdBeginRenderPass)                   \
    X(vkCmdEndRenderPass) X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
    X(vkCmdBeginRenderingKHR) X(vkCmdEndRenderingKHR)

#define GPU_VK_DECLARE(name) extern PFN_##name name;
GPU_VK_GLOBAL(GPU_VK_DECLARE)
GPU_VK_INSTANCE(GPU_VK_DECLARE)
GPU_VK_DEVICE(GPU_VK_DECLARE)
#undef GPU_VK_DECLARE
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

// opens vulkan-1.dll and loads the global functions; false (and why, in reason) when this PC has no Vulkan
bool OpenLibrary(const char** reason);
// the instance and device functions; a missing one is null (optional ones are checked before use)
void LoadInstance(VkInstance instance);
void LoadDevice(VkDevice device);
// the library's own lookup, for ImGui's Vulkan backend
PFN_vkVoidFunction Lookup(const char* name, void* instance);

}  // namespace gpu::vk
