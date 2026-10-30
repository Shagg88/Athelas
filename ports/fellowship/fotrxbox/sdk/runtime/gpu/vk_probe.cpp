// The Vulkan backend's first piece: whether this PC can run it, said in the log. Called at start-up while the
// backend is being written; it changes nothing the game does.
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "gpu/vk_loader.h"

namespace gpu::vk {

// logs the Vulkan version, each GPU with its driver's Vulkan version, and what the backend will need
bool Probe() {
    const char* why = nullptr;
    if (!OpenLibrary(&why)) {
        LOGR("vulkan: not available: %s", why ? why : "?");
        return false;
    }
    uint32_t api = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&api);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Athelas";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ci, nullptr, &instance);
    if (r != VK_SUCCESS) {
        LOGR("vulkan: loader %u.%u, but no instance (error %d)", VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api), int(r));
        return false;
    }
    LoadInstance(instance);
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devices(n);
    if (n) vkEnumeratePhysicalDevices(instance, &n, devices.data());
    LOGR("vulkan: loader %u.%u, %u GPU%s", VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api), n, n == 1 ? "" : "s");
    bool usable = false;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> ext(en);
        if (en) vkEnumerateDeviceExtensionProperties(d, nullptr, &en, ext.data());
        auto has = [&](const char* name) {
            for (auto& e : ext)
                if (!std::strcmp(e.extensionName, name)) return true;
            return false;
        };
        bool ok = p.apiVersion >= VK_API_VERSION_1_2 && has(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        usable = usable || ok;
        LOGR("vulkan:   %s, Vulkan %u.%u%s%s%s", p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
             VK_API_VERSION_MINOR(p.apiVersion), has("VK_KHR_dynamic_rendering") ? ", dynamic rendering" : "",
             has("VK_KHR_push_descriptor") ? ", push descriptors" : "", ok ? "" : " (too old for the backend)");
    }
    vkDestroyInstance(instance, nullptr);
    return usable;
}

}  // namespace gpu::vk
