// Temporal upscaling: AMD FSR (FSR 4 on the Radeon cards that have it, FSR 3.1 on every other card) and NVIDIA DLSS
// Super Resolution (RTX cards), behind one interface. Both take the frame at the render size, its depth, motion
// vectors (from the current pixel to where it was a frame earlier, in UV units) and the camera jitter, and write the
// frame at the output size. Their DLLs are loaded at run time from beside the game: without them (the ARM64 builds,
// a copy without the files) the setting falls back to off and says why in the log.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>

namespace gpu::up {

enum Kind : uint32_t { kOff = 0, kFsr = 1, kDlss = 2 };
// [graphics] upscaler_quality
enum Quality : uint32_t { kNative = 0, kQuality = 1, kBalanced = 2, kPerformance = 3, kUltraPerformance = 4 };

// per-dimension ratio of the output size to the render size
float Ratio(Quality q);
const char* KindName(Kind k);

struct Frame {
    ID3D12GraphicsCommandList* cl = nullptr;
    ID3D12Resource* color = nullptr;  // render size, in PIXEL_SHADER_RESOURCE state
    ID3D12Resource* depth = nullptr;  // render size, R24G8_TYPELESS, in PIXEL_SHADER_RESOURCE state
    ID3D12Resource* motion = nullptr; // render size, R16G16_FLOAT, in PIXEL_SHADER_RESOURCE state
    ID3D12Resource* output = nullptr; // output size, UAV capable, in UNORDERED_ACCESS state
    ID3D12Resource* reactive = nullptr; // render size, R8_UNORM: where 2D covers the picture (0..0.9), PIXEL_SHADER_RESOURCE
    uint32_t render_w = 0, render_h = 0, out_w = 0, out_h = 0;
    float jitter_x = 0, jitter_y = 0;  // pixels at the render size, as added to the projection (+2x/w, -2y/h)
    bool reset = false;                // the camera cut: history thrown away
    float dt_ms = 16.7f;
    bool hdr = false;                  // colour is linear scRGB (HDR output), else gamma encoded 0..1
};

class Upscaler {
public:
    virtual ~Upscaler() = default;
    virtual Kind kind() const = 0;
    virtual std::string Describe() const = 0;  // for the log: "FSR 4.1.1", "DLSS 310.7.0"
    // jitter phases for this render/output ratio (FSR's formula, used for both)
    virtual int Phases(uint32_t render_w, uint32_t out_w) const;
    virtual bool Evaluate(const Frame& f) = 0;  // records into f.cl; the caller restores its root signature and heaps
};

// Streamline wants slInit before the swap chain is made: called at device creation, when the DLSS files are there
// and the card is NVIDIA's. Safe to call when they are missing (returns false and says why).
bool InitStreamline(IDXGIAdapter* adapter, std::string& why);
// after the swap chain is made: Streamline's bookkeeping runs on its Present (manual hooking)
void UpgradeSwapChain(IDXGISwapChain** swap);
// right after the device is made, once Streamline has started: the device it works with
bool SetStreamlineDevice(ID3D12Device* dev);
void ShutdownStreamline();

// an upscaler for the output size; nullptr and why when it can't be had on this PC
std::unique_ptr<Upscaler> Create(Kind k, Quality q, ID3D12Device* dev, ID3D12CommandQueue* queue, IDXGIAdapter* adapter,
                                 uint32_t out_w, uint32_t out_h, uint32_t max_render_w, uint32_t max_render_h, bool hdr,
                                 std::string& why);

// ------------------------------------------------------------------------------------- AMD FSR frame generation
// One made-up frame shown between each two the game draws (FSR 3.1 / FSR 4 frame generation, AMD's
// amd_fidelityfx_framegeneration_dx12.dll): it needs FSR's own swap chain, so it is set up at start, and only with the
// FSR upscaler (it takes the same depth and motion). Frames without 3D (menus, films) are shown as they are.

// [graphics] frame_generation on, FSR the upscaler, the files there: the swap chain is made through FSR (false and
// why: the caller makes the normal one)
bool FgCreateSwapChain(HWND hwnd, DXGI_SWAP_CHAIN_DESC1& desc, IDXGIFactory* factory, ID3D12CommandQueue* queue,
                       IDXGISwapChain4** out, std::string& why);
bool FgActive();  // the swap chain is FSR's

struct FgFrame {
    ID3D12GraphicsCommandList* cl = nullptr;
    ID3D12Resource* depth = nullptr;   // as up::Frame, in PIXEL_SHADER_RESOURCE state
    ID3D12Resource* motion = nullptr;
    uint32_t render_w = 0, render_h = 0;
    uint32_t bb_w = 0, bb_h = 0;       // the swap chain's size
    uint32_t vx = 0, vy = 0, vw = 0, vh = 0;  // where the picture is on it (bars at the sides or top left out)
    float jitter_x = 0, jitter_y = 0;
    float dt_ms = 16.7f;
    bool reset = false;
    bool hdr = false;
    DXGI_FORMAT bb_fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
};
// a 3D frame upscaled: frame generation readied for it (records into f.cl, before the frame is drawn to the screen)
bool FgPrepare(ID3D12Device* dev, const FgFrame& f);
// a frame with nothing to make frames from (menus, films, the upscaler off for it): shown as it is
void FgPause();
// before the swap chain goes (the game's queue idle): FSR's presents waited for, frame generation's context destroyed
void FgShutdown();
// then, once the game has released its references to the swap chain: FSR's swap chain context
void FgReleaseSwapChain();

}  // namespace gpu::up
