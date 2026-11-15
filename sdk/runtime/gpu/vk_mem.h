// The Vulkan renderer's memory: AMD's Vulkan Memory Allocator (third_party/vma, MIT), which suballocates images out
// of large blocks, so a long session stays well under the driver's allocation count limit (often 4096 on Windows;
// notes/vulkan-parity-plan.md, D2). Vulkan 1.0 calls only, handed over from vk_loader (nothing is linked). Every file
// that includes the allocator includes it through here, so all see one configuration.
#pragma once
#include "gpu/vk_loader.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_VULKAN_VERSION 1000000
#define VMA_DEDICATED_ALLOCATION 0
#define VMA_BIND_MEMORY2 0
#define VMA_MEMORY_BUDGET 0
#define VMA_BUFFER_DEVICE_ADDRESS 0
#define VMA_MEMORY_PRIORITY 0
#define VMA_KHR_MAINTENANCE4 0
#define VMA_KHR_MAINTENANCE5 0
#define VMA_EXTERNAL_MEMORY 0
#define VMA_EXTERNAL_MEMORY_WIN32 0
#define VMA_STATS_STRING_ENABLED 0
// (the library's own warnings are its own: not shown)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "vk_mem_alloc.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace gpu::vk {
// the allocator for this device, its functions from vk_loader; null on failure
VmaAllocator CreateAllocator(VkInstance instance, VkPhysicalDevice gpu, VkDevice device);
}  // namespace gpu::vk
