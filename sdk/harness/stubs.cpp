// stand ins for runtime parts the headless build leaves out (app/, the D3D12 and Vulkan backends): the achievements
// do nothing, every graphics API request gets the null backend (null_backend.cpp)
#include <memory>
#include <vector>

#include "app/app.h"
#include "gpu/backend.h"

namespace app {
bool g_vulkan_fallback = false;
}

namespace ach {
uint32_t VirtueDealt() { return 100; }
uint32_t VirtueTaken() { return 100; }
void SharedDeeds(std::vector<Def>&) {}
void Defeated() {}
void LevelStarted(const char*) {}
void LevelDone(const char*) {}
bool FateDone(const char*) { return false; }
bool FellowshipOn() { return false; }
}  // namespace ach

namespace gpu {
std::unique_ptr<Backend> CreateD3D12Backend() { return CreateBackend(); }
std::unique_ptr<Backend> CreateVulkanBackend() { return CreateBackend(); }
namespace vk {
bool Probe() { return false; }
}  // namespace vk
}  // namespace gpu
