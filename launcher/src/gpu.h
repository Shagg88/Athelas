// ROTK Fix Launcher - is there a Vulkan driver DXVK can use, and what kind of graphics card?
// The check runs in a separate copy of the launcher (ROTKFixLauncher.exe --vkprobe), so a
// broken driver can never take the launcher down; the answer arrives a moment after start.
#pragma once
#include <string>

enum GpuState { GPU_CHECKING, GPU_VULKAN_OK, GPU_VULKAN_TOO_OLD, GPU_NO_VULKAN };
struct GpuInfo {
    GpuState state = GPU_CHECKING;
    std::string name;          // the best device's name ("" if none)
    bool integrated = false;   // the best device is integrated graphics (laptop / onboard) or software
    bool software = false;     // a software (CPU) Vulkan driver
    unsigned api = 0;          // its Vulkan version (VK_MAKE_API_VERSION form)
};

int  gpu_probe_main();        // the --vkprobe child: prints one line and exits
void gpu_probe_start();       // starts the check in the background
GpuInfo gpu_info();           // GPU_CHECKING until the answer is in
