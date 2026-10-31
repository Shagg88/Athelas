// The Vulkan backend ([graphics] renderer = 1), phase 1 of notes/vulkan-plan.md: instance, device, swapchain, a frame
// ring, render targets by console address, Clear, and Present showing the front buffer. Draws are not done yet: every
// other call is a safe no-op, logged once.
// Init failing (no Vulkan, no 1.2 device, no swapchain) leaves the game on Direct3D 12 (xdk/d3d8.cpp).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "gpu/vk_loader.h"  // first: the platform define before vulkan.h is read
#ifndef IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#endif
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_win32.h>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/common.h"
#include "gpu/shaders.h"
#include "gpu/spirv.h"
#include "gpu/vertex.h"
#include "gpu/texture.h"
#include "gpu/vk_loader.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace gpu {

namespace {

using namespace gpu::vk;

constexpr uint32_t kFrames = 2;  // frames in flight

bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const VkExtensionProperties& e : list)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

const char* ResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VkResult";
    }
}

class VkBackend final : public Backend {
public:
    ~VkBackend() override { Destroy(); }

    bool Init(uint32_t width, uint32_t height) override;
    const char* InitError() const override { return error_.empty() ? nullptr : error_.c_str(); }

    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
               uint32_t stencil) override;
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override;
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override;
    void Present(const Surface& front) override;
    void Sync(uint32_t data, uint32_t size) override;
    // a NOOVERWRITE lock: read back at once for now (D3D12 hands over a finished copy a frame old instead)
    void SyncLate(uint32_t data) override { Sync(data, 0); }
    void Flush() override;
    void SetGamma(const uint8_t*) override { LOG_FIRST(1, "vulkan: SetGamma is not done yet"); }
    // queries, fences and busy checks: the Backend defaults (none counted, never behind) until drawing is done

private:
    bool Fail(const std::string& why) {
        error_ = why;
        LOGR("vulkan: %s", why.c_str());
        return false;
    }
    bool Fail(const char* what, VkResult r) { return Fail(std::string(what) + " failed (" + ResultName(r) + ")"); }

    bool CreateInstance();
    bool CreateSurface();
    bool PickDevice();
    bool CreateDevice();
    bool CreateSwapchain();
    void DestroySwapchain();
    bool CreateFrames();
    void Destroy();
    bool PickDepthFormat();
    bool CreateDrawResources();

    // an image with its own memory and one view
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t levels = 1, layers = 1;
    };
    bool MakeImage(Image& im, const VkImageCreateInfo& ci, VkImageViewType view, VkImageAspectFlags view_aspect);
    void DestroyImage(Image& im);
    void Retire(Image& im);
    bool CreateDummy(Image& im, VkImageType type, VkImageViewType view, VkFormat fmt, uint32_t layers);
    void Barrier(VkImage image, const VkImageSubresourceRange& range, VkImageLayout& cur, VkImageLayout want);
    bool Upload(VkDeviceSize size, VkDeviceSize align, bool may_flush, VkDeviceSize& off, uint8_t*& cpu);

    // a console surface drawn into: a colour or depth image at the surface's console size (render scale comes later)
    struct Target {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool depth = false;
        uint32_t gw = 0, gh = 0;  // the console's size
        uint32_t w = 0, h = 0;    // drawn at render scale
        uint32_t data = 0, bytes = 0;  // guest memory under it
        uint64_t epoch = 0, sig = 0;   // the last lock seen, its memory as last matched
        bool drawn = false;            // the GPU wrote it since the last read-back
        VkImageView sample_view = VK_NULL_HANDLE;  // a depth target read by a shader: its depth alone
        Image fb;  // the copy a draw reads of the target it draws into
    };
    Target* EnsureTarget(const Surface& s, bool depth);
    Target* FindTarget(const Surface& s, bool depth);
    void DestroyTarget(Target& t);
    // a console rect in the target's own pixels, clamped to it
    static VkRect2D ScaledRect(const Target& t, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void MaskedClear(const State& s, Target& t, uint32_t count, const uint8_t* rects, uint32_t color, uint32_t mask);
    bool CpuWrote(Target& t);
    void UploadTarget(Target& t);
    bool FindMemory(uint32_t bits, VkMemoryPropertyFlags want, uint32_t& index) const;

    // recording: one command buffer per frame, open from the first command to Present (or Flush)
    VkCommandBuffer Cmd();
    void Submit(bool wait);
    void ToLayout(VkImage image, bool depth, VkImageLayout& cur, VkImageLayout want);
    // dynamic rendering into rt / ds (either may be null), kept open across draws until a transfer needs it closed
    bool BeginRendering(Target* rt, Target* ds);
    void EndRendering();
    void BlitToScreen(VkCommandBuffer cb, Target* front, uint32_t index);
    bool InitUi();
    void RenderUi(VkCommandBuffer cb, uint32_t index);
    void CountFps();
    bool ui_ = false;  // ImGui's Vulkan backend is set up
    VkDescriptorPool ui_pool_ = VK_NULL_HANDLE;
    double fps_ = 0;
    uint64_t fps_frames_ = 0;
    uint64_t fps_start_ = 0;

    // draws
    struct Tex {
        Image img;
        TextureDesc d;
        uint32_t levels = 1, host_w = 0, host_h = 0;
        uint64_t sig = 0, frame = 0, palette = 0;
    };
    struct StageBind {
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool target = false;
        uint32_t width = 1, height = 1;
    };
    struct PipeKey {
        VkShaderModule vs, ps;
        uint32_t regs, buffers, strides[16], elements[16];
        uint32_t topo, strip, has_rt, has_ds;
        uint32_t blend, src, dst, op, wmask;
        uint32_t zen, zwrite, zfunc, sten, sfunc, smask, swmask, sfail, szfail, spass;
        uint32_t cull, ccw, fill;
        int32_t zbias;
        float zslope;
    };
    VkShaderModule ShaderModule(uint64_t key, const std::string& hlsl, spirv::Stage stage, const char* entry = "main");
    void BindTexture(const State& s, int i, Target* rt, Target* ds, StageBind& sb, TexInfo& info);
    VkImageView GuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi);
    void UploadTexture(Tex& tx, const uint8_t* p, const uint32_t* colours);
    VkSampler StageSampler(const State& s, int i, const TexInfo& info, bool target);
    VkPipeline Pipeline(const PipeKey& k);

    std::string error_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    bool dynamic_rendering_ = false;  // VK 1.3 or VK_KHR_dynamic_rendering; else render passes (for later phases)
    bool timeline_ = false;           // VK 1.2 timeline semaphores

    VkSwapchainKHR swap_ = VK_NULL_HANDLE;
    VkFormat swap_fmt_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    bool vsync_ = true;
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;  // the menu draws into them
    std::vector<VkSemaphore> rendered_;  // one per swapchain image: presentation waits on it

    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence done = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkBuffer upload = VK_NULL_HANDLE;  // Upload's ring
        VkDeviceMemory upload_mem = VK_NULL_HANDLE;
        uint8_t* upload_cpu = nullptr;
        VkDeviceSize used = 0;
        VkDescriptorPool sets = VK_NULL_HANDLE;
        std::vector<Image> garbage;  // destroyed when the slot comes round again
    };
    static constexpr VkDeviceSize kUploadSize = 64ull << 20;
    static constexpr VkDeviceSize kUploadReserve = 16ull << 20;
    static constexpr uint32_t kSetsPerFrame = 8192;
    Frame frames_[kFrames];
    uint32_t frame_ = 0;
    bool lost_ = false;
    bool recording_ = false;  // frames_[frame_].cmd is open

    VkPhysicalDeviceMemoryProperties mem_props_{};
    VkFormat depth_fmt_ = VK_FORMAT_D24_UNORM_S8_UINT;
    std::unordered_map<uint64_t, Target> targets_;  // by console address << 1 | depth
    Target* render_rt_ = nullptr;  // what the open dynamic rendering draws into
    Target* render_ds_ = nullptr;
    bool rendering_ = false;
    uint64_t presents_ = 0;
    // render scale ([display] render_percent of the window's height), wider by xmul_ for a game with wide targets
    float scale_ = 1.0f, xmul_ = 1.0f;
    uint32_t guest_w_ = 640, guest_h_ = 480;
    void PickScale();
    bool ScaleChanged();
    uint32_t scratch_w_ = 0, scratch_h_ = 0;
    Image scratch_;  // console sized: read-backs shrink into it, CPU pixels are enlarged from it
    bool EnsureScratch(uint32_t w, uint32_t h);

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipe_layout_ = VK_NULL_HANDLE;
    VkPipelineCache pipe_cache_ = VK_NULL_HANDLE;
    VkPipelineLayout clear_layout_ = VK_NULL_HANDLE;  // MaskedClear's: the colour as a push constant
    VkPipeline clear_pipes_[16] = {};                 // by colour write mask
    VkDeviceSize ubo_align_ = 256;
    bool anisotropy_ = false;  // the card's samplerAnisotropy, asked for at device creation
    bool fill_lines_ = false;  // fillModeNonSolid: wireframe
    Image dummy_[4];           // 2D, cube, 3D, depth
    VkBuffer readback_ = VK_NULL_HANDLE;  // Sync's, host visible, grown as needed
    VkDeviceMemory readback_mem_ = VK_NULL_HANDLE;
    uint8_t* readback_cpu_ = nullptr;
    VkDeviceSize readback_size_ = 0;
    VkSampler plain_sampler_ = VK_NULL_HANDLE, compare_sampler_ = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkShaderModule> modules_;
    std::unordered_map<uint64_t, VkPipeline> pipelines_;
    std::unordered_map<uint64_t, VkSampler> samplers_;
    std::unordered_map<uint64_t, Tex> textures_;
    Assembled asm_;
};

constexpr VkFormat kColorFormat = VK_FORMAT_B8G8R8A8_UNORM;  // the console's A8R8G8B8, as D3D12's B8G8R8A8

uint64_t TargetKey(uint32_t data, bool depth) { return uint64_t(data) << 1 | (depth ? 1 : 0); }

// a surface hashed whole up to 64 KB, a bigger one as 64 evenly spaced samples of 64 bytes
uint64_t MemSig(uint32_t data, uint32_t total) {
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(data));
    uint64_t sig = Hash(&total, sizeof(total));
    if (total <= 65536) return Hash(p, total, sig);
    for (uint32_t i = 0; i < 64; i++) sig = Hash(p + uint64_t(total - 64) * i / 63, 64, sig);
    return sig;
}

bool VkBackend::Init(uint32_t width, uint32_t height) {
    guest_w_ = width;
    guest_h_ = height;
    PickScale();
    const char* why = nullptr;
    if (!OpenLibrary(&why)) return Fail(why ? why : "no Vulkan loader");
    if (!CreateInstance() || !CreateSurface() || !PickDevice() || !CreateDevice() || !CreateSwapchain() ||
        !CreateFrames() || !PickDepthFormat() || !CreateDrawResources() || !InitUi()) {
        Destroy();  // nothing left holding the window, so Direct3D 12 can take it
        return false;
    }
    LOGR("vulkan: phase 1 backend: targets, clears, draws and the front buffer; copies and read-backs not yet");
    return true;
}

bool VkBackend::CreateInstance() {
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_2)
        return Fail("the Vulkan loader is " + std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
                    std::to_string(VK_API_VERSION_MINOR(version)) + ", 1.2 is needed");
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> ext(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, ext.data());
    ext.resize(n);
    for (const char* need : {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME})
        if (!HasExtension(ext, need)) return Fail(std::string("the instance has no ") + need);
    const char* names[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = app::kGame.log;
    app_info.pEngineName = "taxbox";
    app_info.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = names;
    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) return Fail("vkCreateInstance", r);
    LoadInstance(instance_);
    if (!vkCreateWin32SurfaceKHR || !vkEnumeratePhysicalDevices || !vkGetDeviceProcAddr)
        return Fail("the instance functions could not be loaded");
    return true;
}

bool VkBackend::CreateSurface() {
    VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    ci.hinstance = ::GetModuleHandleW(nullptr);
    ci.hwnd = static_cast<HWND>(app::Window());
    if (!ci.hwnd) return Fail("there is no game window");
    VkResult r = vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_);
    if (r != VK_SUCCESS) return Fail("vkCreateWin32SurfaceKHR", r);
    return true;
}

bool VkBackend::PickDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> list(n);
    if (n) vkEnumeratePhysicalDevices(instance_, &n, list.data());
    list.resize(n);
    if (list.empty()) return Fail("no Vulkan graphics card");
    int best_score = -1;
    std::string skipped;
    for (VkPhysicalDevice d : list) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        auto skip = [&](const char* why) {
            LOGR("vulkan: %s skipped: %s", p.deviceName, why);
            if (skipped.empty()) skipped = std::string(p.deviceName) + ": " + why;
        };
        if (p.apiVersion < VK_API_VERSION_1_2) {
            skip("Vulkan 1.2 is needed");
            continue;
        }
        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> ext(en);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &en, ext.data());
        ext.resize(en);
        if (!HasExtension(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            skip("no swapchain");
            continue;
        }
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qf.data());
        int family = -1;
        for (uint32_t i = 0; i < qn && family < 0; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) family = int(i);
        }
        if (family < 0) {
            skip("no queue that draws and presents to the game window");
            continue;
        }
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                              : 1;
        if (score > best_score) {
            best_score = score;
            gpu_ = d;
            family_ = uint32_t(family);
        }
    }
    if (!gpu_) return Fail(skipped.empty() ? std::string("no usable graphics card") : skipped);
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(gpu_, &p);
    LOGR("vulkan: %s, Vulkan %u.%u.%u, driver %08X", p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
         VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
    return true;
}

bool VkBackend::CreateDevice() {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(gpu_, &p);
    uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> ext(en);
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &en, ext.data());
    ext.resize(en);

    // what the card has: timeline semaphores (1.2 core) and dynamic rendering (1.3 core or the KHR extension)
    VkPhysicalDeviceDynamicRenderingFeatures dr_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    VkPhysicalDeviceVulkan12Features v12_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    bool dr_ext = HasExtension(ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    bool dr_core = p.apiVersion >= VK_API_VERSION_1_3;
    v12_have.pNext = (dr_ext || dr_core) ? &dr_have : nullptr;
    VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    have.pNext = &v12_have;
    if (vkGetPhysicalDeviceFeatures2) vkGetPhysicalDeviceFeatures2(gpu_, &have);
    // a 1.3 device on our 1.2 instance: the extension is the safe way to ask for it
    dynamic_rendering_ = dr_ext && dr_have.dynamicRendering;
    timeline_ = v12_have.timelineSemaphore;

    VkPhysicalDeviceDynamicRenderingFeatures dr_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dr_on.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceVulkan12Features v12_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    v12_on.timelineSemaphore = timeline_ ? VK_TRUE : VK_FALSE;
    v12_on.pNext = dynamic_rendering_ ? &dr_on : nullptr;
    VkPhysicalDeviceFeatures2 on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    on.pNext = &v12_on;
    anisotropy_ = have.features.samplerAnisotropy;
    fill_lines_ = have.features.fillModeNonSolid;
    on.features.samplerAnisotropy = have.features.samplerAnisotropy;
    on.features.fillModeNonSolid = have.features.fillModeNonSolid;
    ubo_align_ = std::max<VkDeviceSize>(p.limits.minUniformBufferOffsetAlignment, 16);

    std::vector<const char*> names = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (dynamic_rendering_) names.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = vkGetPhysicalDeviceFeatures2 ? &on : nullptr;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = uint32_t(names.size());
    ci.ppEnabledExtensionNames = names.data();
    VkResult r = vkCreateDevice(gpu_, &ci, nullptr, &device_);
    if (r != VK_SUCCESS) return Fail("vkCreateDevice", r);
    LoadDevice(device_);
    if (!vkCreateSwapchainKHR || !vkQueueSubmit || !vkCmdClearColorImage || !vkCmdPipelineBarrier)
        return Fail("the device functions could not be loaded");
    vkGetDeviceQueue(device_, family_, 0, &queue_);
    LOGR("vulkan: dynamic rendering %s, timeline semaphores %s", dynamic_rendering_ ? "on" : "off (render passes)",
         timeline_ ? "on" : "off");
    return true;
}

bool VkBackend::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);
    if (r != VK_SUCCESS) return Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        RECT rc{};
        ::GetClientRect(static_cast<HWND>(app::Window()), &rc);
        ext = {uint32_t(std::max<LONG>(rc.right - rc.left, 1)), uint32_t(std::max<LONG>(rc.bottom - rc.top, 1))};
        ext.width = std::clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0) {  // minimised: none until the next Present finds a size
        DestroySwapchain();
        extent_ = ext;
        return true;
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return Fail("the swapchain images can't be cleared (no transfer destination use)");

    if (swap_fmt_ == VK_FORMAT_UNDEFINED) {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(n);
        if (n) vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, fmts.data());
        fmts.resize(n);
        if (fmts.empty()) return Fail("the window has no surface formats");
        swap_fmt_ = fmts[0].format;
        for (const VkSurfaceFormatKHR& f : fmts)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                swap_fmt_ = f.format;
                break;
            }
    }

    vsync_ = app::Get().vsync;
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always there
    if (!vsync_) {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        if (n) vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, modes.data());
        modes.resize(n);
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
            mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
            mode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    uint32_t count = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
    VkSwapchainKHR old = swap_;
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = count;
    ci.imageFormat = swap_fmt_;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                          : caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
        for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
            if (caps.supportedCompositeAlpha & a) {
                ci.compositeAlpha = a;
                break;
            }
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    VkSwapchainKHR made = VK_NULL_HANDLE;
    r = vkCreateSwapchainKHR(device_, &ci, nullptr, &made);
    DestroySwapchain();  // the old one (retired by oldSwapchain either way) and its per-image semaphores
    if (r != VK_SUCCESS) return Fail("vkCreateSwapchainKHR", r);
    swap_ = made;
    extent_ = ext;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device_, swap_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(device_, swap_, &n, images_.data());
    images_.resize(n);
    rendered_.assign(n, VK_NULL_HANDLE);
    image_views_.assign(n, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swap_fmt_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        r = vkCreateImageView(device_, &vi, nullptr, &image_views_[i]);
        if (r != VK_SUCCESS) return Fail("vkCreateImageView (swapchain)", r);
    }
    if (ui_) ImGui_ImplVulkan_SetMinImageCount(std::max(caps.minImageCount, 2u));
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& s : rendered_) {
        r = vkCreateSemaphore(device_, &si, nullptr, &s);
        if (r != VK_SUCCESS) return Fail("vkCreateSemaphore", r);
    }
    LOGI("vulkan: swapchain %ux%u, %u images, %s", ext.width, ext.height, n,
         mode == VK_PRESENT_MODE_FIFO_KHR ? "v-sync" : mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "immediate");
    return true;
}

// the swapchain and its semaphores; the device must be idle
void VkBackend::DestroySwapchain() {
    for (VkSemaphore s : rendered_)
        if (s) vkDestroySemaphore(device_, s, nullptr);
    rendered_.clear();
    for (VkImageView v : image_views_)
        if (v) vkDestroyImageView(device_, v, nullptr);
    image_views_.clear();
    images_.clear();
    if (swap_) vkDestroySwapchainKHR(device_, swap_, nullptr);
    swap_ = VK_NULL_HANDLE;
}

bool VkBackend::CreateFrames() {
    for (Frame& f : frames_) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = family_;
        VkResult r = vkCreateCommandPool(device_, &pi, nullptr, &f.pool);
        if (r != VK_SUCCESS) return Fail("vkCreateCommandPool", r);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        r = vkAllocateCommandBuffers(device_, &ai, &f.cmd);
        if (r != VK_SUCCESS) return Fail("vkAllocateCommandBuffers", r);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        r = vkCreateFence(device_, &fi, nullptr, &f.done);
        if (r != VK_SUCCESS) return Fail("vkCreateFence", r);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        r = vkCreateSemaphore(device_, &si, nullptr, &f.acquired);
        if (r != VK_SUCCESS) return Fail("vkCreateSemaphore", r);
    }
    return true;
}

void VkBackend::Destroy() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        if (ui_) ImGui_ImplVulkan_Shutdown();
        ui_ = false;
        if (ui_pool_) vkDestroyDescriptorPool(device_, ui_pool_, nullptr);
        ui_pool_ = VK_NULL_HANDLE;
        for (Frame& f : frames_) {
            if (f.acquired) vkDestroySemaphore(device_, f.acquired, nullptr);
            if (f.done) vkDestroyFence(device_, f.done, nullptr);
            if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);  // frees its command buffer
            f.acquired = VK_NULL_HANDLE;
            f.done = VK_NULL_HANDLE;
            f.pool = VK_NULL_HANDLE;
            f.cmd = VK_NULL_HANDLE;
        }
        for (auto& [key, m] : modules_)
            if (m) vkDestroyShaderModule(device_, m, nullptr);
        modules_.clear();
        for (auto& [key, pl] : pipelines_)
            if (pl) vkDestroyPipeline(device_, pl, nullptr);
        pipelines_.clear();
        for (auto& [key, sm] : samplers_) vkDestroySampler(device_, sm, nullptr);
        samplers_.clear();
        for (auto& [key, tx] : textures_) DestroyImage(tx.img);
        textures_.clear();
        for (Image& im : dummy_) DestroyImage(im);
        DestroyImage(scratch_);
        if (readback_) vkDestroyBuffer(device_, readback_, nullptr);
        if (readback_mem_) vkFreeMemory(device_, readback_mem_, nullptr);
        readback_ = VK_NULL_HANDLE;
        readback_mem_ = VK_NULL_HANDLE;
        readback_cpu_ = nullptr;
        readback_size_ = 0;
        if (plain_sampler_) vkDestroySampler(device_, plain_sampler_, nullptr);
        if (compare_sampler_) vkDestroySampler(device_, compare_sampler_, nullptr);
        plain_sampler_ = compare_sampler_ = VK_NULL_HANDLE;
        if (pipe_cache_) vkDestroyPipelineCache(device_, pipe_cache_, nullptr);
        if (pipe_layout_) vkDestroyPipelineLayout(device_, pipe_layout_, nullptr);
        for (VkPipeline& cp : clear_pipes_) {
            if (cp) vkDestroyPipeline(device_, cp, nullptr);
            cp = VK_NULL_HANDLE;
        }
        if (clear_layout_) vkDestroyPipelineLayout(device_, clear_layout_, nullptr);
        clear_layout_ = VK_NULL_HANDLE;
        if (set_layout_) vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
        pipe_cache_ = VK_NULL_HANDLE;
        pipe_layout_ = VK_NULL_HANDLE;
        set_layout_ = VK_NULL_HANDLE;
        for (Frame& f : frames_) {
            for (Image& im : f.garbage) DestroyImage(im);
            f.garbage.clear();
            if (f.sets) vkDestroyDescriptorPool(device_, f.sets, nullptr);
            if (f.upload) vkDestroyBuffer(device_, f.upload, nullptr);
            if (f.upload_mem) vkFreeMemory(device_, f.upload_mem, nullptr);  // unmapped with it
            f.sets = VK_NULL_HANDLE;
            f.upload = VK_NULL_HANDLE;
            f.upload_mem = VK_NULL_HANDLE;
            f.upload_cpu = nullptr;
        }
        for (auto& [key, t] : targets_) DestroyTarget(t);
        targets_.clear();
        render_rt_ = render_ds_ = nullptr;
        rendering_ = recording_ = false;
        DestroySwapchain();
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (instance_) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    gpu_ = VK_NULL_HANDLE;
}

// D24S8 as the console's depth buffer; AMD cards lack it as an attachment, D32S8 then
bool VkBackend::PickDepthFormat() {
    vkGetPhysicalDeviceMemoryProperties(gpu_, &mem_props_);
    for (VkFormat f : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(gpu_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depth_fmt_ = f;
            LOGI("vulkan: depth buffers %s", f == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8");
            return true;
        }
    }
    return Fail("no depth and stencil format the card can draw into");
}

bool VkBackend::FindMemory(uint32_t bits, VkMemoryPropertyFlags want, uint32_t& index) const {
    for (uint32_t i = 0; i < mem_props_.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mem_props_.memoryTypes[i].propertyFlags & want) == want) {
            index = i;
            return true;
        }
    return false;
}

void VkBackend::DestroyTarget(Target& t) {
    if (t.sample_view) vkDestroyImageView(device_, t.sample_view, nullptr);
    DestroyImage(t.fb);
    if (t.view) vkDestroyImageView(device_, t.view, nullptr);
    if (t.image) vkDestroyImage(device_, t.image, nullptr);
    if (t.memory) vkFreeMemory(device_, t.memory, nullptr);
    t = Target{};
}

VkBackend::Target* VkBackend::FindTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    uint32_t gw, gh;
    SurfaceSize(s, gw, gh);
    auto it = targets_.find(TargetKey(mem::VirtualFromPhysical(s.data), depth));
    if (it == targets_.end() || it->second.gw != gw || it->second.gh != gh) return nullptr;
    return &it->second;
}

// the surface's target, made again when its size changed; new targets start cleared (black, depth 1)
VkBackend::Target* VkBackend::EnsureTarget(const Surface& s, bool depth) {
    if (!s.header || lost_) return nullptr;
    uint32_t gw, gh;
    SurfaceSize(s, gw, gh);
    uint64_t key = TargetKey(mem::VirtualFromPhysical(s.data), depth);
    auto it = targets_.find(key);
    if (it != targets_.end()) {
        if (it->second.gw == gw && it->second.gh == gh) return &it->second;
        // the GPU may still read the old one: finish what is recorded first
        Submit(true);
        DestroyTarget(it->second);
        targets_.erase(it);
    }
    Target t;
    t.depth = depth;
    t.gw = gw;
    t.gh = gh;
    t.w = std::max(1u, uint32_t(std::lround(float(gw) * scale_ * xmul_)));
    t.h = std::max(1u, uint32_t(std::lround(float(gh) * scale_)));
    t.data = mem::VirtualFromPhysical(s.data);
    t.bytes = depth ? gw * gh * 4 : GuestPitch(gw) * gh;
    t.epoch = DirtyEpoch(t.data);
    t.sig = MemSig(t.data, t.bytes);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = depth ? depth_fmt_ : kColorFormat;
    ci.extent = {t.w, t.h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
               (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device_, &ci, nullptr, &t.image);
    if (r != VK_SUCCESS) {
        LOG_FIRST(4, "vulkan: a %ux%u target could not be made (%s)", gw, gh, ResultName(r));
        return nullptr;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, t.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    if (!FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ai.memoryTypeIndex) &&
        !FindMemory(req.memoryTypeBits, 0, ai.memoryTypeIndex)) {
        DestroyTarget(t);
        return nullptr;
    }
    r = vkAllocateMemory(device_, &ai, nullptr, &t.memory);
    if (r == VK_SUCCESS) r = vkBindImageMemory(device_, t.image, t.memory, 0);
    if (r == VK_SUCCESS) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                        : VK_IMAGE_ASPECT_COLOR_BIT),
                               0, 1, 0, 1};
        r = vkCreateImageView(device_, &vi, nullptr, &t.view);
        if (r == VK_SUCCESS && depth) {
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            r = vkCreateImageView(device_, &vi, nullptr, &t.sample_view);
        }
    }
    if (r != VK_SUCCESS) {
        LOG_FIRST(4, "vulkan: a %ux%u target's memory could not be set up (%s)", gw, gh, ResultName(r));
        DestroyTarget(t);
        return nullptr;
    }
    // cleared as D3D12 clears its new targets
    VkCommandBuffer cb = Cmd();
    if (!cb) {
        DestroyTarget(t);
        return nullptr;
    }
    EndRendering();
    ToLayout(t.image, depth, t.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (depth) {
        VkClearDepthStencilValue v{1.0f, 0};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        vkCmdClearDepthStencilImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    } else {
        VkClearColorValue v{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    }
    LOG_FIRST(16, "vulkan: %s target %08X %ux%u, drawn at %ux%u", depth ? "depth" : "colour", mem::VirtualFromPhysical(s.data),
              gw, gh, t.w, t.h);
    return &(targets_[key] = t);
}

// the frame's command buffer, begun on first use (after the GPU finished the last use of this frame slot)
VkCommandBuffer VkBackend::Cmd() {
    if (lost_ || !device_) return VK_NULL_HANDLE;
    Frame& f = frames_[frame_];
    if (recording_) return f.cmd;
    VkResult r = vkWaitForFences(device_, 1, &f.done, VK_TRUE, UINT64_MAX);
    if (r == VK_ERROR_DEVICE_LOST) {
        lost_ = true;
        app::Fail(app::kExitGpuLost, L"The graphics driver stopped responding (Vulkan device lost).");
        return VK_NULL_HANDLE;
    }
    vkResetCommandPool(device_, f.pool, 0);
    // the slot's last use is done: its ring, descriptor sets and retired images are free again
    f.used = 0;
    if (f.sets) vkResetDescriptorPool(device_, f.sets, 0);
    for (Image& im : f.garbage) DestroyImage(im);
    f.garbage.clear();
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    recording_ = true;
    return f.cmd;
}

// what is recorded goes to the GPU without a present; wait: until it is done (a lock that reads results)
void VkBackend::Submit(bool wait) {
    if (!recording_ || lost_) return;
    EndRendering();
    Frame& f = frames_[frame_];
    vkEndCommandBuffer(f.cmd);
    recording_ = false;
    vkResetFences(device_, 1, &f.done);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    VkResult r = vkQueueSubmit(queue_, 1, &si, f.done);
    if (r != VK_SUCCESS) {
        lost_ = true;  // the fence was reset and never submitted: waiting on it would never end
        if (r == VK_ERROR_DEVICE_LOST)
            app::Fail(app::kExitGpuLost, L"The graphics driver stopped responding (Vulkan device lost).");
        LOG_FIRST(4, "vulkan: vkQueueSubmit failed (%s)", ResultName(r));
        return;
    }
    if (wait) vkWaitForFences(device_, 1, &f.done, VK_TRUE, UINT64_MAX);
    else frame_ = (frame_ + 1) % kFrames;
}

void VkBackend::Flush() { Submit(true); }

// a full barrier between any earlier use and the next: simple and always right; finer stages come with profiling
void VkBackend::ToLayout(VkImage image, bool depth, VkImageLayout& cur, VkImageLayout want) {
    if (cur == want) return;
    EndRendering();  // no barriers inside a rendering
    VkCommandBuffer cb = Cmd();
    if (!cb) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = cur;
    b.newLayout = want;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                   : VK_IMAGE_ASPECT_COLOR_BIT),
                          0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    cur = want;
}

bool VkBackend::BeginRendering(Target* rt, Target* ds) {
    if (!dynamic_rendering_ || !vkCmdBeginRenderingKHR) return false;  // render passes: a later step
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) ds = nullptr;
    if (!rt && !ds) return false;
    if (rendering_ && rt == render_rt_ && ds == render_ds_) return true;
    EndRendering();
    VkCommandBuffer cb = Cmd();
    if (!cb) return false;
    if (rt) ToLayout(rt->image, false, rt->layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (ds) ToLayout(ds->image, true, ds->layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    VkRenderingAttachmentInfoKHR da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    if (rt) {
        ca.imageView = rt->view;
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    if (ds) {
        da.imageView = ds->view;
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    Target* size = rt ? rt : ds;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {size->w, size->h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = rt ? 1 : 0;
    ri.pColorAttachments = rt ? &ca : nullptr;
    ri.pDepthAttachment = ds ? &da : nullptr;
    ri.pStencilAttachment = ds ? &da : nullptr;
    vkCmdBeginRenderingKHR(cb, &ri);
    rendering_ = true;
    if (rt) rt->drawn = true;
    if (ds) ds->drawn = true;
    render_rt_ = rt;
    render_ds_ = ds;
    return true;
}

void VkBackend::EndRendering() {
    if (!rendering_) return;
    vkCmdEndRenderingKHR(frames_[frame_].cmd);
    rendering_ = false;
    render_rt_ = render_ds_ = nullptr;
}

// some colour channels cleared: a full-screen triangle with that write mask, scissored to each rect
constexpr const char* kMaskedClearHlsl = R"(
[[vk::push_constant]] cbuffer P { float4 col; };
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2 - 1, 0, 1);
}
float4 ps() : SV_Target { return col; }
)";

void VkBackend::MaskedClear(const State& s, Target& t, uint32_t count, const uint8_t* rects, uint32_t color,
                            uint32_t mask) {
    mask &= 0xF;
    if (!mask || !dynamic_rendering_) return;
    if (!clear_layout_) {
        VkPushConstantRange pr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pr;
        if (vkCreatePipelineLayout(device_, &li, nullptr, &clear_layout_) != VK_SUCCESS) return;
    }
    if (!clear_pipes_[mask]) {
        VkShaderModule vs = ShaderModule(0xC1EA5000ull, kMaskedClearHlsl, spirv::Stage::Vertex, "vs");
        VkShaderModule ps = ShaderModule(0xC1EA5001ull, kMaskedClearHlsl, spirv::Stage::Pixel, "ps");
        if (!vs || !ps) return;
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "vs", nullptr};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "ps", nullptr};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = mask;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = 1;
        bs.pAttachments = &cb;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dyn;
        VkFormat color_fmt = kColorFormat;
        VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color_fmt;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.pNext = &ri;
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &bs;
        pi.pDynamicState = &dy;
        pi.layout = clear_layout_;
        if (vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &clear_pipes_[mask]) != VK_SUCCESS) {
            clear_pipes_[mask] = VK_NULL_HANDLE;
            return;
        }
    }
    std::vector<VkRect2D> rs;
    auto add = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        VkRect2D r = ScaledRect(t, x0, y0, x1, y1);
        if (r.extent.width && r.extent.height) rs.push_back(r);
    };
    if (count && rects) {
        for (uint32_t i = 0; i < count; i++) {
            int32_t r[4];
            std::memcpy(r, rects + size_t(i) * sizeof(r), sizeof(r));
            add(r[0], r[1], r[2], r[3]);
        }
    } else {
        add(int32_t(s.viewport.x), int32_t(s.viewport.y), int32_t(s.viewport.x + s.viewport.w),
            int32_t(s.viewport.y + s.viewport.h));
    }
    if (rs.empty() || !BeginRendering(&t, nullptr)) return;
    VkCommandBuffer cb = Cmd();
    float col[4] = {float((color >> 16) & 0xFF) / 255.0f, float((color >> 8) & 0xFF) / 255.0f, float(color & 0xFF) / 255.0f,
                    float(color >> 24) / 255.0f};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, clear_pipes_[mask]);
    vkCmdPushConstants(cb, clear_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(col), col);
    VkViewport v{0, 0, float(t.w), float(t.h), 0, 1};
    vkCmdSetViewport(cb, 0, 1, &v);
    for (const VkRect2D& r : rs) {
        vkCmdSetScissor(cb, 0, 1, &r);
        vkCmdDraw(cb, 3, 1, 0, 0);
    }
    LOG_FIRST(2, "vulkan: a clear of colour channels %X drawn", mask);
}

// rects: count D3DRECTs, else the viewport; colour channels other than all four are not cleared yet (logged)
void VkBackend::Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
                      uint32_t stencil) {
    Target* rt = (flags & kClearTarget) ? EnsureTarget(s.rt, false) : nullptr;
    Target* ds = (flags & (kClearZ | kClearStencil)) ? EnsureTarget(s.zs, true) : nullptr;
    if (rt && CpuWrote(*rt)) UploadTarget(*rt);  // what the CPU wrote goes in first, the clear goes over it
    if (rt && (flags & kClearTarget) != kClearTarget) {
        // vkCmdClearAttachments writes every channel: The Two Towers clears only alpha between drawing the world and
        // its lighting passes, which would wipe the picture; drawn instead with that channel mask
        MaskedClear(s, *rt, count, rects, color, (flags & kClearTarget) >> 4);
        rt = nullptr;
    }
    if (!rt && !ds) return;
    Target* size = rt ? rt : ds;
    std::vector<VkClearRect> rs;
    auto add = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        VkRect2D r = ScaledRect(*size, x0, y0, x1, y1);
        if (r.extent.width && r.extent.height) rs.push_back({r, 0, 1});
    };
    if (count && rects) {
        for (uint32_t i = 0; i < count; i++) {
            int32_t r[4];
            std::memcpy(r, rects + size_t(i) * sizeof(r), sizeof(r));
            add(r[0], r[1], r[2], r[3]);
        }
    } else {
        const Viewport& v = s.viewport;
        add(int32_t(v.x), int32_t(v.y), int32_t(v.x + v.w), int32_t(v.y + v.h));
    }
    if (rs.empty()) return;

    VkClearAttachment at[2]{};
    uint32_t n = 0;
    if (rt) {
        at[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        at[n].colorAttachment = 0;
        // D3DCOLOR ARGB to rgba
        at[n].clearValue.color.float32[0] = float((color >> 16) & 0xFF) / 255.0f;
        at[n].clearValue.color.float32[1] = float((color >> 8) & 0xFF) / 255.0f;
        at[n].clearValue.color.float32[2] = float(color & 0xFF) / 255.0f;
        at[n].clearValue.color.float32[3] = float(color >> 24) / 255.0f;
        n++;
    }
    if (ds) {
        at[n].aspectMask = VkImageAspectFlags(((flags & kClearZ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                                              ((flags & kClearStencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0));
        at[n].clearValue.depthStencil = {std::clamp(z, 0.0f, 1.0f), stencil & 0xFF};
        n++;
    }
    // the clear goes into the targets bound as they are: a depth buffer of another size is cleared on its own
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) {
        if (BeginRendering(rt, nullptr)) vkCmdClearAttachments(Cmd(), 1, &at[0], uint32_t(rs.size()), rs.data());
        rt = nullptr;
        size = ds;
        at[0] = at[1];
        n = 1;
        rs.clear();
        add(0, 0, int32_t(ds->gw), int32_t(ds->gh));
    }
    if (BeginRendering(rt, ds)) {
        vkCmdClearAttachments(Cmd(), n, at, uint32_t(rs.size()), rs.data());
        return;
    }
    LOG_FIRST(1, "vulkan: clears need dynamic rendering (render passes are a later step)");
}

// ---- phase 1b: draws

// D3DBLEND (GL values) as a Vulkan factor; alpha: for the alpha channel, where colour factors read alpha
VkBlendFactor VkBlend(uint32_t v, bool alpha) {
    switch (v) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 0x300: return alpha ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_SRC_COLOR;
        case 0x301: return alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x306: return alpha ? VK_BLEND_FACTOR_DST_ALPHA : VK_BLEND_FACTOR_DST_COLOR;
        case 0x307: return alpha ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 0x308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 0x8001: return alpha ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 0x8002: return alpha ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        // the constant colour's alpha in every channel: Vulkan has the factor D3D12 lacks
        case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp VkBlendOpOf(uint32_t v) {
    switch (v) {
        case 0x800A: return VK_BLEND_OP_SUBTRACT;
        case 0x800B: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 0x8007: return VK_BLEND_OP_MIN;
        case 0x8008: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

// D3DCMP_NEVER 0x200 .. ALWAYS 0x207: the same order as VkCompareOp
VkCompareOp VkCmp(uint32_t v) { return VkCompareOp(v >= 0x200 && v <= 0x207 ? v - 0x200 : 7); }

VkStencilOp VkStencil(uint32_t v) {
    switch (v) {
        case 0: return VK_STENCIL_OP_ZERO;
        case 0x1E01: return VK_STENCIL_OP_REPLACE;
        case 0x1E02: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case 0x1E03: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case 0x150A: return VK_STENCIL_OP_INVERT;
        case 0x8507: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case 0x8508: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default: return VK_STENCIL_OP_KEEP;
    }
}

// D3DTADDRESS: 1 wrap, 2 mirror, 3 clamp, 4 border, 5 clamp to edge
VkSamplerAddressMode VkAddress(uint32_t v) {
    switch (v) {
        case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case 3: case 5: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 4: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

// the DXGI formats vertex.cpp hands its elements over in (numbers, as DXGI defines them)
VkFormat VkVertexFormat(uint32_t dxgi) {
    switch (dxgi) {
        case 2: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case 6: return VK_FORMAT_R32G32B32_SFLOAT;
        case 13: return VK_FORMAT_R16G16B16A16_SNORM;
        case 14: return VK_FORMAT_R16G16B16A16_SINT;
        case 16: return VK_FORMAT_R32G32_SFLOAT;
        case 28: return VK_FORMAT_R8G8B8A8_UNORM;
        case 37: return VK_FORMAT_R16G16_SNORM;
        case 38: return VK_FORMAT_R16G16_SINT;
        case 41: return VK_FORMAT_R32_SFLOAT;
        case 42: return VK_FORMAT_R32_UINT;
        case 49: return VK_FORMAT_R8G8_UNORM;
        case 58: return VK_FORMAT_R16_SNORM;
        case 59: return VK_FORMAT_R16_SINT;
        case 61: return VK_FORMAT_R8_UNORM;
        case 87: return VK_FORMAT_B8G8R8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkFormat VkTextureFormat(Host h) {
    switch (h) {
        case Host::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case Host::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
        case Host::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case Host::R32F: return VK_FORMAT_R32_SFLOAT;
        case Host::R16Unorm: return VK_FORMAT_R16_UNORM;
        default: return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

constexpr uint32_t kFillWireframe = 0x1B01;  // D3DFILL_WIREFRAME
constexpr uint32_t kCullCw = 0x900;          // D3DCULL_CW
constexpr float kDepth24Max = 16777215.0f;
constexpr uint64_t kPixelKeySalt = 0x9E3779B97F4A7C15ull;


// Upload's ring: a draw's constants, vertices, indices and texture levels, host visible, one per frame in flight
bool VkBackend::CreateDrawResources() {
    for (Frame& f : frames_) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = kUploadSize;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult r = vkCreateBuffer(device_, &bi, nullptr, &f.upload);
        if (r != VK_SUCCESS) return Fail("vkCreateBuffer (upload ring)", r);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, f.upload, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        if (!FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        ai.memoryTypeIndex))
            return Fail("no host visible memory for the upload ring");
        r = vkAllocateMemory(device_, &ai, nullptr, &f.upload_mem);
        if (r != VK_SUCCESS) return Fail("vkAllocateMemory (upload ring)", r);
        vkBindBufferMemory(device_, f.upload, f.upload_mem, 0);
        void* p = nullptr;
        r = vkMapMemory(device_, f.upload_mem, 0, VK_WHOLE_SIZE, 0, &p);
        if (r != VK_SUCCESS) return Fail("vkMapMemory (upload ring)", r);
        f.upload_cpu = static_cast<uint8_t*>(p);

        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSetsPerFrame * 2},
                                        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerFrame * 4},
                                        {VK_DESCRIPTOR_TYPE_SAMPLER, kSetsPerFrame * 4}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = kSetsPerFrame;
        pi.poolSizeCount = 3;
        pi.pPoolSizes = sizes;
        r = vkCreateDescriptorPool(device_, &pi, nullptr, &f.sets);
        if (r != VK_SUCCESS) return Fail("vkCreateDescriptorPool", r);
    }

    // set 0: b0 b1 (uniform buffers), t0..t3 at 8..11, s0..s3 at 16..19, as gpu/spirv.cpp maps them
    VkDescriptorSetLayoutBinding b[10]{};
    for (uint32_t i = 0; i < 2; i++) b[i] = {i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr};
    for (uint32_t i = 0; i < 4; i++) {
        b[2 + i] = {8 + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        b[6 + i] = {16 + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 10;
    li.pBindings = b;
    VkResult r = vkCreateDescriptorSetLayout(device_, &li, nullptr, &set_layout_);
    if (r != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout", r);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &set_layout_;
    r = vkCreatePipelineLayout(device_, &pl, nullptr, &pipe_layout_);
    if (r != VK_SUCCESS) return Fail("vkCreatePipelineLayout", r);
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    vkCreatePipelineCache(device_, &ci, nullptr, &pipe_cache_);

    // what a stage without a texture reads: 1x1 black of each kind, and a depth texel for a shadow stage
    if (!CreateDummy(dummy_[0], VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, kColorFormat, 1) ||
        !CreateDummy(dummy_[1], VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE, kColorFormat, 6) ||
        !CreateDummy(dummy_[2], VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D, kColorFormat, 1) ||
        !CreateDummy(dummy_[3], VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, depth_fmt_, 1))
        return Fail("the empty stage textures could not be made");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.maxLod = VK_LOD_CLAMP_NONE;
    r = vkCreateSampler(device_, &si, nullptr, &plain_sampler_);
    if (r != VK_SUCCESS) return Fail("vkCreateSampler", r);
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    r = vkCreateSampler(device_, &si, nullptr, &compare_sampler_);
    if (r != VK_SUCCESS) return Fail("vkCreateSampler", r);
    return true;
}

bool VkBackend::CreateDummy(Image& im, VkImageType type, VkImageViewType view, VkFormat fmt, uint32_t layers) {
    bool depth = fmt == depth_fmt_;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.flags = view == VK_IMAGE_VIEW_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.imageType = type;
    ci.format = fmt;
    ci.extent = {1, 1, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = layers;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!MakeImage(im, ci, view, depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT)) return false;
    VkCommandBuffer cb = Cmd();
    if (!cb) return false;
    VkImageSubresourceRange range{VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                           : VK_IMAGE_ASPECT_COLOR_BIT),
                                  0, 1, 0, layers};
    Barrier(im.image, range, im.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (depth) {
        VkClearDepthStencilValue v{1.0f, 0};
        vkCmdClearDepthStencilImage(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    } else {
        VkClearColorValue v{};
        vkCmdClearColorImage(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    }
    Barrier(im.image, range, im.layout,
            depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

// an image with its own memory and one view; view_aspect: what the view reads
bool VkBackend::MakeImage(Image& im, const VkImageCreateInfo& ci, VkImageViewType view, VkImageAspectFlags view_aspect) {
    VkResult r = vkCreateImage(device_, &ci, nullptr, &im.image);
    if (r != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, im.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    if (!FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ai.memoryTypeIndex) &&
        !FindMemory(req.memoryTypeBits, 0, ai.memoryTypeIndex)) {
        DestroyImage(im);
        return false;
    }
    r = vkAllocateMemory(device_, &ai, nullptr, &im.memory);
    if (r == VK_SUCCESS) r = vkBindImageMemory(device_, im.image, im.memory, 0);
    if (r == VK_SUCCESS) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = im.image;
        vi.viewType = view;
        vi.format = ci.format;
        vi.subresourceRange = {view_aspect, 0, ci.mipLevels, 0, ci.arrayLayers};
        r = vkCreateImageView(device_, &vi, nullptr, &im.view);
    }
    if (r != VK_SUCCESS) {
        DestroyImage(im);
        return false;
    }
    im.levels = ci.mipLevels;
    im.layers = ci.arrayLayers;
    im.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void VkBackend::DestroyImage(Image& im) {
    if (im.view) vkDestroyImageView(device_, im.view, nullptr);
    if (im.image) vkDestroyImage(device_, im.image, nullptr);
    if (im.memory) vkFreeMemory(device_, im.memory, nullptr);
    im = Image{};
}

// an image destroyed once the frames in flight are done with it
void VkBackend::Retire(Image& im) {
    if (im.image) frames_[frame_].garbage.push_back(im);
    im = Image{};
}

void VkBackend::Barrier(VkImage image, const VkImageSubresourceRange& range, VkImageLayout& cur, VkImageLayout want) {
    EndRendering();
    VkCommandBuffer cb = Cmd();
    if (!cb || cur == want) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = cur;
    b.newLayout = want;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = range;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    cur = want;
}

// size bytes in this frame's upload ring; may_flush: when it is full, the recorded work is finished first and the ring
// starts over (only safe before anything of the current draw sits in the ring)
bool VkBackend::Upload(VkDeviceSize size, VkDeviceSize align, bool may_flush, VkDeviceSize& off, uint8_t*& cpu) {
    if (!Cmd()) return false;
    VkDeviceSize at = (frames_[frame_].used + align - 1) & ~(align - 1);
    if (at + size > kUploadSize) {
        if (!may_flush || size > kUploadSize) return false;
        Submit(true);
        if (!Cmd()) return false;
        at = 0;
    }
    Frame& f = frames_[frame_];
    off = at;
    cpu = f.upload_cpu + at;
    f.used = at + size;
    return true;
}

VkShaderModule VkBackend::ShaderModule(uint64_t key, const std::string& hlsl, spirv::Stage stage, const char* entry) {
    auto it = modules_.find(key);
    if (it != modules_.end()) return it->second;
    std::vector<uint32_t> code;
    std::string error;
    VkShaderModule m = VK_NULL_HANDLE;
    if (!spirv::Compile(hlsl, entry, stage, code, error)) {
        LOG_FIRST(8, "vulkan: a %s shader did not compile: %s", stage == spirv::Stage::Vertex ? "vertex" : "pixel",
                  error.c_str());
    } else {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size() * 4;
        ci.pCode = code.data();
        if (vkCreateShaderModule(device_, &ci, nullptr, &m) != VK_SUCCESS) m = VK_NULL_HANDLE;
    }
    modules_[key] = m;  // a failure is remembered, not compiled again every draw
    return m;
}

// a stage's texture: a target the GPU drew (a copy when the draw draws into it), else the guest texture uploaded
void VkBackend::BindTexture(const State& s, int i, Target* rt, Target* ds, StageBind& sb, TexInfo& info) {
    info = {};
    sb = {};
    TextureDesc d;
    if (!s.textures[size_t(i)] || !Describe(s.textures[size_t(i)], d)) return;
    const FormatInfo& fi = Format(d.format);
    info.kind = TextureKind(d);
    info.rect = d.linear;
    sb.width = d.width;
    sb.height = d.height;
    auto tit = targets_.find(TargetKey(d.data, fi.depth));
    if (tit != targets_.end() && tit->second.gw == d.width && tit->second.gh == d.height && !d.cube && d.depth == 1 &&
        !fi.compressed) {
        Target& t = tit->second;
        if (&t == ds) return;  // the depth buffer being drawn with: not read, as D3D12 does
        info.kind = kTex2D;
        sb.target = true;
        if (t.depth) {
            info.shadow = (d.format == 0x2C || d.format == 0x30) ? 2 : 1;  // D16, LIN_D16
            ToLayout(t.image, true, t.layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            sb.view = t.sample_view;
            sb.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            return;
        }
        if (&t == rt) {
            // the console reads the pixels it is about to overwrite: the draw reads a copy
            if (!t.fb.image) {
                VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                ci.imageType = VK_IMAGE_TYPE_2D;
                ci.format = kColorFormat;
                ci.extent = {t.w, t.h, 1};
                ci.mipLevels = 1;
                ci.arrayLayers = 1;
                ci.samples = VK_SAMPLE_COUNT_1_BIT;
                ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                if (!MakeImage(t.fb, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) return;
            }
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ToLayout(t.image, false, t.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            Barrier(t.fb.image, range, t.fb.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy c{};
            c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.extent = {t.w, t.h, 1};
            vkCmdCopyImage(Cmd(), t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.fb.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            Barrier(t.fb.image, range, t.fb.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            sb.view = t.fb.view;
            sb.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            return;
        }
        ToLayout(t.image, false, t.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sb.view = t.view;
        sb.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return;
    }
    if (fi.depth) info.shadow = (d.format == 0x2C || d.format == 0x30) ? 2 : 1;
    if (fi.host == Host::None) return;
    sb.view = GuestTexture(s, i, d, fi);
    sb.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // no view: the stage reads the empty texture of its kind (Draw)
}

// a texture in guest memory: made again when its layout changed, uploaded again when its bytes did
VkImageView VkBackend::GuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi) {
    // a P8 texture's colours
    uint32_t colours[256] = {};
    uint32_t pal_va = 0;
    uint64_t pal_hash = 0;
    if (d.format == 0x0B && s.palettes[size_t(stage)]) {
        uint32_t header = s.palettes[size_t(stage)];
        pal_va = mem::VirtualFromPhysical(kernel::R32(header + 4));
        size_t bytes = (256u >> (kernel::R32(header) >> 30)) * sizeof(uint32_t);
        std::memcpy(colours, kernel::HostPtr(pal_va), bytes);
        pal_hash = Hash(colours, sizeof(colours));
    }
    uint64_t key = uint64_t(pal_va) << 32 | d.data;
    Tex& tx = textures_[key];
    bool same = tx.img.image && tx.d.format == d.format && tx.d.width == d.width && tx.d.height == d.height &&
                tx.d.depth == d.depth && tx.d.levels == d.levels && tx.d.cube == d.cube && tx.d.pitch == d.pitch;
    if (!same) {
        Retire(tx.img);
        tx = Tex{};
        tx.d = d;
        bool bc = fi.compressed;
        uint32_t w = d.width, h = d.height;
        tx.levels = d.levels;
        if (bc && ((w & 3) || (h & 3))) {
            w = (w + 3) & ~3u;
            h = (h + 3) & ~3u;
            tx.levels = 1;
        }
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.flags = d.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        ci.imageType = d.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        ci.format = VkTextureFormat(fi.host);
        ci.extent = {w, h, d.depth};
        ci.mipLevels = tx.levels;
        ci.arrayLayers = d.cube ? 6 : 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VkImageViewType vt = d.cube ? VK_IMAGE_VIEW_TYPE_CUBE : d.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
        if (!MakeImage(tx.img, ci, vt, VK_IMAGE_ASPECT_COLOR_BIT)) {
            LOG_FIRST(8, "vulkan: a %ux%u texture (format %X) could not be made", w, h, d.format);
            textures_.erase(key);
            return VK_NULL_HANDLE;
        }
        tx.host_w = w;
        tx.host_h = h;
    }
    // change check once per frame: lock epochs plus the bytes (fonts the CPU draws into, hashed whole)
    if (tx.frame != presents_ + 1 || !same || tx.palette != pal_hash) {
        tx.frame = presents_ + 1;
        tx.palette = pal_hash;
        uint32_t total = TotalSize(d);
        const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
        bool font_cache = (d.format == 0x19 || d.format == 0x1F) && total <= (1u << 20);
        uint64_t sig = (font_cache ? Hash(p, total, total) : MemSig(d.data, total)) ^ DirtyEpoch(d.data) ^ pal_hash;
        if (sig != tx.sig || !same) {
            tx.sig = sig;
            UploadTexture(tx, p, pal_va ? colours : nullptr);
        }
    }
    return tx.img.view;
}

void VkBackend::UploadTexture(Tex& tx, const uint8_t* p, const uint32_t* colours) {
    const TextureDesc& d = tx.d;
    const FormatInfo& fi = Format(d.format);
    VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, tx.levels, 0, d.cube ? 6u : 1u};
    Barrier(tx.img.image, all, tx.img.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    std::vector<uint8_t> px;
    for (uint32_t f = 0; f < (d.cube ? 6u : 1u); f++) {
        for (uint32_t l = 0; l < tx.levels; l++) {
            uint32_t pitch = 0;
            DecodeLevel(d, l, f, p, colours, px, pitch);
            uint32_t lw = std::max(tx.host_w >> l, 1u), lh = std::max(tx.host_h >> l, 1u);
            uint32_t ld = std::max(d.depth >> l, 1u);
            // the copy's rows as Vulkan reads them: whole blocks for BC, texels otherwise
            uint32_t rows = fi.compressed ? (lh + 3) / 4 : lh;
            uint32_t row_bytes = fi.compressed ? ((lw + 3) / 4) * (fi.host == Host::BC1 ? 8u : 16u)
                                               : lw * (fi.host == Host::R16Unorm ? 2u : 4u);
            VkDeviceSize bytes = VkDeviceSize(row_bytes) * rows * ld;
            VkDeviceSize off;
            uint8_t* cpu;
            if (!Upload(bytes, 16, true, off, cpu)) {
                LOG_FIRST(4, "vulkan: a texture level of %llu bytes did not fit the upload ring",
                          (unsigned long long)bytes);
                return;
            }
            size_t src_rows = pitch ? px.size() / pitch / ld : 0;
            for (uint32_t z = 0; z < ld; z++)
                for (uint32_t y = 0; y < rows; y++) {
                    uint8_t* dst = cpu + (size_t(z) * rows + y) * row_bytes;
                    if (y < src_rows) std::memcpy(dst, &px[(size_t(z) * src_rows + y) * pitch], std::min(pitch, row_bytes));
                    else std::memset(dst, 0, row_bytes);
                }
            VkBufferImageCopy c{};
            c.bufferOffset = off;
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, f, 1};
            c.imageExtent = {lw, lh, ld};
            // the upload may have finished the frame and begun another: barriers again in the new command buffer
            Barrier(tx.img.image, all, tx.img.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, tx.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &c);
        }
    }
    Barrier(tx.img.image, all, tx.img.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// D3DTEXF: 1 point, 2 linear, 3 anisotropic
VkSampler VkBackend::StageSampler(const State& s, int i, const TexInfo& info, bool target) {
    const auto& t = s.tss[size_t(i)];
    auto lin = [](uint32_t f) { return f >= 2; };
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = lin(t[kTssMagFilter]) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.minFilter = lin(t[kTssMinFilter]) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    uint32_t mip = t[kTssMipFilter];
    si.mipmapMode = mip == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VkAddress(t[kTssAddressU]);
    si.addressModeV = VkAddress(t[kTssAddressV]);
    si.addressModeW = VkAddress(t[kTssAddressW]);
    si.mipLodBias = Bits(t[kTssMipLodBias]) + (target ? 0.0f : app::Get().lod_bias);
    si.minLod = float(t[kTssMaxMipLevel]);
    si.maxLod = mip ? VK_LOD_CLAMP_NONE : si.minLod;
    // Vulkan's border is one of three colours: the nearest of transparent black, opaque black, opaque white
    uint32_t bc = t[kTssBorderColor];
    si.borderColor = (bc >> 24) < 0x80 ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
                     : (bc & 0xFFFFFF) >= 0x808080 ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                                   : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    bool anisotropic = t[kTssMinFilter] == 3 || t[kTssMagFilter] == 3;
    uint32_t an = anisotropic ? std::max<uint32_t>(t[kTssMaxAnisotropy], 1) : 1;
    if (app::Get().aniso && lin(t[kTssMinFilter]) && mip && !info.shadow && !target) an = std::max(an, app::Get().aniso);
    if (an > 1 && anisotropy_) {
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = float(std::min(an, 16u));
    }
    if (info.shadow) {
        // the console tests texel OP r, Vulkan tests r OP texel: the order of the comparison swapped
        static const uint8_t swap[8] = {0, 4, 2, 6, 1, 5, 3, 7};
        si.compareEnable = VK_TRUE;
        si.compareOp = VkCompareOp(swap[VkCmp(s.rs[kRsShadowFunc])]);
        si.anisotropyEnable = VK_FALSE;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.maxLod = 0;
    }
    uint64_t key = Hash(&si, sizeof(si));
    auto it = samplers_.find(key);
    if (it != samplers_.end()) return it->second;
    VkSampler smp = VK_NULL_HANDLE;
    if (samplers_.size() >= 3000 || vkCreateSampler(device_, &si, nullptr, &smp) != VK_SUCCESS) {
        LOG_FIRST(2, "vulkan: sampler limit reached, a plain sampler is used");
        return info.shadow ? compare_sampler_ : plain_sampler_;
    }
    samplers_[key] = smp;
    return smp;
}

VkPipeline VkBackend::Pipeline(const PipeKey& k) {
    uint64_t key = Hash(&k, sizeof(k));
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) return it->second;

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, k.vs, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, k.ps, "main", nullptr};

    VkVertexInputBindingDescription vb[16]{};
    for (uint32_t b = 0; b < k.buffers; b++) vb[b] = {b, k.strides[b], VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[16]{};
    uint32_t n = 0;
    for (uint32_t r = 0; r < 16; r++) {
        if (!((k.regs >> r) & 1)) continue;
        uint32_t e = k.elements[r];
        // locations in register order: glslang numbers the VIn (VRaw) members as declared, ascending registers
        va[n] = {n, (e >> 8) & 0xFF, VkVertexFormat(e & 0xFF), e >> 16};
        n++;
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = k.buffers;
    vi.pVertexBindingDescriptions = vb;
    vi.vertexAttributeDescriptionCount = n;
    vi.pVertexAttributeDescriptions = va;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = k.topo == 1 ? (k.strip ? VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
                              : (k.strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = k.fill == kFillWireframe && fill_lines_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = k.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    // drawn through a viewport of negative height the picture stands as on D3D12, and so do the windings
    rs.frontFace = k.ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = (k.zbias || k.zslope) ? VK_TRUE : VK_FALSE;
    rs.depthBiasConstantFactor = float(k.zbias);
    rs.depthBiasSlopeFactor = k.zslope;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkStencilOpState so{VkStencil(k.sfail), VkStencil(k.spass), VkStencil(k.szfail), VkCmp(k.sfunc), k.smask, k.swmask, 0};
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = k.zen;
    ds.depthWriteEnable = k.zen && k.zwrite;
    ds.depthCompareOp = k.zen ? VkCmp(k.zfunc) : VK_COMPARE_OP_ALWAYS;
    ds.stencilTestEnable = k.sten;
    ds.front = ds.back = so;

    VkPipelineColorBlendAttachmentState cb{};
    cb.blendEnable = k.blend;
    cb.srcColorBlendFactor = VkBlend(k.src, false);
    cb.dstColorBlendFactor = VkBlend(k.dst, false);
    cb.colorBlendOp = VkBlendOpOf(k.op);
    cb.srcAlphaBlendFactor = VkBlend(k.src, true);
    cb.dstAlphaBlendFactor = VkBlend(k.dst, true);
    cb.alphaBlendOp = VkBlendOpOf(k.op);
    cb.colorWriteMask = k.wmask;  // R G B A bits, as D3D12's
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = k.has_rt ? 1 : 0;
    bs.pAttachments = &cb;

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                            VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 4;
    dy.pDynamicStates = dyn;

    VkFormat color = kColorFormat;
    VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ri.colorAttachmentCount = k.has_rt ? 1 : 0;
    ri.pColorAttachmentFormats = &color;
    ri.depthAttachmentFormat = k.has_ds ? depth_fmt_ : VK_FORMAT_UNDEFINED;
    ri.stencilAttachmentFormat = k.has_ds ? depth_fmt_ : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext = &ri;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &bs;
    pi.pDynamicState = &dy;
    pi.layout = pipe_layout_;
    VkPipeline p = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &p);
    if (r != VK_SUCCESS) {
        LOG_FIRST(8, "vulkan: a pipeline could not be made (%s)", ResultName(r));
        p = VK_NULL_HANDLE;
    }
    pipelines_[key] = p;
    return p;
}

void VkBackend::Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) {
    if (lost_ || !device_) return;
    if (!dynamic_rendering_) {
        LOG_FIRST(1, "vulkan: draws need dynamic rendering (render passes are a later step)");
        return;
    }
    if (g_draw_observer) g_draw_observer(s);
    Assembled& a = asm_;
    if (!Assemble(s, prim, start, count, indices, a)) return;
    if (a.topology == Topology::Points) {
        LOG_FIRST(1, "vulkan: point draws are not done yet");
        return;
    }
    // room for the draw's constants and vertices: a full ring is finished and started over first
    if (Cmd() && frames_[frame_].used > kUploadSize - kUploadReserve) Submit(true);

    Target* rt = EnsureTarget(s.rt, false);
    Target* ds = s.rs[kRsZEnable] || s.rs[kRsStencilEnable] ? EnsureTarget(s.zs, true) : nullptr;
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) ds = nullptr;
    if (!rt && !ds) return;
    if (rt && CpuWrote(*rt)) UploadTarget(*rt);

    // vertex shader
    ShaderChoice sh = ClassifyShaders(s, a, count);
    if (sh.prog) EnsureProgramHash(s);
    sh.vkey = VertexKey(s, a, sh);
    VkShaderModule vs = VK_NULL_HANDLE;
    {
        auto it = modules_.find(sh.vkey);
        if (it != modules_.end()) {
            vs = it->second;
        } else {
            std::string src = VertexHlsl(s, a, sh);
            if (sh.quad) src = QuadHlsl(std::move(src));
            vs = ShaderModule(sh.vkey, src, spirv::Stage::Vertex);
        }
    }

    // stage textures (uploads and copies end the rendering), then the pixel shader made for them
    StageBind sb[4];
    TexInfo info[4];
    for (int i = 0; i < 4; i++) BindTexture(s, i, rt, ds, sb[i], info[i]);
    PixelKey pk = MakePixelKey(s, info);
    pk.quad = sh.quad;
    uint64_t pkey = Hash(&pk, sizeof(pk)) ^ kPixelKeySalt;
    VkShaderModule ps = VK_NULL_HANDLE;
    {
        auto it = modules_.find(pkey);
        ps = it != modules_.end() ? it->second : ShaderModule(pkey, PixelHlsl(pk), spirv::Stage::Pixel);
    }
    if (!vs || !ps) return;

    // pipeline
    PipeKey k{};
    k.vs = vs;
    k.ps = ps;
    k.regs = a.regs;
    for (int r = 0; r < 16; r++)
        if ((a.regs >> r) & 1) {
            const VertexElement& e = a.elements[size_t(r)];
            k.elements[r] = e.format | uint32_t(e.buffer) << 8 | uint32_t(e.offset) << 16;
            if (VkVertexFormat(e.format) == VK_FORMAT_UNDEFINED) {
                LOG_FIRST(4, "vulkan: vertex format %u has no Vulkan format yet", e.format);
                return;
            }
        }
    k.buffers = a.buffer_count;
    for (uint32_t b = 0; b < a.buffer_count; b++) k.strides[b] = a.buffers[b].stride;
    k.topo = uint32_t(a.topology);
    k.strip = a.strip;
    k.has_rt = rt != nullptr;
    k.has_ds = ds != nullptr;
    k.blend = SimpleState(s, kMBlendEnable) != 0;
    if (k.blend) {
        k.src = SimpleState(s, kMSrcBlend);
        k.dst = SimpleState(s, kMDestBlend);
        k.op = SimpleState(s, kMBlendOp);
    }
    uint32_t cw = SimpleState(s, kMColorWriteEnable);
    k.wmask = ((cw >> 16) & 1) | ((cw >> 8) & 1) << 1 | (cw & 1) << 2 | ((cw >> 24) & 1) << 3;
    if (ds) {
        k.zen = s.rs[kRsZEnable] != 0;
        if (k.zen) {
            k.zwrite = SimpleState(s, kMZWriteEnable) != 0;
            k.zfunc = SimpleState(s, kMZFunc);
        }
        k.sten = s.rs[kRsStencilEnable] != 0;
        if (k.sten) {
            k.sfunc = SimpleState(s, kMStencilFunc);
            k.smask = SimpleState(s, kMStencilMask) & 0xFF;
            k.swmask = SimpleState(s, kMStencilWriteMask) & 0xFF;
            k.sfail = s.rs[kRsStencilFail];
            k.szfail = SimpleState(s, kMStencilZFail);
            k.spass = SimpleState(s, kMStencilPass);
        }
        if (a.topology == Topology::Triangles && SimpleState(s, kMSolidOffsetEnable)) {
            k.zbias = int32_t(std::lround(Bits(SimpleState(s, kMDepthOffset)) * kDepth24Max / s.zscale));
            // the slope is per console pixel, and an upscaled target's pixels are smaller
            k.zslope = Bits(SimpleState(s, kMSlopeScale)) * float(ds->w) / float(ds->gw);
        }
    }
    uint32_t cull = s.rs[kRsCullMode];
    k.cull = cull != 0 && a.topology == Topology::Triangles;
    k.ccw = cull == kCullCw;
    k.fill = s.rs[kRsFillMode];
    VkPipeline pipe = Pipeline(k);
    if (!pipe) return;

    // constants: b0 the vertex registers, b1 DrawConstants
    uint32_t gw, gh;
    SurfaceSize(rt ? s.rt : s.zs, gw, gh);
    VkDeviceSize vc_off, dc_off;
    uint8_t *vc_cpu, *dc_cpu;
    if (!Upload(sizeof(s.vsc), ubo_align_, false, vc_off, vc_cpu) ||
        !Upload(sizeof(DrawConstants), ubo_align_, false, dc_off, dc_cpu)) {
        LOG_FIRST(2, "vulkan: a draw's constants did not fit the upload ring");
        return;
    }
    std::memcpy(vc_cpu, s.vsc.data(), sizeof(s.vsc));
    DrawConstants c{};
    c.surf[0] = float(gw);
    c.surf[1] = float(gh);
    c.surf[2] = 1.0f / s.zscale;
    Color(s.rs[kRsFogColor], c.fog_color);
    c.misc[0] = float(SimpleState(s, kMAlphaRef) & 0xFF);
    c.misc[1] = SimpleState(s, kMAlphaTestEnable) ? float(VkCmp(SimpleState(s, kMAlphaFunc))) : 7.0f;
    c.misc[2] = float(VkCmp(s.rs[kRsShadowFunc]));
    c.misc[3] = s.rs[kRsSpecularEnable] ? 1.0f : 0.0f;
    FillFogConstants(s, sh.prog, c);
    FillCombinerConstants(s, c);
    c.widen[0] = sh.prog ? g_program_widen.load() : 0.0f;
    for (int i = 0; i < 4; i++) {
        if (s.textures[size_t(i)]) {
            c.texsize[i][0] = float(sb[i].width);
            c.texsize[i][1] = float(sb[i].height);
            // the console samples pixels at integer positions: targets are read half a texel back, as on D3D12
            if (sb[i].target && !(info[i].flags & kTexExact)) {
                c.texsize[i][2] = -0.5f / float(sb[i].width);
                c.texsize[i][3] = -0.5f / float(sb[i].height);
            }
        }
        const auto& t = s.tss[size_t(i)];
        c.bump[i][0] = Bits(t[kTssBumpEnvMat00]);
        c.bump[i][1] = Bits(t[kTssBumpEnvMat01]);
        c.bump[i][2] = Bits(t[kTssBumpEnvMat10]);
        c.bump[i][3] = Bits(t[kTssBumpEnvMat11]);
        c.bumpl[i][0] = Bits(t[kTssBumpEnvLScale]);
        c.bumpl[i][1] = Bits(t[kTssBumpEnvLOffset]);
    }
    if (sh.fixed_transform) {
        float pa = 1.0f, pb = 0.0f;
        if (g_ortho_place) {
            float x0, x1, y0, y1;
            if (OrthoExtent(s, start, count, indices, x0, x1, y0, y1) && !g_ortho_place(s, x0, x1, y0, y1, pa, pb)) {
                pa = 1.0f;
                pb = 0.0f;
            }
        }
        FillTransformConstants(s, {gw, gh}, c, pa, pb);
        FillLightConstants(s, sh.fk, c);
    }
    std::memcpy(dc_cpu, &c, sizeof(c));

    // geometry into the ring
    VkDeviceSize vb_off[16];
    for (uint32_t b = 0; b < a.buffer_count; b++) {
        uint8_t* cpu;
        if (!Upload(a.buffers[b].bytes, 16, false, vb_off[b], cpu)) {
            LOG_FIRST(2, "vulkan: a draw's vertices did not fit the upload ring");
            return;
        }
        std::memcpy(cpu, a.buffers[b].src, a.buffers[b].bytes);
    }
    VkDeviceSize ib_off = 0;
    if (a.index_count) {
        uint32_t ibytes = a.index_count * (a.index16 ? 2 : 4);
        uint8_t* cpu;
        if (!Upload(ibytes, 4, false, ib_off, cpu)) {
            LOG_FIRST(2, "vulkan: a draw's indices did not fit the upload ring");
            return;
        }
        std::memcpy(cpu, a.index_src, ibytes);
    }

    // descriptors
    Frame& f = frames_[frame_];
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = f.sets;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &dai, &set) != VK_SUCCESS) {
        LOG_FIRST(2, "vulkan: the frame ran out of descriptor sets (more than %u draws)", kSetsPerFrame);
        return;
    }
    VkDescriptorBufferInfo bufs[2] = {{f.upload, vc_off, sizeof(s.vsc)}, {f.upload, dc_off, sizeof(DrawConstants)}};
    VkDescriptorImageInfo imgs[4], smps[4];
    for (int i = 0; i < 4; i++) {
        VkImageView view = sb[i].view;
        VkImageLayout layout = sb[i].layout;
        if (!view) {
            const Image& dm = info[i].shadow ? dummy_[3] : info[i].kind == kTexCube ? dummy_[1]
                                                         : info[i].kind == kTex3D   ? dummy_[2]
                                                                                    : dummy_[0];
            view = dm.view;
            layout = dm.layout;
        }
        imgs[i] = {VK_NULL_HANDLE, view, layout};
        smps[i] = {StageSampler(s, i, info[i], sb[i].target), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    VkWriteDescriptorSet w[10]{};
    for (uint32_t i = 0; i < 2; i++) {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[i].pBufferInfo = &bufs[i];
    }
    for (uint32_t i = 0; i < 4; i++) {
        w[2 + i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[2 + i].dstSet = set;
        w[2 + i].dstBinding = 8 + i;
        w[2 + i].descriptorCount = 1;
        w[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w[2 + i].pImageInfo = &imgs[i];
        w[6 + i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[6 + i].dstSet = set;
        w[6 + i].dstBinding = 16 + i;
        w[6 + i].descriptorCount = 1;
        w[6 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        w[6 + i].pImageInfo = &smps[i];
    }
    vkUpdateDescriptorSets(device_, 10, w, 0, nullptr);

    // record: the targets, then the draw
    if (!BeginRendering(rt, ds)) return;
    VkCommandBuffer cb = Cmd();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout_, 0, 1, &set, 0, nullptr);
    // the viewport covers the target (the guest viewport is in the shader), negative height to stand the picture up
    Target* size = rt ? rt : ds;
    VkViewport vp{0, float(size->h), float(size->w), -float(size->h), 0.0f, 1.0f};
    vkCmdSetViewport(cb, 0, 1, &vp);
    int32_t x0 = int32_t(s.viewport.x), y0 = int32_t(s.viewport.y);
    int32_t x1 = int32_t(s.viewport.x + s.viewport.w), y1 = int32_t(s.viewport.y + s.viewport.h);
    if (s.scissor_count) {
        x0 = std::max(x0, int32_t(s.scissors[0]));
        y0 = std::max(y0, int32_t(s.scissors[1]));
        x1 = std::min(x1, int32_t(s.scissors[2]));
        y1 = std::min(y1, int32_t(s.scissors[3]));
    }
    VkRect2D sc = ScaledRect(*size, x0, y0, x1, y1);
    if (!sc.extent.width || !sc.extent.height) return;
    vkCmdSetScissor(cb, 0, 1, &sc);
    float bf[4];
    Color(SimpleState(s, kMBlendColor), bf);
    vkCmdSetBlendConstants(cb, bf);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, SimpleState(s, kMStencilRef) & 0xFF);
    VkBuffer bufs16[16];
    for (uint32_t b = 0; b < a.buffer_count; b++) bufs16[b] = f.upload;
    if (a.buffer_count) vkCmdBindVertexBuffers(cb, 0, a.buffer_count, bufs16, vb_off);
    if (a.index_count) {
        vkCmdBindIndexBuffer(cb, f.upload, ib_off, a.index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, a.index_count, 1, 0, a.base_vertex, 0);
    } else {
        vkCmdDraw(cb, a.draw_count, 1, 0, 0);
    }
    LOG_FIRST(1, "vulkan: the first draw was recorded");
}

// ---- render scale

void VkBackend::PickScale() {
    scale_ = app::RenderScale(guest_h_);
    xmul_ = 1.0f;
    if (g_wide_targets && guest_h_ && std::fabs(float(guest_w_) / float(guest_h_) - 4.0f / 3.0f) <= 0.01f)
        xmul_ = std::max(1.0f, app::FrameAspect() / (4.0f / 3.0f));
}

// the menu changed the render scale (or the window's height did): every target is made again at the new size
bool VkBackend::ScaleChanged() {
    float s = scale_, x = xmul_;
    PickScale();
    if (s == scale_ && x == xmul_) return false;
    Submit(true);
    vkDeviceWaitIdle(device_);
    for (auto& [key, t] : targets_) DestroyTarget(t);
    targets_.clear();
    LOGR("vulkan: render scale %.3f (%.2f across): targets made again", scale_, scale_ * xmul_);
    return true;
}

bool VkBackend::EnsureScratch(uint32_t w, uint32_t h) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    if (scratch_.image && scratch_w_ == w && scratch_h_ == h) return true;
    Retire(scratch_);
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = kColorFormat;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!MakeImage(scratch_, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
    scratch_w_ = w;
    scratch_h_ = h;
    return true;
}

VkRect2D VkBackend::ScaledRect(const Target& t, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    float sx = float(t.w) / float(std::max(t.gw, 1u)), sy = float(t.h) / float(std::max(t.gh, 1u));
    auto cx = [&](int32_t v) { return std::clamp<int32_t>(int32_t(std::lround(float(v) * sx)), 0, int32_t(t.w)); };
    auto cy = [&](int32_t v) { return std::clamp<int32_t>(int32_t(std::lround(float(v) * sy)), 0, int32_t(t.h)); };
    int32_t a = cx(x0), b = cy(y0), c = cx(x1), d = cy(y1);
    if (c <= a || d <= b) return {{0, 0}, {0, 0}};
    return {{a, b}, {uint32_t(c - a), uint32_t(d - b)}};
}

// ---- copies and read-backs

// a lock alone proves nothing (reads lock too): only changed bytes mean the CPU wrote the target's memory
bool VkBackend::CpuWrote(Target& t) {
    if (t.depth) return false;
    uint64_t e = DirtyEpoch(t.data);
    if (e <= t.epoch) return false;
    t.epoch = e;
    uint64_t sig = MemSig(t.data, t.bytes);
    if (sig == t.sig) return false;
    t.sig = sig;
    return true;
}

// guest memory (32 bit, rows 64 byte aligned) into a colour target
void VkBackend::UploadTarget(Target& t) {
    uint32_t row = t.gw * 4, pitch = GuestPitch(t.gw);
    VkDeviceSize off;
    uint8_t* cpu;
    if (!Upload(VkDeviceSize(row) * t.gh, 16, true, off, cpu)) return;
    const uint8_t* g = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++) std::memcpy(cpu + size_t(y) * row, g + size_t(y) * pitch, row);
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {t.gw, t.gh, 1};
    if (t.w == t.gw && t.h == t.gh) {
        ToLayout(t.image, false, t.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    } else {
        // the console sized pixels into the scratch image, then enlarged to the target's render scale
        if (!EnsureScratch(t.gw, t.gh)) return;
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Barrier(scratch_.image, range, scratch_.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, scratch_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        Barrier(scratch_.image, range, scratch_.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        ToLayout(t.image, false, t.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit b{};
        b.srcSubresource = b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b.srcOffsets[1] = {int32_t(t.gw), int32_t(t.gh), 1};
        b.dstOffsets[1] = {int32_t(t.w), int32_t(t.h), 1};
        vkCmdBlitImage(Cmd(), scratch_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
    }
    LOG_FIRST(4, "vulkan: target %08X takes what the CPU wrote", t.data);
}

// CopyRects: a target the GPU drew is copied on the GPU, rect by rect; pixels the CPU made are copied in memory
void VkBackend::Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) {
    Target* st = FindTarget(src, false);
    if (!st || !st->drawn) {
        if (!CopySurfaceMemory(src, dst, count, src_rects, dst_points))
            LOG_FIRST(8, "vulkan: CopyRects %08X to %08X skipped", src.data, dst.data);
        else if (Target* dt = FindTarget(dst, false))
            UploadTarget(*dt);
        return;
    }
    Target* dt = EnsureTarget(dst, false);
    if (!dt || dt == st) return;
    std::vector<VkImageBlit> regions;
    uint32_t n = count && src_rects ? count : 1;
    for (uint32_t i = 0; i < n; i++) {
        int32_t l = 0, tp = 0, r = int32_t(std::min(st->gw, dt->gw)), b = int32_t(std::min(st->gh, dt->gh));
        if (count && src_rects) {
            uint32_t a = src_rects + i * 16;
            l = int32_t(kernel::R32(a));
            tp = int32_t(kernel::R32(a + 4));
            r = int32_t(kernel::R32(a + 8));
            b = int32_t(kernel::R32(a + 12));
        }
        int32_t dx = l, dy = tp;
        if (count && src_rects && dst_points) {
            dx = int32_t(kernel::R32(dst_points + i * 8));
            dy = int32_t(kernel::R32(dst_points + i * 8 + 4));
        }
        l = std::max(l, 0);
        tp = std::max(tp, 0);
        r = std::min(r, int32_t(st->gw));
        b = std::min(b, int32_t(st->gh));
        if (dx < 0 || dy < 0 || r <= l || b <= tp || dx >= int32_t(dt->gw) || dy >= int32_t(dt->gh)) continue;
        int32_t w = std::min(r - l, int32_t(dt->gw) - dx), h = std::min(b - tp, int32_t(dt->gh) - dy);
        // console rects to each target's own pixels (both at render scale): a blit, so sizes may differ by rounding
        VkRect2D sr = ScaledRect(*st, l, tp, l + w, tp + h), dr = ScaledRect(*dt, dx, dy, dx + w, dy + h);
        if (!sr.extent.width || !sr.extent.height || !dr.extent.width || !dr.extent.height) continue;
        VkImageBlit c{};
        c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.srcOffsets[0] = {sr.offset.x, sr.offset.y, 0};
        c.srcOffsets[1] = {sr.offset.x + int32_t(sr.extent.width), sr.offset.y + int32_t(sr.extent.height), 1};
        c.dstOffsets[0] = {dr.offset.x, dr.offset.y, 0};
        c.dstOffsets[1] = {dr.offset.x + int32_t(dr.extent.width), dr.offset.y + int32_t(dr.extent.height), 1};
        regions.push_back(c);
    }
    if (regions.empty()) return;
    if (CpuWrote(*dt)) UploadTarget(*dt);  // the copy may cover only part of it
    ToLayout(st->image, false, st->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ToLayout(dt->image, false, dt->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdBlitImage(Cmd(), st->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dt->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   uint32_t(regions.size()), regions.data(), VK_FILTER_NEAREST);
    dt->drawn = true;
}

// the CPU reads a surface the GPU drew: the pixels go back to guest memory (the GPU's work so far finished first)
void VkBackend::Sync(uint32_t data, uint32_t) {
    auto it = targets_.find(TargetKey(data, false));
    if (it == targets_.end() || !it->second.drawn || lost_) return;
    Target& t = it->second;
    VkDeviceSize bytes = VkDeviceSize(t.gw) * t.gh * 4;
    if (bytes > readback_size_) {
        if (readback_) vkDestroyBuffer(device_, readback_, nullptr);
        if (readback_mem_) vkFreeMemory(device_, readback_mem_, nullptr);
        readback_ = VK_NULL_HANDLE;
        readback_mem_ = VK_NULL_HANDLE;
        readback_cpu_ = nullptr;
        readback_size_ = 0;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(device_, &bi, nullptr, &readback_) != VK_SUCCESS) return;
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, readback_, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        void* p = nullptr;
        // cached host memory reads fast; coherent is the fallback
        bool found = FindMemory(req.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                ai.memoryTypeIndex) ||
                     FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                ai.memoryTypeIndex);
        if (!found || vkAllocateMemory(device_, &ai, nullptr, &readback_mem_) != VK_SUCCESS) {
            vkDestroyBuffer(device_, readback_, nullptr);
            readback_ = VK_NULL_HANDLE;
            return;
        }
        vkBindBufferMemory(device_, readback_, readback_mem_, 0);
        vkMapMemory(device_, readback_mem_, 0, VK_WHOLE_SIZE, 0, &p);
        readback_cpu_ = static_cast<uint8_t*>(p);
        readback_size_ = bytes;
    }
    ToLayout(t.image, false, t.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {t.gw, t.gh, 1};
    if (t.w == t.gw && t.h == t.gh) {
        vkCmdCopyImageToBuffer(Cmd(), t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &c);
    } else {
        // shrunk to the console's size first (a linear blit: each pixel the average of the four nearest)
        if (!EnsureScratch(t.gw, t.gh)) return;
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Barrier(scratch_.image, range, scratch_.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit b{};
        b.srcSubresource = b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b.srcOffsets[1] = {int32_t(t.w), int32_t(t.h), 1};
        b.dstOffsets[1] = {int32_t(t.gw), int32_t(t.gh), 1};
        vkCmdBlitImage(Cmd(), t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scratch_.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
        Barrier(scratch_.image, range, scratch_.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(Cmd(), scratch_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &c);
    }
    Submit(true);
    if (lost_ || !readback_cpu_) return;
    uint32_t row = t.gw * 4, pitch = GuestPitch(t.gw);
    uint8_t* g = static_cast<uint8_t*>(kernel::HostPtr(data));
    for (uint32_t y = 0; y < t.gh; y++) std::memcpy(g + size_t(y) * pitch, readback_cpu_ + size_t(y) * row, row);
    t.drawn = false;
    t.sig = MemSig(t.data, t.bytes);
    LOG_FIRST(4, "vulkan: read back %08X (%ux%u)", data, t.gw, t.gh);
}

// ---- the in-game menu, overlay and fps counter (ImGui), drawn into the swapchain image after the frame

bool VkBackend::InitUi() {
    if (!dynamic_rendering_) {
        LOGR("vulkan: the in-game menu needs dynamic rendering; it is not shown on this card yet");
        return true;
    }
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 16;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VkResult r = vkCreateDescriptorPool(device_, &pi, nullptr, &ui_pool_);
    if (r != VK_SUCCESS) return Fail("vkCreateDescriptorPool (menu)", r);
    if (!ImGui_ImplVulkan_LoadFunctions([](const char* name, void* inst) { return Lookup(name, inst); }, instance_))
        return Fail("the menu's Vulkan functions could not be loaded");
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = instance_;
    ii.PhysicalDevice = gpu_;
    ii.Device = device_;
    ii.QueueFamily = family_;
    ii.Queue = queue_;
    ii.DescriptorPool = ui_pool_;
    ii.MinImageCount = 2;
    ii.ImageCount = std::max<uint32_t>(uint32_t(images_.size()), 2);
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.PipelineCache = VK_NULL_HANDLE;
    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swap_fmt_;
    if (!ImGui_ImplVulkan_Init(&ii)) return Fail("the menu's Vulkan backend did not start");
    ui_ = true;
    return true;
}

void VkBackend::RenderUi(VkCommandBuffer cb, uint32_t index) {
    if (!ui_) return;
    std::lock_guard lock(app::UiLock());
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (app::kGame.ui) app::kGame.ui(fps_);
    app::DrawOverlay(fps_);
    // [display] show_fps: the frames presented a second, top right
    if (app::Get().show_fps && fps_ > 0) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        float pad = std::max(8.0f, vp->Size.y / 120.0f);
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - pad, vp->Pos.y + pad), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::SetWindowFontScale(app::UiScale());
        ImGui::Text("%.0f fps", fps_);
        ImGui::End();
    }
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) return;
    // the swapchain image is a transfer destination after the blit: drawn into as a colour attachment
    VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToLayout(images_[index], false, layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = image_views_[index];
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, extent_};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    ImGui_ImplVulkan_RenderDrawData(dd, cb);
    vkCmdEndRenderingKHR(cb);
    ToLayout(images_[index], false, layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

// presents a second, counted over half a second
void VkBackend::CountFps() {
    uint64_t now = ::GetTickCount64();
    if (!fps_start_) fps_start_ = now;
    fps_frames_++;
    if (now - fps_start_ >= 500) {
        fps_ = double(fps_frames_) * 1000.0 / double(now - fps_start_);
        fps_frames_ = 0;
        fps_start_ = now;
    }
}

// the front buffer's target scaled into the window, the frame's shape kept with black at the sides
void VkBackend::BlitToScreen(VkCommandBuffer cb, Target* front, uint32_t index) {
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageLayout swap_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    ToLayout(images_[index], false, swap_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue black{};
    black.float32[3] = 1.0f;
    vkCmdClearColorImage(cb, images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    if (front) {
        ToLayout(front->image, false, front->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        float aspect = app::FrameAspect();
        float sw = float(extent_.width), sh = float(extent_.height);
        float w = std::min(sw, sh * aspect), h = std::min(sh, sw / aspect);
        int32_t x0 = int32_t((sw - w) / 2), y0 = int32_t((sh - h) / 2);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {int32_t(front->w), int32_t(front->h), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[0] = {x0, y0, 0};
        blit.dstOffsets[1] = {x0 + int32_t(w), y0 + int32_t(h), 1};
        vkCmdBlitImage(cb, front->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, images_[index],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }
}

void VkBackend::Present(const Surface& front) {
    if (lost_ || !device_) return;
    {
        // the frame rate every 5 seconds in the log, as the Direct3D 12 renderer writes it (side-by-side tests)
        static auto fps_t = std::chrono::steady_clock::now();
        static uint64_t fps_n = 0;
        fps_n++;
        auto now = std::chrono::steady_clock::now();
        double el = std::chrono::duration<double, std::milli>(now - fps_t).count();
        if (el >= 5000.0) {
            LOGR("fps: %.1f (%.2f ms a frame, the last 5 s; vulkan)", fps_n * 1000.0 / el, el / double(fps_n));
            fps_t = now;
            fps_n = 0;
        }
    }
    // the window resized, minimised or v-sync changed in the menu: a new swapchain
    if (!swap_ || vsync_ != app::Get().vsync) {
        Submit(true);
        vkDeviceWaitIdle(device_);
        if (!CreateSwapchain()) {
            LOG_FIRST(4, "vulkan: the swapchain could not be made again: %s", error_.c_str());
            return;
        }
        if (!swap_) {  // minimised: the frame's work still goes to the GPU
            Submit(false);
            return;
        }
    }

    if (ScaleChanged()) return;  // this frame's targets are gone; the next is drawn at the new size
    Target* t = FindTarget(front, false);
    if (!t) LOG_FIRST(2, "vulkan: Present found no target for the front buffer %08X (black)", front.data);
    VkCommandBuffer cb = Cmd();
    if (!cb) return;
    EndRendering();
    Frame& f = frames_[frame_];
    uint32_t index = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swap_, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        Submit(true);  // the frame's draws still run; the acquire semaphore was not signalled, so it stays usable
        vkDeviceWaitIdle(device_);
        if (!CreateSwapchain()) LOG_FIRST(4, "vulkan: the swapchain could not be made again: %s", error_.c_str());
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        LOG_FIRST(4, "vulkan: vkAcquireNextImageKHR failed (%s)", ResultName(r));
        Submit(false);
        return;
    }
    presents_++;
    BlitToScreen(cb, t, index);
    RenderUi(cb, index);
    CountFps();
    VkImageLayout swap_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;  // as the blit and the menu leave it
    ToLayout(images_[index], false, swap_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer(cb);
    recording_ = false;
    vkResetFences(device_, 1, &f.done);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.acquired;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &rendered_[index];
    r = vkQueueSubmit(queue_, 1, &si, f.done);
    if (r != VK_SUCCESS) {
        lost_ = true;  // the fence is reset and was never submitted: waiting on it would never end
        if (r == VK_ERROR_DEVICE_LOST)
            app::Fail(app::kExitGpuLost, L"The graphics driver stopped responding (Vulkan device lost).");
        LOG_FIRST(4, "vulkan: vkQueueSubmit failed (%s)", ResultName(r));
        return;
    }

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &rendered_[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swap_;
    pi.pImageIndices = &index;
    r = vkQueuePresentKHR(queue_, &pi);
    frame_ = (frame_ + 1) % kFrames;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(device_);
        if (!CreateSwapchain()) LOG_FIRST(4, "vulkan: the swapchain could not be made again: %s", error_.c_str());
    } else if (r != VK_SUCCESS) {
        LOG_FIRST(4, "vulkan: vkQueuePresentKHR failed (%s)", ResultName(r));
    }
}

}  // namespace

std::unique_ptr<Backend> CreateVulkanBackend() { return std::make_unique<VkBackend>(); }

}  // namespace gpu
