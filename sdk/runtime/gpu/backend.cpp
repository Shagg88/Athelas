// The backend the renderer draws with
#include "gpu/backend.h"

#include "app/app.h"

namespace gpu {

// [graphics] renderer: 0 Direct3D 12, 1 Vulkan (xdk/d3d8.cpp falls back to Direct3D 12 when Vulkan's Init fails)
std::unique_ptr<Backend> CreateBackend() {
    if (app::Get().renderer == 1) return CreateVulkanBackend();
    return CreateD3D12Backend();
}

}  // namespace gpu
