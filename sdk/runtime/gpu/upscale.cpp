// FSR, DLSS and XeSS behind gpu::up (upscale.h). All are loaded from DLLs beside the game at run time:
//   FSR:  amd_fidelityfx_loader_dx12.dll + amd_fidelityfx_upscaler_dx12.dll (AMD FSR SDK 2.4: FSR 4.1 where the card
//         and driver have it, FSR 3.1 otherwise; the loader picks), amd_fidelityfx_framegeneration_dx12.dll for
//         frame generation
//   DLSS: sl.interposer.dll, sl.common.dll, sl.dlss.dll, nvngx_dlss.dll (NVIDIA Streamline 2.14, DLSS 4.5), used with
//         manual hooking: only the sl* calls go through Streamline, the renderer keeps its own device and queue; for
//         frame generation sl.dlss_g.dll, nvngx_dlssg.dll, sl.reflex.dll and sl.pcl.dll as well
//   XeSS: libxess.dll (Intel XeSS 3.0), libxess_fg.dll + libxell.dll for frame generation. Called through
//         GetProcAddress, as the SDK's import libraries would make the game need the files to start
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
#include "../../third_party/streamline/sl_dlss_g.h"
#include "../../third_party/streamline/sl_reflex.h"
#include "../../third_party/streamline/sl_pcl.h"
// NVIDIA's signature check needs the Windows 8 WinTrust structures, which MinGW's headers (the Linux test builds)
// lack; the release builds (Visual Studio's Windows SDK) check
#if defined(__MINGW32__)
namespace sl::security {
inline bool verifyEmbeddedSignature(const wchar_t*) { return true; }
}
#else
#include "../../third_party/streamline/sl_security.h"
#endif

// (MinGW's d3d12.h, the Linux test builds, lacks the pipeline library interface XeSS's structures name; only a pointer
// to it is used, left null)
#if defined(__MINGW32__)
struct ID3D12PipelineLibrary;
#endif
#include "../../third_party/xess/xess/xess_d3d12.h"
#include "../../third_party/xess/xess_fg/xefg_swapchain_d3d12.h"
#include "../../third_party/xess/xell/xell_d3d12.h"

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

const char* KindName(Kind k) { return k == kFsr ? "FSR" : k == kDlss ? "DLSS" : k == kXess ? "XeSS" : "off"; }

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

// which frame generation made the swap chain (FgCreateSwapChain)
enum FgKind { kFgNone, kFgFsr, kFgXess, kFgDlss };
FgKind g_fg_kind = kFgNone;
// the frame number XeLL's markers, XeFG's present id and Streamline's frame token share; FgMark moves it on at each
// Present
uint32_t g_frame_id = 1;

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
bool g_sl_fg = false;  // DLSS-G, Reflex and PCL were loaded with DLSS
std::string g_sl_fg_why = "DLSS frame generation was not started";
LUID g_sl_luid{};

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

// the resources' state as the renderer leaves them for the upscalers (d3d12.cpp, Upscale)
constexpr uint32_t kSlRead = uint32_t(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) | uint32_t(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

// the frame's constants for DLSS and DLSS-G: identity camera matrices, as the console games' camera is unknown (DLSS
// works from the motion vectors and depth alone)
sl::Constants SlIdentityConstants(float jx, float jy, float aspect, bool reset) {
    sl::Constants c{};
    for (sl::float4x4* m : {&c.cameraViewToClip, &c.clipToCameraView, &c.clipToLensClip, &c.clipToPrevClip, &c.prevClipToClip})
        for (uint32_t i = 0; i < 4; i++) (*m)[i] = sl::float4(i == 0, i == 1, i == 2, i == 3);
    c.jitterOffset = {jx, jy};
    c.mvecScale = {1.0f, 1.0f};  // already UV units
    c.cameraPinholeOffset = {0, 0};
    c.cameraPos = {0, 0, 0};
    c.cameraUp = {0, 1, 0};
    c.cameraRight = {1, 0, 0};
    c.cameraFwd = {0, 0, 1};
    c.cameraNear = 0.1f;
    c.cameraFar = 1000.0f;
    c.cameraFOV = 1.0f;
    c.cameraAspectRatio = aspect;
    c.motionVectorsInvalidValue = 0.0f;
    c.depthInverted = sl::Boolean::eFalse;
    c.cameraMotionIncluded = sl::Boolean::eTrue;
    c.motionVectors3D = sl::Boolean::eFalse;
    c.reset = reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    c.orthographicProjection = sl::Boolean::eFalse;
    c.motionVectorsDilated = sl::Boolean::eFalse;
    c.motionVectorsJittered = sl::Boolean::eFalse;
    return c;
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
        // with DLSS-G the frame's token is the one its Reflex and PCL markers carry (FgMark's frame number)
        uint32_t index = g_fg_kind == kFgDlss ? g_frame_id : frame_++;
        if (slGetNewFrameToken(token, &index) != sl::Result::eOk || !token) return false;
        sl::Constants c = SlIdentityConstants(f.jitter_x, f.jitter_y, float(f.out_w) / float(std::max(f.out_h, 1u)), f.reset);
        if (slSetConstants(c, *token, view_) != sl::Result::eOk) {
            static int logged = 0;
            if (logged++ < 3) LOGR("upscaler: DLSS did not take the frame's settings; the normal picture instead");
            return false;
        }
        sl::Extent in{0, 0, f.render_w, f.render_h}, out{0, 0, f.out_w, f.out_h};
        sl::Resource color(sl::ResourceType::eTex2d, f.color, nullptr, nullptr, kSlRead);
        sl::Resource depth(sl::ResourceType::eTex2d, f.depth, nullptr, nullptr, kSlRead);
        sl::Resource mv(sl::ResourceType::eTex2d, f.motion, nullptr, nullptr, kSlRead);
        sl::Resource output(sl::ResourceType::eTex2d, f.output, nullptr, nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        sl::Resource react(sl::ResourceType::eTex2d, f.reactive, nullptr, nullptr, kSlRead);
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

// ------------------------------------------------------------------------------------------------------------ XeSS

HMODULE g_xess = nullptr;
struct {
    decltype(&xessGetVersion) GetVersion;
    decltype(&xessD3D12CreateContext) CreateContext;
    decltype(&xessD3D12Init) Init;
    decltype(&xessD3D12Execute) Execute;
    decltype(&xessDestroyContext) DestroyContext;
    decltype(&xessSetVelocityScale) SetVelocityScale;
    decltype(&xessSetLoggingCallback) SetLoggingCallback;
    decltype(&xessIsOptimalDriver) IsOptimalDriver;
    decltype(&xessGetOptimalInputResolution) GetOptimalInputResolution;
} g_xess_fn{};

template <typename F>
bool Get(HMODULE m, F& f, const char* name) {
    f = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
    return f != nullptr;
}

bool LoadXess(std::string& why) {
    if (g_xess_fn.Execute) return true;
    auto dll = Beside(L"libxess.dll");
    if (!Exists(dll)) {
        why = "the XeSS file (libxess.dll) is not beside the game";
        return false;
    }
    if (!g_xess) g_xess = LoadLibraryW(dll.c_str());
    if (!g_xess) {
        why = "libxess.dll could not be loaded (it needs the Microsoft Visual C++ 2015-2022 runtime)";
        return false;
    }
    auto& f = g_xess_fn;
    if (!Get(g_xess, f.GetVersion, "xessGetVersion") || !Get(g_xess, f.CreateContext, "xessD3D12CreateContext") ||
        !Get(g_xess, f.Init, "xessD3D12Init") || !Get(g_xess, f.DestroyContext, "xessDestroyContext") ||
        !Get(g_xess, f.SetVelocityScale, "xessSetVelocityScale") || !Get(g_xess, f.SetLoggingCallback, "xessSetLoggingCallback") ||
        !Get(g_xess, f.IsOptimalDriver, "xessIsOptimalDriver") ||
        !Get(g_xess, f.GetOptimalInputResolution, "xessGetOptimalInputResolution") || !Get(g_xess, f.Execute, "xessD3D12Execute")) {
        why = "libxess.dll lacks the XeSS functions";
        g_xess_fn = {};
        return false;
    }
    return true;
}

void XessLog(const char* message, xess_logging_level_t level) {
    if (level >= XESS_LOGGING_LEVEL_ERROR) LOGE("XeSS: %s", message ? message : "");
    else LOGW("XeSS: %s", message ? message : "");
}

class Xess final : public Upscaler {
public:
    ~Xess() override {
        if (ctx_) g_xess_fn.DestroyContext(ctx_);
    }
    Kind kind() const override { return kXess; }
    std::string Describe() const override { return version_; }

    bool Create(Quality q, ID3D12Device* dev, uint32_t ow, uint32_t oh, uint32_t rw, uint32_t rh, bool hdr, std::string& why) {
        if (!LoadXess(why)) return false;
        xess_result_t r = g_xess_fn.CreateContext(dev, &ctx_);
        if (r != XESS_RESULT_SUCCESS || !ctx_) {
            why = "XeSS could not start on this graphics card (error " + std::to_string(int(r)) + ")";
            ctx_ = nullptr;
            return false;
        }
        g_xess_fn.SetLoggingCallback(ctx_, XESS_LOGGING_LEVEL_WARNING, XessLog);
        // an older Intel driver runs the slower general path (XeSS still works)
        xess_result_t drv = g_xess_fn.IsOptimalDriver(ctx_);
        LOGR("upscaler: XeSS driver check: %s (%d)", drv == XESS_RESULT_SUCCESS ? "optimal" : "not optimal, a newer driver may be faster", int(drv));
        xess_d3d12_init_params_t p{};
        p.outputResolution = {ow, oh};
        // XeSS's presets (since 1.3) by the same ratios as ours (Ratio): its Ultra quality is 1.5x, Quality 1.7x,
        // Balanced 2.0x, Ultra performance 3.0x; Native is its anti-aliasing at full size. The render size is passed
        // at every frame as well, so the preset only picks XeSS's range and tuning.
        p.qualitySetting = q == kNative ? XESS_QUALITY_SETTING_AA
                           : q == kQuality ? XESS_QUALITY_SETTING_ULTRA_QUALITY
                           : q == kBalanced ? XESS_QUALITY_SETTING_QUALITY
                           : q == kPerformance ? XESS_QUALITY_SETTING_BALANCED
                                               : XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
        // the console frame is gamma encoded 0..1 (XeSS's LDR input, exposure 1) unless the HDR output made it scRGB,
        // when XeSS works the exposure out itself. The HUD mask is XeSS's responsive pixel mask (the renderer leaves
        // it out only for a test, XBE_UP_NO_REACTIVE, d3d12.cpp)
        p.initFlags = hdr ? XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE : XESS_INIT_FLAG_LDR_INPUT_COLOR;
        mask_ = !std::getenv("XBE_UP_NO_REACTIVE");
        if (mask_) p.initFlags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
        r = g_xess_fn.Init(ctx_, &p);
        if (r != XESS_RESULT_SUCCESS) {
            why = "XeSS did not take its settings (error " + std::to_string(int(r)) + ")";
            return false;
        }
        // our render size against the range XeSS gives this preset: once, as a hint when the picture looks wrong
        xess_2d_t out{ow, oh}, opt{}, mn{}, mx{};
        if (g_xess_fn.GetOptimalInputResolution(ctx_, &out, p.qualitySetting, &opt, &mn, &mx) == XESS_RESULT_SUCCESS &&
            (rw != opt.x || rh != opt.y)) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                LOGW("upscaler: XeSS would draw this preset at %ux%u (from %ux%u to %ux%u), the game draws %ux%u", opt.x, opt.y,
                     mn.x, mn.y, mx.x, mx.y, rw, rh);
            }
        }
        // motion is in UV units, XeSS takes pixels at the render size (from the current pixel to the earlier one,
        // as ours)
        g_xess_fn.SetVelocityScale(ctx_, float(rw), float(rh));
        xess_version_t v{};
        version_ = "XeSS";
        if (g_xess_fn.GetVersion(&v) == XESS_RESULT_SUCCESS)
            version_ = "XeSS " + std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
        return true;
    }

    // (jitter phases: the base class's 8 * ratio^2, the sequence length Intel's guide asks for)

    bool Evaluate(const Frame& f) override {
        xess_d3d12_execute_params_t e{};
        e.pColorTexture = f.color;
        e.pVelocityTexture = f.motion;
        e.pDepthTexture = f.depth;  // R24G8_TYPELESS: XeSS reads it as R24_UNORM_X8
        e.pResponsivePixelMaskTexture = mask_ ? f.reactive : nullptr;
        e.pOutputTexture = f.output;
        // the jitter as FSR's: pixels at the render size, added to the projection as +2x/w, -2y/h
        e.jitterOffsetX = f.jitter_x;
        e.jitterOffsetY = f.jitter_y;
        e.exposureScale = 1.0f;
        e.resetHistory = f.reset ? 1 : 0;
        e.inputWidth = f.render_w;
        e.inputHeight = f.render_h;
        xess_result_t r = g_xess_fn.Execute(ctx_, f.cl, &e);
        if (r != XESS_RESULT_SUCCESS) {
            static int logged = 0;
            if (logged++ < 3) LOGR("upscaler: XeSS did not run (%d); the normal picture instead", int(r));
            return false;
        }
        return true;
    }

private:
    xess_context_handle_t ctx_ = nullptr;
    bool mask_ = false;
    std::string version_;
};

}  // namespace

bool InitStreamline(IDXGIAdapter* adapter, std::string& why) {
    if (g_sl_ready) return true;
#if defined(_M_ARM64) || defined(__aarch64__)
    why = g_sl_why = "DLSS is not made for Windows on ARM";
    return false;
#endif
    // Wine / Proton (Linux, the Steam Deck): Streamline's driver checks and signature checks don't work there, so it is
    // not tried at all
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"); nt && GetProcAddress(nt, "wine_get_version")) {
        why = g_sl_why = "DLSS is not used under Wine or Proton";
        return false;
    }
    auto dll = Beside(L"sl.interposer.dll");
    if (!Exists(dll) || !Exists(Beside(L"sl.common.dll")) || !Exists(Beside(L"sl.dlss.dll")) || !Exists(Beside(L"nvngx_dlss.dll"))) {
        why = g_sl_why = "the DLSS files (sl.interposer.dll, sl.common.dll, sl.dlss.dll, nvngx_dlss.dll) are not beside the game";
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
    // DLSS frame generation as well when it is asked for and its files are there: DLSS-G, with Reflex and PC latency
    // markers, which it needs (loaded only then, as Streamline hooks the swap chain more deeply for it)
    static const sl::Feature features[] = {sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL, sl::kFeatureDLSS_G};
    uint32_t count = 1;
    g_sl_fg = false;
    if (app::Get().frame_generation) {
        if (Exists(Beside(L"sl.dlss_g.dll")) && Exists(Beside(L"nvngx_dlssg.dll")) && Exists(Beside(L"sl.reflex.dll")) &&
            Exists(Beside(L"sl.pcl.dll"))) {
            count = 4;
            g_sl_fg = true;
        } else {
            g_sl_fg_why = "the DLSS frame generation files (sl.dlss_g.dll, nvngx_dlssg.dll, sl.reflex.dll, sl.pcl.dll) are not beside the game";
        }
    }
    DXGI_ADAPTER_DESC ad{};
    if (SUCCEEDED(adapter->GetDesc(&ad))) g_sl_luid = ad.AdapterLuid;
    static std::wstring plugins = Folder(L"sl.interposer.dll").wstring();
    static const wchar_t* paths[] = {plugins.c_str()};
    sl::Preferences p{};
    p.showConsole = false;
    p.logLevel = sl::LogLevel::eDefault;
    p.logMessageCallback = SlLog;
    p.pathsToPlugins = paths;
    p.numPathsToPlugins = 1;
    p.featuresToLoad = features;
    p.numFeaturesToLoad = count;
    // manual hooking: the renderer's own device and swap chain; no over-the-air updates or downloaded plugins (the
    // files shipped are the ones tested)
    // frame-based tagging: slSetTagForFrame refuses the frame's resources without it (Streamline 2.14), and DLSS was then
    // never run (1.5)
    p.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
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

std::unique_ptr<Upscaler> Create(Kind k, Quality q, ID3D12Device* dev, ID3D12CommandQueue*, IDXGIAdapter* adapter,
                                 uint32_t ow, uint32_t oh, uint32_t rw, uint32_t rh, bool hdr, std::string& why) {
#if defined(_M_ARM64) || defined(__aarch64__)
    // AMD's, NVIDIA's and Intel's files are x64 only
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
    if (k == kXess) {
        // (Intel's, but it runs on any DirectX 12 card with Shader Model 6.4; the slower general path off Intel)
        auto x = std::make_unique<Xess>();
        if (!x->Create(q, dev, ow, oh, rw, rh, hdr, why)) return nullptr;
        return x;
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

bool FsrFgCreate(HWND hwnd, DXGI_SWAP_CHAIN_DESC1& desc, IDXGIFactory* factory, ID3D12CommandQueue* queue,
                 IDXGISwapChain4** out, std::string& why) {
    if (app::Get().frame_generation_frames > 2)
        LOGR("frame generation: FSR makes one frame between each two drawn (2x); %ux is for XeSS on Intel cards and DLSS",
             app::Get().frame_generation_frames);
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

bool FsrFgPrepare(ID3D12Device* dev, const FgFrame& f) {
    if (!g_fg_swap || g_fg_failed) return false;
    uint32_t key[6] = {f.render_w, f.render_h, f.bb_w, f.bb_h, f.hdr ? 1u : 0u, uint32_t(f.bb_fmt)};
    if (!g_fg || std::memcmp(key, g_fg_key, sizeof key) != 0) {
        FgDestroy();  // (the caller has the GPU idle when the sizes change: a new swap chain size or render size)
        ffxCreateBackendDX12Desc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.device = dev;
        // the API version built against, linked in: AMD's header says new APIs don't work without it
        ffxCreateContextDescFrameGenerationVersion ver{};
        ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
        ver.header.pNext = &backend.header;
        ver.version = FFX_FRAMEGENERATION_VERSION;
        ffxCreateContextDescFrameGeneration cd{};
        cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        cd.header.pNext = &ver.header;
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

void FsrFgPause() {
    if (g_fg && g_fg_on) FgConfigure(false, nullptr);
}

void FsrFgShutdown() {
    FgDestroy();
    FgWaitPresents();
}

void FsrFgRelease() {
    if (g_fg_swap) g_ffx_fn.DestroyContext(&g_fg_swap, nullptr);
    g_fg_swap = nullptr;
    g_fg_chain = nullptr;
}

// how many frames the game's one becomes: [graphics] frame_generation_frames, 2x..4x
uint32_t FramesWanted() { return std::clamp(app::Get().frame_generation_frames, 2u, 4u); }

// ------------------------------------------------------------------------------- XeSS frame generation (XeFG)
// Intel's proxy swap chain makes the frames at Present from the depth and motion tagged for that frame; XeLL (Intel's
// latency reduction) must be on for it, timed by FgMark's markers with the same frame number as XeFG's present id.

HMODULE g_xefg_dll = nullptr, g_xell_dll = nullptr;
struct {
    decltype(&xefgSwapChainGetVersion) GetVersion;
    decltype(&xefgSwapChainD3D12CreateContext) CreateContext;
    decltype(&xefgSwapChainSetLoggingCallback) SetLoggingCallback;
    decltype(&xefgSwapChainSetLatencyReduction) SetLatencyReduction;
    decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) InitFromSwapChainDesc;
    decltype(&xefgSwapChainD3D12GetSwapChainPtr) GetSwapChainPtr;
    decltype(&xefgSwapChainGetProperties) GetProperties;
    decltype(&xefgSwapChainSetNumInterpolatedFrames) SetNumInterpolatedFrames;
    decltype(&xefgSwapChainD3D12TagFrameResource) TagFrameResource;
    decltype(&xefgSwapChainTagFrameConstants) TagFrameConstants;
    decltype(&xefgSwapChainSetEnabled) SetEnabled;
    decltype(&xefgSwapChainSetPresentId) SetPresentId;
    decltype(&xefgSwapChainDestroy) Destroy;
} g_xefg_fn{};
struct {
    decltype(&xellD3D12CreateContext) CreateContext;
    decltype(&xellDestroyContext) DestroyContext;
    decltype(&xellSetSleepMode) SetSleepMode;
    decltype(&xellSleep) Sleep;
    decltype(&xellAddMarkerData) AddMarkerData;
} g_xell_fn{};
xefg_swapchain_handle_t g_xefg = nullptr;
xell_context_handle_t g_xell = nullptr;
bool g_xefg_on = false;  // generation enabled (off for frames without 3D)
uint32_t g_xefg_frames = 2;  // 2x..4x as set

void XefgLog(const char* message, xefg_swapchain_logging_level_t level, void*) {
    if (level >= XEFG_SWAPCHAIN_LOGGING_LEVEL_ERROR) LOGE("XeSS frame generation: %s", message ? message : "");
    else LOGW("XeSS frame generation: %s", message ? message : "");
}

bool LoadXefg(std::string& why) {
    if (g_xefg_fn.SetPresentId && g_xell_fn.AddMarkerData) return true;
    auto fg = Beside(L"libxess_fg.dll"), ll = Beside(L"libxell.dll");
    if (!Exists(fg) || !Exists(ll)) {
        why = "the XeSS frame generation files (libxess_fg.dll, libxell.dll) are not beside the game";
        return false;
    }
    if (!g_xell_dll) g_xell_dll = LoadLibraryW(ll.c_str());
    if (!g_xefg_dll) g_xefg_dll = LoadLibraryW(fg.c_str());
    if (!g_xell_dll || !g_xefg_dll) {
        why = "libxess_fg.dll or libxell.dll could not be loaded (they need the Microsoft Visual C++ 2015-2022 runtime)";
        return false;
    }
    auto& f = g_xefg_fn;
    HMODULE m = g_xefg_dll;
    bool ok = Get(m, f.GetVersion, "xefgSwapChainGetVersion") && Get(m, f.CreateContext, "xefgSwapChainD3D12CreateContext") &&
              Get(m, f.SetLoggingCallback, "xefgSwapChainSetLoggingCallback") &&
              Get(m, f.SetLatencyReduction, "xefgSwapChainSetLatencyReduction") &&
              Get(m, f.InitFromSwapChainDesc, "xefgSwapChainD3D12InitFromSwapChainDesc") &&
              Get(m, f.GetSwapChainPtr, "xefgSwapChainD3D12GetSwapChainPtr") && Get(m, f.GetProperties, "xefgSwapChainGetProperties") &&
              Get(m, f.SetNumInterpolatedFrames, "xefgSwapChainSetNumInterpolatedFrames") &&
              Get(m, f.TagFrameResource, "xefgSwapChainD3D12TagFrameResource") &&
              Get(m, f.TagFrameConstants, "xefgSwapChainTagFrameConstants") && Get(m, f.SetEnabled, "xefgSwapChainSetEnabled") &&
              Get(m, f.Destroy, "xefgSwapChainDestroy") && Get(m, f.SetPresentId, "xefgSwapChainSetPresentId");
    auto& l = g_xell_fn;
    m = g_xell_dll;
    ok = ok && Get(m, l.CreateContext, "xellD3D12CreateContext") && Get(m, l.DestroyContext, "xellDestroyContext") &&
         Get(m, l.SetSleepMode, "xellSetSleepMode") && Get(m, l.Sleep, "xellSleep") && Get(m, l.AddMarkerData, "xellAddMarkerData");
    if (!ok) {
        why = "libxess_fg.dll or libxell.dll lacks the XeSS frame generation functions";
        g_xefg_fn = {};
        g_xell_fn = {};
        return false;
    }
    return true;
}

void XefgDestroyContexts() {
    // (XeFG first: it holds XeLL's context, Intel's guide)
    if (g_xefg) g_xefg_fn.Destroy(g_xefg);
    if (g_xell) g_xell_fn.DestroyContext(g_xell);
    g_xefg = nullptr;
    g_xell = nullptr;
    g_xefg_on = false;
}

bool XefgCreate(HWND hwnd, DXGI_SWAP_CHAIN_DESC1& desc, IDXGIFactory* factory, ID3D12Device* dev, ID3D12CommandQueue* queue,
                IDXGISwapChain4** out, std::string& why) {
    // XeFG takes HDR10 (R10G10B10A2) only, not the scRGB (16-bit float) picture the HDR output uses
    if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        why = "XeSS frame generation does not take the HDR picture (scRGB): turn HDR off to use it";
        return false;
    }
    if (!LoadXefg(why)) return false;
    xell_result_t lr = g_xell_fn.CreateContext(dev, &g_xell);
    if (lr != XELL_RESULT_SUCCESS || !g_xell) {
        why = "XeLL (Intel's latency reduction, which XeSS frame generation needs) could not start (error " + std::to_string(int(lr)) + ")";
        g_xell = nullptr;
        return false;
    }
    // XeLL's low latency mode on before generation is: the CPU waits at the start of a frame (FgMark) so the frames
    // XeFG holds back don't add up to lag
    xell_sleep_params_t sp{};
    sp.bLowLatencyMode = 1;
    sp.minimumIntervalUs = 0;  // no frame cap of its own: the game paces itself
    if (g_xell_fn.SetSleepMode(g_xell, &sp) != XELL_RESULT_SUCCESS) LOGR("frame generation: XeLL did not take low latency mode");
    xefg_swapchain_result_t r = g_xefg_fn.CreateContext(dev, &g_xefg);
    if (r != XEFG_SWAPCHAIN_RESULT_SUCCESS || !g_xefg) {
        why = "XeSS frame generation is not supported on this graphics card (error " + std::to_string(int(r)) +
              "; it needs DirectX 12 with Shader Model 6.4)";
        g_xefg = nullptr;
        XefgDestroyContexts();
        return false;
    }
    g_xefg_fn.SetLoggingCallback(g_xefg, XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING, XefgLog, nullptr);
    g_xefg_fn.SetLatencyReduction(g_xefg, g_xell);
    IDXGIFactory2* f2 = nullptr;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&f2)))) {
        why = "no DXGI 1.2 factory";
        XefgDestroyContexts();
        return false;
    }
    xefg_swapchain_d3d12_init_params_t ip{};
    // motion vectors at the render size, in pixels after the scale given each frame; depth not inverted; the HUD is
    // left to XeFG's model (UI composition off, Intel's recommended default)
    ip.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;
    ip.maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
    ip.uiMode = XEFG_SWAPCHAIN_UI_MODE_AUTO;
    r = g_xefg_fn.InitFromSwapChainDesc(g_xefg, hwnd, &desc, nullptr, queue, f2, &ip);
    f2->Release();
    if (r != XEFG_SWAPCHAIN_RESULT_SUCCESS || FAILED(g_xefg_fn.GetSwapChainPtr(g_xefg, IID_PPV_ARGS(out))) || !*out) {
        why = "XeSS frame generation's swap chain could not be made (error " + std::to_string(int(r)) + ")";
        *out = nullptr;
        XefgDestroyContexts();
        return false;
    }
    // 2x..4x: at most what the card does (one made-up frame off Intel's cards, by Intel's rules)
    xefg_swapchain_properties_t props{};
    uint32_t want = FramesWanted() - 1, n = want;
    if (g_xefg_fn.GetProperties(g_xefg, &props) == XEFG_SWAPCHAIN_RESULT_SUCCESS && props.maxSupportedInterpolations)
        n = std::min(want, props.maxSupportedInterpolations);
    if (n != want)
        LOGR("frame generation: XeSS makes at most %u frame(s) between each two drawn on this card: %ux instead of %ux", n,
             n + 1, want + 1);
    g_xefg_fn.SetNumInterpolatedFrames(g_xefg, n);
    g_xefg_frames = n + 1;
    xefg_swapchain_version_t v{};
    if (g_xefg_fn.GetVersion(&v) == XEFG_SWAPCHAIN_RESULT_SUCCESS)
        LOGR("frame generation: XeSS frame generation %u.%u.%u, %ux", v.major, v.minor, v.patch, n + 1);
    return true;
}

bool XefgPrepare(const FgFrame& f) {
    if (!g_xefg_on) {
        if (g_xefg_fn.SetEnabled(g_xefg, 1) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            static int logged = 0;
            if (logged++ < 3) LOGR("frame generation: XeSS's could not be turned on");
            return false;
        }
        g_xefg_on = true;
    }
    // depth and motion are left alone until this frame's Present (the next frame draws after it), so no copies
    xefg_swapchain_d3d12_resource_data_t d{};
    d.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
    d.resourceBase = {0, 0};
    d.resourceSize = {f.render_w, f.render_h};
    d.incomingState = D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    d.type = XEFG_SWAPCHAIN_RES_DEPTH;
    d.pResource = f.depth;
    bool ok = g_xefg_fn.TagFrameResource(g_xefg, f.cl, g_frame_id, &d) == XEFG_SWAPCHAIN_RESULT_SUCCESS;
    d.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR;
    d.pResource = f.motion;
    ok = ok && g_xefg_fn.TagFrameResource(g_xefg, f.cl, g_frame_id, &d) == XEFG_SWAPCHAIN_RESULT_SUCCESS;
    // the picture in the middle of the screen (bars at the sides or top): only that is made up, the rest copied
    if (ok && (f.vw != f.bb_w || f.vh != f.bb_h)) {
        xefg_swapchain_d3d12_resource_data_t bb{};
        bb.type = XEFG_SWAPCHAIN_RES_BACKBUFFER;  // (only the area is read: Intel's guide, Letterboxing)
        bb.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
        bb.resourceBase = {f.vx, f.vy};
        bb.resourceSize = {f.vw, f.vh};
        ok = g_xefg_fn.TagFrameResource(g_xefg, f.cl, g_frame_id, &bb) == XEFG_SWAPCHAIN_RESULT_SUCCESS;
    }
    // the console games' camera is unknown: identity view and projection (as DLSS is told), the motion vectors carry
    // the movement
    xefg_swapchain_frame_constant_data_t c{};
    for (int i = 0; i < 4; i++) c.viewMatrix[i * 5] = c.projectionMatrix[i * 5] = 1.0f;
    c.jitterOffsetX = f.jitter_x;
    c.jitterOffsetY = f.jitter_y;
    c.motionVectorScaleX = float(f.render_w);  // UV units: XeFG takes pixels
    c.motionVectorScaleY = float(f.render_h);
    c.resetHistory = f.reset ? 1 : 0;
    c.frameRenderTime = f.dt_ms;
    ok = ok && g_xefg_fn.TagFrameConstants(g_xefg, g_frame_id, &c) == XEFG_SWAPCHAIN_RESULT_SUCCESS;
    if (!ok) {
        static int logged = 0;
        if (logged++ < 3) LOGR("frame generation: XeSS did not take the frame's depth and motion");
    }
    return ok;
}

void XefgPause() {
    if (g_xefg && g_xefg_on) g_xefg_fn.SetEnabled(g_xefg, 0);
    g_xefg_on = false;
}

// --------------------------------------------------------------------------- DLSS frame generation (DLSS-G)
// Streamline's DLSS-G plugin makes the frames at Present through its swap chain (made through Streamline's factory),
// from the depth and motion tagged for the frame and the constants DLSS's evaluation set (same frame token, same
// viewport). Reflex must be on with it, timed by the PCL markers FgMark sends.

const sl::ViewportHandle kSlView{0};  // the viewport class Dlss uses
sl::DLSSGOptions g_dlssg_opt{};
bool g_dlssg_on = false;
uint32_t g_dlssg_frames = 1;        // made-up frames per drawn one asked of Streamline
uint32_t g_dlssg_checked = 0;       // prepared frames since the state was last read
sl::DLSSGStatus g_dlssg_status = sl::DLSSGStatus::eOk;
bool g_dlssg_logged = false;
bool g_dlssg_failed = false;        // Streamline refused the options: no more tries (no retry at every frame)
uint32_t g_dlssg_bufs = 0;          // the swap chain's buffer count
uint32_t g_dlssg_max = 0;           // Streamline's numFramesToGenerateMax (0: not known)

sl::FrameToken* SlToken() {
    sl::FrameToken* t = nullptr;
    uint32_t id = g_frame_id;
    return slGetNewFrameToken(t, &id) == sl::Result::eOk ? t : nullptr;
}

bool DlssgCreate(HWND hwnd, DXGI_SWAP_CHAIN_DESC1& desc, IDXGIFactory* factory, ID3D12CommandQueue* queue,
                 IDXGISwapChain4** out, std::string& why) {
    if (!g_sl_ready) {
        why = g_sl_why;
        return false;
    }
    if (!g_sl_fg) {
        why = g_sl_fg_why;
        return false;
    }
    bool loaded = false;
    if (slIsFeatureLoaded(sl::kFeatureDLSS_G, loaded) != sl::Result::eOk || !loaded) {
        why = "Streamline did not load DLSS frame generation (sl.dlss_g.dll)";
        return false;
    }
    // Streamline's own answer for this card, logged as it is: no check of our own on the card's generation, so a tool
    // that changes Streamline's answer is not second-guessed here
    sl::AdapterInfo info{};
    info.deviceLUID = reinterpret_cast<uint8_t*>(&g_sl_luid);
    info.deviceLUIDSizeInBytes = sizeof(LUID);
    sl::Result sup = slIsFeatureSupported(sl::kFeatureDLSS_G, info);
    LOGR("frame generation: Streamline says DLSS frame generation is %s on this graphics card (result %d)",
         sup == sl::Result::eOk ? "supported" : "not supported", int(sup));
    if (sup != sl::Result::eOk) {
        why = sup == sl::Result::eErrorDriverOutOfDate ? "DLSS frame generation needs a newer NVIDIA driver"
              : sup == sl::Result::eErrorOSOutOfDate
                  ? "DLSS frame generation needs a newer Windows with hardware-accelerated GPU scheduling on"
                  : "DLSS frame generation is not supported on this graphics card (NVIDIA: RTX 40 and 50 series)";
        return false;
    }
    sl::Result rx = slIsFeatureSupported(sl::kFeatureReflex, info);
    if (rx != sl::Result::eOk) {
        why = "Reflex, which DLSS frame generation needs, is not supported here (result " + std::to_string(int(rx)) + ")";
        return false;
    }
    // the swap chain made through Streamline's factory, so DLSS-G can present its own frames (the factory proxy is
    // given the reference added here and released with it)
    IDXGIFactory* proxy = factory;
    proxy->AddRef();
    if (slUpgradeInterface(reinterpret_cast<void**>(&proxy)) != sl::Result::eOk) {
        proxy->Release();
        why = "Streamline could not take over the DXGI factory";
        return false;
    }
    IDXGIFactory2* f2 = nullptr;
    IDXGISwapChain1* sc1 = nullptr;
    HRESULT hr = proxy->QueryInterface(IID_PPV_ARGS(&f2));
    if (SUCCEEDED(hr)) hr = f2->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &sc1);
    if (SUCCEEDED(hr)) hr = sc1->QueryInterface(IID_PPV_ARGS(out));
    if (sc1) sc1->Release();
    if (f2) f2->Release();
    proxy->Release();
    if (FAILED(hr) || !*out) {
        why = "Streamline's swap chain could not be made (" + std::to_string(long(hr)) + ")";
        *out = nullptr;
        return false;
    }
    // Reflex on: DLSS-G refuses to run without it (DLSSGStatus::eFailReflexNotDetectedAtRuntime); the markers FgMark
    // sends are used to time the frame
    sl::ReflexOptions ro{};
    ro.mode = sl::ReflexMode::eLowLatency;
    ro.useMarkersToOptimize = true;
    if (slReflexSetOptions(ro) != sl::Result::eOk) LOGR("frame generation: Reflex did not start; DLSS frame generation may stay off");
    g_dlssg_bufs = desc.BufferCount;
    // how many frames the card makes, before the first options: more than that is asked as the most it makes
    g_dlssg_frames = FramesWanted() - 1;
    sl::DLSSGState st{};
    if (slDLSSGGetState(kSlView, st, nullptr) == sl::Result::eOk) {
        g_dlssg_max = st.numFramesToGenerateMax;
        LOGR("frame generation: DLSS frame generation: %ux asked, up to %ux on this card (status %08X)", g_dlssg_frames + 1,
             g_dlssg_max + 1, unsigned(st.status));
        if (g_dlssg_max && g_dlssg_frames > g_dlssg_max) {
            g_dlssg_frames = g_dlssg_max;
            LOGR("frame generation: DLSS makes %ux on this card", g_dlssg_frames + 1);
        }
    } else {
        LOGR("frame generation: DLSS frame generation's state could not be read; %ux asked", g_dlssg_frames + 1);
    }
    g_dlssg_logged = true;
    return true;
}

bool DlssgSetOptions(const FgFrame* f) {
    g_dlssg_opt.mode = f ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    g_dlssg_opt.numFramesToGenerate = g_dlssg_frames;
    // (kept between menus: frames without 3D turn it off and on again often)
    g_dlssg_opt.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
    if (f) {
        g_dlssg_opt.mvecDepthWidth = f->render_w;
        g_dlssg_opt.mvecDepthHeight = f->render_h;
        g_dlssg_opt.colorWidth = f->bb_w;
        g_dlssg_opt.colorHeight = f->bb_h;
        g_dlssg_opt.colorBufferFormat = uint32_t(f->bb_fmt);
        g_dlssg_opt.numBackBuffers = g_dlssg_bufs;
        g_dlssg_opt.mvecBufferFormat = uint32_t(DXGI_FORMAT_R16G16_FLOAT);
        g_dlssg_opt.depthBufferFormat = uint32_t(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);  // (R24G8_TYPELESS read as depth)
    }
    if (slDLSSGSetOptions(kSlView, g_dlssg_opt) != sl::Result::eOk) return false;
    g_dlssg_on = f != nullptr;
    return true;
}

// what DLSS-G says of itself: logged when it changes (the reason it makes no frames, most often)
void DlssgCheckState(bool log_always = false) {
    sl::DLSSGState st{};
    if (slDLSSGGetState(kSlView, st, &g_dlssg_opt) != sl::Result::eOk) return;
    g_dlssg_max = st.numFramesToGenerateMax;
    if (log_always)
        LOGR("frame generation: DLSS-G status %08X, %ux of up to %ux, %u frames shown since the last look", unsigned(st.status),
             g_dlssg_frames + 1, g_dlssg_max + 1, st.numFramesActuallyPresented);
    if (st.status != g_dlssg_status) {
        g_dlssg_status = st.status;
        auto has = [&](sl::DLSSGStatus b) { return (uint32_t(st.status) & uint32_t(b)) != 0; };
        if (st.status == sl::DLSSGStatus::eOk) LOGR("frame generation: DLSS frame generation running");
        else
            LOGR("frame generation: DLSS frame generation reports a problem (%08X)%s%s%s%s", unsigned(st.status),
                 has(sl::DLSSGStatus::eFailResolutionTooLow) ? ": the screen size is too small" : "",
                 has(sl::DLSSGStatus::eFailReflexNotDetectedAtRuntime) ? ": Reflex is not running" : "",
                 has(sl::DLSSGStatus::eFailHDRFormatNotSupported) ? ": the HDR picture's format is not taken" : "",
                 has(sl::DLSSGStatus::eFailCommonConstantsInvalid) ? ": the frame's constants were refused" : "");
    }
}

bool DlssgPrepare(const FgFrame& f) {
    if (g_dlssg_failed) return false;
    sl::FrameToken* token = SlToken();
    if (!token) return false;
    if (!g_dlssg_on || g_dlssg_opt.mvecDepthWidth != f.render_w || g_dlssg_opt.mvecDepthHeight != f.render_h ||
        g_dlssg_opt.colorWidth != f.bb_w || g_dlssg_opt.colorHeight != f.bb_h) {
        if (!DlssgSetOptions(&f)) {
            // once refused, it would be refused at every frame: off for this run, said once
            g_dlssg_failed = true;
            LOGR("frame generation: DLSS did not take its settings; DLSS frame generation off until the next start");
            return false;
        }
    }
    // the frame's constants set here as well, not only by DLSS's evaluation: the same identity set, for the same
    // frame token and viewport (an upscaler changed in the game leaves none set)
    sl::Constants c = SlIdentityConstants(f.jitter_x, f.jitter_y, float(f.vw) / float(std::max(f.vh, 1u)), f.reset);
    if (slSetConstants(c, *token, kSlView) != sl::Result::eOk) {
        static int logged = 0;
        if (logged++ < 3) LOGR("frame generation: DLSS did not take the frame's constants");
        return false;
    }
    // depth and motion kept for DLSS-G until Present; the picture's place on the screen every frame (an area only,
    // no resource)
    sl::Extent in{0, 0, f.render_w, f.render_h}, shown{f.vy, f.vx, f.vw, f.vh};  // (top, left, width, height)
    sl::Resource depth(sl::ResourceType::eTex2d, f.depth, nullptr, nullptr, kSlRead);
    sl::Resource mv(sl::ResourceType::eTex2d, f.motion, nullptr, nullptr, kSlRead);
    sl::ResourceTag tags[] = {
        sl::ResourceTag{&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &in},
        sl::ResourceTag{&mv, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &in},
        sl::ResourceTag{nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent, &shown},
    };
    if (slSetTagForFrame(*token, kSlView, tags, 3, reinterpret_cast<sl::CommandBuffer*>(f.cl)) != sl::Result::eOk) {
        static int logged = 0;
        if (logged++ < 3) LOGR("frame generation: DLSS did not take the frame's depth and motion");
        return false;
    }
    if (++g_dlssg_checked >= 120) {
        g_dlssg_checked = 0;
        DlssgCheckState();
    }
    return true;
}

void DlssgPause() {
    if (g_dlssg_on) DlssgSetOptions(nullptr);
}

// --------------------------------------------------------------------------------------- latency markers
bool g_frame_begun = false;

void XellMark(xell_latency_marker_type_t m) { g_xell_fn.AddMarkerData(g_xell, g_frame_id, m); }

void SlMark(sl::PCLMarker m) {
    if (sl::FrameToken* t = SlToken()) slPCLSetMarker(m, *t);
}

// a frame's start: XeLL / Reflex wait here when the GPU is behind, so the game reads the controls later
void BeginFrame() {
    g_frame_begun = true;
    if (g_fg_kind == kFgXess && g_xell) {
        g_xell_fn.Sleep(g_xell, g_frame_id);
        XellMark(XELL_SIMULATION_START);
    } else if (g_fg_kind == kFgDlss) {
        if (sl::FrameToken* t = SlToken()) {
            slReflexSleep(*t);
            slPCLSetMarker(sl::PCLMarker::eSimulationStart, *t);
        }
    }
}

}  // namespace

bool FgCreateSwapChain(HWND hwnd, DXGI_SWAP_CHAIN_DESC1& desc, IDXGIFactory* factory, ID3D12Device* dev,
                       ID3D12CommandQueue* queue, IDXGISwapChain4** out, std::string& why) {
    const app::Settings& st = app::Get();
    if (!st.frame_generation) {
        why = "off";
        return false;
    }
#if defined(_M_ARM64) || defined(__aarch64__)
    why = "frame generation is not made for Windows on ARM";
    return false;
#endif
    // each kind only with its own upscaler: it is readied from the depth, motion and jitter that upscaler was given,
    // on the frames it upscaled (XeFG's API would take another upscaler's, but FSR's and DLSS's don't: one rule)
    bool ok = false;
    FgKind kind = kFgNone;
    if (st.upscaler == kFsr) {
        kind = kFgFsr;
        ok = FsrFgCreate(hwnd, desc, factory, queue, out, why);
    } else if (st.upscaler == kXess) {
        kind = kFgXess;
        ok = XefgCreate(hwnd, desc, factory, dev, queue, out, why);
    } else if (st.upscaler == kDlss) {
        kind = kFgDlss;
        ok = DlssgCreate(hwnd, desc, factory, queue, out, why);
    } else {
        why = "frame generation goes with an upscaler: choose AMD FSR, Intel XeSS or NVIDIA DLSS under Upscaler";
    }
    if (ok) g_fg_kind = kind;
    return ok;
}

bool FgActive() { return g_fg_kind != kFgNone; }

const char* FgName() {
    return g_fg_kind == kFgFsr ? "AMD FSR" : g_fg_kind == kFgXess ? "Intel XeSS" : g_fg_kind == kFgDlss ? "NVIDIA DLSS" : "off";
}

namespace {
// for the log line every 600 presents
uint32_t g_fg_prepared = 0, g_fg_paused = 0, g_fg_resets = 0, g_fg_presents = 0;
bool g_fg_last_on = false;

FgKind KindOf(Kind k) { return k == kFsr ? kFgFsr : k == kXess ? kFgXess : k == kDlss ? kFgDlss : kFgNone; }
}  // namespace

bool FgPrepare(ID3D12Device* dev, const FgFrame& f) {
    // the upscaler changed in the game: this frame generation's inputs are not what it was made for, so it rests
    if (KindOf(f.upscaler) != g_fg_kind) {
        FgPause();
        return false;
    }
    if (f.reset) g_fg_resets++;
    bool ok = false;
    switch (g_fg_kind) {
        case kFgFsr: ok = FsrFgPrepare(dev, f); break;
        case kFgXess: ok = XefgPrepare(f); break;
        case kFgDlss: ok = DlssgPrepare(f); break;
        default: break;
    }
    g_fg_last_on = ok;
    if (ok) g_fg_prepared++;
    return ok;
}

bool FgPresentUnsynced() { return g_fg_kind == kFgDlss; }

void FgResize() {
    // DLSS-G off (back on at the new size with the next 3D frame, its history started again); FSR's context, made for
    // the old display size, destroyed once its presents are done (made again by FsrFgPrepare); XeFG paused, its
    // tagging command lists already finished by the caller (Intel's guide, Changing Window State)
    switch (g_fg_kind) {
        case kFgFsr: FsrFgShutdown(); break;
        case kFgXess: XefgPause(); break;
        case kFgDlss: DlssgPause(); break;
        default: break;
    }
}

void FgPause() {
    if (g_fg_kind != kFgNone) {
        g_fg_paused++;
        g_fg_last_on = false;
    }
    switch (g_fg_kind) {
        case kFgFsr: FsrFgPause(); break;
        case kFgXess: XefgPause(); break;
        case kFgDlss: DlssgPause(); break;
        default: break;
    }
}

void FgShutdown() {
    switch (g_fg_kind) {
        case kFgFsr: FsrFgShutdown(); break;
        case kFgXess: XefgPause(); break;  // (XeFG's work ends with its swap chain, FgReleaseSwapChain)
        case kFgDlss:
            DlssgPause();
            slFreeResources(sl::kFeatureDLSS_G, kSlView);
            break;
        default: break;
    }
}

void FgReleaseSwapChain() {
    // (the game's references to the swap chain gone: XeFG's destroy fails while any are left)
    if (g_fg_kind == kFgFsr) FsrFgRelease();
    else if (g_fg_kind == kFgXess) XefgDestroyContexts();
    g_fg_kind = kFgNone;
}

void FgMark(Mark m) {
    if (m == kPresentEnd && g_fg_kind != kFgNone && ++g_fg_presents % 600 == 0) {
        uint32_t frames = g_fg_kind == kFgDlss ? g_dlssg_frames + 1 : g_fg_kind == kFgFsr ? 2 : g_xefg_frames;
        LOGR("frame generation: %s, %s, %ux; last 600 presents: %u prepared, %u paused, %u resets", FgName(),
             g_fg_last_on ? "on" : "paused", frames, g_fg_prepared, g_fg_paused, g_fg_resets);
        g_fg_prepared = g_fg_paused = g_fg_resets = 0;
        if (g_fg_kind == kFgDlss && !g_dlssg_failed) DlssgCheckState(true);
    }
    if (g_fg_kind != kFgXess && g_fg_kind != kFgDlss) {
        if (m == kPresentEnd) g_frame_id++;
        return;
    }
    if (!g_frame_begun) BeginFrame();
    if (g_fg_kind == kFgXess) {
        if (!g_xell) return;
        switch (m) {
            case kSimEnd: XellMark(XELL_SIMULATION_END); break;
            case kSubmitStart: XellMark(XELL_RENDERSUBMIT_START); break;
            case kSubmitEnd: XellMark(XELL_RENDERSUBMIT_END); break;
            case kPresentStart:
                XellMark(XELL_PRESENT_START);
                g_xefg_fn.SetPresentId(g_xefg, g_frame_id);  // the frame's tagged depth and motion, by the same number
                break;
            case kPresentEnd: XellMark(XELL_PRESENT_END); break;
        }
    } else {
        switch (m) {
            case kSimEnd: SlMark(sl::PCLMarker::eSimulationEnd); break;
            case kSubmitStart: SlMark(sl::PCLMarker::eRenderSubmitStart); break;
            case kSubmitEnd: SlMark(sl::PCLMarker::eRenderSubmitEnd); break;
            case kPresentStart: SlMark(sl::PCLMarker::ePresentStart); break;
            case kPresentEnd: SlMark(sl::PCLMarker::ePresentEnd); break;
        }
    }
    if (m == kPresentEnd) {
        // the next frame starts here: the game's own work for it follows this Present
        g_frame_id++;
        BeginFrame();
    }
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
