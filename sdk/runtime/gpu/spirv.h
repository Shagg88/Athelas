// HLSL to SPIR-V at run time for the Vulkan backend, through glslang's HLSL front end (linked statically).
// Register mapping, descriptor set 0: b0..b2 -> bindings 0..2, t0..t3 -> 8..11, s0..s3 -> 16..19.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpu::spirv {

enum class Stage { Vertex, Pixel };

// Compiles one HLSL stage (Vulkan 1.1, SPIR-V 1.3). False on failure, with glslang's log in error.
bool Compile(const std::string& hlsl, const char* entry, Stage stage, std::vector<uint32_t>& out, std::string& error);

}  // namespace gpu::spirv
