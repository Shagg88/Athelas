// See gpu.h. Only the few Vulkan calls needed, declared here (no Vulkan SDK needed to build).
#include "gpu.h"
#include "port.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VKAPI __stdcall   // VKAPI_PTR on 32-bit Windows
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef void (VKAPI *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (VKAPI *PFN_vkGetInstanceProcAddr)(VkInstance, const char *);
struct VkApplicationInfo { int sType; const void *pNext; const char *pApplicationName; uint32_t applicationVersion;
                           const char *pEngineName; uint32_t engineVersion; uint32_t apiVersion; };
struct VkInstanceCreateInfo { int sType; const void *pNext; uint32_t flags; const VkApplicationInfo *pApplicationInfo;
                              uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames;
                              uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames; };
typedef int (VKAPI *PFN_vkCreateInstance)(const VkInstanceCreateInfo *, const void *, VkInstance *);
typedef void (VKAPI *PFN_vkDestroyInstance)(VkInstance, const void *);
typedef int (VKAPI *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void (VKAPI *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, void *);
typedef int (VKAPI *PFN_vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice, const char *, uint32_t *, void *);

// the extensions DXVK 3 needs beyond Vulkan 1.4, as ROTK Fix checks them (src/graphics.c dxvk_extensions): 1 all
// there (or it can't tell: DXVK decides), 0 one missing
static int dxvk_extensions_ok(PFN_vkEnumerateDeviceExtensionProperties ext, VkPhysicalDevice dev)
{
    static unsigned char p[600 * 260];   // VkExtensionProperties: name[256], specVersion
    uint32_t n = 600;
    if (!ext || ext(dev, NULL, &n, p) < 0) return 1;
    int clip = 0, rob = 0, xfb = 0;
    for (uint32_t i = 0; i < n; i++) {
        const char *e = (const char *)p + (size_t)i * 260;
        if (!strcmp(e, "VK_EXT_depth_clip_enable")) clip = 1;
        if (!strcmp(e, "VK_EXT_robustness2") || !strcmp(e, "VK_KHR_robustness2")) rob = 1;
        if (!strcmp(e, "VK_EXT_transform_feedback")) xfb = 1;
    }
    return clip && rob && xfb;
}

#define VK_API(maj, min) (((uint32_t)(maj) << 22) | ((uint32_t)(min) << 12))
static const uint32_t k_needed = VK_API(1, 4);   // DXVK 3.x (bundled 3.1.1) needs Vulkan 1.4

static void say(const char *s)
{
    DWORD n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &n, NULL);
}

// Prints "none", or "<api> <type> <name>" for the best device (Vulkan 1.4 first, then discrete first).
int gpu_probe_main()
{
    if (getenv("ROTKFIX_NO_VULKAN")) { say("none"); return 0; }   // testing
    HMODULE vk = LoadLibraryA("vulkan-1.dll");   // the 32-bit loader, as the 32-bit game uses
    if (!vk) { say("none"); return 0; }
    auto gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(vk, "vkGetInstanceProcAddr");
    if (!gipa) { say("none"); return 0; }
    auto create = (PFN_vkCreateInstance)(void *)gipa(NULL, "vkCreateInstance");
    if (!create) { say("none"); return 0; }
    VkApplicationInfo app = { 0, NULL, "ROTK Fix Launcher", 1, "ROTK Fix", 1, VK_API(1, 1) };
    VkInstanceCreateInfo ci = { 1, NULL, 0, &app, 0, NULL, 0, NULL };
    VkInstance inst = NULL;
    if (create(&ci, NULL, &inst) != 0 || !inst) { say("none"); return 0; }
    auto destroy = (PFN_vkDestroyInstance)(void *)gipa(inst, "vkDestroyInstance");
    auto enumerate = (PFN_vkEnumeratePhysicalDevices)(void *)gipa(inst, "vkEnumeratePhysicalDevices");
    auto props = (PFN_vkGetPhysicalDeviceProperties)(void *)gipa(inst, "vkGetPhysicalDeviceProperties");
    auto exts = (PFN_vkEnumerateDeviceExtensionProperties)(void *)gipa(inst, "vkEnumerateDeviceExtensionProperties");
    uint32_t n = 0;
    VkPhysicalDevice devs[16];
    if (!enumerate || !props || enumerate(inst, &n, NULL) != 0 || n == 0) { if (destroy) destroy(inst, NULL); say("none"); return 0; }
    if (n > 16) n = 16;
    enumerate(inst, &n, devs);
    int best = -1, bestScore = -1;
    uint32_t bestApi = 0, bestType = 0;
    int bestExt = 0;
    char bestName[257] = "";
    static unsigned char buf[4096];   // VkPhysicalDeviceProperties: apiVersion@0, deviceType@16, deviceName@20
    for (uint32_t i = 0; i < n; i++) {
        memset(buf, 0, sizeof buf);
        props(devs[i], buf);
        uint32_t api, type;
        memcpy(&api, buf, 4);
        memcpy(&type, buf + 16, 4);
        int extOk = api >= k_needed ? dxvk_extensions_ok(exts, devs[i]) : 0;
        int score = (api >= k_needed ? 100 : 0) + extOk * 50 + (type == 2 ? 10 : type == 1 ? 5 : 0);   // 2 discrete, 1 integrated
        if (score > bestScore) {
            bestScore = score; best = (int)i; bestApi = api; bestType = type; bestExt = extOk;
            memcpy(bestName, buf + 20, 256); bestName[256] = 0;
        }
    }
    if (destroy) destroy(inst, NULL);
    if (best < 0) { say("none"); return 0; }
    char line[400];
    snprintf(line, sizeof line, "%u %u %d %s", bestApi, bestType, bestExt, bestName);
    say(line);
    return 0;
}

static volatile LONG g_done;
static GpuInfo g_info;

static DWORD WINAPI probe_thread(LPVOID)
{
    char self[MAX_PATH * 4] = "";   // (UTF-8: up to three bytes a letter)
    GetModuleFileNameA(NULL, self, sizeof self);
    std::string out;
    GpuInfo r;
    RunResult rc = run_capture(self, "--vkprobe", out, 15000);
    unsigned api = 0, type = 0;
    int ext = 1;
    char name[300] = "";
    if (rc.ok() && sscanf(out.c_str(), "%u %u %d %299[^\r\n]", &api, &type, &ext, name) >= 3) {
        r.api = api; r.integrated = type == 1 || type == 4; r.software = type == 4; r.name = name;   // 4 = software (CPU): weak too
        r.state = api < k_needed ? GPU_VULKAN_TOO_OLD : ext ? GPU_VULKAN_OK : GPU_VULKAN_NO_EXT;
    } else {
        r.state = GPU_NO_VULKAN;   // no driver, or the check failed or crashed
    }
    g_info = r;
    InterlockedExchange(&g_done, 1);
    return 0;
}

void gpu_probe_start()
{
    HANDLE h = CreateThread(NULL, 0, probe_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

GpuInfo gpu_info()
{
    if (!InterlockedCompareExchange(&g_done, 0, 0)) return GpuInfo();
    return g_info;
}
