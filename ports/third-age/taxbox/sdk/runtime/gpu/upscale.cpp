// FSR and DLSS behind gpu::up (upscale.h). Both are loaded from DLLs beside the game at run time:
//   FSR:  amd_fidelityfx_loader_dx12.dll + amd_fidelityfx_upscaler_dx12.dll (AMD FSR SDK 2.4: FSR 4.1 where the card
//         and driver have it, FSR 3.1 otherwise; the loader picks), amd_fidelityfx_framegeneration_dx12.dll for
//         frame generation
//   DLSS: sl.interposer.dll, sl.common.dll, sl.dlss.dll, nvngx_dlss.dll (NVIDIA Streamline 2.14, DLSS 4.5), used with
//         manual hooking: only the sl* calls go through Streamline, the renderer keeps its own device and queue
#include "gpu/upscale.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#include "app/app.h"
#include "core/log.h"

#include "../../third_party/ffx/ffx_api.h"
#include "../../third_party/ffx/ffx_api_loader.h"
#include "../../third_party/ffx/ffx_upscale.h"
#include "../../third_party/ffx/dx12/ffx_api_dx12.h"
#include "../../third_party/ffx/ffx_framegeneration.h"
#include "../../third_party/ffx/dx12/ffx_api_framegeneration_dx12.h"

#include "../../third_party/streamline/sl.h"
#include "../../third_party/streamline/sl_consts.h"
#include "../../third_party/streamline/sl_dlss.h"
// NVIDIA's signature check needs the Windows 8 WinTrust structures, which MinGW's headers (the Linux test builds)
// lack; the release builds (Visual Studio's Windows SDK) check
#if defined(__MINGW32__)
namespace sl::security {
inline bool verifyEmbeddedSignature(const wchar_t*) { return true; }
}
#else
#include "../../third_party/streamline/sl_security.h"
#endif

namespace gpu::up {

float Ratio(Quality q) {
    switch (q) {
        case kNative: return 1.0f;
        case kQuality: return 1.5f;
        case kBalanced: return 1.7f;
        case kPerformance: return 2.0f;
        default: return 3.0f;
    }
}

const char* KindName(Kind k) { return k == kFsr ? "FSR" : k == kDlss ? "DLSS" : "off"; }

int Upscaler::Phases(uint32_t render_w, uint32_t out_w) const {
    float r = float(out_w) / float(std::max(render_w, 1u));
    return std::max(8, int(std::ceil(8.0f * r * r)));
}

namespace {

bool Exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

// where the upscalers' files are: beside the game, or the release's shared "upscalers" folder (Athelas\upscalers,
// one copy for the three games); the folder is added to the DLL search, as the FSR loader and Streamline load
// further files of their own from it
std::filesystem::path Folder(const wchar_t* name) {
    std::filesystem::path dirs[] = {app::ExeDir(), app::ExeDir().parent_path() / L"upscalers", app::ExeDir() / L"upscalers"};
    for (auto& d : dirs)
        if (Exists(d / name)) {
            static std::filesystem::path added;
            if (added != d && d != app::ExeDir()) {
                SetDllDirectoryW(d.c_str());
                added = d;
            }
            return d;
        }
    return app::ExeDir();
}
std::filesystem::path Beside(const wchar_t* name) { return Folder(name) / name; }

// ------------------------------------------------------------------------------------------------------------- FSR

HMODULE g_ffx = nullptr;
ffxFunctions g_ffx_fn{};

void FfxMessage(uint32_t type, const wchar_t* message) {
    char buf[512];
    WideCharToMultiByte(CP_UTF8, 0, message, -1, buf, sizeof(buf), nullptr, nullptr);
    if (type == FFX_API_MESSAGE_TYPE_ERROR) LOGE("FSR: %s", buf);
    else LOGI("FSR: %s", buf);
}

bool LoadFfx(std::string& why) {
    if (!g_ffx_fn.CreateContext) {
        auto loader = Beside(L"amd_fidelityfx_loader_dx12.dll");
        if (!Exists(loader) || !Exists(Beside(L"amd_fidelityfx_upscaler_dx12.dll"))) {
            why = "the FSR files (amd_fidelityfx_loader_dx12.dll, amd_fidelityfx_upscaler_dx12.dll) are not beside the game";
            return false;
        }
        if (!g_ffx) g_ffx = LoadLibraryW(loader.c_str());
        if (!g_ffx) {
            why = "amd_fidelityfx_loader_dx12.dll could not be loaded";
            return false;
        }
        // (ffx_api_loader.h's ffxLoadFunctions only fills these in where _WINDOWS is defined)
        g_ffx_fn.CreateContext = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(g_ffx, "ffxCreateContext"));
        g_ffx_fn.DestroyContext = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(g_ffx, "ffxDestroyContext"));
        g_ffx_fn.Configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(g_ffx, "ffxConfigure"));
        g_ffx_fn.Query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(g_ffx, "ffxQuery"));
        g_ffx_fn.Dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(g_ffx, "ffxDispatch"));
        if (!g_ffx_fn.CreateContext || !g_ffx_fn.Dispatch || !g_ffx_fn.Query || !g_ffx_fn.DestroyContext || !g_ffx_fn.Configure) {
            why = "amd_fidelityfx_loader_dx12.dll lacks the FSR functions";
            g_ffx_fn = {};
            return false;
        }
    }
    return true;
}

class Fsr final : public Upscaler {
public:
    ~Fsr() override {
        if (ctx_) g_ffx_fn.DestroyContext(&ctx_, nullptr);
    }
    Kind kind() const override { return kFsr; }
    std::string Describe() const override { return version_; }

    bool Create(ID3D12Device* dev, uint32_t ow, uint32_t oh, uint32_t rw, uint32_t rh, bool hdr, std::string& why) {
        if (!LoadFfx(why)) return false;
        ffxCreateBackendDX12Desc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.device = dev;
        ffxCreateContextDescUpscale cd{};
        cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        cd.header.pNext = &backend.header;
        // the console frame is gamma encoded (non linear) unless the HDR output made it scRGB; exposure is worked out
        cd.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | (hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE);
        cd.maxRenderSize = {rw, rh};
        cd.maxUpscaleSize = {ow, oh};
        cd.fpMessage = FfxMessage;
        ffxReturnCode_t r = g_ffx_fn.CreateContext(&ctx_, &cd.header, nullptr);
        if (r != FFX_API_RETURN_OK) {
            why = "FSR could not start on this graphics card (error " + std::to_string(r) + ")";
            ctx_ = nullptr;
            return false;
        }
        hdr_ = hdr;
        // the version the loader picked: FSR 4 on cards that run it, else 3.1
        uint64_t count = 0;
        ffxQueryDescGetVersions qv{};
        qv.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        qv.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        qv.device = dev;
        qv.outputCount = &count;
        version_ = "FSR";
        if (g_ffx_fn.Query(nullptr, &qv.header) == FFX_API_RETURN_OK && count) {
            std::vector<uint64_t> ids(count);
            std::vector<const char*> names(count);
            qv.versionIds = ids.data();
            qv.versionNames = names.data();
            if (g_ffx_fn.Query(nullptr, &qv.header) == FFX_API_RETURN_OK && names[0]) version_ = std::string("FSR ") + names[0];
        }
        ffxQueryGetProviderVersion pv{};
        pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (g_ffx_fn.Query(&ctx_, &pv.header) == FFX_API_RETURN_OK && pv.versionName)
            version_ = std::string("FSR ") + pv.versionName;
        return true;
    }

    int Phases(uint32_t rw, uint32_t ow) const override {
        int32_t n = 0;
        ffxQueryDescUpscaleGetJitterPhaseCount q{};
        q.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
        q.renderWidth = rw;
        q.displayWidth = ow;
        q.pOutPhaseCount = &n;
        if (g_ffx_fn.Query(const_cast<ffxContext*>(&ctx_), &q.header) == FFX_API_RETURN_OK && n > 0) return n;
        return Upscaler::Phases(rw, ow);
    }

    bool Evaluate(const Frame& f) override {
        ffxDispatchDescUpscale d{};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = f.cl;
        d.color = ffxApiGetResourceDX12(f.color, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.depth = ffxApiGetResourceDX12(f.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.motionVectors = ffxApiGetResourceDX12(f.motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.output = ffxApiGetResourceDX12(f.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        // the HUD and text over the picture: taken from this frame, not smeared from the last (FSR's reactive mask)
        if (f.reactive) d.reactive = ffxApiGetResourceDX12(f.reactive, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.jitterOffset = {f.jitter_x, f.jitter_y};
        // motion is in UV units: FSR takes pixels
        d.motionVectorScale = {float(f.render_w), float(f.render_h)};
        d.renderSize = {f.render_w, f.render_h};
        d.upscaleSize = {f.out_w, f.out_h};
        d.enableSharpening = false;  // [graphics] sharpening runs after, on the picture as shown
        d.frameTimeDelta = f.dt_ms;
        d.preExposure = 1.0f;
        d.reset = f.reset;
        // the console games' scale is unknown; FSR uses these for its depth heuristics only
        d.cameraNear = 0.1f;
        d.cameraFar = 1000.0f;
        d.cameraFovAngleVertical = 1.0f;
        d.viewSpaceToMetersFactor = 1.0f;
        d.flags = hdr_ ? 0 : FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        ffxReturnCode_t r = g_ffx_fn.Dispatch(&ctx_, &d.header);
        if (r != FFX_API_RETURN_OK) {
            static int logged = 0;
            if (logged++ < 3) LOGR("upscaler: FSR did not run (%u); the normal picture instead", unsigned(r));
            return false;
        }
        return true;
    }

private:
    ffxContext ctx_ = nullptr;
    bool hdr_ = false;
    std::string version_;
};

// ------------------------------------------------------------------------------------------------------------ DLSS

HMODULE g_sl = nullptr;
bool g_sl_ready = false;
std::string g_sl_why = "DLSS was not started";

template <typename F>
F* SlFn(const char* name) {
    return g_sl ? reinterpret_cast<F*>(GetProcAddress(g_sl, name)) : nullptr;
}

void SlLog(sl::LogType type, const char* msg) {
    std::string m(msg ? msg : "");
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) m.pop_back();
    if (type == sl::LogType::eError) LOGE("DLSS: %s", m.c_str());
    else if (type == sl::LogType::eWarn) LOGW("DLSS: %s", m.c_str());
}

bool IsNvidia(IDXGIAdapter* adapter) {
    DXGI_ADAPTER_DESC d{};
    return adapter && SUCCEEDED(adapter->GetDesc(&d)) && d.VendorId == 0x10DE;
}

class Dlss final : public Upscaler {
public:
    ~Dlss() override {
        if (made_) slFreeResources(sl::kFeatureDLSS, view_);
    }
    Kind kind() const override { return kDlss; }
    std::string Describe() const override { return version_; }

    bool Create(Quality q, uint32_t ow, uint32_t oh, bool hdr, std::string& why) {
        opt_.mode = q == kNative ? sl::DLSSMode::eDLAA
                    : q == kQuality ? sl::DLSSMode::eMaxQuality
                    : q == kBalanced ? sl::DLSSMode::eBalanced
                    : q == kPerformance ? sl::DLSSMode::eMaxPerformance
                                        : sl::DLSSMode::eUltraPerformance;
        opt_.outputWidth = ow;
        opt_.outputHeight = oh;
        opt_.colorBuffersHDR = hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        opt_.useAutoExposure = sl::Boolean::eTrue;
        if (slDLSSSetOptions(view_, opt_) != sl::Result::eOk) {
            why = "DLSS did not take its settings";
            return false;
        }
        sl::FeatureVersion v{};
        version_ = "DLSS";
        if (slGetFeatureVersion(sl::kFeatureDLSS, v) == sl::Result::eOk)
            version_ = "DLSS " + std::to_string(v.versionNGX.major) + "." + std::to_string(v.versionNGX.minor) + "." +
                       std::to_string(v.versionNGX.build);
        made_ = true;
        return true;
    }

    bool Evaluate(const Frame& f) override {
        sl::FrameToken* token = nullptr;
        uint32_t index = frame_++;
        if (slGetNewFrameToken(token, &index) != sl::Result::eOk || !token) return false;
        sl::Constants c{};
        // identity camera matrices: DLSS Super Resolution works from the motion vectors and depth alone
        for (sl::float4x4* m : {&c.cameraViewToClip, &c.clipToCameraView, &c.clipToLensClip, &c.clipToPrevClip, &c.prevClipToClip})
            for (uint32_t i = 0; i < 4; i++) (*m)[i] = sl::float4(i == 0, i == 1, i == 2, i == 3);
        c.jitterOffset = {f.jitter_x, f.jitter_y};
        c.mvecScale = {1.0f, 1.0f};  // already UV units
        c.cameraPinholeOffset = {0, 0};
        c.cameraPos = {0, 0, 0};
        c.cameraUp = {0, 1, 0};
        c.cameraRight = {1, 0, 0};
        c.cameraFwd = {0, 0, 1};
        c.cameraNear = 0.1f;
        c.cameraFar = 1000.0f;
        c.cameraFOV = 1.0f;
        c.cameraAspectRatio = float(f.out_w) / float(std::max(f.out_h, 1u));
        c.motionVectorsInvalidValue = 0.0f;
        c.depthInverted = sl::Boolean::eFalse;
        c.cameraMotionIncluded = sl::Boolean::eTrue;
        c.motionVectors3D = sl::Boolean::eFalse;
        c.reset = f.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        c.orthographicProjection = sl::Boolean::eFalse;
        c.motionVectorsDilated = sl::Boolean::eFalse;
        c.motionVectorsJittered = sl::Boolean::eFalse;
        if (slSetConstants(c, *token, view_) != sl::Result::eOk) {
            static int logged = 0;
            if (logged++ < 3) LOGR("upscaler: DLSS did not take the frame's settings; the normal picture instead");
            return false;
        }
        sl::Extent in{0, 0, f.render_w, f.render_h}, out{0, 0, f.out_w, f.out_h};
        sl::Resource color(sl::ResourceType::eTex2d, f.color, nullptr, nullptr, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        sl::Resource depth(sl::ResourceType::eTex2d, f.depth, nullptr, nullptr, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        sl::Resource mv(sl::ResourceType::eTex2d, f.motion, nullptr, nullptr, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        sl::Resource output(sl::ResourceType::eTex2d, f.output, nullptr, nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        sl::Resource react(sl::ResourceType::eTex2d, f.reactive, nullptr, nullptr, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // the HUD and text over the picture: DLSS's "bias current colour" hint, so they're taken from this frame
        sl::ResourceTag tags[] = {
            sl::ResourceTag{&color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &in},
            sl::ResourceTag{&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &in},
            sl::ResourceTag{&mv, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &in},
            sl::ResourceTag{&output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &out},
            sl::ResourceTag{&react, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilEvaluate, &in},
        };
        auto* cmd = reinterpret_cast<sl::CommandBuffer*>(f.cl);
        if (slSetTagForFrame(*token, view_, tags, f.reactive ? 5 : 4, cmd) != sl::Result::eOk) {
            static int logged = 0;
            if (logged++ < 3) LOGR("upscaler: DLSS did not take the frame's resources; the normal picture instead");
            return false;
        }
        const sl::BaseStructure* inputs[] = {&view_};
        sl::Result r = slEvaluateFeature(sl::kFeatureDLSS, *token, inputs, 1, cmd);
        if (r != sl::Result::eOk) {
            static int logged = 0;
            if (logged++ < 3) LOGR("upscaler: DLSS did not run (%d); the normal picture instead", int(r));
            return false;
        }
        return true;
    }

private:
    sl::ViewportHandle view_{0};
    sl::DLSSOptions opt_{};
    std::string version_;
    uint32_t frame_ = 0;
    bool made_ = false;
};

}  // namespace

bool InitStreamline(IDXGIAdapter* adapter, std::string& why) {
    if (g_sl_ready) return true;
#if defined(_M_ARM64) || defined(__aarch64__)
    why = g_sl_why = "DLSS is not made for Windows on ARM";
    return false;
#endif
    auto dll = Beside(L"sl.interposer.dll");
    if (!Exists(dll) || !Exists(Beside(L"sl.dlss.dll")) || !Exists(Beside(L"nvngx_dlss.dll"))) {
        why = g_sl_why = "the DLSS files (sl.interposer.dll, sl.dlss.dll, nvngx_dlss.dll) are not beside the game";
        return false;
    }
    if (!IsNvidia(adapter)) {
        why = g_sl_why = "DLSS needs an NVIDIA RTX graphics card";
        return false;
    }
    // NVIDIA's signature on the file, as the Streamline guide asks before loading it
    if (!sl::security::verifyEmbeddedSignature(dll.c_str())) {
        why = g_sl_why = "sl.interposer.dll is not signed by NVIDIA";
        return false;
    }
    g_sl = LoadLibraryW(dll.c_str());
    if (!g_sl) {
        why = g_sl_why = "sl.interposer.dll could not be loaded";
        return false;
    }
    static const sl::Feature features[] = {sl::kFeatureDLSS};
    static std::wstring plugins = Folder(L"sl.interposer.dll").wstring();
    static const wchar_t* paths[] = {plugins.c_str()};
    sl::Preferences p{};
    p.showConsole = false;
    p.logLevel = sl::LogLevel::eDefault;
    p.logMessageCallback = SlLog;
    p.pathsToPlugins = paths;
    p.numPathsToPlugins = 1;
    p.featuresToLoad = features;
    p.numFeaturesToLoad = 1;
    // manual hooking: the renderer's own device and swap chain; OTA lets the NVIDIA driver bring newer DLSS models
    // frame-based tagging: slSetTagForFrame refuses the frame's resources without it (Streamline 2.14), and DLSS was then
    // never run (1.5)
    p.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eAllowOTA | sl::PreferenceFlags::eLoadDownloadedPlugins |
              sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    p.engine = sl::EngineType::eCustom;
    p.engineVersion = "Athelas";
    p.projectId = "a7d3b1e2-5c4f-4e8a-9b6d-41f0c2e8a915";
    p.renderAPI = sl::RenderAPI::eD3D12;
    sl::Result r = slInit(p, sl::kSDKVersion);
    if (r != sl::Result::eOk) {
        why = g_sl_why = "Streamline did not start (" + std::to_string(int(r)) + ")";
        FreeLibrary(g_sl);
        g_sl = nullptr;
        return false;
    }
    g_sl_ready = true;
    return true;
}

namespace {
bool g_sl_device = false;
}

// right after the device is made: Streamline's Present bookkeeping needs it before the swap chain is upgraded, else it
// logs an error at every frame until the first upscaled one
bool SetStreamlineDevice(ID3D12Device* dev) {
    if (!g_sl_ready || g_sl_device) return g_sl_device;
    if (slSetD3DDevice(dev) != sl::Result::eOk) return false;
    g_sl_device = true;
    return true;
}

void UpgradeSwapChain(IDXGISwapChain** swap) {
    if (g_sl_ready && swap && *swap && slUpgradeInterface(reinterpret_cast<void**>(swap)) != sl::Result::eOk)
        LOGW("DLSS: the swap chain was not upgraded");
}

void ShutdownStreamline() {
    if (g_sl_ready) slShutdown();
    g_sl_ready = false;
}

std::unique_ptr<Upscaler> Create(Kind k, Quality q, ID3D12Device* dev, ID3D12CommandQueue*, IDXGIAdapter* adapter,
                                 uint32_t ow, uint32_t oh, uint32_t rw, uint32_t rh, bool hdr, std::string& why) {
#if defined(_M_ARM64) || defined(__aarch64__)
    // AMD's and NVIDIA's files are x64 only
    why = "the upscalers are not made for Windows on ARM";
    return nullptr;
#endif
    if (k == kFsr) {
        auto f = std::make_unique<Fsr>();
        if (!f->Create(dev, ow, oh, rw, rh, hdr, why)) return nullptr;
        return f;
    }
    if (k == kDlss) {
        if (!g_sl_ready) {
            why = g_sl_why;
            return nullptr;
        }
        if (!SetStreamlineDevice(dev)) {
            why = "DLSS could not use the graphics device";
            return nullptr;
        }
        DXGI_ADAPTER_DESC ad{};
        if (adapter) adapter->GetDesc(&ad);
        sl::AdapterInfo info{};
        info.deviceLUID = reinterpret_cast<uint8_t*>(&ad.AdapterLuid);
        info.deviceLUIDSizeInBytes = sizeof(LUID);
        sl::Result sup = slIsFeatureSupported(sl::kFeatureDLSS, info);
        if (sup != sl::Result::eOk) {
            why = sup == sl::Result::eErrorDriverOutOfDate ? "DLSS needs a newer NVIDIA driver"
                                                            : "DLSS is not supported on this graphics card (it needs an NVIDIA RTX card)";
            return nullptr;
        }
        auto d = std::make_unique<Dlss>();
        if (!d->Create(q, ow, oh, hdr, why)) return nullptr;
        return d;
    }
    why = "off";
    return nullptr;
}

// ------------------------------------------------------------------------------------- FSR frame generation

namespace {

ffxContext g_fg_swap = nullptr;  // FSR's swap chain (made at start)
ffxContext g_fg = nullptr;       // frame generation, made at the first 3D frame (again when the sizes change)
IDXGISwapChain4* g_fg_chain = nullptr;
uint32_t g_fg_key[6] = {};       // render w, h, screen w, h, hdr, format: made for these
uint64_t g_fg_frame = 0;         // FSR's frame number: one more at each prepared frame, else its history starts over
bool g_fg_on = false;            // generation on at the last configure
bool g_fg_failed = false;

ffxReturnCode_t FgDispatch(ffxDispatchDescFrameGeneration* params, void* user) {
    return g_ffx_fn.Dispatch(reinterpret_cast<ffxContext*>(user), &params->header);
}

bool FgConfigure(bool on, const FgFrame* f) {
    ffxConfigureDescFrameGeneration c{};
    c.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    c.swapChain = g_fg_chain;
    c.presentCallback = nullptr;  // FSR copies the frame to the screen itself
    c.frameGenerationCallback = FgDispatch;
    c.frameGenerationCallbackUserContext = &g_fg;
    c.frameGenerationEnabled = on;
    c.allowAsyncWorkloads = false;
    c.flags = std::getenv("XBE_FG_DEBUG") ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES | FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES : 0;
    c.onlyPresentGenerated = false;
    if (f) c.generationRect = {int32_t(f->vx), int32_t(f->vy), int32_t(f->vw), int32_t(f->vh)};
    c.frameID = g_fg_frame;
    if (g_ffx_fn.Configure(&g_fg, &c.header) != FFX_API_RETURN_OK) return false;
    g_fg_on = on;
    return true;
}

// FSR's own present queue may still be making a frame with the context: waited for first (review, 1.7.5)
void FgWaitPresents() {
    if (!g_fg_swap) return;
    ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 w{};
    w.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
    g_ffx_fn.Dispatch(&g_fg_swap, &w.header);
}

void FgDestroy() {
    if (g_fg) {
        if (g_fg_on) FgConfigure(false, nullptr);
        FgWaitPresents();
        g_ffx_fn.DestroyContext(&g_fg, nullptr);
    }
    g_fg = nullptr;
    g_fg_on = false;
}

}  // namespace

bool FgCreateSwapChain(HWND hwnd, DXGI_SWAP_CHAIN_DESC1& desc, IDXGIFactory* factory, ID3D12CommandQueue* queue,
                       IDXGISwapChain4** out, std::string& why) {
    const app::Settings& st = app::Get();
    if (!st.frame_generation) {
        why = "off";
        return false;
    }
#if defined(_M_ARM64) || defined(__aarch64__)
    why = "frame generation is not made for Windows on ARM";
    return false;
#endif
    if (st.upscaler != kFsr) {
        why = "frame generation works with the AMD FSR upscaler: choose it under Upscaler";
        return false;
    }
    if (!Exists(Beside(L"amd_fidelityfx_framegeneration_dx12.dll"))) {
        why = "amd_fidelityfx_framegeneration_dx12.dll is not beside the game";
        return false;
    }
    if (!LoadFfx(why)) return false;
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 d{};
    d.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    d.swapchain = out;
    d.hwnd = hwnd;
    d.desc = &desc;
    d.fullscreenDesc = nullptr;
    d.dxgiFactory = factory;
    d.gameQueue = queue;
    ffxReturnCode_t r = g_ffx_fn.CreateContext(&g_fg_swap, &d.header, nullptr);
    if (r != FFX_API_RETURN_OK || !*out) {
        why = "FSR's swap chain could not be made (error " + std::to_string(r) + ")";
        g_fg_swap = nullptr;
        return false;
    }
    g_fg_chain = *out;
    return true;
}

bool FgActive() { return g_fg_swap != nullptr; }

bool FgPrepare(ID3D12Device* dev, const FgFrame& f) {
    if (!g_fg_swap || g_fg_failed) return false;
    uint32_t key[6] = {f.render_w, f.render_h, f.bb_w, f.bb_h, f.hdr ? 1u : 0u, uint32_t(f.bb_fmt)};
    if (!g_fg || std::memcmp(key, g_fg_key, sizeof key) != 0) {
        FgDestroy();  // (the caller has the GPU idle when the sizes change: a new swap chain size or render size)
        ffxCreateBackendDX12Desc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.device = dev;
        ffxCreateContextDescFrameGeneration cd{};
        cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        cd.header.pNext = &backend.header;
        cd.flags = f.hdr ? FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0;
        cd.displaySize = {f.bb_w, f.bb_h};
        cd.maxRenderSize = {f.render_w, f.render_h};
        cd.backBufferFormat = ffxApiGetSurfaceFormatDX12(f.bb_fmt);
        ffxReturnCode_t r = g_ffx_fn.CreateContext(&g_fg, &cd.header, nullptr);
        if (r != FFX_API_RETURN_OK) {
            LOGR("frame generation: FSR's could not start (error %u); frames shown as drawn", unsigned(r));
            g_fg = nullptr;
            g_fg_failed = true;
            return false;
        }
        std::memcpy(g_fg_key, key, sizeof key);
        std::string v = "FSR frame generation";
        ffxQueryGetProviderVersion pv{};
        pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (g_ffx_fn.Query(&g_fg, &pv.header) == FFX_API_RETURN_OK && pv.versionName) v += std::string(" ") + pv.versionName;
        LOGR("frame generation: %s, %ux%u drawn, %ux%u shown", v.c_str(), f.render_w, f.render_h, f.bb_w, f.bb_h);
    }
    g_fg_frame++;
    if (!FgConfigure(true, &f)) {
        static int logged = 0;
        if (logged++ < 3) LOGR("frame generation: FSR did not take its settings");
        return false;
    }
    ffxDispatchDescFrameGenerationPrepareV2 d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    d.frameID = g_fg_frame;
    d.commandList = f.cl;
    d.renderSize = {f.render_w, f.render_h};
    d.jitterOffset = {f.jitter_x, f.jitter_y};
    d.motionVectorScale = {float(f.render_w), float(f.render_h)};  // UV units, as the upscaler takes them
    d.frameTimeDelta = f.dt_ms;
    d.reset = f.reset;
    d.cameraNear = 0.1f;
    d.cameraFar = 1000.0f;
    d.cameraFovAngleVertical = 1.0f;
    d.viewSpaceToMetersFactor = 1.0f;
    d.depth = ffxApiGetResourceDX12(f.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    d.motionVectors = ffxApiGetResourceDX12(f.motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    d.cameraUp[1] = 1.0f;
    d.cameraRight[0] = 1.0f;
    d.cameraForward[2] = 1.0f;
    ffxReturnCode_t r = g_ffx_fn.Dispatch(&g_fg, &d.header);
    if (r != FFX_API_RETURN_OK) {
        static int logged = 0;
        if (logged++ < 3) LOGR("frame generation: FSR did not run (%u)", unsigned(r));
        FgConfigure(false, nullptr);
        return false;
    }
    return true;
}

void FgPause() {
    if (g_fg && g_fg_on) FgConfigure(false, nullptr);
}

void FgShutdown() {
    FgDestroy();
    FgWaitPresents();
}

void FgReleaseSwapChain() {
    if (g_fg_swap) g_ffx_fn.DestroyContext(&g_fg_swap, nullptr);
    g_fg_swap = nullptr;
    g_fg_chain = nullptr;
}

}  // namespace gpu::up

// ------------------------------------------------------------------------------- Streamline's API, from its DLL
// sl.h declares these extern "C"; they are defined here as calls into sl.interposer.dll, loaded at run time, so the
// game starts without the DLSS files (and the sl_dlss.h helpers, which go through slGetFeatureFunction, work as well)
using gpu::up::SlFn;

#define SL_FORWARD(name, sig, call, ...)                                     \
    extern "C" sl::Result name sig {                                         \
        static auto* f = SlFn<PFun_##name>(#name);                           \
        return f ? f call : sl::Result::eErrorNotInitialized;                \
    }

SL_FORWARD(slInit, (const sl::Preferences& pref, uint64_t sdkVersion), (pref, sdkVersion))
SL_FORWARD(slShutdown, (), ())
SL_FORWARD(slIsFeatureSupported, (sl::Feature feature, const sl::AdapterInfo& adapterInfo), (feature, adapterInfo))
SL_FORWARD(slIsFeatureLoaded, (sl::Feature feature, bool& loaded), (feature, loaded))
SL_FORWARD(slSetFeatureLoaded, (sl::Feature feature, bool loaded), (feature, loaded))
SL_FORWARD(slSetTagForFrame,
           (const sl::FrameToken& frame, const sl::ViewportHandle& viewport, const sl::ResourceTag* resources, uint32_t numResources,
            sl::CommandBuffer* cmdBuffer),
           (frame, viewport, resources, numResources, cmdBuffer))
SL_FORWARD(slSetConstants, (const sl::Constants& values, const sl::FrameToken& frame, const sl::ViewportHandle& viewport),
           (values, frame, viewport))
SL_FORWARD(slGetFeatureRequirements, (sl::Feature feature, sl::FeatureRequirements& requirements), (feature, requirements))
SL_FORWARD(slGetFeatureVersion, (sl::Feature feature, sl::FeatureVersion& version), (feature, version))
SL_FORWARD(slAllocateResources, (sl::CommandBuffer* cmdBuffer, sl::Feature feature, const sl::ViewportHandle& viewport),
           (cmdBuffer, feature, viewport))
SL_FORWARD(slFreeResources, (sl::Feature feature, const sl::ViewportHandle& viewport), (feature, viewport))
SL_FORWARD(slEvaluateFeature,
           (sl::Feature feature, const sl::FrameToken& frame, const sl::BaseStructure** inputs, uint32_t numInputs,
            sl::CommandBuffer* cmdBuffer),
           (feature, frame, inputs, numInputs, cmdBuffer))
SL_FORWARD(slUpgradeInterface, (void** baseInterface), (baseInterface))
SL_FORWARD(slGetNativeInterface, (void* proxyInterface, void** baseInterface), (proxyInterface, baseInterface))
SL_FORWARD(slGetFeatureFunction, (sl::Feature feature, const char* functionName, void*& function), (feature, functionName, function))
SL_FORWARD(slGetNewFrameToken, (sl::FrameToken * &token, const uint32_t* frameIndex), (token, frameIndex))
SL_FORWARD(slSetD3DDevice, (void* d3dDevice), (d3dDevice))
