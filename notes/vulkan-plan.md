# Vulkan backend plan (2026-10-29)

Goal: Vulkan beside D3D12 for all four Xbox ports, same settings, x64 and ARM64; no Microsoft files, nothing
for the player to install.

- Shaders: keep shaders.cpp's HLSL; compile it to SPIR-V at runtime with glslang's HLSL front end (built by
  llvm-mingw, static, ENABLE_OPT=OFF; BSD/MIT/Apache). Bindings: b0..b2 -> 0..2, t0..t3 -> 8..11, s0..s3 ->
  16..19, set 0. Negative-height viewport for Y. Cache key salted "spv". First test: compile every shader in a
  shader seed through glslang (--shader-test) and fix what it rejects (mac_mul's vector ||, casts...).
- Loader: vendored Khronos headers (VK_NO_PROTOTYPES), in-repo volk-style loader of vulkan-1.dll; missing DLL
  or Init failure -> fall back to D3D12. ImGui: imgui_impl_vulkan with NO_PROTOTYPES. Target Vulkan 1.2
  (timeline semaphores), optional dynamic rendering / push descriptors / extended dynamic state / HDR.
- Phase 0 (no behaviour change): move API-neutral code out of d3d12.cpp (PsoKey, constants fill, movie place,
  target lookup by guest address, late readback ring, shader cache, pass HLSL) into gpu/common.cpp,
  shader_cache.cpp, backend.cpp; split ext.h (TargetInfo neutral, D3D12 Target in ext_d3d12.h); neutral
  vertex format enum. Verify with captures before/after.
- Phase 1: Two Towers menu on Vulkan (instance/device/swapchain/frame ring/upload ring/allocator, targets by
  guest address, textures, samplers, descriptor set, pipelines by PsoKey, Draw, Clear, Present + blit + ImGui,
  blocking Sync/Fence). Queries, late sync, MSAA, HDR, upscalers off.
- Phase 2: in-game parity (Copy, feedback copies, queries, async readbacks, warm-up, captures, texture packs,
  extension hooks). Phase 3: MSAA, alpha to coverage, render scale, HDR scRGB. Phase 4: FSR 3.1 Vulkan (FSR 4
  and DLSS stay D3D12-only). Phase 5: copy the SDK to all four ports.
- Setting: [graphics] renderer = 0 Direct3D 12 / 1 Vulkan (restart).
- Risks: glslang HLSL gaps, pipeline stutter (VkPipelineCache + warm-up), D24S8 missing on AMD (D32S8
  fallback), four SDK copies in sync.

- 2026-10-29: glslang (HLSL front end) compiled all 21 captured TA shaders and the 7 pass shaders in d3d12.cpp
  to SPIR-V unchanged (glslangValidator -D -V --hlsl-iomap --sib 8 --ssb 16). Headers + loader + probe added.
- 2026-10-29: phase 1a: targets by console address (own image + memory each, console size; render scale later),
  Clear through dynamic rendering + vkCmdClearAttachments (rects or viewport; partial-channel clears logged, not
  done), Flush, and Present blitting the front buffer's target to the swapchain at FrameAspect with black sides.
  Full barriers (ALL_COMMANDS) for now. Next: 1b Draw (vertex.cpp Assemble -> shaders.cpp HLSL -> spirv.cpp ->
  pipeline by PsoKey; needs ClassifyShaders/VertexKey/MakePixelKey moved out of d3d12.cpp into common), textures.
- 2026-10-29: phase 1b (ee4e089): Draw. Same shaders as D3D12 (shader choice moved to gpu/common.cpp), pipelines
  per key with dynamic rendering, a 64 MB upload ring per frame, 8192 descriptor sets per frame, textures from
  texture.cpp (BC1-3 kept), targets sampled (a copy when drawn into), shadow compare samplers, negative-height
  viewport. Missing: points, Copy, Sync/read-backs, CPU-written targets, render scale, MSAA, partial clears,
  render passes (no dynamic rendering), ImGui menu, screenshots/captures, gamma. Untested on the PC so far.
