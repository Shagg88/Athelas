// The title's Direct3D 8 calls drawn with Direct3D 12
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/ext.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "gpu/texture_pack.h"
#include "gpu/upscale.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"

using Microsoft::WRL::ComPtr;

namespace gpu {

namespace {

constexpr int kFrames = 3;
constexpr uint64_t kStaleFrames = 8;  // frames a target may go unused and still be read back
constexpr uint64_t kUploadSize = 64ull << 20;
// a draw starting with less than this left in the frame's ring waits for the GPU and starts the ring over
constexpr uint64_t kUploadReserve = 8ull << 20;
constexpr uint32_t kSrvCount = 3 * 32768;
constexpr uint32_t kSamplerCount = 2048;
constexpr uint32_t kRtvCount = 1024;
constexpr uint32_t kDsvCount = 256;
constexpr uint32_t kMotionSlots = 4;  // RTVs for the upscaler's motion targets, after the back buffers
constexpr DXGI_FORMAT kMotionFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;  // motion in xy, a the cut-out test
constexpr DXGI_FORMAT kBgra = DXGI_FORMAT_B8G8R8A8_UNORM;

// perf diagnostic: where a frame's time goes, logged once a second with diag::Perf
using PerfClock = std::chrono::steady_clock;
double PerfMs(PerfClock::time_point t) {
    return std::chrono::duration<double, std::milli>(PerfClock::now() - t).count();
}
struct Perf {
    // milliseconds
    double draw = 0;
    double assemble = 0;
    double bind = 0;
    double sig = 0;
    double texup = 0;
    double compile = 0;
    double clear = 0;
    double copy = 0;
    double present = 0;
    double wait = 0;
    double swap = 0;
    double sync = 0;
    double pso = 0;     // pipeline creation
    double create = 0;  // texture and target resources made
    double flush = 0;   // the upload ring filled mid-frame: submitted and waited for
    uint64_t flushes = 0;
    // counts and bytes
    uint64_t draws = 0;
    uint64_t verts = 0;
    uint64_t upload = 0;
    uint64_t overflow = 0;
    uint64_t sigs = 0;
    uint64_t sig_bytes = 0;
    uint64_t texups = 0;
    uint64_t texup_bytes = 0;
    uint64_t newtex = 0;
    uint64_t targets = 0;
    uint64_t rtups = 0;
    uint64_t shrinks = 0;
    uint64_t syncs = 0;
    uint64_t psos = 0;
    uint64_t shaders = 0;
    uint64_t raw = 0;
    uint64_t lates = 0;
};
Perf g_perf;
struct PerfScope {
    double& acc;
    PerfClock::time_point t = PerfClock::now();
    ~PerfScope() { acc += PerfMs(t); }
};

// F3 captures only with diag::CaptureDir set
bool CaptureOn() {
    if (diag::CaptureDir().empty()) LOG_FIRST(1, "capture: set XBE_CAPTURE_DIR");
    return !diag::CaptureDir().empty();
}

const char* kBlitHlsl = R"(
cbuffer B : register(b1) { float4 uvrect; float4 opts; float4 sharp; float4 hdr; float4 look; float4 look2; float4 shade; float4 light; };
Texture2D img : register(t0);
Texture2D lut : register(t1);
SamplerState ls : register(s0);
SamplerState ps : register(s1);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uvrect.xy + t * uvrect.zw;
    return o;
}
float4 psmain(O i) : SV_Target {
    float4 c = 0;
    int n = max((int)opts.z, 1);
    float2 px = fwidth(i.uv);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) c += img.SampleLevel(ls, i.uv + px * ((float2(x, y) + 0.5) / n - 0.5), 0);
    c /= n * n;
    // sharpening (sharp.x 0..1), contrast adaptive as AMD's CAS: the cross of source texels around this one sets how
    // much each neighbour is taken away, less where the contrast is already high, so edges get no haloes
    if (sharp.x > 0) {
        float2 t = sharp.yz;
        float3 a = img.SampleLevel(ls, i.uv + float2(0, -t.y), 0).rgb, b = img.SampleLevel(ls, i.uv + float2(-t.x, 0), 0).rgb;
        float3 d = img.SampleLevel(ls, i.uv + float2(t.x, 0), 0).rgb, e = img.SampleLevel(ls, i.uv + float2(0, t.y), 0).rgb;
        float3 mn = min(min(min(a, b), min(d, e)), c.rgb), mx = max(max(max(a, b), max(d, e)), c.rgb);
        float3 amp = sqrt(saturate(min(mn, 2.0 - mx) / max(mx, 1e-4)));
        float3 w = amp * (-1.0 / lerp(8.0, 5.0, sharp.x));
        c.rgb = saturate((c.rgb + (a + b + d + e) * w) / (1.0 + 4.0 * w));
    }
    c.rgb *= opts.w;
    if (opts.x > 0) c.rgb = float3(lut.Sample(ps, float2(c.r, 0.5)).r, lut.Sample(ps, float2(c.g, 0.5)).g, lut.Sample(ps, float2(c.b, 0.5)).b);
    // colour look (look2.y > 0, its strength), as ROTK Fix grades: a gamma, contrast about a low pivot (look.y) with a
    // soft shoulder, saturation look.z with vibrance look.w lifting dull colours most, a tint fading in over the
    // shadows (shade) and one over the lights (light)
    if (look2.y > 0) {
        float3 in0 = saturate(c.rgb);
        float3 x = look.y + (pow(in0, look2.x) - look.y) * look.x;
        x = max(x, 0);
        // 0.9 + 0.1 * tanh((x - 0.9) / 0.1), tanh spelled out: Wine's and Proton's HLSL compiler lacks it
        x = lerp(x, 0.9 + 0.1 * (1 - 2 / (exp(min((x - 0.9) / 0.05, 60)) + 1)), step(0.9, x));
        float l = dot(x, float3(0.2126, 0.7152, 0.0722));
        float mx = max(x.r, max(x.g, x.b)), mn = min(x.r, min(x.g, x.b));
        float sat = look.z * (1 + look.w * (1 - (mx - mn) / (mx + 1e-4)));
        float ws = (1 - l) * min(l * 6, 1);
        float3 v = l + (x - l) * sat + ws * shade.rgb + l * light.rgb;
        c.rgb = lerp(in0, saturate(v), look2.y);
    }
    // HDR (hdr.x): the SDR picture to linear light on an scRGB screen, normal scenes at paper white (hdr.y, in scRGB's
    // 80 nit units) and the brightest tops (fire, sky, glows) raised smoothly to the screen's peak (hdr.z times paper
    // white); the bright part is judged by the strongest channel, so saturated flames rise as well as white
    if (hdr.x > 0) {
        float3 lin = pow(saturate(c.rgb), 2.2);
        float mx = max(lin.r, max(lin.g, lin.b));
        float s = saturate((mx - 0.6) / 0.4);
        lin *= 1 + (hdr.z - 1) * s * s * s;
        c.rgb = lin * hdr.y;
    }
    return float4(c.rgb, opts.y > 0 ? c.a : 1);
}
)";

// a clear of some colour channels only (D3DCLEAR_TARGET_R/G/B/A): drawn, with the other channels masked off
const char* kClearHlsl = R"(
cbuffer B : register(b1) { float4 col; };
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 psmain() : SV_Target { return col; }
)";

// XBE_UP_SHOW_MOTION=1: the upscaler's motion vectors shown in place of the frame (grey still; red/green the
// horizontal/vertical motion, blue its size), to check them where no upscaler runs
const char* kMotionShowHlsl = R"(
Texture2D<float4> img : register(t0);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 psmain(O i) : SV_Target {
    if (i.uv.x < 0.5) discard;  // the picture on the left, its motion on the right
    uint w, h;
    img.GetDimensions(w, h);
    float2 m = img.Load(int3(min(i.uv * float2(w, h), float2(w - 1, h - 1)), 0)).xy;
    return float4(saturate(0.5 + m.x * 40), saturate(0.5 + m.y * 40), saturate(length(m) * 100), 1);
}
)";

const char* kDepthHlsl = R"(
Texture2D<float> img : register(t0);
SamplerState ps : register(s1);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float psmain(O i) : SV_Depth { return img.SampleLevel(ps, i.uv, 0); }
)";

// anti-aliasing: a target's pixels carried into its multisampled twin (every sample the pixel's), colour or depth,
// and a multisampled depth buffer resolved into the plain one (its first sample)
const char* kMsUpHlsl = R"(
Texture2D<float4> img : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 psmain(float4 p : SV_Position) : SV_Target { return img.Load(int3(p.xy, 0)); }
)";
const char* kMsUpDepthHlsl = R"(
Texture2D<float> img : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float psmain(float4 p : SV_Position) : SV_Depth { return img.Load(int3(p.xy, 0)); }
)";
const char* kMsDepthResolveHlsl = R"(
Texture2DMS<float> img : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float psmain(float4 p : SV_Position) : SV_Depth { return img.Load(int2(p.xy), 0); }
)";

float Bits(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void Color(uint32_t argb, float* o) {
    o[0] = ((argb >> 16) & 255) / 255.0f;
    o[1] = ((argb >> 8) & 255) / 255.0f;
    o[2] = (argb & 255) / 255.0f;
    o[3] = (argb >> 24) / 255.0f;
}

// D3DBLEND, the console uses GL values
constexpr uint32_t kBlendDestColor = 0x306;  // D3DBLEND_DESTCOLOR

D3D12_BLEND Blend(uint32_t v, bool alpha) {
    switch (v) {
        case 0: return D3D12_BLEND_ZERO;
        case 1: return D3D12_BLEND_ONE;
        case 0x300: return alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
        case 0x301: return alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
        case 0x302: return D3D12_BLEND_SRC_ALPHA;
        case 0x303: return D3D12_BLEND_INV_SRC_ALPHA;
        case 0x304: return D3D12_BLEND_DEST_ALPHA;
        case 0x305: return D3D12_BLEND_INV_DEST_ALPHA;
        case kBlendDestColor: return alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
        case 0x307: return alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
        case 0x308: return D3D12_BLEND_SRC_ALPHA_SAT;
        case 0x8001: case 0x8003: return D3D12_BLEND_BLEND_FACTOR;
        case 0x8002: case 0x8004: return D3D12_BLEND_INV_BLEND_FACTOR;
        default: return D3D12_BLEND_ONE;
    }
}

// D3DBLENDOP, GL values
D3D12_BLEND_OP BlendOp(uint32_t v) {
    switch (v) {
        case 0x800A: return D3D12_BLEND_OP_SUBTRACT;
        case 0x800B: return D3D12_BLEND_OP_REV_SUBTRACT;
        case 0x8007: return D3D12_BLEND_OP_MIN;
        case 0x8008: return D3D12_BLEND_OP_MAX;
        default: return D3D12_BLEND_OP_ADD;
    }
}

// D3DCMP_NEVER 0x200 to D3DCMP_ALWAYS 0x207, D3D12's order from 1
D3D12_COMPARISON_FUNC Cmp(uint32_t v) {
    uint32_t n = v >= 0x200 && v <= 0x207 ? v - 0x200 : 7;
    return D3D12_COMPARISON_FUNC(n + 1);
}

// D3DSTENCILOP, GL values
D3D12_STENCIL_OP StencilOp(uint32_t v) {
    switch (v) {
        case 0: return D3D12_STENCIL_OP_ZERO;
        case 0x1E01: return D3D12_STENCIL_OP_REPLACE;
        case 0x1E02: return D3D12_STENCIL_OP_INCR_SAT;
        case 0x1E03: return D3D12_STENCIL_OP_DECR_SAT;
        case 0x150A: return D3D12_STENCIL_OP_INVERT;
        case 0x8507: return D3D12_STENCIL_OP_INCR;
        case 0x8508: return D3D12_STENCIL_OP_DECR;
        default: return D3D12_STENCIL_OP_KEEP;
    }
}

// D3DTADDRESS: 1 wrap, 2 mirror, 3 clamp, 4 border, 5 clamp to edge
D3D12_TEXTURE_ADDRESS_MODE Address(uint32_t v) {
    switch (v) {
        case 2: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case 3: case 5: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case 4: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

DXGI_FORMAT HostFormat(Host h) {
    switch (h) {
        case Host::BC1: return DXGI_FORMAT_BC1_UNORM;
        case Host::BC2: return DXGI_FORMAT_BC2_UNORM;
        case Host::BC3: return DXGI_FORMAT_BC3_UNORM;
        case Host::R32F: return DXGI_FORMAT_R32_FLOAT;
        default: return DXGI_FORMAT_B8G8R8A8_UNORM;
    }
}

constexpr uint32_t kFmtD16 = 0x2C;     // D3DFMT_D16
constexpr uint32_t kFmtLinD16 = 0x30;  // D3DFMT_LIN_D16

// TexInfo::shadow of a depth texture
uint8_t ShadowDepthKind(uint32_t format) { return format == kFmtD16 || format == kFmtLinD16 ? 2 : 1; }

// bytes per row of a 32 bit surface in guest memory, rounded to 64
uint32_t GuestPitch(uint32_t width) { return (width * 4 + 63) & ~63u; }

// rows of a texture upload start D3D12_TEXTURE_DATA_PITCH_ALIGNMENT apart
uint32_t UploadPitch(uint32_t row_bytes) { return (row_bytes + 255) & ~255u; }

D3D12_RESOURCE_DESC BufferDesc(uint64_t bytes) {
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = bytes;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return bd;
}

// one level, one sample: callers set Flags where they differ
D3D12_RESOURCE_DESC Texture2DDesc(DXGI_FORMAT fmt, uint64_t w, uint32_t h) {
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w;
    td.Height = h;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    return td;
}

// MemSig hashes a surface up to kSigWholeMax bytes whole, a bigger one as evenly spaced samples
constexpr uint32_t kSigWholeMax = 65536;
constexpr uint32_t kSigSamples = 64;
constexpr uint32_t kSigSampleBytes = 64;

uint64_t MemSig(uint32_t data, uint32_t total) {
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(data));
    uint64_t sig = Hash(&total, sizeof(total));
    g_perf.sigs++;
    g_perf.sig_bytes += total <= kSigWholeMax ? total : kSigSamples * kSigSampleBytes;
    if (total <= kSigWholeMax) return Hash(p, total, sig);
    for (uint32_t i = 0; i < kSigSamples; i++) {
        const uint8_t* sample = p + uint64_t(total - kSigSampleBytes) * i / (kSigSamples - 1);
        sig = Hash(sample, kSigSampleBytes, sig);
    }
    return sig;
}

// MemSig samples a big surface and misses small writes (XFONT text): after a Sync every byte is compared
bool Changed(const Target& t) {
    if (t.drawn || t.read.empty()) return false;
    uint32_t row = t.gw * 4;
    uint32_t pitch = GuestPitch(t.gw);
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++) {
        if (std::memcmp(p + size_t(y) * pitch, &t.read[size_t(y) * row], row)) return true;
    }
    return false;
}

// a lock alone proves nothing, reads lock too: only changed bytes mean the CPU wrote it
bool LatchCpuWrite(Target& t) {
    uint64_t e = DirtyEpoch(t.data);
    if (e <= t.epoch) return false;
    t.epoch = e;
    uint64_t sig = MemSig(t.data, t.bytes);
    if (sig == t.sig && !Changed(t)) return false;
    t.sig = sig;
    return true;
}

struct Tex {
    ComPtr<ID3D12Resource> res;
    TextureDesc d;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    uint32_t levels = 1;
    uint64_t sig = 0;
    uint64_t frame = 0;
    uint64_t palette = 0;  // hash of the colours a P8 texture was decoded with
    uint8_t flags = 0;  // Extension::Decoded's
    // a texture pack's file drawn in its place (TexturePack), shared by every texture with the same contents
    ComPtr<ID3D12Resource> repl;
    DXGI_FORMAT repl_fmt = DXGI_FORMAT_UNKNOWN;
    uint32_t repl_levels = 0;
    uint64_t pack_hash = 0;  // the contents' hash a texture pack names it by, 0 if it cannot be in one
    // a game's own picture for this linear texture (g_builtin_contents), with the contents' hash it was made for:
    // drawn again while the game uploads the same contents (ROTK's menu pictures, every frame)
    ComPtr<ID3D12Resource> own;
    DXGI_FORMAT own_fmt = DXGI_FORMAT_UNKNOWN;
    uint32_t own_levels = 0;
    uint64_t own_hash = 0;
};

// a texture pack file loaded, or a failed load remembered
struct PackTex {
    ComPtr<ID3D12Resource> res;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    uint32_t levels = 0;
};

// a shader source from shaders.seed (see ReadShaderSeed)
struct SeedShader {
    std::string entry, profile, src;
};

struct PsoKey {
    uint64_t vs;
    uint64_t ps;
    uint32_t regs;
    uint32_t topo;
    uint32_t has_rt;
    uint32_t has_ds;
    uint32_t elements[16];  // per register: DXGI format, buffer << 8, offset << 16
    uint32_t blend;
    uint32_t src;
    uint32_t dst;
    uint32_t op;
    uint32_t wmask;
    uint32_t zen;
    uint32_t zwrite;
    uint32_t zfunc;
    uint32_t sten;
    uint32_t sfunc;
    uint32_t smask;
    uint32_t swmask;
    uint32_t sfail;
    uint32_t szfail;
    uint32_t spass;
    uint32_t cull;
    uint32_t ccw;
    uint32_t fill;
    int32_t zbias;
    float zslope;
    uint32_t rtfmt;
    uint32_t samples;  // anti-aliasing: the bound targets' sample count, 0 or 1 without
    uint32_t a2c;      // alpha to coverage for a cut-out (g_alpha_edges)
};

// Blit's settings past the source and its rectangle
struct BlitOptions {
    bool gamma = false;       // through the gamma lookup
    bool alpha = false;       // the source's alpha, else 1
    uint32_t taps = 1;        // n x n box filter per pixel
    float mul = 1;            // colour scale
    DXGI_FORMAT rtv = kBgra;  // the target's format
    bool over = false;        // premultiplied over the target, which keeps its alpha
    float sharpen = 0;        // 0..1, contrast adaptive sharpening; texel is the source's texel size then
    float texel[2] = {0, 0};
    bool hdr = false;         // to the HDR (scRGB) screen: linear, highlights raised (kBlitHlsl)
    bool look = false;        // through [graphics] colour_look
};

// the colour looks, ROTK Fix's own numbers: contrast, pivot, saturation, vibrance, gamma, shadow tint, light tint
struct ColourLook {
    float contrast, pivot, sat, vibrance, gamma, shadow[3], light[3];
};
constexpr ColourLook kLooks[] = {
    {1.00f, 0.30f, 1.00f, 0.00f, 1.00f, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}},     // original
    {1.12f, 0.30f, 1.06f, 0.15f, 0.97f, {-0.012f, 0.006f, 0.028f}, {0.045f, 0.018f, -0.030f}},   // film
    {1.08f, 0.30f, 1.30f, 0.35f, 0.93f, {0.000f, 0.000f, 0.000f}, {0.010f, 0.005f, 0.000f}},     // vivid
    {1.15f, 0.30f, 0.72f, 0.00f, 1.05f, {-0.020f, 0.000f, 0.025f}, {0.012f, 0.006f, -0.006f}},   // dark fantasy
};

// Shrink's settings past the source and the size
struct ShrinkOptions {
    uint32_t taps = 1;
    // into: a copy from an earlier call to draw over again, in state into_state
    ID3D12Resource* into = nullptr;
    D3D12_RESOURCE_STATES into_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    float mul = 1;
};

// a draw's four texture stages: BindStageTexturesAndTargets fills them, the constants, views and samplers read them
struct DrawTextures {
    TexInfo info[4];
    D3D12_SHADER_RESOURCE_VIEW_DESC sv[4];
    ID3D12Resource* res[4];
    bool rt[4] = {};            // stage samples a render target
    float k[4] = {1, 1, 1, 1};  // Extension::Sample's k
    float wide[4] = {};         // host texels per console texel of a target read into a smaller one, 0 otherwise
    float sharp[4] = {};        // kTexSharp's strength, 0 otherwise
    float sharp_texel[4][2] = {};  // kTexSharp: the bound picture's texel in uv
};

// a draw's shaders: what they are made from, their cache keys, the hashes Compile returned and the compiled blobs
struct DrawShaders {
    bool prog = false;  // a vertex program, else the fixed function pipeline
    bool quad = false;
    bool fixed_transform = false;  // fixed function on untransformed vertices: the shader reads matrices and lights
    FixedKey fk;
    uint64_t vkey = 0;
    uint64_t vh = 0;
    uint64_t pkey = 0;
    uint64_t ph = 0;
    ID3DBlob* vs_code = nullptr;
    ID3DBlob* ps_code = nullptr;
};

// the render target's console size in pixels, or the depth buffer's without one
struct GuestExtent {
    uint32_t w;
    uint32_t h;
};

class D3D12Backend final : public Backend, public Context {
public:
    ~D3D12Backend() override;
    bool Init(uint32_t width, uint32_t height) override;
    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
               uint32_t stencil) override;
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override;
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override;
    void Present(const Surface& front) override;
    void Sync(uint32_t data, uint32_t size) override;
    void SyncLate(uint32_t data) override;
    bool DeliverLate(uint32_t data) override;
    void RecordLate(uint32_t data) override;
    void Flush() override;
    void SetGamma(const uint8_t* ramp) override;
    void BeginQuery() override;
    void Screenshot(Target& t, const std::string& suffix = {});  // suffix: a pair's (F7) name after the game's
    int pair_step_ = 0;  // F7: 1 while the pair's second shot is due
    std::string pair_name_;
    uint8_t Brighten(uint8_t v) const;
    void EndQuery(uint32_t index) override;
    bool QueryResult(uint32_t index, uint32_t& pixels) override;
    bool Queries() const override { return !query_failed_; }

    ID3D12Device* Device() override { return dev_.Get(); }
    ID3D12GraphicsCommandList* List() override { return cl_.Get(); }
    void Release(ComPtr<ID3D12Resource> r) override { CurrentFrame().garbage.push_back(std::move(r)); }
    D3D12_CPU_DESCRIPTOR_HANDLE View(const Target& t) override { return Cpu(rtv_heap_.Get(), t.view, rtv_inc_); }
    void Unbind() override { bound_rt_ = bound_ds_ = nullptr; }
    void Capture(Target& t, const char* tag, float mul) override;
    float Scale() const override { return scale_; }
    void GuestSize(uint32_t& w, uint32_t& h) const override {
        w = guest_w_;
        h = guest_h_;
    }
    uint64_t Presents() const override { return presents_; }

private:
    struct Frame {
        ComPtr<ID3D12CommandAllocator> alloc;
        ComPtr<ID3D12Resource> upload;
        uint8_t* map = nullptr;
        uint64_t used = 0;
        uint64_t fence = 0;
        std::vector<ComPtr<ID3D12Resource>> garbage;
        uint32_t srv = 0;
    };
    struct Alloc {
        ID3D12Resource* res;
        uint64_t off;
        uint8_t* cpu;
        D3D12_GPU_VIRTUAL_ADDRESS gpu;
    };

    Alloc Upload(uint64_t size, uint64_t align = 256);
    Frame& CurrentFrame() { return frames_[frame_ % kFrames]; }
    uint32_t AllocSrvSlots(uint32_t n);
    // the last draw's texture views: a draw with the same textures and views in the same frame reuses its slots
    // (more than half the draws do, The Fellowship of the Ring in Bywater) instead of writing four views again
    struct LastViews {
        uint64_t frame = ~0ull;
        uint32_t slot = 0;
        ID3D12Resource* res[4] = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC sv[4] = {};
    } last_views_;
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(ID3D12DescriptorHeap* h, uint32_t i, uint32_t inc) {
        auto c = h->GetCPUDescriptorHandleForHeapStart();
        c.ptr += size_t(i) * inc;
        return c;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(ID3D12DescriptorHeap* h, uint32_t i, uint32_t inc) {
        auto g = h->GetGPUDescriptorHandleForHeapStart();
        g.ptr += uint64_t(i) * inc;
        return g;
    }
    void Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES& cur, D3D12_RESOURCE_STATES want) override;
    void Begin();
    void Submit(bool wait);
    void WaitFence(uint64_t value);
    void SetFullViewport(uint32_t w, uint32_t h);
    void SetBlitTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t w, uint32_t h);
    void Blit(ID3D12Resource* src, DXGI_FORMAT fmt, const float uvrect[4], bool gamma, bool alpha, uint32_t taps = 1,
              float mul = 1, DXGI_FORMAT rtv = kBgra) override {
        Blit(src, fmt, uvrect, BlitOptions{.gamma = gamma, .alpha = alpha, .taps = taps, .mul = mul, .rtv = rtv});
    }
    void Blit(ID3D12Resource* src, DXGI_FORMAT fmt, const float uvrect[4], const BlitOptions& o);
    uint32_t EnsureBlitSamplers();
    void DrawFullScreen(ID3D12PipelineState* pso, D3D12_GPU_VIRTUAL_ADDRESS cb, uint32_t srv_slot,
                        std::optional<uint32_t> sampler_slot = {});
    ID3D12Resource* Feedback(Target& t) override;
    void Resolve(Target& t) {
        ReadyRes(t);
        if (t.range != 1 && ext_) ext_->Resolve(*this, t);
    }
    // anti-aliasing ([graphics] anti_aliasing): targets at the frame's size draw into a multisampled twin
    uint32_t PickSamples();
    bool MsFormatOk(DXGI_FORMAT fmt);
    bool WantMs(const Target& t);
    bool CreateMsResource(Target& t);
    void CreateMsView(Target& t);
    ID3D12PipelineState* EnsureMsPipeline(uint32_t kind, DXGI_FORMAT fmt, uint32_t samples);
    void ReadyRes(Target& t);  // res as new as ms: before anything but a draw reads or partly writes res
    void ReadyMs(Target& t);   // ms as new as res: before a draw into ms
    void ResWritten(Target& t) {
        if (t.ms) t.res_new = true;
    }
    // Pass, then its step
    void Pass(const char* hlsl, const char* entry, DXGI_FORMAT rtv, D3D12_CPU_DESCRIPTOR_HANDLE rh, uint32_t w,
              uint32_t h, ID3D12Resource* const src[3], const DXGI_FORMAT fmt[3], const float p[8],
              float blend) override;
    ID3D12PipelineState* EnsurePassPipeline(const char* hlsl, const char* entry, DXGI_FORMAT rtv, float blend);
    // Draw, in call order, each callee right after its caller
    bool DepthCopy(const State& s);
    ID3D12PipelineState* EnsureDepthCopyPipeline();
    void DrawDepthCopy(Target& src, Target& dst, ID3D12PipelineState* pso);
    bool AssembleDraw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices,
                      Assembled& a);
    bool BindTargets(const State& s);
    void EnsureVertexShader(const State& s, const Assembled& a, DrawShaders& sh, MotionKind mk = kMotionNone);
    bool BindStageTexturesAndTargets(const State& s, DrawTextures& stages);
    ID3D12Resource* BindTexture(const State& s, int stage, DrawTextures& stages);
    ID3D12Resource* BindTargetTexture(const State& s, int stage, const TextureDesc& d, Target& t, bool box,
                                      DrawTextures& stages);
    ID3D12Resource* BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi,
                                     DrawTextures& stages);
    bool CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, uint64_t key);
    void UploadTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, const uint8_t* p, const uint32_t* colours,
                       bool same);
    void UploadTextureLevel(Tex& tx, uint32_t f, uint32_t l, const std::vector<uint8_t>& px, uint32_t pitch);
    void EnsurePixelShader(const PixelKey& pk, DrawShaders& sh, bool motion = false);
    void BuildPipelineKey(const State& s, const Assembled& a, uint64_t vh, uint64_t ph, PsoKey& k);
    void KeyDepthStencil(const State& s, const Assembled& a, PsoKey& k);
    ID3D12PipelineState* EnsurePipeline(const PsoKey& k, ID3DBlob* vs, ID3DBlob* ps);
    void FillPipelineDesc(const PsoKey& k, ID3DBlob* vs, ID3DBlob* ps, D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) const;
    void UploadConstants(const State& s, const DrawShaders& sh, const PsoKey& k, const DrawTextures& stages,
                         GuestExtent size, DrawConstants& c);
    void FillTextureConstants(const State& s, const DrawTextures& stages, DrawConstants& c);
    void FillTexelConstants(int i, const TextureDesc& d, const DrawTextures& stages, DrawConstants& c);
    void CaptureDraw(const State& s, uint32_t prim, uint32_t count, const PsoKey& k, const DrawConstants& c,
                     const DrawTextures& stages);
    void CaptureTextures(const State& s, const DrawTextures& stages);
    void EndDrawCapture(const State& s, const Assembled& a, uint32_t count, const DrawConstants& c);
    void CaptureInputs(const State& s, const Assembled& a, const DrawConstants& c);
    void CaptureShaders(const State& s, const Assembled& a, const DrawShaders& sh, const PixelKey& pk);
    void CaptureShader(const char* kind, uint64_t key, const std::string& src);
    void SetTextureViewsAndSamplers(const State& s, const Assembled& a, DrawTextures& stages);
    uint32_t EnsureStageSamplers(const State& s, const DrawTextures& stages, bool clamp);
    void SetViewportScissor(const State& s, uint32_t gw, uint32_t gh);
    void SetBlendFactorStencilRef(const State& s);
    void DrawGeometry(const Assembled& a);
    void UploadVertexBuffers(const Assembled& a);
    // shaders by source hash: Compile returns the bytecode and sets hash
    ID3DBlob* Compile(const std::string& src, const char* entry, const char* profile, uint64_t& hash);
    // Blit's pipeline
    ID3D12PipelineState* EnsureBlitPipeline(DXGI_FORMAT rtv, bool over = false);
    ID3D12PipelineState* EnsureMaskedClearPipeline(DXGI_FORMAT rtv, uint32_t mask, uint32_t samples = 1);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC FullScreenPipelineDesc(ID3DBlob* vs, ID3DBlob* ps, DXGI_FORMAT rtv) const;
    // Init, in order, each callee right after its caller
    bool CreateDevice();
    bool CreateSwapChain(HWND hwnd);
    void CreateHeaps();
    uint32_t CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE t, uint32_t n, bool visible, ComPtr<ID3D12DescriptorHeap>& out);
    void CreateBackBufferViews();
    bool CreateRootSignature();
    void CreateFrames();
    void CreateGammaLut();
    void LoadSettings();
    // Present, in call order, each callee right after its caller; DrawFrame runs the two after it
    void FinishCapture(const Surface& front);
    void StartCapture();
    void ResizeSwap();
    void UploadGammaRamp();
    void DrawFrame(Target* t, uint32_t bi);
    void BlitFrame(Target& t, D3D12_CPU_DESCRIPTOR_HANDLE rh);
    void KeepMovieFrame(Target& t);
    void BlackSides(D3D12_CPU_DESCRIPTOR_HANDLE rh, uint32_t w, uint32_t h, float aspect);
    float frame_sides_ = 1.0f;  // g_frame_sides for the frame being shown
    bool BlitMovie(const Target& t);
    void RenderUi();
    void ReleaseOldTextures();
    void ResetFrame();
    void CountFps();
    void ApplySettings();
    ComPtr<ID3D12Resource> Shrink(ID3D12Resource* src, D3D12_RESOURCE_STATES& st, DXGI_FORMAT fmt, uint32_t w,
                                  uint32_t h, const ShrinkOptions& o = {});
    bool ReadBack(ID3D12Resource* res, uint32_t w, uint32_t h, std::vector<uint8_t>& out);
    void CapStep(const Surface& rt);
    // a resource CapSave shrinks to w x h and writes, in state *state, colour times mul
    struct CapImage {
        ID3D12Resource* res;
        D3D12_RESOURCE_STATES* state;
        DXGI_FORMAT fmt;
        uint32_t w;
        uint32_t h;
        float mul = 1;
    };
    void CapSave(const CapImage& img, const char* name);
    void CapSave(Target& t, const char* name);
    // a surface's place in targets_: its key, console size and entry, end when it has none
    struct TargetSlot {
        uint32_t data;
        uint64_t key;
        uint32_t gw;
        uint32_t gh;
        std::unordered_map<uint64_t, Target>::iterator it;
        bool sized;  // the entry is there at the surface's size
    };
    Target* FindTarget(const Surface& s, bool depth);
    Target* EnsureTarget(const Surface& s, bool depth);
    // EnsureTarget's steps, in call order
    TargetSlot FindTargetSlot(const Surface& s, bool depth, uint64_t alt);
    uint64_t SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const;
    bool TargetStale(uint32_t data) const;
    void ReleaseTarget(std::unordered_map<uint64_t, Target>::iterator it);
    bool CreateTargetResource(Target& t, const Surface& s);
    bool CreateTargetView(Target& t);
    // UploadTarget, then its step
    void UploadTarget(Target& t, const Surface& s);
    ComPtr<ID3D12Resource> CreatePixelTexture(const std::vector<uint8_t>& px, uint32_t pitch, uint32_t w, uint32_t h);
    // Clear's arguments past the state
    struct ClearArgs {
        uint32_t count;
        const uint8_t* rects;
        uint32_t flags;
        uint32_t color;
        float z;
        uint32_t stencil;
    };
    void ClearTarget(Target& t, const State& s, const ClearArgs& c, std::vector<D3D12_RECT>& rs);
    // Copy's guest lists: count src_rects, dst_points 0 or one per rect
    struct CopyList {
        uint32_t count;
        uint32_t src_rects;
        uint32_t dst_points;
    };
    void CopyByDraw(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state, Target& dst_t,
                    const CopyList& l);
    void CopyByRegion(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state, Target& dst_t,
                      const CopyList& l);
    // the target as a guest sized BGRA copy source, and the shrunk copy that holds it when it is not one already
    struct CopySource {
        ID3D12Resource* res;
        ComPtr<ID3D12Resource> down;
    };
    CopySource PrepareCopySource(Target& t, const ShrinkOptions& o);
    // RecordLate's steps, under late_lock_
    struct Late;
    void ResetLate(Late& l, const Target& t);
    ID3D12Resource* PrepareLateSource(Target& t, Late& l);
    void RecordLateCopy(Late& l, uint32_t slot, ID3D12Resource* src);

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<ID3D12Device> dev_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swap_;
    ComPtr<ID3D12Resource> bb_[kFrames];
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr;
    uint64_t fence_value_ = 0;
    Frame frames_[kFrames];
    uint64_t frame_ = 0;
    ComPtr<ID3D12GraphicsCommandList> cl_;
    ComPtr<ID3D12DescriptorHeap> srv_heap_;
    ComPtr<ID3D12DescriptorHeap> smp_heap_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12DescriptorHeap> ui_heap_;
    uint32_t srv_inc_ = 0;
    uint32_t smp_inc_ = 0;
    uint32_t rtv_inc_ = 0;
    uint32_t dsv_inc_ = 0;
    std::vector<uint32_t> free_rtv_;
    std::vector<uint32_t> free_dsv_;
    ComPtr<ID3D12RootSignature> root_;
    std::unordered_map<uint64_t, ComPtr<ID3DBlob>> blobs_;
    std::unordered_map<uint64_t, uint64_t> shader_src_;  // vertex and pixel program keys to source hash
    std::unordered_map<uint64_t, ComPtr<ID3D12PipelineState>> psos_;
    // pipeline warm-up: every draw pipeline made is listed in the shader cache (pipelines.bin); at the next start a
    // background thread makes them again from the cached shaders, before the game asks for them
    void StartWarmUp();
    void WarmUp(std::vector<PsoKey> keys);
    void CompileSeed(std::vector<SeedShader> seed);
    std::thread warm_;
    std::atomic<bool> warm_stop_{false};
    std::mutex warm_mu_;
    std::unordered_map<uint64_t, ComPtr<ID3D12PipelineState>> warm_psos_;  // made ahead, not yet asked for
    std::unordered_set<uint64_t> warm_taken_;                              // asked for or made: not made again
    std::unordered_set<uint64_t> listed_;                                  // keys already in pipelines.bin
    FILE* pso_list_ = nullptr;
    std::unordered_map<uint64_t, uint32_t> samplers_;
    uint32_t smp_used_ = 0;
    std::unordered_map<uint64_t, Target> targets_;
    std::unordered_map<uint32_t, uint64_t> target_used_;  // target address to the frame it was last used
    std::unordered_map<uint64_t, Tex> textures_;  // by data address, a P8 texture's with its palette's above
    // texture packs: textures/ beside the exe holds replacements named by content hash, textures/dump/ what
    // texture_dump writes
    pack::Library pack_lib_;
    float place_a_ = 1.0f, place_b_ = 0.0f;  // this draw's sideways move (g_ortho_place)
    std::unordered_map<uint64_t, PackTex> pack_cache_;
    std::unordered_set<uint64_t> hud_logged_;  // textures already named as drawn in 2D
    std::unordered_set<uint64_t> dumped_;
    void TexturePack(Tex& tx, const TextureDesc& d, const uint8_t* p, const uint32_t* colours,
                     const std::vector<std::vector<uint8_t>>& levels);
    bool LoadPackTexture(const std::filesystem::path& file, PackTex& out);
    bool CreatePackTexture(const pack::Image& img, const std::string& name, PackTex& out);
    std::unordered_map<uint64_t, ComPtr<ID3D12PipelineState>> blit_psos_;
    Extension* ext_ = nullptr;
    // one frame capture: every draw in a text file, each target as the frame leaves it, the textures it read
    FILE* cap_ = nullptr;
    std::filesystem::path cap_dir_;
    uint32_t cap_i_ = 0;
    uint32_t cap_rt_ = 0;
    std::unordered_set<uint32_t> cap_seen_;
    std::unordered_set<uint64_t> cap_sh_;
    struct CapTex {
        ComPtr<ID3D12Resource> res;
        DXGI_FORMAT fmt;
        uint32_t data;
        uint32_t w;
        uint32_t h;
    };
    std::vector<CapTex> cap_tex_;
    ComPtr<ID3D12Resource> lut_;
    D3D12_RESOURCE_STATES lut_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    uint8_t ramp_[768] = {};
    bool ramp_dirty_ = false;
    bool ramp_set_ = false;  // the title set a ramp of its own
    bool gamma_ = false;
    Target* bound_rt_ = nullptr;
    Target* bound_ds_ = nullptr;
    bool bound_ms_ = false;    // the bound targets are the multisampled twins
    // movies (g_movie): each presented frame kept at up to twice the console's size, the picture shown is the newest that is
    // [graphics] cutscene_audio_delay old
    struct MovieFrame {
        ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_SOURCE;
        double time = 0;
    };
    static constexpr uint32_t kMovieFrames = 32;
    std::array<MovieFrame, kMovieFrames> movie_ring_;
    uint32_t movie_n_ = 0;  // frames kept since the movie began
    bool movie_on_ = false;
    // the frame averaged down to the size it is shown at, when it is drawn larger (render_percent over 100)
    ComPtr<ID3D12Resource> shown_;
    D3D12_RESOURCE_STATES shown_state_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
    MovieFrame* PickMovieFrame();
    uint32_t samples_ = 1;     // anti-aliasing samples, 1 off
    std::unordered_map<uint32_t, bool> ms_ok_;  // formats that take samples_
    Target* draw_rt_ = nullptr;  // the draw's targets, the bound ones go when a texture's copy is drawn
    Target* draw_ds_ = nullptr;
    uint32_t guest_w_ = 640;
    uint32_t guest_h_ = 480;
    uint32_t bb_w_ = 0;
    uint32_t bb_h_ = 0;
    float scale_ = 1.0f;
    float xmul_ = 1.0f;  // extra width of the targets (g_wide_targets)
    float TargetScale();  // render_percent's, or the upscaler's render size

    // upscaling, [graphics] upscaler (FSR, DLSS): the frame drawn smaller and jittered, with motion vectors to a
    // second target, then the upscaler makes the shown frame from it and its history (upscale.h)
    std::unique_ptr<up::Upscaler> up_;
    uint32_t up_kind_ = 0, up_quality_ = 0, up_ow_ = 0, up_oh_ = 0, up_rw_ = 0, up_rh_ = 0;  // as made
    bool up_hdr_ = false;
    uint32_t up_failed_ = 0;      // the kind that could not be made (back to render_percent until the setting changes)
    std::string up_logged_;       // the last line logged about it
    ComPtr<IDXGIAdapter1> adapter_;
    bool UpWanted() const;        // the setting asks for one that may work here
    bool UpOn() const { return UpWanted(); }  // the frame is drawn for one (made at the first Present that needs it)
    bool Upscale(Target& t, uint32_t ow, uint32_t oh, D3D12_CPU_DESCRIPTOR_HANDLE rh);  // true: up_out_ holds the frame
    void NextJitter(const Target& t, uint32_t ow);
    ComPtr<ID3D12Resource> up_out_;
    D3D12_RESOURCE_STATES up_out_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    struct Motion {
        ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        uint32_t w = 0, h = 0, slot = 0;
        uint64_t frame = ~0ull;  // presents_ when last cleared, so drawn this frame when equal
        const Target* depth = nullptr;  // the depth drawn with it this frame (checked against targets_ before use)
    };
    std::unordered_map<uint32_t, Motion> motion_;  // by guest surface (Target::data), for the surfaces shown
    std::array<uint32_t, 4> shown_surf_{};         // the surfaces presented lately
    Motion* EnsureMotion(const Target& rt);
    Motion* bound_mv_ = nullptr;
    float jit_[4] = {};      // this frame's jitter in clip units (b2's jit)
    float jit_px_[2] = {};   // and in render pixels, for the upscaler
    uint64_t jit_n_ = 0;
    // each draw's constants a frame ago: matched by what it draws and the how-manieth such draw of the frame it is
    struct PrevDraw {
        std::array<std::array<float, 4>, 192> vsc;
        float wvp[16];
    };
    std::unordered_map<uint64_t, uint32_t> prev_index_, cur_index_, occ_;
    std::vector<PrevDraw> prev_draws_, cur_draws_;
    // matched by the exact draw (its buffers and range), else the same shader drawing as many, else the same shader
    // by its order (buffers a game refills each frame move, so the looser keys catch those)
    uint64_t mv_keys_[3] = {};
    uint64_t mv_key_ = 0;
    uint32_t mv_draws_ = 0, mv_found_ = 0;
    bool up_reset_ = true;
    std::chrono::steady_clock::time_point up_t_ = std::chrono::steady_clock::now();
    float WideMul() const;
    // surfaces locked without a wait: copies in flight, the newest finished one goes to guest memory
    struct Late {
        ComPtr<ID3D12Resource> rb[kFrames];
        ComPtr<ID3D12Resource> down;
        D3D12_RESOURCE_STATES down_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
        uint64_t fence[kFrames] = {};
        uint64_t frame[kFrames] = {};
        uint64_t delivered = 0;
        uint32_t gw = 0;
        uint32_t gh = 0;
        uint32_t row_pitch = 0;
        uint32_t next = 0;
    };
    std::unordered_map<uint32_t, Late> late_;
    std::mutex late_lock_;                      // DeliverLate runs on the title's thread

    // visibility tests: occlusion queries from a ring of entries, resolved into one readback buffer, 8 bytes an
    // entry. Each title index keeps its last few entries, so a result a few frames old is there while newer ones run.
    static constexpr uint32_t kQueryEntries = 1024, kQueryIndexes = 256, kQueryRing = 4;
    struct QuerySlot {
        uint32_t entry[kQueryRing] = {};
        uint64_t fence[kQueryRing] = {};  // the submit that carries the entry's resolve, 0 for none
        float ratio[kQueryRing] = {};     // the title's pixels over ours on the target drawn to
        uint32_t next = 0, pixels = 0;
        uint64_t got = 0;  // the fence of the result in pixels
        bool have = false;
    };
    bool EnsureQueries();
    ComPtr<ID3D12QueryHeap> query_heap_;
    ComPtr<ID3D12Resource> query_rb_;
    const uint64_t* query_map_ = nullptr;
    bool query_failed_ = false;
    QuerySlot queries_[kQueryIndexes];
    uint32_t query_next_ = 0;    // the entry the next test takes
    int64_t query_active_ = -1;  // the entry begun on the open command list
    std::mutex query_lock_;      // QueryResult runs on the title's thread
    std::atomic<uint64_t> presents_seen_{0};    // presents_ for DeliverLate
    Assembled asm_;  // kept between draws for its buffers
    bool tearing_ = false;
    // [graphics] hdr on an HDR screen: an FP16 scRGB swap chain, the frame blit raising highlights
    DXGI_FORMAT bb_fmt_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    bool hdr_ = false;
    float hdr_paper_ = 200.0f / 80.0f;  // normal scenes and the HUD at 200 nits, in scRGB units
    float hdr_peak_ = 600.0f / 80.0f;   // the screen's peak, read from it
    bool HdrScreen(HWND hwnd, float& max_nits);
    uint64_t presents_ = 0;
    double fps_ = 0;
    std::chrono::steady_clock::time_point fps_t_ = std::chrono::steady_clock::now();
    float brightness_ = 1.0f;  // the brightness the gamma ramp was last made with
    uint64_t fps_n_ = 0;
};

bool D3D12Backend::Init(uint32_t width, uint32_t height) {
    guest_w_ = width;
    guest_h_ = height;
    HWND hwnd = static_cast<HWND>(app::Window());
    if (!CreateDevice()) return false;
    if (!CreateSwapChain(hwnd)) return false;
    CreateHeaps();
    CreateBackBufferViews();
    if (!CreateRootSignature()) return false;
    StartWarmUp();
    CreateFrames();
    CreateGammaLut();
    ImGui_ImplDX12_Init(dev_.Get(), kFrames, bb_fmt_, ui_heap_.Get(),
                        ui_heap_->GetCPUDescriptorHandleForHeapStart(), ui_heap_->GetGPUDescriptorHandleForHeapStart());
    LoadSettings();
    Begin();
    LOGI("d3d12: %ux%u guest, %ux%u window, render scale %.3f (%.2f across)", width, height, bb_w_, bb_h_, scale_,
         scale_ * xmul_);
    // packs anywhere under textures/replace (older ones straight in textures/), dumps in textures/dump
    if (app::Get().texture_replace) pack_lib_.Scan(app::TextureDir(), "dump");
    if (pack_lib_.Size()) LOGR("texture pack: %zu replacement files under %s", pack_lib_.Size(), app::TextureDir().string().c_str());
    if (app::Get().texture_dump) LOGR("texture pack: dumping textures to %s", (app::TextureDir() / "dump").string().c_str());
    return true;
}

bool D3D12Backend::CreateDevice() {
    if (GetEnvironmentVariableW(L"XBE_D3D12_DEBUG", nullptr, 0)) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
    }
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)))) return false;
    // dedicated GPU first on hybrid laptops
    ComPtr<IDXGIFactory6> f6;
    ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(factory_.As(&f6))) {
        f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
    }
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev_))) &&
        FAILED(D3D12CreateDevice((adapter = nullptr).Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev_)))) {
        LOGE("d3d12: no device");
        return false;
    }
    DXGI_ADAPTER_DESC1 ad{};
    if (adapter) adapter->GetDesc1(&ad);
    LOGR("D3D12 adapter: %ls", adapter ? ad.Description : L"default");
    adapter_ = adapter;
    if (!adapter_) {
        // the default adapter, for DLSS's card check
        ComPtr<IDXGIAdapter1> a0;
        if (SUCCEEDED(factory_->EnumAdapters1(0, &a0))) adapter_ = a0;
    }
    // DLSS: Streamline starts before the swap chain is made; it is set up only when asked for, so it takes effect at
    // the next start
    if (app::Get().upscaler == up::kDlss) {
        std::string why;
        if (up::InitStreamline(adapter_.Get(), why)) {
            LOGR("upscaler: NVIDIA Streamline started for DLSS");
            if (!up::SetStreamlineDevice(dev_.Get())) LOGR("upscaler: DLSS could not use the graphics device");
        }
        else LOGR("upscaler: DLSS is not available: %s", why.c_str());
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)))) {
        LOGE("d3d12: no command queue");
        return false;
    }
    ComPtr<IDXGIFactory5> f5;
    BOOL tear = FALSE;
    if (SUCCEEDED(factory_.As(&f5))) f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tear, sizeof(tear));
    tearing_ = tear;
    return true;
}

bool D3D12Backend::CreateSwapChain(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    bb_w_ = std::max<LONG>(rc.right - rc.left, 1);
    bb_h_ = std::max<LONG>(rc.bottom - rc.top, 1);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = bb_w_;
    sd.Height = bb_h_;
    float nits = 0;
    hdr_ = app::Get().hdr && HdrScreen(hwnd, nits);
    if (app::Get().hdr && !hdr_) LOGR("d3d12: HDR is on in the settings, but this screen has HDR off (or none): the normal picture");
    bb_fmt_ = hdr_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.Format = bb_fmt_;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kFrames;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sd, nullptr, nullptr, &sc1))) {
        LOGE("d3d12: no swap chain");
        return false;
    }
    factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    {
        // DLSS (manual hooking): Streamline's own bookkeeping runs on the swap chain's Present
        IDXGISwapChain1* raw = sc1.Detach();
        up::UpgradeSwapChain(reinterpret_cast<IDXGISwapChain**>(&raw));
        sc1.Attach(raw);
    }
    sc1.As(&swap_);
    if (hdr_) {
        UINT support = 0;
        constexpr DXGI_COLOR_SPACE_TYPE kScRgb = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        if (SUCCEEDED(swap_->CheckColorSpaceSupport(kScRgb, &support)) &&
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) && SUCCEEDED(swap_->SetColorSpace1(kScRgb))) {
            // a screen that reports less than twice paper white still gets some headroom; very bright ones are held
            // to 1000 nits, past which the 2003 picture only glares
            float peak = std::clamp(nits, 400.0f, 1000.0f);
            hdr_peak_ = peak / 80.0f;
            LOGR("d3d12: HDR on (screen peak %.0f nits, highlights up to %.0f, normal scenes at %.0f)", nits, peak,
                 hdr_paper_ * 80.0f);
        } else {
            LOGR("d3d12: HDR could not be set up on this screen: the normal picture");
            hdr_ = false;
            bb_fmt_ = DXGI_FORMAT_B8G8R8A8_UNORM;
            swap_->ResizeBuffers(kFrames, bb_w_, bb_h_, bb_fmt_, sd.Flags);
        }
    }
    return true;
}

// the window's screen shows HDR (Windows HDR on): max_nits its peak brightness
bool D3D12Backend::HdrScreen(HWND hwnd, float& max_nits) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIFactory1> f1;
    if (FAILED(factory_.As(&f1))) return false;
    ComPtr<IDXGIAdapter1> ad;
    for (UINT a = 0; f1->EnumAdapters1(a, &ad) != DXGI_ERROR_NOT_FOUND; a++) {
        ComPtr<IDXGIOutput> out;
        for (UINT o = 0; ad->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; o++) {
            DXGI_OUTPUT_DESC od{};
            out->GetDesc(&od);
            if (od.Monitor != mon) continue;
            ComPtr<IDXGIOutput6> o6;
            DXGI_OUTPUT_DESC1 d1{};
            if (FAILED(out.As(&o6)) || FAILED(o6->GetDesc1(&d1))) return false;
            max_nits = d1.MaxLuminance;
            return d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        }
    }
    return false;
}

uint32_t D3D12Backend::CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE t, uint32_t n, bool visible,
                                  ComPtr<ID3D12DescriptorHeap>& out) {
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = t;
    hd.NumDescriptors = n;
    hd.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dev_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&out));
    return dev_->GetDescriptorHandleIncrementSize(t);
}

void D3D12Backend::CreateBackBufferViews() {
    for (int i = 0; i < kFrames; i++) {
        swap_->GetBuffer(i, IID_PPV_ARGS(&bb_[i]));
        dev_->CreateRenderTargetView(bb_[i].Get(), nullptr, Cpu(rtv_heap_.Get(), kRtvCount + i, rtv_inc_));
    }
}

void D3D12Backend::CreateHeaps() {
    srv_inc_ = CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCount, true, srv_heap_);
    smp_inc_ = CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerCount, true, smp_heap_);
    rtv_inc_ = CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvCount + kFrames + kMotionSlots, false, rtv_heap_);
    dsv_inc_ = CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDsvCount, false, dsv_heap_);
    CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true, ui_heap_);
    for (uint32_t i = kRtvCount; i-- > 0;) free_rtv_.push_back(i);
    for (uint32_t i = kDsvCount; i-- > 0;) free_dsv_.push_back(i);
}

bool D3D12Backend::CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 4;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = 4;
    D3D12_ROOT_PARAMETER params[5] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;  // b2: the previous frame's constants, motion shaders only
    params[4].Descriptor.ShaderRegister = 2;
    for (int i = 0; i < 2; i++) {
        params[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2 + i].DescriptorTable.NumDescriptorRanges = 1;
        params[2 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
        params[2 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 5;
    rsd.pParameters = params;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> rs;
    ComPtr<ID3DBlob> err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &err))) return false;
    dev_->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), IID_PPV_ARGS(&root_));
    return true;
}

void D3D12Backend::CreateFrames() {
    for (auto& f : frames_) {
        dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc));
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC bd = BufferDesc(kUploadSize);
        dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                      IID_PPV_ARGS(&f.upload));
        f.upload->Map(0, nullptr, reinterpret_cast<void**>(&f.map));
    }
    dev_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].alloc.Get(), nullptr, IID_PPV_ARGS(&cl_));
}

// gamma ramp as a 256 x 1 lookup
void D3D12Backend::CreateGammaLut() {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = Texture2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, 256, 1);
    dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                  IID_PPV_ARGS(&lut_));
}

// g_wide_targets: the frame's shape over the guest's 4:3, so targets come out at the shape the frame is shown at
float D3D12Backend::WideMul() const {
    if (!g_wide_targets || !guest_h_ || std::fabs(float(guest_w_) / float(guest_h_) - 4.0f / 3.0f) > 0.01f) return 1.0f;
    return std::max(1.0f, app::FrameAspect() / (4.0f / 3.0f));
}

void D3D12Backend::LoadSettings() {
    scale_ = TargetScale();
    xmul_ = WideMul();
    g_colour_look = std::min(app::Get().colour_look, kColourLooks - 1);
    samples_ = PickSamples();
    // brightness goes through the gamma ramp: a game that never sets one gets the identity ramp to carry it
    if (app::Get().brightness < 0.999f || app::Get().brightness > 1.001f) {
        if (!ramp_set_)
            for (int i = 0; i < 256; i++) ramp_[i] = ramp_[256 + i] = ramp_[512 + i] = uint8_t(i);
        ramp_dirty_ = true;
    }
    ext_ = g_extension.get();
    if (ext_) {
        ext_->Settings();
        ext_->Reset(*this);
    }
}

void D3D12Backend::Begin() {
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), smp_heap_.Get()};
    cl_->SetDescriptorHeaps(2, heaps);
    cl_->SetGraphicsRootSignature(root_.Get());
    bound_rt_ = bound_ds_ = nullptr;
    // a test open when the last list was closed goes on in this one; what it counted there is dropped
    if (query_active_ >= 0) cl_->BeginQuery(query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, UINT(query_active_));
}

void D3D12Backend::Submit(bool wait) {
    if (query_active_ >= 0) cl_->EndQuery(query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, UINT(query_active_));
    cl_->Close();
    ID3D12CommandList* lists[] = {cl_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    queue_->Signal(fence_.Get(), ++fence_value_);
    Frame& f = CurrentFrame();
    f.fence = fence_value_;
    if (wait) {
        WaitFence(fence_value_);
        f.alloc->Reset();
        f.garbage.clear();
        f.used = 0;
        f.srv = 0;
        last_views_.frame = ~0ull;  // the slots are written again
        cl_->Reset(f.alloc.Get(), nullptr);
        Begin();
    }
}

void D3D12Backend::Flush() { Submit(true); }

// kMovieFit's shape: the screen's (app::FrameAspect), from 16:9 up to g_movie_fit_max (a game whose films are wider
// than 16:9 raises it to their own shape)
float MovieFitAspect() { return std::clamp(app::FrameAspect(), 16.0f / 9.0f, std::max(g_movie_fit_max.load(), 16.0f / 9.0f)); }

// the picture as shown: the frame at its render size, made as wide as the screen shows it (16:9 with widescreen),
// saved as <PREFIX>_<date>_<time>_<ms>.png as ROTK Fix names its own, on the texture pack writer's thread
void D3D12Backend::Screenshot(Target& t, const std::string& suffix) {
    // the frame through the same pass as the screen (brightness, sharpening, colour look), without HDR; a movie as
    // the screen shows it: the screen's shape at the frame's height, the picture placed by movie_display as in
    // BlitMovie, with its bars
    const app::Settings& stg = app::Get();
    uint32_t h = t.h, w = app::Widescreen() ? uint32_t(float(t.h) * app::FrameAspect() + 0.5f) : t.w;
    ID3D12Resource* src = t.res.Get();
    D3D12_RESOURCE_STATES* src_state = &t.state;
    DXGI_FORMAT fmt = t.fmt;
    float uv[4] = {0, 0, 1, 1};
    float vx = 0, vy = 0, vw = 0, vh = 0;  // a movie's place in the shot; 0: all of it
    BlitOptions o{.gamma = gamma_, .sharpen = g_sharpen_off ? 0.0f : std::min(stg.sharpening, 100u) / 100.0f,
                  .texel = {1.0f / float(std::max(t.w, 1u)), 1.0f / float(std::max(t.h, 1u))}, .look = true};
    if (MovieFrame* f = PickMovieFrame()) {
        src = f->res.Get();
        src_state = &f->state;
        fmt = kBgra;
        o.sharpen = std::min(stg.movie_sharpening, 100u) / 100.0f;
        o.texel[0] = 1.0f / float(std::max(t.gw, 1u));
        o.texel[1] = 1.0f / float(std::max(t.gh, 1u));
        float aspect = 4.0f / 3.0f;  // the picture's shape in the shot, placed with bars as BlitMovie shows it
        uint32_t shape = g_movie_shape;
        if (stg.movie_display == 0 && shape == kMovieFit) {
            aspect = MovieFitAspect();
            uv[3] = (4.0f / 3.0f) / aspect;
            uv[1] = MovieFitTop(uv[3]);
        } else if (stg.movie_display == 0 && shape == kMovieWide) {
            aspect = 16.0f / 9.0f;
        }
        w = uint32_t(uint64_t(h) * std::max(bb_w_, 1u) / std::max(bb_h_, 1u));
        vw = std::min(float(w), float(h) * aspect);
        vh = vw / aspect;
        if (stg.movie_display == 1) {
            vw = float(w);
            vh = float(h);
        }
        vx = (float(w) - vw) / 2;
        vy = (float(h) - vh) / 2;
    }
    ComPtr<ID3D12Resource> down;
    {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC td = Texture2DDesc(kBgra, w, h);
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = kBgra;
        if (!free_rtv_.empty() && SUCCEEDED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                                                         D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                                                                         IID_PPV_ARGS(&down)))) {
            // borrows the next free view without taking it, as Shrink does
            D3D12_CPU_DESCRIPTOR_HANDLE rh = Cpu(rtv_heap_.Get(), free_rtv_.back(), rtv_inc_);
            dev_->CreateRenderTargetView(down.Get(), nullptr, rh);
            Transition(src, *src_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            SetBlitTarget(rh, w, h);
            if (vw > 0) {
                float black[4] = {0, 0, 0, 1};
                cl_->ClearRenderTargetView(rh, black, 0, nullptr);
                D3D12_VIEWPORT vp{vx, vy, vw, vh, 0, 1};
                cl_->RSSetViewports(1, &vp);
            }
            Blit(src, fmt, uv, o);
            if (src == t.res.Get()) BlackSides(rh, w, h, float(w) / float(std::max(h, 1u)));
            D3D12_RESOURCE_STATES ds = D3D12_RESOURCE_STATE_RENDER_TARGET;
            Transition(down.Get(), ds, D3D12_RESOURCE_STATE_COPY_SOURCE);
        } else {
            down.Reset();
        }
    }
    std::vector<uint8_t> px;
    if (!down || !ReadBack(down.Get(), w, h, px)) {
        LOGW("screenshot: the frame could not be read back");
        return;
    }
    for (size_t i = 0; i < px.size(); i += 4) {  // BGRA to RGBA, opaque
        std::swap(px[i], px[i + 2]);
        px[i + 3] = 255;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::string prefix = app::kGame.log;
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    char name[96];
    std::snprintf(name, sizeof(name), "%s_%04u-%02u-%02u_%02u-%02u-%02u_%03u.png", prefix.c_str(), st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::filesystem::path file = app::ScreenshotDir() / (suffix.empty() ? std::string(name) : prefix + suffix);
    pack::WritePngLater(file, w, h, std::move(px));
    LOGR("screenshot: %s (%ux%u)", file.string().c_str(), w, h);
}

bool D3D12Backend::EnsureQueries() {
    if (query_heap_) return true;
    if (query_failed_) return false;
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_OCCLUSION, kQueryEntries, 0};
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd = BufferDesc(uint64_t(kQueryEntries) * 8);
    void* map = nullptr;
    D3D12_RANGE all{0, size_t(kQueryEntries) * 8};
    if (FAILED(dev_->CreateQueryHeap(&qd, IID_PPV_ARGS(&query_heap_))) ||
        FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&query_rb_))) ||
        FAILED(query_rb_->Map(0, &all, &map))) {
        LOGE("d3d12: occlusion queries unavailable, visibility tests read as fully visible");
        query_heap_.Reset();
        query_rb_.Reset();
        query_failed_ = true;
        return false;
    }
    query_map_ = static_cast<const uint64_t*>(map);
    return true;
}

void D3D12Backend::BeginQuery() {
    if (!EnsureQueries()) return;
    if (query_active_ >= 0) cl_->EndQuery(query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, UINT(query_active_));  // left unnamed
    query_active_ = query_next_;
    query_next_ = (query_next_ + 1) % kQueryEntries;
    cl_->BeginQuery(query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, UINT(query_active_));
}

void D3D12Backend::EndQuery(uint32_t index) {
    if (query_active_ < 0) return;
    UINT e = UINT(query_active_);
    cl_->EndQuery(query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, e);
    cl_->ResolveQueryData(query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, e, 1, query_rb_.Get(), uint64_t(e) * 8);
    query_active_ = -1;
    // counted at our resolution on the target the test drew to, given back in the title's
    const Target* t = bound_ds_ ? bound_ds_ : bound_rt_;
    float ratio = t && t->w && t->h ? float(t->gw) * t->gh / (float(t->w) * t->h) : 1 / (scale_ * xmul_ * scale_);
    if (t && bound_ms_) ratio /= float(samples_);  // anti-aliasing counts samples, not pixels
    std::lock_guard lock(query_lock_);
    QuerySlot& q = queries_[index % kQueryIndexes];
    q.entry[q.next] = e;
    q.fence[q.next] = fence_value_ + 1;  // the next Submit carries it
    q.ratio[q.next] = ratio;
    q.next = (q.next + 1) % kQueryRing;
}

bool D3D12Backend::QueryResult(uint32_t index, uint32_t& pixels) {
    if (!query_map_) return false;
    uint64_t done = fence_->GetCompletedValue();
    std::lock_guard lock(query_lock_);
    QuerySlot& q = queries_[index % kQueryIndexes];
    int best = -1;
    for (uint32_t i = 0; i < kQueryRing; i++)
        if (q.fence[i] && q.fence[i] <= done && q.fence[i] > q.got && (best < 0 || q.fence[i] > q.fence[best])) best = int(i);
    if (best >= 0) {
        uint64_t raw = query_map_[q.entry[best]];
        q.pixels = uint32_t(std::min<double>(std::round(double(raw) * q.ratio[best]), 4294967295.0));
        q.got = q.fence[best];
        q.have = true;
    }
    pixels = q.pixels;
    return q.have;
}

void D3D12Backend::WaitFence(uint64_t value) {
    fence_->SetEventOnCompletion(value, event_);
    WaitForSingleObject(event_, INFINITE);
}

void D3D12Backend::SetFullViewport(uint32_t w, uint32_t h) {
    D3D12_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
    D3D12_RECT scissor{0, 0, LONG(w), LONG(h)};
    cl_->RSSetViewports(1, &vp);
    cl_->RSSetScissorRects(1, &scissor);
}

// one w x h colour target for a Blit, the draw's bound targets forgotten
void D3D12Backend::SetBlitTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t w, uint32_t h) {
    cl_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    bound_rt_ = bound_ds_ = nullptr;
    SetFullViewport(w, h);
}

D3D12Backend::Alloc D3D12Backend::Upload(uint64_t size, uint64_t align) {
    Frame& f = CurrentFrame();
    g_perf.upload += size;
    uint64_t o = (f.used + align - 1) & ~(align - 1);
    if (o + size <= kUploadSize) {
        f.used = o + size;
        return {f.upload.Get(), o, f.map + o, f.upload->GetGPUVirtualAddress() + o};
    }
    // too big for the ring: a buffer of its own, released with the frame
    g_perf.overflow++;
    ComPtr<ID3D12Resource> r;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
    constexpr uint64_t kOwnAlign = 0x10000;  // a committed buffer's size granularity
    D3D12_RESOURCE_DESC bd = BufferDesc((size + kOwnAlign - 1) & ~(kOwnAlign - 1));
    dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                  IID_PPV_ARGS(&r));
    uint8_t* p = nullptr;
    r->Map(0, nullptr, reinterpret_cast<void**>(&p));
    f.garbage.push_back(r);
    return {r.Get(), 0, p, r->GetGPUVirtualAddress()};
}

uint32_t D3D12Backend::AllocSrvSlots(uint32_t n) {
    Frame& f = CurrentFrame();
    const uint32_t per = kSrvCount / kFrames;
    if (f.srv + n > per) {
        LOG_FIRST(4, "d3d12: descriptor ring wrapped within a frame");
        f.srv = 0;
        last_views_.frame = ~0ull;  // the slots are written again
    }
    uint32_t i = uint32_t(frame_ % kFrames) * per + f.srv;
    f.srv += n;
    return i;
}

void D3D12Backend::Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES& cur, D3D12_RESOURCE_STATES want) {
    if (cur == want) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = cur;
    b.Transition.StateAfter = want;
    cl_->ResourceBarrier(1, &b);
    cur = want;
}

// a resized target gives back its view and its resources
void D3D12Backend::ReleaseTarget(std::unordered_map<uint64_t, Target>::iterator it) {
    if (bound_rt_ == &it->second || bound_ds_ == &it->second) bound_rt_ = bound_ds_ = nullptr;
    (it->second.depth ? free_dsv_ : free_rtv_).push_back(it->second.view);
    CurrentFrame().garbage.push_back(it->second.res);
    if (it->second.ms) {
        (it->second.depth ? free_dsv_ : free_rtv_).push_back(it->second.ms_view);
        CurrentFrame().garbage.push_back(it->second.ms);
    }
    if (it->second.fb) CurrentFrame().garbage.push_back(it->second.fb);
    targets_.erase(it);
}

// the host texture of a new target, at render scale or the extension's
bool D3D12Backend::CreateTargetResource(Target& t, const Surface& s) {
    float sc = scale_;
    if (ext_) ext_->MakeTarget(s, t.depth, t.gw, t.gh, sc, t.fmt);
    // a scale the extension chose (a console sized target) holds both ways
    float sx = sc == scale_ ? scale_ * xmul_ : sc;
    t.w = std::max(1u, uint32_t(std::lround(t.gw * sx)));
    t.h = std::max(1u, uint32_t(std::lround(t.gh * sc)));
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = Texture2DDesc(DXGI_FORMAT_UNKNOWN, t.w, t.h);
    D3D12_CLEAR_VALUE cv{};
    if (t.depth) {
        td.Format = DXGI_FORMAT_R24G8_TYPELESS;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        cv.DepthStencil.Depth = 1;
        t.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    } else {
        td.Format = t.fmt;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        cv.Format = td.Format;
        t.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, t.state, &cv, IID_PPV_ARGS(&t.res))))
        return false;
    if (WantMs(t) && !CreateMsResource(t)) LOG_FIRST(2, "d3d12: anti-aliasing target %ux%u could not be made", t.w, t.h);
    return true;
}

// a view from the free pool, cleared; false when the pool is empty
bool D3D12Backend::CreateTargetView(Target& t) {
    auto& pool = t.depth ? free_dsv_ : free_rtv_;
    if (pool.empty()) return false;
    t.view = pool.back();
    pool.pop_back();
    if (t.depth) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dev_->CreateDepthStencilView(t.res.Get(), &dv, Cpu(dsv_heap_.Get(), t.view, dsv_inc_));
        float one = 1;
        cl_->ClearDepthStencilView(Cpu(dsv_heap_.Get(), t.view, dsv_inc_),
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, one, 0, 0, nullptr);
    } else {
        dev_->CreateRenderTargetView(t.res.Get(), nullptr, Cpu(rtv_heap_.Get(), t.view, rtv_inc_));
        float black[4] = {0, 0, 0, 0};
        cl_->ClearRenderTargetView(Cpu(rtv_heap_.Get(), t.view, rtv_inc_), black, 0, nullptr);
    }
    if (t.ms) CreateMsView(t);
    return true;
}

// targets_ key of a surface's colour or depth target
uint64_t TargetMapKey(uint32_t data, bool depth) { return uint64_t(data) << 1 | (depth ? 1 : 0); }

// a smaller depth surface's console size, above the address bits
constexpr int kKeyWidthShift = 34;
constexpr int kKeyHeightShift = 48;

// a smaller depth surface laid on the depth buffer's memory is a target of its own
uint64_t D3D12Backend::SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const {
    uint64_t key = TargetMapKey(data, depth);
    bool small_depth = depth && (gw != guest_w_ || gh != guest_h_);
    if (!small_depth) return key;
    return key | uint64_t(gw) << kKeyWidthShift | uint64_t(gh) << kKeyHeightShift;
}

// alt: the extension's bits of the key
D3D12Backend::TargetSlot D3D12Backend::FindTargetSlot(const Surface& s, bool depth, uint64_t alt) {
    TargetSlot slot;
    slot.data = mem::VirtualFromPhysical(s.data);
    SurfaceSize(s, slot.gw, slot.gh);
    slot.key = SizedTargetKey(slot.data, depth, slot.gw, slot.gh) | alt;
    slot.it = targets_.find(slot.key);
    slot.sized = slot.it != targets_.end() && slot.it->second.gw == slot.gw && slot.it->second.gh == slot.gh;
    return slot;
}

// the surface's target at its size, null when there is none
Target* D3D12Backend::FindTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    uint64_t alt = 0;
    if (ext_ && !ext_->TargetKey(s, depth, alt)) return nullptr;
    TargetSlot slot = FindTargetSlot(s, depth, alt);
    if (!slot.sized) return nullptr;
    target_used_[slot.data] = frame_;
    return &slot.it->second;
}

// the surface's target, made again at its size when that changed
Target* D3D12Backend::EnsureTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    uint64_t alt = 0;
    if (ext_ && !ext_->TargetKey(s, depth, alt)) return nullptr;
    TargetSlot slot = FindTargetSlot(s, depth, alt);
    target_used_[slot.data] = frame_;
    if (slot.sized) return &slot.it->second;
    if (slot.it != targets_.end()) ReleaseTarget(slot.it);
    Target t;
    t.depth = depth;
    t.gw = slot.gw;
    t.gh = slot.gh;
    {
        PerfScope perf_create{g_perf.create};
        if (!CreateTargetResource(t, s)) return nullptr;
    }
    g_perf.targets++;
    // an empty pool drops the new resource with t
    if (!CreateTargetView(t)) return nullptr;
    t.data = slot.data;
    t.bytes = GuestPitch(slot.gw) * slot.gh;
    if (depth) t.bytes = slot.gw * slot.gh * 4;
    t.epoch = DirtyEpoch(slot.data);
    t.sig = MemSig(slot.data, t.bytes);
    auto& ref = targets_[slot.key] = std::move(t);
    return &ref;
}

bool D3D12Backend::BindTargets(const State& s) {
    Target* rt = EnsureTarget(s.rt, false);
    Target* ds = s.rs[kRsZEnable] || s.rs[kRsStencilEnable] ? EnsureTarget(s.zs, true) : nullptr;
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) ds = nullptr;
    if (!rt && !ds) return false;
    if (rt && LatchCpuWrite(*rt)) UploadTarget(*rt, s.rt);
    // the multisampled twins when every bound target has one, else the plain targets
    bool ms = (!rt || rt->ms) && (!ds || ds->ms);
    for (Target* t : {rt, ds}) {
        if (!t) continue;
        if (ms) ReadyMs(*t);
        else ReadyRes(*t);
    }
    for (Target* t : {rt, ds}) {
        if (!t) continue;
        D3D12_RESOURCE_STATES want = t->depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (ms) {
            Transition(t->ms.Get(), t->ms_state, want);
            t->ms_new = true;
        } else {
            Transition(t->res.Get(), t->state, want);
            ResWritten(*t);
        }
        t->drawn = true;
    }
    // a frame for the upscaler: its motion target beside it, cleared on the frame's first draw
    Motion* mv = UpOn() && rt && !ms ? EnsureMotion(*rt) : nullptr;
    if (mv) {
        Transition(mv->res.Get(), mv->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (mv->frame != presents_) {
            float zero[4] = {};
            cl_->ClearRenderTargetView(Cpu(rtv_heap_.Get(), mv->slot, rtv_inc_), zero, 0, nullptr);
            mv->frame = presents_;
            mv->depth = nullptr;
        }
        if (ds) mv->depth = ds;
    }
    if (rt != bound_rt_ || ds != bound_ds_ || ms != bound_ms_ || mv != bound_mv_) {
        D3D12_CPU_DESCRIPTOR_HANDLE rh[2] = {
            rt ? Cpu(rtv_heap_.Get(), ms ? rt->ms_view : rt->view, rtv_inc_) : D3D12_CPU_DESCRIPTOR_HANDLE{},
            mv ? Cpu(rtv_heap_.Get(), mv->slot, rtv_inc_) : D3D12_CPU_DESCRIPTOR_HANDLE{}};
        D3D12_CPU_DESCRIPTOR_HANDLE dh =
            ds ? Cpu(dsv_heap_.Get(), ms ? ds->ms_view : ds->view, dsv_inc_) : D3D12_CPU_DESCRIPTOR_HANDLE{};
        cl_->OMSetRenderTargets(rt ? (mv ? 2 : 1) : 0, rt ? rh : nullptr, FALSE, ds ? &dh : nullptr);
        bound_rt_ = rt;
        bound_ds_ = ds;
        bound_ms_ = ms;
        bound_mv_ = mv;
    }
    return true;
}

// anti-aliasing's sample count: the setting, lowered to what the card takes for the frame's colour and depth
uint32_t D3D12Backend::PickSamples() {
    uint32_t want = UpWanted() ? 0 : app::Get().msaa;
    if (want < 2) return 1;
    for (uint32_t n = std::min(want, 8u); n >= 2; n /= 2) {
        bool ok = true;
        for (DXGI_FORMAT f : {kBgra, DXGI_FORMAT_D24_UNORM_S8_UINT}) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{f, n, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0};
            if (FAILED(dev_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) ||
                !q.NumQualityLevels) {
                ok = false;
            }
        }
        if (!ok) continue;
        static uint32_t logged = 0;
        if (logged != n) {
            logged = n;
            if (n != want) LOGR("d3d12: anti-aliasing %ux (%ux is not supported by this graphics card)", n, want);
            else LOGR("d3d12: anti-aliasing %ux", n);
        }
        return n;
    }
    LOG_FIRST(1, "d3d12: anti-aliasing is not supported by this graphics card");
    return 1;
}

bool D3D12Backend::MsFormatOk(DXGI_FORMAT fmt) {
    auto it = ms_ok_.find(uint32_t(fmt));
    if (it != ms_ok_.end()) return it->second;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{fmt, samples_, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0};
    bool ok = SUCCEEDED(dev_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) &&
              q.NumQualityLevels;
    if (ok && fmt == DXGI_FORMAT_D24_UNORM_S8_UINT) {
        // the resolve pass reads the depth twin through a multisampled view of it
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R24_UNORM_X8_TYPELESS};
        ok = SUCCEEDED(dev_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) &&
             (fs.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD);
    } else if (ok) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{fmt};
        ok = SUCCEEDED(dev_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) &&
             (fs.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE);
    }
    ms_ok_[uint32_t(fmt)] = ok;
    return ok;
}

// the frame's own targets (the console's back buffer size) and the depth drawn with them; render-to-texture passes
// of other sizes (shadows, water, glows) keep one sample
bool D3D12Backend::WantMs(const Target& t) {
    if (samples_ < 2 || t.gw != guest_w_ || t.gh != guest_h_) return false;
    return MsFormatOk(t.depth ? DXGI_FORMAT_D24_UNORM_S8_UINT : t.fmt);
}

bool D3D12Backend::CreateMsResource(Target& t) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = t.res->GetDesc();
    td.SampleDesc.Count = samples_;
    D3D12_CLEAR_VALUE cv{};
    if (t.depth) {
        cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        cv.DepthStencil.Depth = 1;
        t.ms_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    } else {
        cv.Format = t.fmt;
        t.ms_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, t.ms_state, &cv, IID_PPV_ARGS(&t.ms)))) {
        t.ms.Reset();
        return false;
    }
    return true;
}

// the twin's view, cleared like the plain target's; no free view leaves the target without a twin
void D3D12Backend::CreateMsView(Target& t) {
    auto& pool = t.depth ? free_dsv_ : free_rtv_;
    if (pool.empty()) {
        t.ms.Reset();
        return;
    }
    t.ms_view = pool.back();
    pool.pop_back();
    if (t.depth) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        dev_->CreateDepthStencilView(t.ms.Get(), &dv, Cpu(dsv_heap_.Get(), t.ms_view, dsv_inc_));
        cl_->ClearDepthStencilView(Cpu(dsv_heap_.Get(), t.ms_view, dsv_inc_),
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1, 0, 0, nullptr);
    } else {
        D3D12_RENDER_TARGET_VIEW_DESC rv{};
        rv.Format = t.fmt;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        dev_->CreateRenderTargetView(t.ms.Get(), &rv, Cpu(rtv_heap_.Get(), t.ms_view, rtv_inc_));
        float black[4] = {0, 0, 0, 0};
        cl_->ClearRenderTargetView(Cpu(rtv_heap_.Get(), t.ms_view, rtv_inc_), black, 0, nullptr);
    }
}

constexpr uint32_t kMsUp = 0;            // colour into the twin
constexpr uint32_t kMsUpDepth = 1;       // depth into the twin
constexpr uint32_t kMsResolveDepth = 2;  // the twin's depth into the plain buffer

// kind's pipeline at a sample count, kept in blit_psos_; null when it can't be made
ID3D12PipelineState* D3D12Backend::EnsureMsPipeline(uint32_t kind, DXGI_FORMAT fmt, uint32_t samples) {
    auto& pso = blit_psos_[0xA5A5000000000000ull | uint64_t(kind) << 40 | uint64_t(samples) << 32 | uint64_t(fmt)];
    if (pso) return pso.Get();
    const char* src = kind == kMsUp ? kMsUpHlsl : kind == kMsUpDepth ? kMsUpDepthHlsl : kMsDepthResolveHlsl;
    uint64_t h;
    ID3DBlob* vs = Compile(src, "vs", "vs_5_0", h);
    ID3DBlob* ps = Compile(src, "psmain", "ps_5_0", h);
    if (!vs || !ps) return nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = FullScreenPipelineDesc(vs, ps, fmt);
    if (kind != kMsUp) {
        d.NumRenderTargets = 0;
        d.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
        d.DepthStencilState.DepthEnable = TRUE;
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        d.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    }
    d.SampleDesc.Count = samples;
    if (FAILED(dev_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) {
        LOG_FIRST(2, "d3d12: anti-aliasing pass %u could not be made", kind);
        return nullptr;
    }
    return pso.Get();
}

void D3D12Backend::ReadyRes(Target& t) {
    if (!t.ms || !t.ms_new) return;
    if (!t.depth) {
        t.ms_new = false;
        Transition(t.ms.Get(), t.ms_state, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cl_->ResolveSubresource(t.res.Get(), 0, t.ms.Get(), 0, t.fmt);
        return;
    }
    // depth has no hardware resolve: the first sample of each pixel, drawn as depth (the stencil stays behind)
    ID3D12PipelineState* pso = EnsureMsPipeline(kMsResolveDepth, DXGI_FORMAT_UNKNOWN, 1);
    if (!pso) return;
    t.ms_new = false;
    Transition(t.ms.Get(), t.ms_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    D3D12_CPU_DESCRIPTOR_HANDLE dh = Cpu(dsv_heap_.Get(), t.view, dsv_inc_);
    cl_->OMSetRenderTargets(0, nullptr, FALSE, &dh);
    bound_rt_ = bound_ds_ = nullptr;
    SetFullViewport(t.w, t.h);
    uint32_t slot = AllocSrvSlots(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dev_->CreateShaderResourceView(t.ms.Get(), &sv, Cpu(srv_heap_.Get(), slot, srv_inc_));
    DrawFullScreen(pso, Upload(256, 256).gpu, slot);
}

void D3D12Backend::ReadyMs(Target& t) {
    if (!t.ms || !t.res_new) return;
    ID3D12PipelineState* pso = EnsureMsPipeline(t.depth ? kMsUpDepth : kMsUp, t.depth ? DXGI_FORMAT_UNKNOWN : t.fmt, samples_);
    if (!pso) return;
    t.res_new = false;
    if (t.depth) LOG_FIRST(2, "d3d12: anti-aliasing: depth %08X written outside a draw, carried over without its stencil", t.data);
    Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    if (t.depth) {
        Transition(t.ms.Get(), t.ms_state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        D3D12_CPU_DESCRIPTOR_HANDLE dh = Cpu(dsv_heap_.Get(), t.ms_view, dsv_inc_);
        cl_->OMSetRenderTargets(0, nullptr, FALSE, &dh);
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    } else {
        Transition(t.ms.Get(), t.ms_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rh = Cpu(rtv_heap_.Get(), t.ms_view, rtv_inc_);
        cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);
        sv.Format = t.fmt;
    }
    bound_rt_ = bound_ds_ = nullptr;
    SetFullViewport(t.w, t.h);
    uint32_t slot = AllocSrvSlots(1);
    dev_->CreateShaderResourceView(t.res.Get(), &sv, Cpu(srv_heap_.Get(), slot, srv_inc_));
    DrawFullScreen(pso, Upload(256, 256).gpu, slot);
}

constexpr uint64_t kDepthCopyKey = ~0ull;  // DepthCopy's pipeline in blit_psos_

// DepthCopy's pipeline, kept in blit_psos_: writes depth only, always passing; null when it can't be made
ID3D12PipelineState* D3D12Backend::EnsureDepthCopyPipeline() {
    auto& pso = blit_psos_[kDepthCopyKey];
    if (pso) return pso.Get();
    uint64_t h;
    ID3DBlob* vs = Compile(kDepthHlsl, "vs", "vs_5_0", h);
    ID3DBlob* ps = Compile(kDepthHlsl, "psmain", "ps_5_0", h);
    if (!vs || !ps) return nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root_.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.DepthStencilState.DepthEnable = TRUE;
    p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    p.SampleDesc.Count = 1;
    if (FAILED(dev_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pso)))) return nullptr;
    return pso.Get();
}

// src's depth drawn into dst's depth buffer
void D3D12Backend::DrawDepthCopy(Target& src, Target& dst, ID3D12PipelineState* pso) {
    if (cap_) fprintf(cap_, "%04u depth copy %08X to %08X\n", cap_i_++, src.data, dst.data);
    ReadyRes(src);
    ReadyRes(dst);
    Transition(src.res.Get(), src.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(dst.res.Get(), dst.state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    D3D12_CPU_DESCRIPTOR_HANDLE dh = Cpu(dsv_heap_.Get(), dst.view, dsv_inc_);
    cl_->OMSetRenderTargets(0, nullptr, FALSE, &dh);
    SetFullViewport(dst.w, dst.h);
    uint32_t slot = AllocSrvSlots(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    dev_->CreateShaderResourceView(src.res.Get(), &sv, Cpu(srv_heap_.Get(), slot, srv_inc_));
    Alloc cb = Upload(256, 256);
    DrawFullScreen(pso, cb.gpu, slot);
    dst.drawn = true;
    ResWritten(dst);
    bound_rt_ = bound_ds_ = nullptr;
}

// a depth buffer drawn as colour into another depth buffer's memory: copied as depth
bool D3D12Backend::DepthCopy(const State& s) {
    TextureDesc d;
    if (!s.rt.header || !s.textures[0] || !Describe(s.textures[0], d) || Format(d.format).depth || d.cube ||
        d.depth != 1) {
        return false;
    }
    uint32_t gw;
    uint32_t gh;
    SurfaceSize(s.rt, gw, gh);
    auto di = targets_.find(SizedTargetKey(mem::VirtualFromPhysical(s.rt.data), true, gw, gh));
    auto si = targets_.find(SizedTargetKey(d.data, true, d.width, d.height));
    if (di == targets_.end() || si == targets_.end() || di == si) return false;
    Target& dst = di->second;
    Target& src = si->second;
    ID3D12PipelineState* pso = EnsureDepthCopyPipeline();
    if (!pso) return false;
    DrawDepthCopy(src, dst, pso);
    return true;
}

// the pixels the CPU changed since the last Sync, opaque, the rest 0; t.read takes them
std::vector<uint8_t> LatchCpuChanges(Target& t) {
    std::vector<uint8_t> px(t.read.size(), 0);
    uint32_t guest_pitch = GuestPitch(t.gw);
    const uint8_t* guest = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++) {
        for (uint32_t x = 0; x < t.gw; x++) {
            const uint8_t* c = guest + size_t(y) * guest_pitch + x * 4;
            size_t i = (size_t(y) * t.gw + x) * 4;
            if (!std::memcmp(c, &t.read[i], 4)) continue;
            std::memcpy(&t.read[i], c, 4);
            std::memcpy(&px[i], c, 3);
            px[i + 3] = 255;
        }
    }
    return px;
}

// BGRA pixels into a new w x h texture, left as a shader resource
ComPtr<ID3D12Resource> D3D12Backend::CreatePixelTexture(const std::vector<uint8_t>& px, uint32_t pitch, uint32_t w,
                                                        uint32_t h) {
    ComPtr<ID3D12Resource> tex;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = Texture2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, w, h);
    dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                  IID_PPV_ARGS(&tex));
    uint32_t row = UploadPitch(w * 4);
    Alloc a = Upload(uint64_t(row) * h, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    for (uint32_t y = 0; y < h; y++) std::memcpy(a.cpu + size_t(y) * row, &px[size_t(y) * pitch], w * 4);
    D3D12_TEXTURE_COPY_LOCATION dst{tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION src{a.res, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint.Offset = a.off;
    src.PlacedFootprint.Footprint = {DXGI_FORMAT_B8G8R8A8_UNORM, w, h, 1, row};
    cl_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_STATES st = D3D12_RESOURCE_STATE_COPY_DEST;
    Transition(tex.Get(), st, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    return tex;
}

// CPU pixels of a color surface into its host target, scaled
void D3D12Backend::UploadTarget(Target& t, const Surface& s) {
    g_perf.rtups++;
    TextureDesc d;
    if (!Describe(s.header, d) || Format(d.format).depth) return;
    ReadyRes(t);  // drawn over what the target holds
    std::vector<uint8_t> px;
    uint32_t pitch;
    // after a Sync only the pixels the CPU changed go over the picture, which stays at render scale
    bool over = !t.drawn && d.width == t.gw && d.height == t.gh && t.read.size() == size_t(t.gw) * t.gh * 4;
    if (over) {
        LOG_FIRST(4, "d3d12: CPU pixels drawn over target %08X", t.data);
        pitch = t.gw * 4;
        px = LatchCpuChanges(t);
    } else {
        DecodeLevel(d, 0, 0, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), nullptr, px, pitch);
    }
    ComPtr<ID3D12Resource> tex = CreatePixelTexture(px, pitch, d.width, d.height);
    Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    SetBlitTarget(Cpu(rtv_heap_.Get(), t.view, rtv_inc_), t.w, t.h);
    float uv[4] = {0, 0, 1, 1};
    float mul = 1 / t.range;
    Blit(tex.Get(), kBgra, uv, {.alpha = true, .mul = mul, .rtv = t.fmt, .over = over});
    CurrentFrame().garbage.push_back(tex);
    ResWritten(t);
    t.epoch = DirtyEpoch(t.data);
    t.sig = MemSig(t.data, t.bytes);
}

// samplers_ key of the full screen passes' samplers, apart from the stage samplers' hashed keys
constexpr uint64_t kBlitSamplerKey = 0xB117;

// four clamp samplers: linear at slot 0, point at slots 1 to 3
uint32_t D3D12Backend::EnsureBlitSamplers() {
    uint64_t key = kBlitSamplerKey;
    auto it = samplers_.find(key);
    uint32_t si;
    if (it == samplers_.end()) {
        if (smp_used_ + 4 > kSamplerCount) {
            samplers_.clear();
            smp_used_ = 0;
        }
        si = smp_used_;
        smp_used_ += 4;
        for (int i = 0; i < 4; i++) {
            D3D12_SAMPLER_DESC sd{};
            sd.Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sd.MaxLOD = D3D12_FLOAT32_MAX;
            dev_->CreateSampler(&sd, Cpu(smp_heap_.Get(), si + i, smp_inc_));
        }
        samplers_[key] = si;
    } else {
        si = it->second;
    }
    return si;
}

// the full screen triangle: cb at b0 and b1, views at srv_slot; without a sampler slot EnsureBlitSamplers gives it
void D3D12Backend::DrawFullScreen(ID3D12PipelineState* pso, D3D12_GPU_VIRTUAL_ADDRESS cb, uint32_t srv_slot,
                                  std::optional<uint32_t> sampler_slot) {
    cl_->SetPipelineState(pso);
    cl_->SetGraphicsRootConstantBufferView(0, cb);
    cl_->SetGraphicsRootConstantBufferView(1, cb);
    cl_->SetGraphicsRootDescriptorTable(2, Gpu(srv_heap_.Get(), srv_slot, srv_inc_));
    // without a slot the passes take the blit samplers, looked up at this point as they always were
    uint32_t si = sampler_slot ? *sampler_slot : EnsureBlitSamplers();
    cl_->SetGraphicsRootDescriptorTable(3, Gpu(smp_heap_.Get(), si, smp_inc_));
    cl_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl_->DrawInstanced(3, 1, 0, 0);
}

void D3D12Backend::Blit(ID3D12Resource* src, DXGI_FORMAT fmt, const float uvrect[4], const BlitOptions& o) {
    uint32_t slot = AllocSrvSlots(2);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = fmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    dev_->CreateShaderResourceView(src, &sv, Cpu(srv_heap_.Get(), slot, srv_inc_));
    sv.Format = kBgra;
    dev_->CreateShaderResourceView(lut_.Get(), &sv, Cpu(srv_heap_.Get(), slot + 1, srv_inc_));
    uint32_t si = EnsureBlitSamplers();
    Alloc cb = Upload(256, 256);
    const app::Settings& st = app::Get();
    uint32_t li = std::min<uint32_t>(g_colour_look, std::size(kLooks) - 1);
    const ColourLook& L = kLooks[li];
    float strength = o.look && li ? std::min(st.look_strength, 100u) / 100.0f : 0.0f;
    float c[32] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3],
                   o.gamma ? 1.0f : 0.0f, o.alpha ? 1.0f : 0.0f, float(o.taps), o.mul,
                   o.sharpen, o.texel[0], o.texel[1], 0,
                   o.hdr ? 1.0f : 0.0f, hdr_paper_, hdr_peak_ / hdr_paper_, 0,
                   L.contrast, L.pivot, L.sat, L.vibrance,
                   L.gamma, strength, 0, 0,
                   L.shadow[0], L.shadow[1], L.shadow[2], 0,
                   L.light[0], L.light[1], L.light[2], 0};
    std::memcpy(cb.cpu, c, sizeof(c));
    ID3D12PipelineState* pso = EnsureBlitPipeline(o.rtv, o.over);
    if (!pso) return;
    DrawFullScreen(pso, cb.gpu, slot, si);
}

// a compiled shader's file in the shader cache, by Compile's hash: the name covers the source, entry point, profile
// and compile options; a different compiler or flags make new names
static const char kCacheSalt[] = "d3dcompile O1 v1";
std::filesystem::path CachedShaderPath(uint64_t hash) {
    return app::ShaderCacheDir() / (pack::Name(Hash(kCacheSalt, sizeof(kCacheSalt), hash)) + ".dxbc");
}

// a compiled shader into the cache through a temporary file, so another thread never reads half of one
void WriteCachedShader(uint64_t hash, const void* data, size_t size) {
    std::filesystem::path cached = CachedShaderPath(hash);
    std::error_code ec;
    std::filesystem::create_directories(cached.parent_path(), ec);
    static std::atomic<uint32_t> seq{0};
    std::filesystem::path tmp = cached;
    tmp += ".tmp" + std::to_string(GetCurrentThreadId()) + "_" + std::to_string(seq++);
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(static_cast<const char*>(data), std::streamsize(size));
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    std::filesystem::rename(tmp, cached, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

uint64_t ShaderHash(const std::string& src, const char* entry, const char* profile) {
    uint64_t profile_seed = Hash(profile, std::strlen(profile));
    uint64_t entry_seed = Hash(entry, std::strlen(entry), profile_seed);
    return Hash(src.data(), src.size(), entry_seed);
}

// Shader seeds: shaders.seed beside the exe lists shader sources (entry point, profile, HLSL) recorded from every
// level of the game (XBE_SHADER_RECORD=<file> in a test build writes such a list); at a start, the ones not in the
// shader cache yet are compiled in the background, so a first play does not stall on them. pipelines.seed (the
// pipelines.bin layout) adds those pipelines to the warm-up. A seed shader the game never makes is only unused.
constexpr char kSeedMagic[4] = {'A', 'S', 'S', '1'};

bool ReadSeedString(FILE* f, std::string& out) {
    uint32_t n = 0;
    if (std::fread(&n, 4, 1, f) != 1 || n > (16u << 20)) return false;
    out.resize(n);
    return n == 0 || std::fread(out.data(), 1, n, f) == n;
}

std::vector<SeedShader> ReadShaderSeed(const std::filesystem::path& file) {
    std::vector<SeedShader> v;
    FILE* f = _wfopen(file.c_str(), L"rb");
    if (!f) return v;
    char magic[4];
    if (std::fread(magic, 4, 1, f) == 1 && !std::memcmp(magic, kSeedMagic, 4)) {
        SeedShader s;
        while (ReadSeedString(f, s.entry) && ReadSeedString(f, s.profile) && ReadSeedString(f, s.src)) v.push_back(s);
    }
    std::fclose(f);
    return v;
}

// XBE_SHADER_RECORD=<file>: each shader source compiled or read from the cache, once, appended to a seed file
void RecordShader(const std::string& src, const char* entry, const char* profile, uint64_t hash) {
    static const char* path = std::getenv("XBE_SHADER_RECORD");
    if (!path || !*path) return;
    static std::mutex mu;
    static std::unordered_set<uint64_t> seen;
    static FILE* f = nullptr;
    std::lock_guard<std::mutex> lock(mu);
    if (!f) {
        std::filesystem::path p = std::filesystem::u8path(path);
        for (const SeedShader& s : ReadShaderSeed(p)) seen.insert(ShaderHash(s.src, s.entry.c_str(), s.profile.c_str()));
        bool fresh = seen.empty();
        f = _wfopen(p.c_str(), fresh ? L"wb" : L"ab");
        if (!f) return;
        if (fresh) std::fwrite(kSeedMagic, 4, 1, f);
    }
    if (!seen.insert(hash).second) return;
    auto put = [&](const void* d, uint32_t n) {
        std::fwrite(&n, 4, 1, f);
        if (n) std::fwrite(d, 1, n, f);
    };
    put(entry, uint32_t(std::strlen(entry)));
    put(profile, uint32_t(std::strlen(profile)));
    put(src.data(), uint32_t(src.size()));
    std::fflush(f);
}

ComPtr<ID3DBlob> ReadCachedShader(uint64_t hash) {
    ComPtr<ID3DBlob> code;
    std::ifstream f(CachedShaderPath(hash), std::ios::binary | std::ios::ate);
    std::streamsize n = f ? std::streamsize(f.tellg()) : 0;
    if (n <= 0 || FAILED(D3DCreateBlob(SIZE_T(n), &code))) return nullptr;
    f.seekg(0);
    if (!f.read(static_cast<char*>(code->GetBufferPointer()), n)) return nullptr;
    return code;
}

ID3DBlob* D3D12Backend::Compile(const std::string& src, const char* entry, const char* profile, uint64_t& hash) {
    // one source can hold several entry points
    hash = ShaderHash(src, entry, profile);
    auto it = blobs_.find(hash);
    if (it != blobs_.end()) return it->second.Get();
    RecordShader(src, entry, profile, hash);
    // compiled once and kept in the shader cache (shadercache\ beside the exe, or --shader-cache): a shader the game
    // first uses mid-play (a cutscene ending, a new effect) does not stall the frame on D3DCompile again in later runs
    ComPtr<ID3DBlob> code = ReadCachedShader(hash);
    if (code) {
        blobs_[hash] = code;
        return code.Get();
    }
    ComPtr<ID3DBlob> err;
    PerfScope perf_compile{g_perf.compile};
    g_perf.shaders++;
    HRESULT hr = D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, entry, profile,
                            D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &code, &err);
    if (FAILED(hr)) {
        LOGE("d3d12: %s compile failed: %s", profile, err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        LOGE("%s", src.c_str());
        blobs_[hash] = nullptr;
        return nullptr;
    }
    blobs_[hash] = code;
    WriteCachedShader(hash, code->GetBufferPointer(), code->GetBufferSize());
    return code.Get();
}

// a full screen pass's pipeline: solid, no culling, one target, writing all channels; callers set the blend
D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12Backend::FullScreenPipelineDesc(ID3DBlob* vs, ID3DBlob* ps,
                                                                        DXGI_FORMAT rtv) const {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root_.Get();
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = rtv;
    d.SampleDesc.Count = 1;
    return d;
}

// kClearHlsl's pipeline for a format and channel mask, kept in blit_psos_; null when it can't be made
ID3D12PipelineState* D3D12Backend::EnsureMaskedClearPipeline(DXGI_FORMAT rtv, uint32_t mask, uint32_t samples) {
    auto& pso = blit_psos_[0xC1EA000000000000ull | uint64_t(samples) << 40 | uint64_t(rtv) << 8 | mask];
    if (pso) return pso.Get();
    uint64_t h;
    ID3DBlob* vs = Compile(kClearHlsl, "vs", "vs_5_0", h);
    ID3DBlob* ps = Compile(kClearHlsl, "psmain", "ps_5_0", h);
    if (!vs || !ps) return nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = FullScreenPipelineDesc(vs, ps, rtv);
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = UINT8(mask);
    d.SampleDesc.Count = samples;
    if (FAILED(dev_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) return nullptr;
    return pso.Get();
}

ID3D12PipelineState* D3D12Backend::EnsureBlitPipeline(DXGI_FORMAT rtv, bool over) {
    auto& pso = blit_psos_[uint64_t(rtv) | uint64_t(over) << 32];
    if (pso) return pso.Get();
    uint64_t h;
    ID3DBlob* vs = Compile(kBlitHlsl, "vs", "vs_5_0", h);
    ID3DBlob* ps = Compile(kBlitHlsl, "psmain", "ps_5_0", h);
    if (!vs || !ps) {
        LOG_FIRST(1, "d3d12: the picture pass did not compile");
        return nullptr;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = FullScreenPipelineDesc(vs, ps, rtv);
    if (over) {
        auto& blend = d.BlendState.RenderTarget[0];
        // premultiplied colour, the target keeps its alpha
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
        blend.DestBlendAlpha = D3D12_BLEND_ONE;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    if (FAILED(dev_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) {
        LOG_FIRST(1, "d3d12: the picture pass could not be made");
        return nullptr;
    }
    return pso.Get();
}

// Pass's pipeline for the shader, kept in blit_psos_; null when it can't be made
ID3D12PipelineState* D3D12Backend::EnsurePassPipeline(const char* hlsl, const char* entry, DXGI_FORMAT rtv,
                                                      float blend) {
    uint64_t seed = uint64_t(rtv) << 1 | (blend >= 0 ? 1 : 0);
    uint64_t source_seed = Hash(hlsl, std::strlen(hlsl), seed);
    auto& pso = blit_psos_[Hash(entry, std::strlen(entry), source_seed)];
    if (pso) return pso.Get();
    uint64_t h;
    ID3DBlob* vs = Compile(hlsl, "vs", "vs_5_0", h);
    ID3DBlob* ps = Compile(hlsl, entry, "ps_5_0", h);
    if (!vs || !ps) return nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = FullScreenPipelineDesc(vs, ps, rtv);
    if (blend >= 0) {
        auto& b = d.BlendState.RenderTarget[0];
        b.BlendEnable = TRUE;
        b.SrcBlend = b.SrcBlendAlpha = D3D12_BLEND_BLEND_FACTOR;
        b.DestBlend = b.DestBlendAlpha = D3D12_BLEND_INV_BLEND_FACTOR;
        b.BlendOp = b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    if (FAILED(dev_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) return nullptr;
    return pso.Get();
}

void D3D12Backend::Pass(const char* hlsl, const char* entry, DXGI_FORMAT rtv, D3D12_CPU_DESCRIPTOR_HANDLE rh,
                        uint32_t w, uint32_t h, ID3D12Resource* const src[3], const DXGI_FORMAT fmt[3],
                        const float p[8], float blend) {
    ID3D12PipelineState* pso = EnsurePassPipeline(hlsl, entry, rtv, blend);
    if (!pso) return;
    uint32_t slot = AllocSrvSlots(4);
    for (int i = 0; i < 4; i++) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = i < 3 && src[i] ? fmt[i] : kBgra;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        dev_->CreateShaderResourceView(i < 3 ? src[i] : nullptr, &sv, Cpu(srv_heap_.Get(), slot + i, srv_inc_));
    }
    Alloc cb = Upload(256, 256);
    std::memcpy(cb.cpu, p, 8 * sizeof(float));
    cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);
    SetFullViewport(w, h);
    if (blend >= 0) {
        float f[4] = {blend, blend, blend, blend};
        cl_->OMSetBlendFactor(f);
    }
    DrawFullScreen(pso, cb.gpu, slot);
    bound_rt_ = bound_ds_ = nullptr;
}

// a copy of the target to read while drawing to it
ID3D12Resource* D3D12Backend::Feedback(Target& t) {
    ReadyRes(t);
    if (!t.fb) {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC td = t.res->GetDesc();
        td.Flags = D3D12_RESOURCE_FLAG_NONE;
        t.fb_state = D3D12_RESOURCE_STATE_COPY_DEST;
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, t.fb_state, nullptr,
                                                 IID_PPV_ARGS(&t.fb)))) {
            return nullptr;
        }
    }
    Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(t.fb.Get(), t.fb_state, D3D12_RESOURCE_STATE_COPY_DEST);
    cl_->CopyResource(t.fb.Get(), t.res.Get());
    Transition(t.fb.Get(), t.fb_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    return t.fb.Get();
}

// the key's vertex attributes, ATTR0 to ATTR15 in register order; returns how many
uint32_t FillInputLayout(const PsoKey& k, D3D12_INPUT_ELEMENT_DESC el[16]) {
    uint32_t n = 0;
    for (uint32_t r = 0; r < 16; r++) {
        if (!((k.regs >> r) & 1)) continue;
        uint32_t e = k.elements[r];
        el[n++] = {"ATTR", r, DXGI_FORMAT(e & 0xFF), (e >> 8) & 0xFF, e >> 16,
                   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    }
    return n;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType(uint32_t topo) {
    switch (topo) {
        case 0: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case 1: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

constexpr uint32_t kFillWireframe = 0x1B01;  // D3DFILL_WIREFRAME
constexpr uint32_t kCullCw = 0x900;          // D3DCULL_CW

void FillDepthStencilDesc(const PsoKey& k, D3D12_DEPTH_STENCIL_DESC& ds) {
    ds.DepthEnable = k.zen;
    ds.DepthWriteMask = k.zwrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = Cmp(k.zfunc);
    ds.StencilEnable = k.sten;
    ds.StencilReadMask = UINT8(k.smask);
    ds.StencilWriteMask = UINT8(k.swmask);
    D3D12_DEPTH_STENCILOP_DESC so{StencilOp(k.sfail), StencilOp(k.szfail), StencilOp(k.spass), Cmp(k.sfunc)};
    ds.FrontFace = ds.BackFace = so;
}

// everything of the pipeline but its input layout
void D3D12Backend::FillPipelineDesc(const PsoKey& k, ID3DBlob* vs, ID3DBlob* ps,
                                    D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) const {
    d.pRootSignature = root_.Get();
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    auto& b = d.BlendState.RenderTarget[0];
    b.BlendEnable = k.blend;
    b.SrcBlend = Blend(k.src, false);
    b.DestBlend = Blend(k.dst, false);
    b.BlendOp = BlendOp(k.op);
    b.SrcBlendAlpha = Blend(k.src, true);
    b.DestBlendAlpha = Blend(k.dst, true);
    b.BlendOpAlpha = BlendOp(k.op);
    b.RenderTargetWriteMask = UINT8(k.wmask);
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = k.fill == kFillWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = k.cull ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    d.RasterizerState.FrontCounterClockwise = k.ccw;
    d.RasterizerState.DepthBias = k.zbias;
    d.RasterizerState.SlopeScaledDepthBias = k.zslope;
    d.RasterizerState.DepthClipEnable = TRUE;
    FillDepthStencilDesc(k, d.DepthStencilState);
    d.PrimitiveTopologyType = TopologyType(k.topo);
    d.NumRenderTargets = k.has_rt;
    d.RTVFormats[0] = k.has_rt ? DXGI_FORMAT(k.rtfmt) : DXGI_FORMAT_UNKNOWN;
    if (k.has_rt == 2) {
        // motion: written by what draws over the scene; of what blends into it, only alpha blended cut-outs (leaves,
        // grass, fences) where they are at least half opaque (the shader's alpha is 0 or 1); additive glows and the
        // like keep the motion of what is behind them
        d.RTVFormats[1] = kMotionFormat;
        d.BlendState.IndependentBlendEnable = TRUE;
        auto& m = d.BlendState.RenderTarget[1];
        bool cutout = k.blend && Blend(k.src, false) == D3D12_BLEND_SRC_ALPHA && Blend(k.dst, false) == D3D12_BLEND_INV_SRC_ALPHA;
        m.BlendEnable = cutout;
        m.SrcBlend = m.SrcBlendAlpha = cutout ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
        m.DestBlend = m.DestBlendAlpha = cutout ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
        m.BlendOp = m.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        m.RenderTargetWriteMask = k.blend && !cutout ? 0 : UINT8(D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN);
    }
    d.DSVFormat = k.has_ds ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = std::max(k.samples, 1u);
    d.BlendState.AlphaToCoverageEnable = k.a2c != 0;
}

ID3D12PipelineState* D3D12Backend::EnsurePipeline(const PsoKey& k, ID3DBlob* vs, ID3DBlob* ps) {
    uint64_t h = Hash(&k, sizeof(k));
    auto it = psos_.find(h);
    if (it != psos_.end()) return it->second.Get();
    {
        // made ahead by the warm-up: taken over; otherwise the warm-up leaves it to this thread
        std::lock_guard<std::mutex> lock(warm_mu_);
        auto w = warm_psos_.find(h);
        if (w != warm_psos_.end()) {
            ID3D12PipelineState* p = (psos_[h] = std::move(w->second)).Get();
            warm_psos_.erase(w);
            return p;
        }
        warm_taken_.insert(h);
    }
    D3D12_INPUT_ELEMENT_DESC el[16];
    uint32_t n = FillInputLayout(k, el);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.InputLayout = {el, n};
    FillPipelineDesc(k, vs, ps, d);
    ComPtr<ID3D12PipelineState> pso;
    g_perf.psos++;
    auto perf_pso = PerfClock::now();
    if (FAILED(dev_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) LOGE("d3d12: pipeline creation failed");
    g_perf.pso += PerfMs(perf_pso);
    psos_[h] = pso;
    // listed for the next start's warm-up (the list stops growing at 50000)
    if (pso && pso_list_ && listed_.size() < 50000 && listed_.insert(h).second) {
        std::fwrite(&k, sizeof(k), 1, pso_list_);
        std::fflush(pso_list_);
    }
    return pso.Get();
}

// pipelines.bin: a header, then PsoKeys as EnsurePipeline built them (zeroed padding, so equal keys are equal bytes)
struct PipelineListHeader {
    char magic[4] = {'A', 'P', 'L', '1'};
    uint32_t key_size = sizeof(PsoKey);
};

void D3D12Backend::StartWarmUp() {
    std::filesystem::path file = app::ShaderCacheDir() / "pipelines.bin";
    std::vector<PsoKey> keys;
    PipelineListHeader want;
    bool fresh = true;
    if (FILE* f = _wfopen(file.c_str(), L"rb")) {
        PipelineListHeader have;
        if (std::fread(&have, sizeof(have), 1, f) == 1 && !std::memcmp(&have, &want, sizeof(want))) {
            fresh = false;
            PsoKey k;
            while (std::fread(&k, sizeof(k), 1, f) == 1)
                if (listed_.insert(Hash(&k, sizeof(k))).second) keys.push_back(k);
        }
        std::fclose(f);
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // a list from another build's key layout (or a damaged one) is started again
    pso_list_ = _wfopen(file.c_str(), fresh ? L"wb" : L"ab");
    if (pso_list_ && fresh) {
        std::fwrite(&want, sizeof(want), 1, pso_list_);
        std::fflush(pso_list_);
    }
    if (!pso_list_) LOGW("d3d12: the pipeline list %s can't be written; no warm-up next time", file.string().c_str());
    size_t own = keys.size();
    // the release's pipeline seed (pipelines.seed beside the exe, the same layout): added after the player's own
    std::unordered_set<uint64_t> have;
    for (const PsoKey& k : keys) have.insert(Hash(&k, sizeof(k)));
    if (FILE* f = _wfopen((app::ExeDir() / "pipelines.seed").c_str(), L"rb")) {
        PipelineListHeader h;
        if (std::fread(&h, sizeof(h), 1, f) == 1 && !std::memcmp(&h, &want, sizeof(want))) {
            PsoKey k;
            while (std::fread(&k, sizeof(k), 1, f) == 1)
                if (have.insert(Hash(&k, sizeof(k))).second) keys.push_back(k);
        }
        std::fclose(f);
    }
    std::vector<SeedShader> seed = ReadShaderSeed(app::ExeDir() / "shaders.seed");
    if (keys.empty() && seed.empty()) return;
    LOGR("d3d12: warming up %zu pipelines from earlier runs and %zu from the seed, %zu seed shaders", own,
         keys.size() - own, seed.size());
    warm_ = std::thread([this, keys = std::move(keys), seed = std::move(seed)]() mutable {
        CompileSeed(std::move(seed));
        WarmUp(std::move(keys));
    });
}

// the seed's shaders not in the cache yet, compiled on a few background threads (half the processors, at most 4)
void D3D12Backend::CompileSeed(std::vector<SeedShader> seed) {
    if (seed.empty()) return;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    auto start = PerfClock::now();
    std::vector<size_t> todo;
    for (size_t i = 0; i < seed.size(); i++) {
        std::error_code ec;
        uint64_t h = ShaderHash(seed[i].src, seed[i].entry.c_str(), seed[i].profile.c_str());
        if (!std::filesystem::exists(CachedShaderPath(h), ec)) todo.push_back(i);
    }
    if (todo.empty()) return;
    std::atomic<size_t> next{0}, made{0};
    unsigned n = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < n; t++)
        pool.emplace_back([&] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            for (size_t i; !warm_stop_ && (i = next++) < todo.size();) {
                const SeedShader& s = seed[todo[i]];
                ComPtr<ID3DBlob> code, err;
                if (FAILED(D3DCompile(s.src.data(), s.src.size(), nullptr, nullptr, nullptr, s.entry.c_str(),
                                      s.profile.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &code, &err)))
                    continue;
                WriteCachedShader(ShaderHash(s.src, s.entry.c_str(), s.profile.c_str()), code->GetBufferPointer(),
                                  code->GetBufferSize());
                made++;
            }
        });
    for (auto& t : pool) t.join();
    LOGR("d3d12: compiled %zu seed shaders in %.1f s (%zu were cached already)", made.load(), PerfMs(start) / 1000.0,
         seed.size() - todo.size());
}

void D3D12Backend::WarmUp(std::vector<PsoKey> keys) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    auto start = PerfClock::now();
    std::unordered_map<uint64_t, ComPtr<ID3DBlob>> blobs;
    auto blob = [&](uint64_t hash) -> ID3DBlob* {
        auto it = blobs.find(hash);
        if (it == blobs.end()) it = blobs.emplace(hash, ReadCachedShader(hash)).first;
        return it->second.Get();
    };
    size_t made = 0, missing = 0;
    for (const PsoKey& k : keys) {
        if (warm_stop_) break;
        uint64_t h = Hash(&k, sizeof(k));
        {
            std::lock_guard<std::mutex> lock(warm_mu_);
            if (warm_taken_.count(h)) continue;
        }
        ID3DBlob* vs = blob(k.vs);
        ID3DBlob* ps = blob(k.ps);
        if (!vs || !ps) {
            missing++;
            continue;
        }
        D3D12_INPUT_ELEMENT_DESC el[16];
        uint32_t n = FillInputLayout(k, el);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.InputLayout = {el, n};
        FillPipelineDesc(k, vs, ps, d);
        ComPtr<ID3D12PipelineState> pso;
        if (FAILED(dev_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) continue;
        std::lock_guard<std::mutex> lock(warm_mu_);
        if (warm_taken_.insert(h).second) {
            warm_psos_[h] = std::move(pso);
            made++;
        }
    }
    LOGR("d3d12: warm-up made %zu pipelines in %.1f s%s", made, PerfMs(start) / 1000.0,
         missing ? " (some cached shaders were missing)" : "");
}

D3D12Backend::~D3D12Backend() {
    warm_stop_ = true;
    if (warm_.joinable()) warm_.join();
    if (pso_list_) std::fclose(pso_list_);
}

TexKind TextureKind(const TextureDesc& d) {
    if (d.cube) return kTexCube;
    if (d.depth > 1) return kTex3D;
    return kTex2D;
}

constexpr float kUpscaledMin = 1.01f;  // host texels per console texel past rounding: the target is upscaled

ID3D12Resource* D3D12Backend::BindTargetTexture(const State& s, int stage, const TextureDesc& d, Target& t, bool box,
                                                DrawTextures& stages) {
    TexInfo& info = stages.info[stage];
    D3D12_SHADER_RESOURCE_VIEW_DESC& sv = stages.sv[stage];
    if (&t == draw_ds_) return nullptr;
    bool feedback = &t == draw_rt_;
    stages.rt[stage] = true;
    if (!t.depth && LatchCpuWrite(t)) {
        Surface su;
        su.header = s.textures[size_t(stage)];
        UploadTarget(t, su);
    }
    info.kind = kTex2D;
    // a pass shrinking it averages each tap over a console texel, else upscaled taps skip texels and ghost
    Target* rt = FindTarget(s.rt, false);
    float k = float(t.w) / (t.gw * stages.k[stage]);
    if (!t.depth && box && k > kUpscaledMin && rt && rt != &t && (t.gw > rt->gw || t.gh > rt->gh)) {
        stages.wide[stage] = k;
        info.flags |= kTexWide;
    }
    if (ext_) ext_->Sampled(d, &t, 0, info);
    sv.Format = t.depth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : t.fmt;
    // the console reads the pixels it is about to overwrite: give the draw a copy
    if (feedback) return Feedback(t);
    ReadyRes(t);
    Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (t.depth) info.shadow = ShadowDepthKind(d.format);
    info.kind = kTex2D;
    return t.res.Get();
}

bool SameTexture(const Tex& tx, const TextureDesc& d) {
    return tx.res && tx.d.format == d.format && tx.d.width == d.width && tx.d.height == d.height &&
           tx.d.depth == d.depth && tx.d.levels == d.levels && tx.d.cube == d.cube && tx.d.pitch == d.pitch;
}

// false when the texture could not be made, its entry dropped
bool D3D12Backend::CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, uint64_t key) {
    g_perf.newtex++;
    PerfScope perf_create{g_perf.create};
    if (tx.res) CurrentFrame().garbage.push_back(tx.res);
    if (tx.repl) CurrentFrame().garbage.push_back(tx.repl);
    if (tx.own) CurrentFrame().garbage.push_back(tx.own);
    tx = Tex{};
    tx.d = d;
    tx.fmt = HostFormat(fi.host);
    bool bc = fi.compressed;
    uint32_t w = d.width;
    uint32_t h = d.height;
    tx.levels = d.levels;
    if (bc && ((w & 3) || (h & 3))) {
        w = (w + 3) & ~3u;
        h = (h + 3) & ~3u;
        tx.levels = 1;
    }
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td{};
    td.Dimension = d.depth > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w;
    td.Height = h;
    td.DepthOrArraySize = UINT16(d.cube ? 6 : d.depth);
    td.MipLevels = UINT16(tx.levels);
    td.Format = tx.fmt;
    td.SampleDesc.Count = 1;
    if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&tx.res)))) {
        textures_.erase(key);
        return false;
    }
    return true;
}

void CopyLevelRows(uint8_t* out, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp, UINT rows, UINT64 row_size,
                   const std::vector<uint8_t>& px, uint32_t pitch) {
    uint32_t slices = fp.Footprint.Depth;
    size_t src_rows = pitch ? px.size() / pitch / std::max(slices, 1u) : 0;
    for (uint32_t z = 0; z < slices; z++) {
        for (uint32_t y = 0; y < rows; y++) {
            uint8_t* dst = out + (size_t(z) * rows + y) * fp.Footprint.RowPitch;
            if (y < src_rows) {
                std::memcpy(dst, &px[(size_t(z) * src_rows + y) * pitch], std::min<size_t>(pitch, row_size));
            } else {
                std::memset(dst, 0, size_t(row_size));
            }
        }
    }
}

void D3D12Backend::UploadTextureLevel(Tex& tx, uint32_t f, uint32_t l, const std::vector<uint8_t>& px,
                                      uint32_t pitch) {
    D3D12_RESOURCE_DESC rd = tx.res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT rows;
    UINT64 row_size;
    UINT64 bytes;
    uint32_t sub = f * tx.levels + l;
    dev_->GetCopyableFootprints(&rd, sub, 1, 0, &fp, &rows, &row_size, &bytes);
    Alloc a = Upload(bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    CopyLevelRows(a.cpu, fp, rows, row_size, px, pitch);
    fp.Offset = a.off;
    D3D12_TEXTURE_COPY_LOCATION dl{tx.res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    dl.SubresourceIndex = sub;
    D3D12_TEXTURE_COPY_LOCATION sl{a.res, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    sl.PlacedFootprint = fp;
    cl_->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
}

void D3D12Backend::UploadTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi, const uint8_t* p,
                                 const uint32_t* colours, bool same) {
    D3D12_RESOURCE_STATES st = same ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_DEST;
    Transition(tx.res.Get(), st, D3D12_RESOURCE_STATE_COPY_DEST);
    uint32_t faces = d.cube ? 6 : 1;
    std::vector<uint8_t> px;
    // a plain 2D texture in a format a DDS file holds can be dumped and replaced
    pack::Format pf = pack::Format(tx.fmt);
    // linear textures only for a game's own replacements (g_builtin_contents), never dumped
    bool packable = !d.cube && d.depth == 1 && (!d.linear || pack::g_builtin_contents) && pack::Supported(pf) &&
                    (pack_lib_.Size() || app::Get().texture_dump || pack::g_builtin || pack::g_builtin_contents);
    std::vector<std::vector<uint8_t>> levels;
    for (uint32_t f = 0; f < faces; f++) {
        for (uint32_t l = 0; l < tx.levels; l++) {
            uint32_t pitch;
            DecodeLevel(d, l, f, p, colours, px, pitch);
            if (packable && !d.linear && app::Get().texture_dump && f == 0 && l == 0) {
                // the level as decoded, before an extension changes it, rows tightly packed
                uint32_t lw = std::max(d.width >> l, 1u), lh = std::max(d.height >> l, 1u);
                uint32_t rb = pack::RowBytes(pf, lw), rows = pack::Rows(pf, lh);
                std::vector<uint8_t>& out = levels.emplace_back(size_t(rb) * rows, 0);
                for (uint32_t y = 0; y < rows && size_t(y + 1) * pitch <= px.size(); y++)
                    std::memcpy(&out[size_t(y) * rb], &px[size_t(y) * pitch], std::min(rb, pitch));
            }
            if (ext_) ext_->Decoded(d, fi, f, l, px, pitch, tx.flags);
            UploadTextureLevel(tx, f, l, px, pitch);
        }
    }
    Transition(tx.res.Get(), st, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (tx.repl) CurrentFrame().garbage.push_back(std::move(tx.repl));  // may still be drawn by a frame in flight
    tx.repl = nullptr;
    tx.pack_hash = 0;
    if (packable) TexturePack(tx, d, p, colours, levels);
}

// the texture's contents hashed (with its format, size and a P8 texture's colours): written to textures/dump with
// texture_dump on, and a file of that name under textures/ drawn instead
void D3D12Backend::TexturePack(Tex& tx, const TextureDesc& d, const uint8_t* p, const uint32_t* colours,
                               const std::vector<std::vector<uint8_t>>& levels) {
    uint64_t seed = uint64_t(d.format) | uint64_t(d.width) << 8 | uint64_t(d.height) << 24 | uint64_t(d.levels) << 40;
    uint64_t h = Hash(p, TotalSize(d), seed);
    if (colours) h = Hash(colours, 256 * sizeof(uint32_t), h);
    if (!levels.empty() && dumped_.insert(h).second) {
        // level 0 as a PNG, written on the pack thread
        std::vector<uint8_t> rgba;
        std::filesystem::path file = app::TextureDir() / "dump" / pack::PngName(d.width, d.height, h);
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
        } else if (pack::ToRgba(pack::Format(tx.fmt), d.width, d.height, levels[0], rgba)) {
            pack::WritePngLater(file, d.width, d.height, std::move(rgba));
        } else {
            LOG_FIRST(4, "texture pack: %s is in a format a PNG does not take", file.filename().string().c_str());
        }
    }
    tx.pack_hash = h;
    const std::filesystem::path* file = pack_lib_.Find(h);
    if (!file && !pack::g_builtin && !pack::g_builtin_contents) return;
    auto [it, fresh] = pack_cache_.try_emplace(h);
    if (fresh && file && !LoadPackTexture(*file, it->second)) LOGW("texture pack: %s is not a PNG or DDS file the renderer takes", file->string().c_str());
    if (fresh && !file && d.linear && tx.own && tx.own_hash == h) {
        pack_cache_.erase(it);  // the same contents as last time: the picture made then
        tx.repl = tx.own;
        tx.repl_fmt = tx.own_fmt;
        tx.repl_levels = tx.own_levels;
        return;
    }
    if (fresh && !file) {
        pack::Image img;
        bool got = pack::g_builtin_contents &&
                   pack::g_builtin_contents({d.format, d.width, d.height, d.levels, p, TotalSize(d), colours}, img);
        if (!got && pack::g_builtin) got = pack::g_builtin(h, d.width, d.height, img);
        if (got) CreatePackTexture(img, pack::PngName(d.width, d.height, h), it->second);
        if (got && d.linear) {
            // a game's own picture for a linear texture (text it draws afresh, Fellowship's sharper text): kept by
            // this texture alone, not by contents, as such textures change all the time
            if (tx.own) CurrentFrame().garbage.push_back(std::move(tx.own));
            tx.own = it->second.res;
            tx.own_fmt = it->second.fmt;
            tx.own_levels = it->second.levels;
            tx.own_hash = h;
            tx.repl = tx.own;
            tx.repl_fmt = tx.own_fmt;
            tx.repl_levels = tx.own_levels;
            pack_cache_.erase(it);
            return;
        }
        if (!got && d.linear) {  // not kept: linear textures are often movie frames, each new
            pack_cache_.erase(it);
            return;
        }
    }
    if (!it->second.res) return;
    tx.repl = it->second.res;
    tx.repl_fmt = it->second.fmt;
    tx.repl_levels = it->second.levels;
}

// a PNG or DDS file into a texture of its own size: a DDS file's mip levels, or ones made for a PNG
bool D3D12Backend::LoadPackTexture(const std::filesystem::path& file, PackTex& out) {
    pack::Image img;
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (!(ext == ".png" ? pack::ReadPng(file, img) : pack::ReadDds(file, img))) return false;
    return CreatePackTexture(img, file.filename().string(), out);
}

// a picture, with its mip levels, into a texture of its own size
bool D3D12Backend::CreatePackTexture(const pack::Image& img, const std::string& name, PackTex& out) {
    bool block = img.format != pack::Format::BGRA8 && img.format != pack::Format::RGBA8;
    if (block && ((img.width & 3) || (img.height & 3))) return false;  // BC level 0 must be whole blocks
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = img.width;
    td.Height = img.height;
    td.DepthOrArraySize = 1;
    td.MipLevels = UINT16(img.levels.size());
    td.Format = DXGI_FORMAT(img.format);
    td.SampleDesc.Count = 1;
    Tex t;
    if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&t.res)))) {
        return false;
    }
    t.levels = uint32_t(img.levels.size());
    for (uint32_t l = 0; l < t.levels; l++)
        UploadTextureLevel(t, 0, l, img.levels[l], pack::RowBytes(img.format, std::max(img.width >> l, 1u)));
    D3D12_RESOURCE_STATES st = D3D12_RESOURCE_STATE_COPY_DEST;
    Transition(t.res.Get(), st, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    out.res = t.res;
    out.fmt = td.Format;
    out.levels = t.levels;
    LOG_FIRST(16, "texture pack: %s, %ux%u, %u levels", name.c_str(), img.width, img.height,
              t.levels);
    return true;
}

void FillTextureView(const Tex& tx, const TextureDesc& d, D3D12_SHADER_RESOURCE_VIEW_DESC& sv) {
    sv.Format = tx.fmt;
    if (d.cube) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = tx.levels;
    } else if (d.depth > 1) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        sv.Texture3D.MipLevels = tx.levels;
    } else {
        sv.Texture2D.MipLevels = tx.levels;
    }
}

// a P8 texture's palette: its colours, their address (part of the texture's key) and their hash, all 0 without one
struct StagePalette {
    uint32_t colours[256] = {};
    uint32_t va = 0;
    uint64_t hash = 0;
};

// a P8 texture is decoded with the stage's palette: 256 colours, fewer by the size code in the top two bits
void ReadStagePalette(const State& s, int stage, const TextureDesc& d, StagePalette& palette) {
    constexpr uint32_t kFmtP8 = 0x0B;          // D3DFMT_P8
    constexpr uint32_t kPaletteCommon = 0;     // D3DPalette: Common, its top two bits the size code
    constexpr uint32_t kPaletteData = 4;       // D3DPalette: Data, physical address of the colours
    constexpr uint32_t kPaletteSizeShift = 30;
    if (d.format != kFmtP8 || !s.palettes[size_t(stage)]) return;
    uint32_t header = s.palettes[size_t(stage)];
    palette.va = mem::VirtualFromPhysical(kernel::R32(header + kPaletteData));
    uint32_t size_code = kernel::R32(header + kPaletteCommon) >> kPaletteSizeShift;
    size_t bytes = (std::size(palette.colours) >> size_code) * sizeof(uint32_t);
    std::memcpy(palette.colours, kernel::HostPtr(palette.va), bytes);
    palette.hash = Hash(palette.colours, sizeof(palette.colours));
}

// a texture in guest memory: made again when its layout changed, uploaded again when its bytes did
ID3D12Resource* D3D12Backend::BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi,
                                               DrawTextures& stages) {
    TexInfo& info = stages.info[stage];
    StagePalette palette;
    ReadStagePalette(s, stage, d, palette);
    const uint64_t key = uint64_t(palette.va) << 32 | d.data;
    Tex& tx = textures_[key];
    uint32_t total = TotalSize(d);
    bool same = SameTexture(tx, d);
    if (!same && !CreateTexture(tx, d, fi, key)) return nullptr;
    // cheap change check once per frame: lock epochs plus a sample of the bytes
    if (tx.frame != presents_ + 1 || !same || tx.palette != palette.hash) {
        tx.frame = presents_ + 1;
        tx.palette = palette.hash;
        const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
        auto perf_t = PerfClock::now();
        uint64_t sig = MemSig(d.data, total) ^ DirtyEpoch(d.data) ^ palette.hash;
        g_perf.sig += PerfMs(perf_t);
        if (sig != tx.sig || !same) {
            PerfScope perf_up{g_perf.texup};
            g_perf.texups++;
            g_perf.texup_bytes += total;
            tx.sig = sig;
            UploadTexture(tx, d, fi, p, palette.va ? palette.colours : nullptr, same);
        }
    }
    FillTextureView(tx, d, stages.sv[stage]);
    if (fi.depth) info.shadow = ShadowDepthKind(d.format);
    if (ext_) ext_->Sampled(d, nullptr, tx.flags, info);
    // dumping: the first screen space draw of each texture names its file, to find a HUD's pieces by
    if (tx.pack_hash && app::Get().texture_dump && g_screen_space_test && !hud_logged_.count(tx.pack_hash) &&
        g_screen_space_test(s)) {
        hud_logged_.insert(tx.pack_hash);
        LOGR("texture pack: drawn in 2D (HUD, menu, text): %s", pack::PngName(d.width, d.height, tx.pack_hash).c_str());
    }
    if (tx.repl && !g_pack_off) {
        stages.sv[stage].Format = tx.repl_fmt;
        stages.sv[stage].Texture2D.MipLevels = tx.repl_levels;
        return tx.repl.Get();
    }
    return tx.res.Get();
}

ID3D12Resource* D3D12Backend::BindTexture(const State& s, int stage, DrawTextures& stages) {
    TexInfo& info = stages.info[stage];
    D3D12_SHADER_RESOURCE_VIEW_DESC& sv = stages.sv[stage];
    info = {};
    sv = {};
    stages.rt[stage] = false;
    stages.k[stage] = 1;
    stages.wide[stage] = 0;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sv.Texture2D.MipLevels = 1;
    TextureDesc d;
    if (!s.textures[size_t(stage)] || !Describe(s.textures[size_t(stage)], d)) return nullptr;
    const FormatInfo& fi = Format(d.format);
    info.kind = TextureKind(d);
    info.rect = d.linear;

    // rendered by the GPU: sample the target itself; DXT data loaded over a target's memory is not it
    uint64_t alt = 0;
    bool box = true;
    if (ext_) ext_->Sample(d.data, fi.depth, stage, alt, stages.k[stage], box);
    auto tit = targets_.find(SizedTargetKey(d.data, fi.depth, d.width, d.height) | alt);
    if (tit != targets_.end() && !d.cube && d.depth == 1 && !fi.compressed) {
        return BindTargetTexture(s, stage, d, tit->second, box, stages);
    }
    return BindGuestTexture(s, stage, d, fi, stages);
}

uint32_t SimpleState(const State& s, uint32_t method);  // below
// D3DTEXF: 1 point, 2 linear, 3 anisotropic
constexpr uint32_t kTexfLinear = 2;
constexpr uint32_t kTexfAnisotropic = 3;
// D3DTADDRESS values the samplers test
constexpr uint32_t kTaddressWrap = 1;
constexpr uint32_t kTaddressClamp = 3;

// extra: added to the mip bias (console_textures on a 3D draw)
D3D12_SAMPLER_DESC StageSampler(const State& s, int i, const DrawTextures& stages, bool clamp, uint32_t aniso,
                                float extra) {
    D3D12_SAMPLER_DESC sd = {};
    const auto& t = s.tss[size_t(i)];
    uint32_t mag = t[kTssMagFilter];
    uint32_t min = t[kTssMinFilter];
    uint32_t mip = t[kTssMipFilter];
    auto lin = [](uint32_t f) { return f >= kTexfLinear; };
    D3D12_FILTER_TYPE fmin = lin(min) ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE fmag = lin(mag) ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    D3D12_FILTER_TYPE fmip = mip == kTexfLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    sd.Filter = D3D12_FILTER(fmin << 4 | fmag << 2 | fmip);
    bool anisotropic = min == kTexfAnisotropic || mag == kTexfAnisotropic;
    uint32_t an = anisotropic ? std::max<uint32_t>(t[kTssMaxAnisotropy], 1) : 1;
    // forced only where the title filters across mip levels and the texture has them
    if (aniso && lin(min) && mip && stages.sv[i].Texture2D.MipLevels > 1 && !stages.info[i].shadow) an = aniso;
    if (an > 1) {
        sd.Filter = D3D12_FILTER_ANISOTROPIC;
        sd.MaxAnisotropy = std::min<uint32_t>(an, 16);
    }
    // smooth_2d: the bicubic taps need a linear sampler, on the top level as the taps read it
    if (stages.info[i].flags & kTexSmooth) sd.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    // clamp: the draw's coordinates all lie in 0..1 (UnitTexcoords), so wrap is read as clamp
    sd.AddressU = Address(clamp && t[kTssAddressU] == kTaddressWrap ? kTaddressClamp : t[kTssAddressU]);
    sd.AddressV = Address(clamp && t[kTssAddressV] == kTaddressWrap ? kTaddressClamp : t[kTssAddressV]);
    sd.AddressW = Address(t[kTssAddressW]);
    sd.MipLODBias = Bits(t[kTssMipLodBias]) + app::Get().lod_bias + extra;  // texture sharpness: 0 as the game asks
    sd.MinLOD = float(t[kTssMaxMipLevel]);
    sd.MaxLOD = mip ? D3D12_FLOAT32_MAX : sd.MinLOD;
    Color(t[kTssBorderColor], sd.BorderColor);
    if (stages.info[i].shadow) {
        // the console tests texel OP r, D3D12 tests r OP texel
        static const uint8_t swap[8] = {0, 4, 2, 6, 1, 5, 3, 7};
        sd.Filter = lin(min) || lin(mag) ? D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
                                         : D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        sd.ComparisonFunc = D3D12_COMPARISON_FUNC(swap[Cmp(s.rs[kRsShadowFunc]) - 1] + 1);
        sd.MaxAnisotropy = 0;
    }
    return sd;
}

uint32_t D3D12Backend::EnsureStageSamplers(const State& s, const DrawTextures& stages, bool clamp) {
    D3D12_SAMPLER_DESC sd[4];
    uint32_t aniso = app::Get().aniso;
    // console_textures: a 3D draw's textures one mip level softer per doubling of the console's 480 lines, so they
    // show the detail they had on a TV (2D pictures, render targets and the shadow maps keep theirs)
    float extra = 0;
    float lines = scale_ * float(guest_h_);
    if (app::Get().console_textures && lines > 480.0f && !(g_screen_space_test && g_screen_space_test(s)))
        extra = std::log2(lines / 480.0f);
    for (int i = 0; i < 4; i++) sd[i] = StageSampler(s, i, stages, clamp, aniso, stages.rt[i] ? 0.0f : extra);
    // Above the console's 480 lines, textures the game reads unfiltered in a 3D draw would be enlarged texel by
    // texel into blocks the console never showed: there they were shrunk or drawn at about their own size.
    //   - "linear when shrunk, point when enlarged": enlarged linearly too, as the texture was read on the console;
    //   - cut-out pictures with one-bit alpha read point both ways in an alpha-tested draw (Gates of Moria's hanging
    //     moss and reeds, 128x128 A1R5G5B5): read linearly, so the alpha test cuts a smooth edge instead of steps.
    // Other point-read textures (lookup tables, ramps) are left as the game reads them.
    static const bool keep_point = std::getenv("XBE_KEEP_POINT") != nullptr;  // A/B test: as before
    if (!keep_point && lines > 1.2f * 480.0f && !(g_screen_space_test && g_screen_space_test(s))) {
        bool alpha_test = SimpleState(s, kMAlphaTestEnable) != 0;
        for (int i = 0; i < 4; i++) {
            if (stages.rt[i] || stages.info[i].shadow) continue;
            UINT f = UINT(sd[i].Filter);
            if (f >= 0x40) continue;  // anisotropic, comparison or min/max: not a point read
            bool min_linear = (f >> 4) & 1, mag_linear = (f >> 2) & 1;
            if (mag_linear) continue;
            UINT add = 0;
            if (min_linear) {
                add = 1u << 2;
            } else if (alpha_test) {
                TextureDesc td;
                // A1R5G5B5 (swizzled 0x02, linear 0x10) and DXT1 (0x0C): one-bit alpha
                if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], td) &&
                    (td.format == 0x02 || td.format == 0x10 || td.format == 0x0C))
                    add = (1u << 2) | (1u << 4);
            }
            if (add) {
                sd[i].Filter = D3D12_FILTER(f | add);
                LOG_FIRST(1, "d3d12: textures the game enlarges unfiltered are enlarged smoothly (%.0f lines)", lines);
            }
        }
    }
    uint64_t key = Hash(sd, sizeof(sd));
    auto it = samplers_.find(key);
    if (it != samplers_.end()) return it->second;
    if (smp_used_ + 4 > kSamplerCount) {
        samplers_.clear();
        smp_used_ = 0;
    }
    uint32_t base = smp_used_;
    smp_used_ += 4;
    for (int i = 0; i < 4; i++) dev_->CreateSampler(&sd[i], Cpu(smp_heap_.Get(), base + i, smp_inc_));
    samplers_[key] = base;
    return base;
}

uint32_t SimpleState(const State& s, uint32_t method) {
    return s.simple[method >> 2];
}

constexpr uint32_t kQuadVertices = 6;  // a quad drawn as two triangles

DrawShaders ClassifyShaders(const State& s, const Assembled& a, uint32_t count) {
    DrawShaders sh;
    // a shader made from a declaration alone (no program) is the fixed function pipeline on those inputs
    sh.prog = (s.vs_handle & 1) && s.vs && !s.vs->program.empty();
    // up to a quad with no depth test: it may be a pass over the whole target, see QuadHlsl
    sh.quad = count <= kQuadVertices && !s.rs[kRsZEnable];
    sh.fixed_transform = !sh.prog && !a.pretransformed;
    if (!sh.prog) sh.fk = MakeFixedKey(s);
    return sh;
}

// the program's hash is cached in the guest shader object
void EnsureProgramHash(const State& s) {
    if (!s.vs->hash) s.vs->hash = Hash(s.vs->program.data(), s.vs->program.size() * 4) | 1;
}

// vertex shader cache key salts
constexpr uint64_t kDeclarationSalt = 0x10000;    // fixed function seed, above the 16 bit register mask
constexpr uint64_t kPretransformedSalt = 0x5A5A;  // screen space fixed function vertices, apart from untransformed
constexpr uint64_t kQuadSalt = 0xC3C30000;        // the QuadHlsl variant, apart from the plain shader

uint64_t VertexKey(const State& s, const Assembled& a, const DrawShaders& sh) {
    uint64_t seed = sh.prog ? s.vs->hash ^ a.regs : Hash(&s.vs_handle, 4, a.regs | kDeclarationSalt);
    uint64_t vkey = Hash(&a.decode, 8, seed);
    if (!sh.prog && a.pretransformed) vkey ^= kPretransformedSalt;
    if (sh.quad) vkey ^= kQuadSalt;
    if (!sh.prog) vkey = Hash(&sh.fk, sizeof(sh.fk), vkey);
    return vkey;
}

std::string VertexHlsl(const State& s, const Assembled& a, const DrawShaders& sh) {
    if (sh.prog) return VertexProgramHlsl(s.vs->program, a.regs, a.decode);
    return FixedVertexHlsl(a.regs, a.pretransformed, a.decode, sh.fk);
}

// sets sh.vh and sh.vs_code, compiling the shader on a cache miss
void D3D12Backend::EnsureVertexShader(const State& s, const Assembled& a, DrawShaders& sh, MotionKind mk) {
    auto vit = shader_src_.find(sh.vkey);
    if (vit != shader_src_.end()) {
        sh.vs_code = blobs_[vit->second].Get();
        sh.vh = vit->second;
        return;
    }
    std::string src = VertexHlsl(s, a, sh);
    if (sh.quad) src = QuadHlsl(std::move(src));
    if (mk) src = MotionHlsl(std::move(src), mk);
    sh.vs_code = Compile(src, "main", "vs_5_0", sh.vh);
    shader_src_[sh.vkey] = sh.vh;
}

bool D3D12Backend::BindStageTexturesAndTargets(const State& s, DrawTextures& stages) {
    auto perf_t = PerfClock::now();
    draw_rt_ = bound_rt_;
    draw_ds_ = bound_ds_;
    for (int i = 0; i < 4; i++) stages.res[i] = BindTexture(s, i, stages);
    // HUD sharpening, as ROTK Fix: with [graphics] sharpening on, the pictures of 2D draws are sharpened at their own
    // texel size, more the larger they are drawn (40% per 480 lines past the first 480, up to 90%)
    float hud = 0;
    if (g_screen_space_test && app::Get().sharpening && !g_sharpen_off && !g_movie)
        hud = std::clamp(0.4f * (scale_ * float(guest_h_) / 480.0f - 1.0f), 0.0f, 0.9f);
    // smooth_2d: a 2D picture the game reads unfiltered (point), as menus and HUD pictures often are, turns to blocks
    // once the frame is drawn larger than the console's; it is upscaled bicubic instead (and not sharpened as well)
    bool smooth = g_screen_space_test && app::Get().smooth_2d && !g_movie && scale_ * float(guest_h_) > 1.2f * 480.0f;
    bool screen_space = (hud > 0 || smooth) && g_screen_space_test(s);
    if (smooth && screen_space) {
        for (int i = 0; i < 4; i++) {
            TexInfo& t = stages.info[i];
            // linear (rect) pictures too: the menus' full-screen backgrounds are 640 x 480 ones; their coordinates
            // reach the taps already divided by the size
            if (!stages.res[i] || stages.rt[i] || t.kind != kTex2D || t.shadow || (t.flags & kTexWide)) continue;
            if (s.tss[size_t(i)][kTssMagFilter] >= kTexfLinear) continue;  // filtered already
            LOG_FIRST(1, "d3d12: smooth 2D pictures on (%.0f lines)", scale_ * float(guest_h_));
            t.flags |= kTexSmooth;
            // an alpha-only picture (A8, swizzled or linear) is a font: its letters keep a crisp edge
            TextureDesc td;
            if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], td) && (td.format == 0x19 || td.format == 0x1F))
                t.flags |= kTexCrisp;
            D3D12_RESOURCE_DESC rd = stages.res[i]->GetDesc();
            stages.sharp_texel[i][0] = 1.0f / float(std::max<UINT64>(rd.Width, 1));
            stages.sharp_texel[i][1] = 1.0f / float(std::max<UINT>(rd.Height, 1));
        }
    }
    if (hud > 0 && screen_space) {
        LOG_FIRST(1, "d3d12: sharper HUD on (%.0f%% at %.0f lines)", hud * 100, scale_ * float(guest_h_));
        for (int i = 0; i < 4; i++) {
            TexInfo& t = stages.info[i];
            if (!stages.res[i] || stages.rt[i] || t.kind != kTex2D || t.rect || t.shadow ||
                (t.flags & (kTexWide | kTexSmooth)))
                continue;
            t.flags |= kTexSharp;
            stages.sharp[i] = hud;
            // the texel of what is bound: a texture pack's picture may be larger than the console's
            D3D12_RESOURCE_DESC rd = stages.res[i]->GetDesc();
            stages.sharp_texel[i][0] = 1.0f / float(std::max<UINT64>(rd.Width, 1));
            stages.sharp_texel[i][1] = 1.0f / float(std::max<UINT>(rd.Height, 1));
        }
    }
    g_perf.bind += PerfMs(perf_t);
    // textures may have recorded copies that unbound the targets
    return BindTargets(s);
}

constexpr uint64_t kPixelKeySalt = 0x9E3779B97F4A7C15ull;

// sets sh.ph and sh.ps_code, compiling the shader on a cache miss
void D3D12Backend::EnsurePixelShader(const PixelKey& pk, DrawShaders& sh, bool motion) {
    // pixel shaders share the map with vertex shaders, the salt keeps their keys apart
    auto pit = shader_src_.find(sh.pkey ^ kPixelKeySalt);
    if (pit != shader_src_.end()) {
        sh.ps_code = blobs_[pit->second].Get();
        sh.ph = pit->second;
        return;
    }
    sh.ps_code = Compile(motion ? MotionPixelHlsl(PixelHlsl(pk)) : PixelHlsl(pk), "main", "ps_5_0", sh.ph);
    shader_src_[sh.pkey ^ kPixelKeySalt] = sh.ph;
}

void KeyBlend(const State& s, PsoKey& k) {
    k.blend = SimpleState(s, kMBlendEnable) != 0;
    if (k.blend) {
        k.src = SimpleState(s, kMSrcBlend);
        k.dst = SimpleState(s, kMDestBlend);
        k.op = SimpleState(s, kMBlendOp);
    }
    uint32_t cw = SimpleState(s, kMColorWriteEnable);
    // console ARGB bytes to D3D12 RGBA bits
    k.wmask = ((cw >> 16) & 1) | ((cw >> 8) & 1) << 1 | (cw & 1) << 2 | ((cw >> 24) & 1) << 3;
}

constexpr float kDepth24Max = 16777215.0f;  // 2^24 minus 1: a 24 bit depth buffer's units per 1.0

void D3D12Backend::KeyDepthStencil(const State& s, const Assembled& a, PsoKey& k) {
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
    // polygon offset: bias in depth buffer units, the target is always 24 bit; the slope is per console pixel,
    // and an upscaled target's pixels are smaller
    if (a.topology == Topology::Triangles && SimpleState(s, kMSolidOffsetEnable)) {
        k.zbias = int32_t(std::lround(Bits(SimpleState(s, kMDepthOffset)) * kDepth24Max / s.zscale));
        k.zslope = Bits(SimpleState(s, kMSlopeScale)) * (bound_ds_ ? float(bound_ds_->w) / bound_ds_->gw : 1.0f);
    }
}

void KeyRasterizer(const State& s, const Assembled& a, PsoKey& k) {
    uint32_t cull = s.rs[kRsCullMode];
    k.cull = cull != 0 && a.topology == Topology::Triangles;
    k.ccw = cull == kCullCw;  // culling clockwise leaves counter clockwise as the front
    k.fill = s.rs[kRsFillMode];
}

void D3D12Backend::BuildPipelineKey(const State& s, const Assembled& a, uint64_t vh, uint64_t ph, PsoKey& k) {
    std::memset(&k, 0, sizeof(k));
    k.vs = vh;
    k.ps = ph;
    k.regs = a.regs;
    for (int r = 0; r < 16; r++) {
        if ((a.regs >> r) & 1) {
            const VertexElement& e = a.elements[size_t(r)];
            k.elements[r] = e.format | uint32_t(e.buffer) << 8 | uint32_t(e.offset) << 16;
        }
    }
    k.topo = uint32_t(a.topology);
    k.has_rt = bound_rt_ ? (bound_mv_ ? 2 : 1) : 0;  // 2: with the upscaler's motion target
    k.has_ds = bound_ds_ != nullptr;
    k.rtfmt = bound_rt_ ? bound_rt_->fmt : 0;
    k.samples = bound_ms_ ? samples_ : 1;
    KeyBlend(s, k);
    // smooth cut-out edges: an alpha test passing above the reference, unblended, into anti-aliased targets
    if (g_alpha_edges && k.samples > 1 && !k.blend && SimpleState(s, kMAlphaTestEnable)) {
        int f = int(Cmp(SimpleState(s, kMAlphaFunc))) - 1;
        k.a2c = f == 4 || f == 6;  // greater, greater or equal
    }
    if (k.has_ds) KeyDepthStencil(s, a, k);
    KeyRasterizer(s, a, k);
}

// alpha test off: compare always, in Cmp() minus 1 terms like the enabled test
constexpr float kAlphaTestAlways = float(D3D12_COMPARISON_FUNC_ALWAYS - 1);

// D3DRS_FOGTABLEMODE values
constexpr uint32_t kFogTableNone = 0;
constexpr float kFogTableLinear = 3.0f;

// table mode none under a vertex program: oFog is the blend factor, passed on as linear fog from 1 to 0
void FillFogConstants(const State& s, bool vertex_program, DrawConstants& c) {
    if (!s.rs[kRsFogEnable]) return;
    if (vertex_program && s.rs[kRsFogTableMode] == kFogTableNone) {
        c.fog[0] = 1.0f;
        c.fog[1] = 0.0f;
        c.fog[2] = 0.0f;
        c.fog[3] = kFogTableLinear;
        return;
    }
    float k = g_fog_scale;
    c.fog[0] = Bits(s.rs[kRsFogStart]) * k;
    c.fog[1] = Bits(s.rs[kRsFogEnd]) * k;
    c.fog[2] = Bits(s.rs[kRsFogDensity]) / k;
    c.fog[3] = float(s.rs[kRsFogTableMode]);
}

void FillSurfaceConstants(const State& s, GuestExtent size, DrawConstants& c) {
    c.surf[0] = float(size.w);
    c.surf[1] = float(size.h);
    c.surf[2] = 1.0f / s.zscale;
    Color(s.rs[kRsFogColor], c.fog_color);
    c.misc[0] = float(SimpleState(s, kMAlphaRef) & 0xFF);
    c.misc[1] = SimpleState(s, kMAlphaTestEnable) ? float(Cmp(SimpleState(s, kMAlphaFunc)) - 1) : kAlphaTestAlways;
    c.misc[2] = float(Cmp(s.rs[kRsShadowFunc]) - 1);
    c.misc[3] = s.rs[kRsSpecularEnable] ? 1.0f : 0.0f;
}

void FillCombinerConstants(const State& s, DrawConstants& c) {
    for (int i = 0; i < 8; i++) {
        Color(SimpleState(s, kMPsConstant0 + i * 4), c.pc[i * 2]);
        Color(SimpleState(s, kMPsConstant1 + i * 4), c.pc[i * 2 + 1]);
    }
    Color(SimpleState(s, kMPsFinalConstant0), c.pc[16]);
    Color(SimpleState(s, kMPsFinalConstant1), c.pc[17]);
    Color(s.rs[kRsTextureFactor], c.tfactor);
}

constexpr float kGlowBoxTexels = 3.0f;  // console texels across the box each wide tap averages

void D3D12Backend::FillTexelConstants(int i, const TextureDesc& d, const DrawTextures& stages, DrawConstants& c) {
    c.texsize[i][0] = float(d.width);
    c.texsize[i][1] = float(d.height);
    if (stages.sharp[i] > 0 || (stages.info[i].flags & kTexSmooth)) {
        c.spread[i][0] = stages.sharp_texel[i][0];
        c.spread[i][1] = stages.sharp_texel[i][1];
        c.spread[i][2] = stages.sharp[i];
    }
    // the console samples pixels at integer positions, D3D12 half a pixel on: targets are read half a texel back
    if (!stages.rt[i] || (stages.info[i].flags & kTexExact)) return;
    float k = stages.k[i];
    c.texsize[i][2] = -0.5f / (float(d.width) * k);
    c.texsize[i][3] = -0.5f / (float(d.height) * k);
    // a 3 console texel box per tap keeps the console chain's glow width and brightness
    if (stages.wide[i] > 0) {
        float n = std::ceil(kGlowBoxTexels * stages.wide[i] * 0.5f);
        c.spread[i][0] = kGlowBoxTexels / (n * float(d.width) * k);
        c.spread[i][1] = kGlowBoxTexels / (n * float(d.height) * k);
        c.spread[i][2] = n;
    }
}

void D3D12Backend::FillTextureConstants(const State& s, const DrawTextures& stages, DrawConstants& c) {
    for (int i = 0; i < 4; i++) {
        TextureDesc d;
        if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], d)) FillTexelConstants(i, d, stages, c);
        const auto& t = s.tss[size_t(i)];
        c.bump[i][0] = Bits(t[kTssBumpEnvMat00]);
        c.bump[i][1] = Bits(t[kTssBumpEnvMat01]);
        c.bump[i][2] = Bits(t[kTssBumpEnvMat10]);
        c.bump[i][3] = Bits(t[kTssBumpEnvMat11]);
        c.bumpl[i][0] = Bits(t[kTssBumpEnvLScale]);
        c.bumpl[i][1] = Bits(t[kTssBumpEnvLOffset]);
    }
}

void MatMul(const float* x, const float* y, float* o) {
    for (int r = 0; r < 4; r++) {
        for (int col = 0; col < 4; col++) {
            o[r * 4 + col] = x[r * 4] * y[col] + x[r * 4 + 1] * y[4 + col] + x[r * 4 + 2] * y[8 + col] +
                             x[r * 4 + 3] * y[12 + col];
        }
    }
}

// HLSL mul(row vector, matrix) with column major packing wants the transpose
void Transpose(const float* m, float* o) {
    for (int r = 0; r < 4; r++) {
        for (int col = 0; col < 4; col++) o[col * 4 + r] = m[r * 4 + col];
    }
}

void InverseTranspose3x3(const float* wv, float* n) {
    float det = wv[0] * (wv[5] * wv[10] - wv[6] * wv[9]) - wv[1] * (wv[4] * wv[10] - wv[6] * wv[8]) +
                wv[2] * (wv[4] * wv[9] - wv[5] * wv[8]);
    float id = det != 0 ? 1.0f / det : 0.0f;
    for (int r = 0; r < 3; r++) {
        for (int col = 0; col < 3; col++) {
            int r1 = (r + 1) % 3;
            int r2 = (r + 2) % 3;
            int c1 = (col + 1) % 3;
            int c2 = (col + 2) % 3;
            n[r * 4 + col] = (wv[r1 * 4 + c1] * wv[r2 * 4 + c2] - wv[r1 * 4 + c2] * wv[r2 * 4 + c1]) * id;
        }
    }
}

// D3DTRANSFORMSTATETYPE indices into s.transforms
constexpr size_t kTsView = 2;
constexpr size_t kTsProjection = 3;
constexpr size_t kTsTexture0 = 16;
constexpr size_t kTsWorld = 256;

void FillTransformConstants(const State& s, GuestExtent size, DrawConstants& c, float place_a, float place_b) {
    uint32_t gw = size.w;
    uint32_t gh = size.h;
    // world, view, projection, then the viewport inside the target
    float wv[16];
    float wvp[16];
    MatMul(s.transforms[kTsWorld].data(), s.transforms[kTsView].data(), wv);
    MatMul(wv, s.transforms[kTsProjection].data(), wvp);
    // moved sideways by the game (g_ortho_place): clip x' = a * x + b * w
    if (place_a != 1.0f || place_b != 0.0f)
        for (int r = 0; r < 4; r++) wvp[r * 4] = place_a * wvp[r * 4] + place_b * wvp[r * 4 + 3];
    const auto& v = s.viewport;
    float sx = float(v.w) / gw;
    float sy = float(v.h) / gh;
    float ox = (2.0f * v.x + v.w) / gw - 1;
    float oy = 1 - (2.0f * v.y + v.h) / gh;
    float vpm[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, v.max_z - v.min_z, 0, ox, oy, v.min_z, 1};
    float full[16];
    MatMul(wvp, vpm, full);
    Transpose(full, c.wvp);
    Transpose(wv, c.wv);
    // normals take the inverse transpose of the upper 3x3
    float n[16] = {};
    InverseTranspose3x3(wv, n);
    Transpose(n, c.wvn);
    for (int t = 0; t < 4; t++) Transpose(s.transforms[kTsTexture0 + size_t(t)].data(), c.tm[t]);
}

// D3DLIGHT8 as floats: Type, Diffuse[4], Specular[4], Ambient[4], Position[3], Direction[3], Range, Falloff,
// Attenuation0 to 2, Theta, Phi
constexpr int kLightFloats = 26;
constexpr int kLightDiffuse = 1;
constexpr int kLightSpecular = 5;
constexpr int kLightAmbient = 9;
constexpr int kLightPosition = 13;
constexpr int kLightDirection = 16;
constexpr int kLightRange = 19;
constexpr int kLightFalloff = 20;
constexpr int kLightAttenuation0 = 21;
constexpr int kLightAttenuation1 = 22;
constexpr int kLightAttenuation2 = 23;
constexpr int kLightTheta = 24;
constexpr int kLightPhi = 25;

void LightToView(const std::array<uint32_t, kLightFloats>& l, const float* vm, float (&o)[7][4]) {
    float f[kLightFloats];
    std::memcpy(f, l.data(), sizeof(f));
    for (int j = 0; j < 4; j++) {
        o[0][j] = f[kLightDiffuse + j];
        o[1][j] = f[kLightSpecular + j];
        o[2][j] = f[kLightAmbient + j];
    }
    const float* pos = f + kLightPosition;
    const float* dir = f + kLightDirection;
    for (int j = 0; j < 3; j++) {
        o[3][j] = pos[0] * vm[j] + pos[1] * vm[4 + j] + pos[2] * vm[8 + j] + vm[12 + j];
        o[4][j] = dir[0] * vm[j] + dir[1] * vm[4 + j] + dir[2] * vm[8 + j];
    }
    o[3][3] = f[kLightRange];
    o[4][3] = f[kLightFalloff];
    o[5][0] = f[kLightAttenuation0];
    o[5][1] = f[kLightAttenuation1];
    o[5][2] = f[kLightAttenuation2];
    o[6][0] = std::cos(f[kLightTheta] * 0.5f);
    o[6][1] = std::cos(f[kLightPhi] * 0.5f);
}

void FillLightConstants(const State& s, const FixedKey& fk, DrawConstants& c) {
    std::memcpy(c.mat, s.material.data(), sizeof(float) * 16);
    c.mat[4][0] = s.material[16];
    Color(s.rs[kRsAmbient], c.amb);
    // lights to view space
    const float* vm = s.transforms[kTsView].data();
    for (int i = 0; i < 8; i++) {
        if (!((fk.types >> (i * 2)) & 3)) continue;
        LightToView(s.lights[size_t(i)], vm, c.lt[i]);
    }
}

void D3D12Backend::UploadConstants(const State& s, const DrawShaders& sh, const PsoKey& k, const DrawTextures& stages,
                                   GuestExtent size, DrawConstants& c) {
    Alloc vc = Upload(sizeof(s.vsc), 256);
    std::memcpy(vc.cpu, s.vsc.data(), sizeof(s.vsc));
    Alloc dc = Upload(sizeof(DrawConstants), 256);
    FillSurfaceConstants(s, size, c);
    c.surf[3] = k.a2c ? 1.0f : 0.0f;
    FillFogConstants(s, sh.prog, c);
    FillCombinerConstants(s, c);
    if (ext_) ext_->Constants(reinterpret_cast<float(*)[4]>(vc.cpu), c.ext, bound_rt_, k.blend ? k.src : 1);
    FillTextureConstants(s, stages, c);
    if (sh.fixed_transform) {
        FillTransformConstants(s, size, c, place_a_, place_b_);
        FillLightConstants(s, sh.fk, c);
    }
    std::memcpy(dc.cpu, &c, sizeof(c));
    cl_->SetGraphicsRootConstantBufferView(0, vc.gpu);
    cl_->SetGraphicsRootConstantBufferView(1, dc.gpu);
    if (bound_mv_) {
        // b2: the draw's constants a frame ago (its own now when it is new: no motion of its own), and the jitter
        struct MotionConstants {
            float cprev[192][4];
            float wvp_prev[16];
            float jit[4];
        };
        Alloc mc = Upload(sizeof(MotionConstants), 256);
        auto* m = reinterpret_cast<MotionConstants*>(mc.cpu);
        const PrevDraw* pd = nullptr;
        if (mv_key_) {
            mv_draws_++;
            for (uint64_t key : mv_keys_) {
                auto it = prev_index_.find(key);
                if (it != prev_index_.end()) {
                    pd = &prev_draws_[it->second];
                    mv_found_++;
                    break;
                }
            }
            uint32_t idx = uint32_t(cur_draws_.size());
            cur_draws_.emplace_back();
            std::memcpy(cur_draws_.back().vsc.data(), s.vsc.data(), sizeof(s.vsc));
            std::memcpy(cur_draws_.back().wvp, c.wvp, sizeof(c.wvp));
            for (uint64_t key : mv_keys_) cur_index_.emplace(key, idx);
        }
        std::memcpy(m->cprev, pd ? pd->vsc.data() : s.vsc.data(), sizeof(m->cprev));
        std::memcpy(m->wvp_prev, pd ? pd->wvp : c.wvp, sizeof(m->wvp_prev));
        std::memcpy(m->jit, jit_, sizeof(m->jit));
        cl_->SetGraphicsRootConstantBufferView(4, mc.gpu);
    }
}

void D3D12Backend::CaptureTextures(const State& s, const DrawTextures& stages) {
    for (int i = 0; i < 4; i++) {
        ID3D12Resource* res = stages.res[i];
        TextureDesc d;
        if (!s.textures[size_t(i)] || !Describe(s.textures[size_t(i)], d)) continue;
        bool tg = targets_.count(SizedTargetKey(d.data, Format(d.format).depth, d.width, d.height));
        fprintf(cap_, " | t%d %08X fmt %X %ux%u%s%s", i, d.data, d.format, d.width, d.height, tg ? " target" : "",
                res ? "" : " unbound");
        if (res && !tg && stages.info[i].kind == kTex2D && cap_seen_.insert(d.data).second) {
            cap_tex_.push_back({res, stages.sv[i].Format, d.data, d.width, d.height});
        }
    }
}

// s.vsc holds the 192 vertex shader constants in hardware order: D3D register + 96
constexpr int kVsConstants = 192;
constexpr int kVsConstantBias = 96;

void D3D12Backend::CaptureInputs(const State& s, const Assembled& a, const DrawConstants& c) {
    fprintf(cap_, "\n     verts");
    uint32_t n = 0;
    for (int r = 0; r < 16; r++) n += (a.regs >> r) & 1;
    for (size_t v = 0; v < std::min<size_t>(4, n ? a.vertices.size() / (n * 4) : 0); v++) {
        fprintf(cap_, " [");
        for (uint32_t k = 0; k < n * 4; k++) fprintf(cap_, " %g", a.vertices[v * n * 4 + k]);
        fprintf(cap_, " ]");
    }
    for (int r = 0; r < kVsConstants; r++) {
        const auto& v = s.vsc[r];
        if (v[0] || v[1] || v[2] || v[3]) {
            fprintf(cap_, "\n     c%d %g %g %g %g", r - kVsConstantBias, v[0], v[1], v[2], v[3]);
        }
    }
    fprintf(cap_, "\n     pc");
    for (size_t r = 0; r < std::size(c.pc); r++) {
        fprintf(cap_, " (%.3f %.3f %.3f %.3f)", c.pc[r][0], c.pc[r][1], c.pc[r][2], c.pc[r][3]);
    }
}

// the draw's line: its state, then its textures
void D3D12Backend::CaptureDraw(const State& s, uint32_t prim, uint32_t count, const PsoKey& k, const DrawConstants& c,
                               const DrawTextures& stages) {
    fprintf(cap_,
            "%04u draw rt %08X ds %08X prim %u count %u vs %08X ps %08X blend %u %X %X zen %u %X zwrite %u "
            "zbias %d %.2f cull %u cw %X c0 %.3f %.3f %.3f %.3f",
            cap_i_++, s.rt.data, bound_ds_ ? s.zs.data : 0, prim, count, s.vs_handle, s.ps ? s.ps->handle : 0, k.blend,
            k.src, k.dst, k.zen, k.zfunc, k.zwrite, k.zbias, k.zslope, k.cull, k.wmask, c.pc[0][0], c.pc[0][1],
            c.pc[0][2], c.pc[0][3]);
    CaptureTextures(s, stages);
}

// small draws: their inputs and constants; then ends the draw's line
void D3D12Backend::EndDrawCapture(const State& s, const Assembled& a, uint32_t count, const DrawConstants& c) {
    if (count <= kQuadVertices) CaptureInputs(s, a, c);
    fputc('\n', cap_);
}

// each shader once, as translated
void D3D12Backend::CaptureShader(const char* kind, uint64_t key, const std::string& src) {
    if (!cap_sh_.insert(key).second) return;
    char name[48];
    snprintf(name, sizeof(name), "%s_%016llX.hlsl", kind, (unsigned long long)key);
    if (FILE* f = _wfopen((cap_dir_ / name).c_str(), L"w")) {
        fwrite(src.data(), 1, src.size(), f);
        fclose(f);
    }
    fprintf(cap_, "     %s\n", name);
}

void D3D12Backend::CaptureShaders(const State& s, const Assembled& a, const DrawShaders& sh, const PixelKey& pk) {
    std::string src = VertexHlsl(s, a, sh);
    CaptureShader("vs", sh.vkey, sh.quad ? QuadHlsl(std::move(src)) : src);
    CaptureShader("ps", sh.pkey, PixelHlsl(pk));
}

D3D12_SRV_DIMENSION NullViewDimension(uint8_t kind) {
    if (kind == kTexCube) return D3D12_SRV_DIMENSION_TEXTURECUBE;
    if (kind == kTex3D) return D3D12_SRV_DIMENSION_TEXTURE3D;
    return D3D12_SRV_DIMENSION_TEXTURE2D;
}

// A small draw whose every float2 input lies in 0..1: sprites, HUD pieces, text, full screen passes. The console's
// pixel centres sample such a quad from its first texel to half a texel short of its far edge; upscaled, the far
// edge's last half texel is drawn too, and wrapped it blends in the opposite edge (thin dark lines on HUD sprites).
// Clamped, it takes the edge texel instead, which a wrapped texture used once over 0..1 has there anyway.
bool UnitTexcoords(const Assembled& a) {
    constexpr float kLo = -1e-4f;
    constexpr float kHi = 1.0001f;
    constexpr uint32_t kSmallDrawVertices = 4096;  // most vertices a small draw has
    if (!a.vertices.empty() || !a.vertex_count || a.vertex_count > kSmallDrawVertices) return false;
    bool any = false;
    for (int r = 0; r < 16; r++) {
        const VertexElement& e = a.elements[size_t(r)];
        if (!(a.regs >> r & 1) || e.format != DXGI_FORMAT_R32G32_FLOAT) continue;
        const VertexBuffer& vb = a.buffers[e.buffer];
        if (!vb.src || !vb.stride || uint64_t(a.vertex_count) * vb.stride > vb.bytes) return false;
        for (uint32_t v = 0; v < a.vertex_count; v++) {
            float uv[2];
            std::memcpy(uv, vb.src + size_t(v) * vb.stride + e.offset, sizeof(uv));
            if (!(uv[0] >= kLo && uv[0] <= kHi && uv[1] >= kLo && uv[1] <= kHi)) return false;
        }
        any = true;
    }
    return any;
}

// a stage without a texture gets a null view of its kind; the samplers read the views after
void D3D12Backend::SetTextureViewsAndSamplers(const State& s, const Assembled& a, DrawTextures& stages) {
    for (int i = 0; i < 4; i++) {
        D3D12_SHADER_RESOURCE_VIEW_DESC& sv = stages.sv[i];
        if (!stages.res[i]) {
            sv.ViewDimension = NullViewDimension(stages.info[i].kind);
            sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        }
    }
    uint32_t slot;
    if (last_views_.frame == frame_ && std::memcmp(last_views_.res, stages.res, sizeof(last_views_.res)) == 0 &&
        std::memcmp(last_views_.sv, stages.sv, sizeof(last_views_.sv)) == 0) {
        slot = last_views_.slot;
    } else {
        slot = AllocSrvSlots(4);
        for (int i = 0; i < 4; i++)
            dev_->CreateShaderResourceView(stages.res[i], &stages.sv[i], Cpu(srv_heap_.Get(), slot + i, srv_inc_));
        last_views_.frame = frame_;
        last_views_.slot = slot;
        std::memcpy(last_views_.res, stages.res, sizeof(last_views_.res));
        std::memcpy(last_views_.sv, stages.sv, sizeof(last_views_.sv));
    }
    cl_->SetGraphicsRootDescriptorTable(2, Gpu(srv_heap_.Get(), slot, srv_inc_));
    uint32_t samplers = EnsureStageSamplers(s, stages, UnitTexcoords(a));
    cl_->SetGraphicsRootDescriptorTable(3, Gpu(smp_heap_.Get(), samplers, smp_inc_));
}

// viewport covers the target, the guest viewport is in the shader; scissor clips to it
void D3D12Backend::SetViewportScissor(const State& s, uint32_t gw, uint32_t gh) {
    float W = float(bound_rt_ ? bound_rt_->w : bound_ds_->w);
    float H = float(bound_rt_ ? bound_rt_->h : bound_ds_->h);
    D3D12_VIEWPORT vp{0, 0, W, H, 0, 1};
    cl_->RSSetViewports(1, &vp);
    float sx = W / gw, sy = H / std::max(gh, 1u);  // across and down differ with g_wide_targets
    LONG x0 = LONG(s.viewport.x * sx);
    LONG y0 = LONG(s.viewport.y * sy);
    LONG x1 = LONG((s.viewport.x + s.viewport.w) * sx);
    LONG y1 = LONG((s.viewport.y + s.viewport.h) * sy);
    if (s.scissor_count) {
        x0 = std::max(x0, LONG(s.scissors[0] * sx));
        y0 = std::max(y0, LONG(s.scissors[1] * sy));
        x1 = std::min(x1, LONG(s.scissors[2] * sx));
        y1 = std::min(y1, LONG(s.scissors[3] * sy));
    }
    D3D12_RECT rect{std::clamp<LONG>(x0, 0, LONG(W)), std::clamp<LONG>(y0, 0, LONG(H)),
                    std::clamp<LONG>(x1, 0, LONG(W)), std::clamp<LONG>(y1, 0, LONG(H))};
    cl_->RSSetScissorRects(1, &rect);
}

void D3D12Backend::SetBlendFactorStencilRef(const State& s) {
    float bf[4];
    Color(SimpleState(s, kMBlendColor), bf);
    // D3DBLEND_CONSTANTALPHA and its inverse (0x8003, 0x8004) weigh the colour by the blend colour's alpha, and are
    // drawn with D3D12's blend factor, so the factor is that alpha in all four channels; with the colour itself the
    // RGB weight was the colour's RGB (ROTK fades its characters with black ones, 0xF4000000 and so on: they vanished).
    // Only when no blend in the draw takes the constant colour (0x8001, 0x8002) as well, which one factor can't serve.
    auto constant_alpha = [](uint32_t f) { return f == 0x8003 || f == 0x8004; };
    auto constant_color = [](uint32_t f) { return f == 0x8001 || f == 0x8002; };
    uint32_t src = SimpleState(s, kMSrcBlend), dst = SimpleState(s, kMDestBlend);
    if ((constant_alpha(src) || constant_alpha(dst)) && !constant_color(src) && !constant_color(dst)) {
        bf[0] = bf[1] = bf[2] = bf[3];
    }
    cl_->OMSetBlendFactor(bf);
    cl_->OMSetStencilRef(SimpleState(s, kMStencilRef) & 0xFF);
}

D3D_PRIMITIVE_TOPOLOGY PrimitiveTopology(const Assembled& a) {
    if (a.topology == Topology::Points) return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    if (a.topology == Topology::Lines) {
        return a.strip ? D3D_PRIMITIVE_TOPOLOGY_LINESTRIP : D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    }
    return a.strip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

void D3D12Backend::UploadVertexBuffers(const Assembled& a) {
    D3D12_VERTEX_BUFFER_VIEW vbv[16];
    for (uint32_t i = 0; i < a.buffer_count; i++) {
        const VertexBuffer& b = a.buffers[i];
        Alloc vb = Upload(b.bytes, 16);
        std::memcpy(vb.cpu, b.src, b.bytes);
        vbv[i] = {vb.gpu, b.bytes, b.stride};
    }
    cl_->IASetVertexBuffers(0, a.buffer_count, vbv);
}

void D3D12Backend::DrawGeometry(const Assembled& a) {
    UploadVertexBuffers(a);
    cl_->IASetPrimitiveTopology(PrimitiveTopology(a));
    if (a.index_count) {
        uint32_t ibytes = a.index_count * (a.index16 ? 2 : 4);
        Alloc ib = Upload(ibytes, 4);
        std::memcpy(ib.cpu, a.index_src, ibytes);
        D3D12_INDEX_BUFFER_VIEW ibv{ib.gpu, ibytes, a.index16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
        cl_->IASetIndexBuffer(&ibv);
        cl_->DrawIndexedInstanced(a.index_count, 1, 0, a.base_vertex, 0);
    } else {
        cl_->DrawInstanced(a.draw_count, 1, 0, 0);
    }
}

// calls the extension's EndDraw on every return from Draw once made
struct DrawEnd {
    Extension* e;
    ~DrawEnd() {
        if (e) e->EndDraw();
    }
};

// Assemble, timed and counted
bool D3D12Backend::AssembleDraw(const State& s, uint32_t prim, uint32_t start, uint32_t count,
                                const uint8_t* indices, Assembled& a) {
    auto perf_t = PerfClock::now();
    bool assembled = Assemble(s, prim, start, count, indices, a);
    g_perf.assemble += PerfMs(perf_t);
    g_perf.verts += a.vertex_count;
    g_perf.raw += a.vertices.empty();
    return assembled;
}

void D3D12Backend::Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) {
    PerfScope perf_draw{g_perf.draw};
    g_perf.draws++;
    if (g_draw_observer) g_draw_observer(s);
    if (cap_) CapStep(s.rt);
    if (DepthCopy(s)) return;
    DrawEnd draw_end{ext_};
    if (ext_) ext_->BeginDraw(s);
    Assembled& a = asm_;
    if (!AssembleDraw(s, prim, start, count, indices, a)) return;
    place_a_ = 1.0f;
    place_b_ = 0.0f;
    if (g_ortho_place) {
        float x0, x1, y0, y1;
        if (OrthoExtent(s, start, count, indices, x0, x1, y0, y1) && !g_ortho_place(s, x0, x1, y0, y1, place_a_, place_b_)) {
            place_a_ = 1.0f;
            place_b_ = 0.0f;
        }
    }
    // past the ring every upload is a buffer of its own: seconds lost on a frame that draws a whole level once
    if (CurrentFrame().used > kUploadSize - kUploadReserve) {
        PerfScope perf_flush{g_perf.flush};
        g_perf.flushes++;
        Submit(true);
    }
    if (!BindTargets(s)) return;
    if (ext_ && ext_->Bound(*this, s, bound_rt_, bound_ds_, a.pretransformed) && !BindTargets(s)) return;

    // vertex shader; drawn for an upscaler, the motion variant: still and unjittered for 2D in screen space
    DrawShaders sh = ClassifyShaders(s, a, count);
    if (sh.prog) EnsureProgramHash(s);
    sh.vkey = VertexKey(s, a, sh);
    MotionKind mk = kMotionNone;
    mv_key_ = 0;
    if (bound_mv_) {
        bool flat = a.pretransformed || (g_screen_space_test && g_screen_space_test(s));
        mk = flat ? kMotionFlat : sh.prog ? kMotionProgram : kMotionFixed;
        if (!flat) {
            // the same draw a frame ago: what it draws, then the how-manieth such draw this frame
            // not the target (the game swaps between back buffers) nor where inline data sits (a ring)
            uint64_t id[4] = {sh.vkey, s.streams[0].vb, uint64_t(s.index_buffer) << 32 | start, uint64_t(count) << 8 | prim};
            uint64_t bases[3] = {Hash(id, sizeof(id)), Hash(&id[3], 8, sh.vkey ^ 0x5A5A), Hash(&sh.vkey, 8, 0xA5A5)};
            for (int j = 0; j < 3; j++) {
                uint32_t n = occ_[bases[j]]++;
                mv_keys_[j] = Hash(&n, sizeof(n), bases[j]) | 1;
            }
            mv_key_ = mv_keys_[0];
        }
    }
    if (mk) sh.vkey = Hash(&mk, sizeof(mk), sh.vkey ^ 0x6D6F74696F6E5F31ull);
    EnsureVertexShader(s, a, sh, mk);

    // stage textures, then the pixel shader made for them
    DrawTextures stages;
    if (!BindStageTexturesAndTargets(s, stages)) return;
    PixelKey pk = MakePixelKey(s, stages.info);
    pk.quad = sh.quad;
    sh.pkey = Hash(&pk, sizeof(pk));
    if (mk) sh.pkey ^= 0x6D6F74696F6E5F32ull;
    EnsurePixelShader(pk, sh, mk != kMotionNone);
    if (!sh.vs_code || !sh.ps_code) return;

    // pipeline
    PsoKey k;
    BuildPipelineKey(s, a, sh.vh, sh.ph, k);
    ID3D12PipelineState* pso = EnsurePipeline(k, sh.vs_code, sh.ps_code);
    if (!pso) return;
    cl_->SetPipelineState(pso);

    // constants
    GuestExtent size;
    SurfaceSize(bound_rt_ ? s.rt : s.zs, size.w, size.h);
    DrawConstants c{};
    UploadConstants(s, sh, k, stages, size, c);

    if (cap_) {
        CaptureDraw(s, prim, count, k, c, stages);
        EndDrawCapture(s, a, count, c);
        CaptureShaders(s, a, sh, pk);
    }

    // texture views and samplers, viewport and scissor, blend factor and stencil reference
    SetTextureViewsAndSamplers(s, a, stages);
    SetViewportScissor(s, size.w, size.h);
    SetBlendFactorStencilRef(s);

    // geometry
    DrawGeometry(a);
}

// a guest rect scaled to the target and clamped to it
D3D12_RECT ScaledRect(const Target& t, int x0, int y0, int x1, int y1) {
    float sx = float(t.w) / t.gw, sy = float(t.h) / t.gh;
    return {std::clamp<LONG>(LONG(x0 * sx), 0, t.w), std::clamp<LONG>(LONG(y0 * sy), 0, t.h),
            std::clamp<LONG>(LONG(x1 * sx), 0, t.w), std::clamp<LONG>(LONG(y1 * sy), 0, t.h)};
}

// rs: the rects buffer, reused across targets
void D3D12Backend::ClearTarget(Target& t, const State& s, const ClearArgs& c, std::vector<D3D12_RECT>& rs) {
    rs.clear();
    if (c.count && c.rects) {
        for (uint32_t i = 0; i < c.count; i++) {
            int32_t r[4];
            std::memcpy(r, c.rects + size_t(i) * sizeof(r), sizeof(r));
            rs.push_back(ScaledRect(t, r[0], r[1], r[2], r[3]));
        }
    } else {
        const auto& v = s.viewport;
        rs.push_back(ScaledRect(t, int(v.x), int(v.y), int(v.x + v.w), int(v.y + v.h)));
    }
    // a target with a multisampled twin clears both, so neither falls behind the other
    if (t.depth) {
        D3D12_CLEAR_FLAGS cf = D3D12_CLEAR_FLAGS(((c.flags & kClearZ) ? D3D12_CLEAR_FLAG_DEPTH : 0) |
                                                 ((c.flags & kClearStencil) ? D3D12_CLEAR_FLAG_STENCIL : 0));
        for (int twin = 0; twin < (t.ms ? 2 : 1); twin++) {
            Transition(twin ? t.ms.Get() : t.res.Get(), twin ? t.ms_state : t.state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cl_->ClearDepthStencilView(Cpu(dsv_heap_.Get(), twin ? t.ms_view : t.view, dsv_inc_), cf,
                                       std::clamp(c.z, 0.0f, 1.0f), UINT8(c.stencil), UINT(rs.size()), rs.data());
        }
    } else {
        // pixels the CPU wrote before this clear go in first, a later upload would paint over the clear
        if (LatchCpuWrite(t)) UploadTarget(t, s.rt);
        float col[4];
        Color(c.color, col);
        for (int i = 0; t.range != 1 && i < 3; i++) col[i] /= t.range;
        uint32_t mask = (c.flags & kClearTarget) >> 4;  // R G B A, as D3D12_COLOR_WRITE_ENABLE_*
        for (int twin = 0; twin < (t.ms ? 2 : 1); twin++) {
            Transition(twin ? t.ms.Get() : t.res.Get(), twin ? t.ms_state : t.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
            D3D12_CPU_DESCRIPTOR_HANDLE rh = Cpu(rtv_heap_.Get(), twin ? t.ms_view : t.view, rtv_inc_);
            if (mask == 0xF) {
                cl_->ClearRenderTargetView(rh, col, UINT(rs.size()), rs.data());
            } else if (ID3D12PipelineState* pso = EnsureMaskedClearPipeline(t.fmt, mask, twin ? samples_ : 1)) {
                // ClearRenderTargetView writes every channel: The Two Towers clears only alpha (0x80) between drawing
                // the world and its lighting passes, which would wipe the picture
                cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);
                bound_rt_ = bound_ds_ = nullptr;
                D3D12_VIEWPORT vp{0, 0, float(t.w), float(t.h), 0, 1};
                cl_->RSSetViewports(1, &vp);
                Alloc cb = Upload(256, 256);
                std::memcpy(cb.cpu, col, sizeof(col));
                uint32_t slot = AllocSrvSlots(1);
                D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
                sv.Format = kBgra;
                sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sv.Texture2D.MipLevels = 1;
                dev_->CreateShaderResourceView(nullptr, &sv, Cpu(srv_heap_.Get(), slot, srv_inc_));
                for (const D3D12_RECT& r : rs) {
                    cl_->RSSetScissorRects(1, &r);
                    DrawFullScreen(pso, cb.gpu, slot);
                }
            }
        }
    }
    t.drawn = true;
}

void D3D12Backend::Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
                         uint32_t stencil) {
    PerfScope perf_clear{g_perf.clear};
    if (cap_) {
        CapStep(s.rt);
        fprintf(cap_, "%04u clear rt %08X ds %08X flags %X color %08X z %.3f\n", cap_i_++, s.rt.data, s.zs.data, flags,
                color, z);
    }
    Target* rt = (flags & kClearTarget) ? EnsureTarget(s.rt, false) : nullptr;
    Target* ds = (flags & (kClearZ | kClearStencil)) ? EnsureTarget(s.zs, true) : nullptr;
    uint32_t gw;
    uint32_t gh;
    SurfaceSize(rt ? s.rt : s.zs, gw, gh);
    std::vector<D3D12_RECT> rs;
    ClearArgs c{.count = count, .rects = rects, .flags = flags, .color = color, .z = z, .stencil = stencil};
    for (Target* t : {rt, ds}) {
        if (t) ClearTarget(*t, s, c, rs);
    }
}

// CopyRects' guest lists: src_rects RECTs, dst_points POINTs, all int32
constexpr uint32_t kGuestRectBytes = 16;
constexpr uint32_t kRectLeft = 0;
constexpr uint32_t kRectTop = 4;
constexpr uint32_t kRectRight = 8;
constexpr uint32_t kRectBottom = 12;
constexpr uint32_t kGuestPointBytes = 8;
constexpr uint32_t kPointX = 0;
constexpr uint32_t kPointY = 4;

struct GuestRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct GuestPoint {
    int32_t x;
    int32_t y;
};

GuestRect ReadGuestRect(uint32_t rects, uint32_t i) {
    uint32_t r = rects + i * kGuestRectBytes;
    GuestRect g;
    g.left = int32_t(kernel::R32(r + kRectLeft));
    g.top = int32_t(kernel::R32(r + kRectTop));
    g.right = int32_t(kernel::R32(r + kRectRight));
    g.bottom = int32_t(kernel::R32(r + kRectBottom));
    return g;
}

GuestPoint ReadGuestPoint(uint32_t points, uint32_t i) {
    uint32_t p = points + i * kGuestPointBytes;
    GuestPoint g;
    g.x = int32_t(kernel::R32(p + kPointX));
    g.y = int32_t(kernel::R32(p + kPointY));
    return g;
}

// a drawn copy's rect in guest pixels: the source corners and where it lands
struct DrawnRect {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    float dx = 0;
    float dy = 0;
};

// without dst_points the rect lands where it is
DrawnRect ReadDrawnRect(uint32_t src_rects, uint32_t dst_points, uint32_t i) {
    GuestRect g = ReadGuestRect(src_rects, i);
    DrawnRect r;
    r.x0 = float(g.left);
    r.y0 = float(g.top);
    r.x1 = float(g.right);
    r.y1 = float(g.bottom);
    r.dx = r.x0;
    r.dy = r.y0;
    if (dst_points) {
        GuestPoint p = ReadGuestPoint(dst_points, i);
        r.dx = float(p.x);
        r.dy = float(p.y);
    }
    return r;
}

// a guest coordinate read as unsigned, times the host scale
UINT ScaleUnsigned(int32_t v, float sc) { return UINT(uint32_t(v) * sc); }

// a drawn copy's taps round down a ratio a hair over a whole number
constexpr float kTapsSlack = 1e-3f;

// one Blit per rect: for formats that differ
void D3D12Backend::CopyByDraw(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state,
                              Target& dst_t, const CopyList& l) {
    Transition(src_res, src_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(dst_t.res.Get(), dst_t.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rh = Cpu(rtv_heap_.Get(), dst_t.view, rtv_inc_);
    cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);
    float src_scale = float(src_t.w) / src_t.gw, src_sy = float(src_t.h) / src_t.gh;
    float dst_scale = float(dst_t.w) / dst_t.gw, dst_sy = float(dst_t.h) / dst_t.gh;
    bool listed = l.count && l.src_rects;
    uint32_t n = listed ? l.count : 1;
    for (uint32_t i = 0; i < n; i++) {
        float whole_w = float(std::min(src_t.gw, dst_t.gw));
        float whole_h = float(std::min(src_t.gh, dst_t.gh));
        DrawnRect r{.x0 = 0, .y0 = 0, .x1 = whole_w, .y1 = whole_h};
        if (listed) r = ReadDrawnRect(l.src_rects, l.dst_points, i);
        if (r.x1 <= r.x0 || r.y1 <= r.y0) continue;
        D3D12_VIEWPORT vp{r.dx * dst_scale, r.dy * dst_sy, (r.x1 - r.x0) * dst_scale, (r.y1 - r.y0) * dst_sy, 0, 1};
        LONG right = std::min(LONG(vp.TopLeftX + vp.Width), LONG(dst_t.w));
        LONG bottom = std::min(LONG(vp.TopLeftY + vp.Height), LONG(dst_t.h));
        D3D12_RECT scissor{LONG(vp.TopLeftX), LONG(vp.TopLeftY), right, bottom};
        if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) continue;
        cl_->RSSetViewports(1, &vp);
        cl_->RSSetScissorRects(1, &scissor);
        float uv[4] = {r.x0 / src_t.gw, r.y0 / src_t.gh, (r.x1 - r.x0) / src_t.gw, (r.y1 - r.y0) / src_t.gh};
        uint32_t taps = uint32_t(std::ceil(std::max({src_scale / dst_scale, src_sy / dst_sy, 1.0f}) - kTapsSlack));
        float mul = 1 / dst_t.range;
        BlitOptions o{.alpha = true, .taps = taps, .mul = mul, .rtv = dst_t.fmt};
        Blit(src_res, src_t.fmt, uv, o);
    }
    dst_t.drawn = true;
    bound_rt_ = bound_ds_ = nullptr;
}

// one CopyTextureRegion per rect, clamped to both targets
void D3D12Backend::CopyByRegion(const Target& src_t, ID3D12Resource* src_res, D3D12_RESOURCE_STATES& src_state,
                                Target& dst_t, const CopyList& l) {
    Transition(src_res, src_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(dst_t.res.Get(), dst_t.state, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dl{dst_t.res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION sl{src_res, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    float dst_scale = float(dst_t.w) / dst_t.gw, dst_sy = float(dst_t.h) / dst_t.gh;
    bool listed = l.count && l.src_rects;
    uint32_t n = listed ? l.count : 1;
    for (uint32_t i = 0; i < n; i++) {
        D3D12_BOX b{0, 0, 0, std::min(src_t.w, dst_t.w), std::min(src_t.h, dst_t.h), 1};
        uint32_t dx = 0;
        uint32_t dy = 0;
        if (listed) {
            GuestRect r = ReadGuestRect(l.src_rects, i);
            b.left = ScaleUnsigned(r.left, dst_scale);
            b.top = ScaleUnsigned(r.top, dst_sy);
            b.right = std::min(ScaleUnsigned(r.right, dst_scale), src_t.w);
            b.bottom = std::min(ScaleUnsigned(r.bottom, dst_sy), src_t.h);
            dx = b.left;
            dy = b.top;
            if (l.dst_points) {
                GuestPoint p = ReadGuestPoint(l.dst_points, i);
                dx = ScaleUnsigned(p.x, dst_scale);
                dy = ScaleUnsigned(p.y, dst_sy);
            }
        }
        if (b.right <= b.left || b.bottom <= b.top || dx >= dst_t.w || dy >= dst_t.h) continue;
        b.right = std::min<UINT>(b.right, b.left + dst_t.w - dx);
        b.bottom = std::min<UINT>(b.bottom, b.top + dst_t.h - dy);
        cl_->CopyTextureRegion(&dl, dx, dy, 0, &sl, &b);
    }
    dst_t.drawn = true;
    bound_rt_ = bound_ds_ = nullptr;
}

void D3D12Backend::Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects,
                        uint32_t dst_points) {
    PerfScope perf_copy{g_perf.copy};
    Target* src_t = FindTarget(src, false);
    if (cap_) {
        const char* from = "memory";
        if (src_t) from = src_t->drawn ? "target" : "idle target";
        fprintf(cap_, "%04u copy %08X %s to %08X\n", cap_i_++, src.data, from, dst.data);
    }
    // pixels the CPU made: the copy happens in memory, textures and targets pick it up from there
    if (!src_t || !src_t->drawn) {
        if (!CopySurfaceMemory(src, dst, count, src_rects, dst_points)) {
            LOG_FIRST(8, "d3d12: CopyRects %08X to %08X skipped", src.data, dst.data);
        } else if (Target* dst_t = FindTarget(dst, false)) {
            UploadTarget(*dst_t, dst);
        }
        return;
    }
    Resolve(*src_t);
    Target* dst_t = EnsureTarget(dst, false);
    if (!dst_t || src_t == dst_t) return;
    CopyList l{.count = count, .src_rects = src_rects, .dst_points = dst_points};
    ReadyRes(*dst_t);  // a copy may cover part of it
    if (src_t->fmt != dst_t->fmt) {
        // formats differ: drawn across
        CopyByDraw(*src_t, src_t->res.Get(), src_t->state, *dst_t, l);
        ResWritten(*dst_t);
        return;
    }
    CopyByRegion(*src_t, src_t->res.Get(), src_t->state, *dst_t, l);
    ResWritten(*dst_t);
    if (cap_) {
        char name[32];
        snprintf(name, sizeof(name), "%04u_copy_%08X", cap_i_, dst_t->data);
        CapSave(*dst_t, name);
    }
}

void D3D12Backend::ResizeSwap() {
    HWND hwnd = static_cast<HWND>(app::Window());
    RECT rc;
    GetClientRect(hwnd, &rc);
    uint32_t w = std::max<LONG>(rc.right - rc.left, 1);
    uint32_t h = std::max<LONG>(rc.bottom - rc.top, 1);
    if (w == bb_w_ && h == bb_h_) return;
    Flush();
    for (auto& f : frames_) {
        if (f.fence) WaitFence(f.fence);
    }
    for (auto& b : bb_) b.Reset();
    swap_->ResizeBuffers(kFrames, w, h, bb_fmt_, tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    CreateBackBufferViews();
    bb_w_ = w;
    bb_h_ = h;
}

void D3D12Backend::FinishCapture(const Surface& front) {
    CapStep(Surface{});
    if (Target* f = FindTarget(front, false)) CapSave(*f, "final");
    for (auto& t : cap_tex_) {
        char name[32];
        snprintf(name, sizeof(name), "tex_%08X", t.data);
        D3D12_RESOURCE_STATES st = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        CapSave({.res = t.res.Get(), .state = &st, .fmt = t.fmt, .w = t.w, .h = t.h}, name);
    }
    fclose(cap_);
    cap_ = nullptr;
    cap_tex_.clear();
    LOGI("d3d12: frame captured to %s", cap_dir_.string().c_str());
}

void D3D12Backend::StartCapture() {
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%02u%02u%02u", tm.wHour, tm.wMinute, tm.wSecond);
    std::error_code ec;
    // beside the log folder: logs\..\captures by default, or next to a launcher's log
    std::filesystem::path base = app::Args().log.empty() ? app::ExeDir() : app::LogFolder();
    cap_dir_ = std::filesystem::weakly_canonical(base / diag::CaptureDir() / stamp, ec);
    std::filesystem::create_directories(cap_dir_, ec);
    cap_ = _wfopen((cap_dir_ / "frame.txt").c_str(), L"w");
    cap_i_ = cap_rt_ = 0;
    cap_seen_.clear();
    cap_sh_.clear();
}

// ramp_ holds the red, green and blue ramps, 256 entries each
constexpr int kRampRed = 0;
constexpr int kRampGreen = 256;
constexpr int kRampBlue = 512;

// brightness over the game's ramp: v^(1/b), black and white kept, the darks lifted (b > 1) or deepened
uint8_t D3D12Backend::Brighten(uint8_t v) const {
    float b = app::Get().brightness;
    if (b > 0.999f && b < 1.001f) return v;
    return uint8_t(std::lround(255.0f * std::pow(v / 255.0f, 1.0f / std::clamp(b, 0.5f, 2.0f))));
}

void D3D12Backend::UploadGammaRamp() {
    constexpr uint32_t kRampBytes = 256 * 4;  // one BGRA texel per ramp entry
    ramp_dirty_ = false;
    Alloc a = Upload(kRampBytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    bool identity = true;
    for (int i = 0; i < 256; i++) {
        uint8_t* p = a.cpu + i * 4;
        p[0] = Brighten(ramp_[kRampBlue + i]);
        p[1] = Brighten(ramp_[kRampGreen + i]);
        p[2] = Brighten(ramp_[kRampRed + i]);
        p[3] = 255;
        identity &= p[2] == i && p[1] == i && p[0] == i;
    }
    gamma_ = !identity;
    Transition(lut_.Get(), lut_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dl{lut_.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION sl{a.res, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    sl.PlacedFootprint.Offset = a.off;
    sl.PlacedFootprint.Footprint = {DXGI_FORMAT_B8G8R8A8_UNORM, 256, 1, 1, kRampBytes};
    cl_->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
}

// g_frame_sides: the frame's sides painted black, in a w x h target the frame fills at aspect, centred
void D3D12Backend::BlackSides(D3D12_CPU_DESCRIPTOR_HANDLE rh, uint32_t w, uint32_t h, float aspect) {
    if (frame_sides_ >= 0.999f) return;
    float vw = std::min(float(w), float(h) * aspect), keep = vw * frame_sides_;
    LONG l = LONG((float(w) - keep) / 2), r = LONG((float(w) + keep) / 2);
    D3D12_RECT rs[2] = {{0, 0, l, LONG(h)}, {r, 0, LONG(w), LONG(h)}};
    float black[4] = {0, 0, 0, 1};
    cl_->ClearRenderTargetView(rh, black, 2, rs);
}

void D3D12Backend::BlitFrame(Target& t, D3D12_CPU_DESCRIPTOR_HANDLE rh) {
    // 16:9 (or the wider screen's shape, app::FrameAspect) when the console reports a widescreen display, else 4:3
    float aspect = app::FrameAspect();
    float w = float(bb_w_);
    float h = float(bb_h_);
    float vw = std::min(w, h * aspect);
    float vh = vw / aspect;
    float uv[4] = {0, 0, 1, 1};
    ID3D12Resource* src = t.res.Get();
    D3D12_RESOURCE_STATES* src_state = &t.state;
    DXGI_FORMAT fmt = t.fmt;
    float texel[2] = {1.0f / float(std::max(t.w, 1u)), 1.0f / float(std::max(t.h, 1u))};
    // drawn larger than shown (render_percent over 100): averaged down first, every pixel shown the mean of the
    // drawn pixels it covers. One bilinear tap per screen pixel would weigh the drawn pixels unevenly at 1.5x, so fine
    // detail would pulse lighter and darker as the picture moves; sharpening then works on the shown pixels.
    float k = std::max(float(t.w) / vw, float(t.h) / vh);
    uint32_t ow = uint32_t(std::lround(vw)), oh = uint32_t(std::lround(vh));
    bool show_motion = false;
    if (const char* e = std::getenv("XBE_UP_SHOW_MOTION"); e && *e == '1' && UpOn()) show_motion = true;
    if (UpOn() && !show_motion && Upscale(t, ow, oh, rh)) {
        src = up_out_.Get();
        src_state = &up_out_state_;
        fmt = up_out_->GetDesc().Format;
        texel[0] = 1.0f / float(ow);
        texel[1] = 1.0f / float(oh);
        k = 1.0f;
    }
    if (k > 1.01f) {
        uint32_t sw = uint32_t(std::lround(vw)), sh = uint32_t(std::lround(vh));
        ID3D12Resource* into = shown_.Get();
        if (into && (into->GetDesc().Width != sw || into->GetDesc().Height != sh)) {
            CurrentFrame().garbage.push_back(shown_);  // the screen changed; the GPU may still read the old one
            shown_.Reset();
            into = nullptr;
        }
        ShrinkOptions o{.taps = uint32_t(std::ceil(k - 0.01f)), .into = into, .into_state = shown_state_};
        if (ComPtr<ID3D12Resource> down = Shrink(t.res.Get(), t.state, t.fmt, sw, sh, o)) {
            shown_ = down;
            shown_state_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
            src = shown_.Get();
            src_state = &shown_state_;
            fmt = kBgra;
            texel[0] = 1.0f / float(sw);
            texel[1] = 1.0f / float(sh);
        }
        cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);  // Shrink drew elsewhere
    }
    D3D12_VIEWPORT vp{(w - vw) / 2, (h - vh) / 2, vw, vh, 0, 1};
    D3D12_RECT scissor{0, 0, LONG(bb_w_), LONG(bb_h_)};
    cl_->RSSetViewports(1, &vp);
    cl_->RSSetScissorRects(1, &scissor);
    Transition(src, *src_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // sharpening on the frame as shown: [graphics] sharpening, 0 the original picture
    float sharpen = g_sharpen_off ? 0.0f : std::min(app::Get().sharpening, 100u) / 100.0f;
    Blit(src, fmt, uv,
         {.gamma = gamma_, .rtv = bb_fmt_, .sharpen = sharpen, .texel = {texel[0], texel[1]}, .hdr = hdr_, .look = true});
    if (show_motion) {
        auto it = motion_.find(t.data);
        if (it != motion_.end() && it->second.res) {
            Transition(it->second.res.Get(), it->second.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            ID3D12Resource* src3[3] = {it->second.res.Get(), nullptr, nullptr};
            DXGI_FORMAT f3[3] = {kMotionFormat, kBgra, kBgra};
            float p[8] = {};
            Pass(kMotionShowHlsl, "psmain", bb_fmt_, rh, bb_w_, bb_h_, src3, f3, p, -1);
            cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);
        }
    }
}

double MovieClock() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// the movie frame just drawn, averaged down to the console's size (the movie's own) into the ring
void D3D12Backend::KeepMovieFrame(Target& t) {
    if (!movie_on_) {
        movie_on_ = true;
        movie_n_ = 0;
    }
    MovieFrame& f = movie_ring_[movie_n_ % kMovieFrames];
    // kept at up to twice the console's size: the movie's own detail (its pictures are about 700x500) survives to the
    // screen, and movie_sharpening works one console pixel wide, about one movie pixel, before the scale to the screen
    uint32_t kw = std::min(t.w, std::max(t.gw, 1u) * 2), kh = std::min(t.h, std::max(t.gh, 1u) * 2);
    float k = std::max(float(t.w) / float(std::max(kw, 1u)), float(t.h) / float(std::max(kh, 1u)));
    ID3D12Resource* into = f.res.Get();
    if (into && (into->GetDesc().Width != kw || into->GetDesc().Height != kh)) into = nullptr;  // the screen changed
    ShrinkOptions o{.taps = uint32_t(std::max(1.0f, std::ceil(k - 0.01f))), .into = into, .into_state = f.state,
                    .mul = t.range};
    ComPtr<ID3D12Resource> down = Shrink(t.res.Get(), t.state, t.fmt, kw, kh, o);
    if (!down) return;
    f.res = down;
    f.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    f.time = MovieClock();
    movie_n_++;
}

// a movie frame as [graphics] movie_display shows it, sharpened at its own size; false when no movie is on
// the movie frame to show: the newest one old enough, else the oldest kept (the picture waits for the late sound);
// null when no movie is on
D3D12Backend::MovieFrame* D3D12Backend::PickMovieFrame() {
    if (!movie_on_ || !movie_n_) return nullptr;
    double due = MovieClock() - app::Get().movie_delay / 1000.0;
    uint32_t kept = std::min(movie_n_, kMovieFrames);
    MovieFrame* pick = &movie_ring_[(movie_n_ - kept) % kMovieFrames];
    for (uint32_t i = movie_n_; i-- > movie_n_ - kept;) {
        MovieFrame& f = movie_ring_[i % kMovieFrames];
        if (f.time <= due) {
            pick = &f;
            break;
        }
    }
    if (kept == kMovieFrames && pick->time > due)
        LOG_FIRST(1, "d3d12: cutscene sound delay held to %u frames (the game presents faster than the delay allows)", kMovieFrames);
    return pick->res ? pick : nullptr;
}

bool D3D12Backend::BlitMovie(const Target& t) {
    MovieFrame* pick = PickMovieFrame();
    if (!pick) return false;
    const app::Settings& st = app::Get();
    float w = float(bb_w_);
    float h = float(bb_h_);
    float uv[4] = {0, 0, 1, 1};
    float aspect = 4.0f / 3.0f;
    uint32_t shape = g_movie_shape;
    if (st.movie_display == 0 && shape == kMovieFit) {
        // fit: the middle of the 4:3 frame, where the films are (their bars are outside it): 16:9, or up to the
        // films' own shape on wider screens, which then get bars at the sides rather than lose the film's top
        aspect = MovieFitAspect();
        uv[3] = (4.0f / 3.0f) / aspect;
        uv[1] = MovieFitTop(uv[3]);
    } else if (st.movie_display == 0 && shape == kMovieWide) {
        aspect = 16.0f / 9.0f;  // a widescreen film squeezed into the 4:3 frame: the whole frame, unsqueezed
    }
    float vw = std::min(w, h * aspect);
    float vh = vw / aspect;
    if (st.movie_display == 1) {
        vw = w;
        vh = h;
    }
    D3D12_VIEWPORT vp{(w - vw) / 2, (h - vh) / 2, vw, vh, 0, 1};
    D3D12_RECT scissor{0, 0, LONG(bb_w_), LONG(bb_h_)};
    cl_->RSSetViewports(1, &vp);
    cl_->RSSetScissorRects(1, &scissor);
    Transition(pick->res.Get(), pick->state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    float sharpen = std::min(st.movie_sharpening, 100u) / 100.0f;
    Blit(pick->res.Get(), kBgra, uv,
         {.gamma = gamma_, .rtv = bb_fmt_, .sharpen = sharpen,
          .texel = {1.0f / float(std::max(t.gw, 1u)), 1.0f / float(std::max(t.gh, 1u))}, .hdr = hdr_, .look = true});
    return true;
}

void D3D12Backend::RenderUi() {
    std::lock_guard lock(app::UiLock());
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (app::kGame.ui) app::kGame.ui(fps_);
    app::DrawOverlay(fps_);
    // [display] show_fps: the frames presented a second (counted over half a second), top right
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
    ID3D12DescriptorHeap* uh[] = {ui_heap_.Get()};
    cl_->SetDescriptorHeaps(1, uh);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cl_.Get());
}

void D3D12Backend::DrawFrame(Target* t, uint32_t bi) {
    D3D12_RESOURCE_STATES bs = D3D12_RESOURCE_STATE_PRESENT;
    Transition(bb_[bi].Get(), bs, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rh = Cpu(rtv_heap_.Get(), kRtvCount + bi, rtv_inc_);
    float black[4] = {0, 0, 0, 1};
    cl_->ClearRenderTargetView(rh, black, 0, nullptr);
    cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);
    bound_rt_ = bound_ds_ = nullptr;
    if (t && !BlitMovie(*t)) {
        BlitFrame(*t, rh);
        BlackSides(rh, bb_w_, bb_h_, app::FrameAspect());
    }
    RenderUi();
    Transition(bb_[bi].Get(), bs, D3D12_RESOURCE_STATE_PRESENT);
}

void D3D12Backend::ReleaseOldTextures() {
    constexpr uint64_t kSweepEvery = 60;    // presents between sweeps
    constexpr size_t kTexturesKept = 2048;  // no sweep below this many
    constexpr uint64_t kIdleFrames = 600;
    // textures unused for ten seconds go, levels stream theirs in and out
    if (presents_ % kSweepEvery != 0 || textures_.size() <= kTexturesKept) return;
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->second.frame + kIdleFrames < presents_) {
            CurrentFrame().garbage.push_back(it->second.res);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

void D3D12Backend::ResetFrame() {
    Frame& f = CurrentFrame();
    if (f.fence && fence_->GetCompletedValue() < f.fence) {
        auto perf_wait = PerfClock::now();
        WaitFence(f.fence);
        g_perf.wait += PerfMs(perf_wait);
    }
    f.alloc->Reset();
    f.garbage.clear();
    f.used = 0;
    f.srv = 0;
    last_views_.frame = ~0ull;  // the slots are written again
    cl_->Reset(f.alloc.Get(), nullptr);
    Begin();
}

void D3D12Backend::CountFps() {
    fps_n_++;
    auto now = std::chrono::steady_clock::now();
    double el = std::chrono::duration<double>(now - fps_t_).count();
    if (el >= 0.5) {
        fps_ = fps_n_ / el;
        fps_n_ = 0;
        fps_t_ = now;
    }
}

// without XBE_PERF: a frame that took 50 ms or more is logged briefly with what it waited on (shaders compiled,
// pipelines made, textures uploaded), at most one line a second and 300 a run, so a player's log shows hitches
void LogHitch() {
    static PerfClock::time_point last_frame = PerfClock::now(), last_line{};
    static Perf before;
    static uint64_t frames = 0, lines = 0, skipped = 0;
    double frame_ms = PerfMs(last_frame);
    last_frame = PerfClock::now();
    const Perf& p = g_perf;
    // the game's own side of the frame: reads, waits and sleeps on this thread and on the others (frame_work.cpp)
    kernel::FrameWork mine, others;
    std::string slow_file;
    double slow_ms = 0;
    kernel::TakeFrameWork(mine, others, slow_file, slow_ms);
    if (frame_ms >= 50.0 && frames > 60 && lines < 300) {
        if (lines && PerfMs(last_line) < 1000.0) {
            skipped++;
        } else {
            // what the renderer did on this thread; the rest is the game's own work or waiting on it
            double host = (p.draw + p.clear + p.copy + p.present + p.sync) -
                          (before.draw + before.clear + before.copy + before.present + before.sync);
            LOGR("hitch: frame %.0f ms | shaders %llu (%.0f ms), pipelines %llu (%.0f ms), textures %llu new (%.0f ms), "
                 "resources %.0f ms, swap %.0f ms, game and other %.0f ms%s",
                 frame_ms, (unsigned long long)(p.shaders - before.shaders), p.compile - before.compile,
                 (unsigned long long)(p.psos - before.psos), p.pso - before.pso,
                 (unsigned long long)(p.newtex - before.newtex), p.texup - before.texup, p.create - before.create,
                 p.swap - before.swap, std::max(frame_ms - host, 0.0), skipped ? " (and earlier ones not listed)" : "");
            // what the game did meanwhile: this thread reading files, waiting on its other threads, sleeping, and
            // the other threads' reads (a level streaming in)
            auto ms = [](int64_t ns) { return double(ns) / 1e6; };
            std::string slow = slow_ms >= 5 && !slow_file.empty()
                                   ? ", slowest " + slow_file + " " + std::to_string(int(slow_ms + 0.5)) + " ms"
                                   : std::string();
            LOGR("hitch:   game thread: reading %.0f ms (%llu reads, %llu KB), waiting %.0f ms, sleeping %.0f ms | other "
                 "threads: reading %.0f ms (%llu reads, %llu KB)%s",
                 ms(mine.read_ns), (unsigned long long)mine.reads, (unsigned long long)(mine.read_bytes / 1024),
                 ms(mine.wait_ns), ms(mine.sleep_ns), ms(others.read_ns), (unsigned long long)others.reads,
                 (unsigned long long)(others.read_bytes / 1024), slow.c_str());
            last_line = PerfClock::now();
            skipped = 0;
            if (++lines == 300) LOGR("hitch: 300 listed, no more this run");
        }
    }
    before = g_perf;
    frames++;
}

void LogPerf() {
    static PerfClock::time_point perf_start = PerfClock::now();
    static uint64_t perf_frames = 0;
    // a frame that took twice a 60 Hz frame or more: what it spent its time on, against the frame before
    static PerfClock::time_point last_frame = PerfClock::now();
    static Perf before;
    double frame_ms = PerfMs(last_frame);
    last_frame = PerfClock::now();
    if (frame_ms > 33.0 && perf_frames) {
        const Perf& p = g_perf;
        double host = (p.draw + p.clear + p.copy + p.present + p.sync) - (before.draw + before.clear + before.copy +
                                                                          before.present + before.sync);
        LOGR("perf: slow frame %.1f ms | compile %.1f ms (%llu shaders), pipelines %.1f ms (%llu), texture upload "
             "%.1f ms (%llu, %.2f MB, %llu new), resources made %.1f ms (%llu targets), ring full %.1f ms (%llu), "
             "assemble %.1f, bind %.1f, draw %.1f ms (%llu draws), sync %.1f, swap %.1f | title and other %.1f ms",
             frame_ms, p.compile - before.compile, (unsigned long long)(p.shaders - before.shaders), p.pso - before.pso,
             (unsigned long long)(p.psos - before.psos), p.texup - before.texup,
             (unsigned long long)(p.texups - before.texups), (p.texup_bytes - before.texup_bytes) / 1048576.0,
             (unsigned long long)(p.newtex - before.newtex), p.create - before.create,
             (unsigned long long)(p.targets - before.targets), p.flush - before.flush,
             (unsigned long long)(p.flushes - before.flushes), p.assemble - before.assemble, p.bind - before.bind,
             p.draw - before.draw, (unsigned long long)(p.draws - before.draws), p.sync - before.sync,
             p.swap - before.swap, frame_ms - host);
    }
    before = g_perf;
    perf_frames++;
    double el = PerfMs(perf_start);
    if (el >= 1000.0) {
        Perf& p = g_perf;
        double n = double(perf_frames);
        double host = p.draw + p.clear + p.copy + p.present + p.sync;
        LOGR("perf: %.1f fps %.2f ms/frame | host %.2f (draw %.2f: assemble %.2f bind %.2f [check %.2f, upload %.2f] "
             "compile %.2f; clear %.2f copy %.2f sync %.2f present %.2f [swap %.2f wait %.2f]) other %.2f",
             n * 1000.0 / el, el / n, host / n, p.draw / n, p.assemble / n, p.bind / n, p.sig / n, p.texup / n,
             p.compile / n, p.clear / n, p.copy / n, p.sync / n, p.present / n, p.swap / n, p.wait / n,
             (el - host) / n);
        LOGR("perf: title thread per frame: recording %.2f ms, waiting %.2f ms (%.1f waits: read-back %.2f "
             "resource %.2f fence %.2f flush %.2f copy %.2f, next frame %.2f) | read-backs waited for: %.1f lock, "
             "%.1f NOOVERWRITE",
             g_perf_capture_ns.exchange(0) / n / 1e6, g_perf_wait_ns.exchange(0) / n / 1e6,
             g_perf_waits.exchange(0) / n, g_perf_wait_kind_ns[0].exchange(0) / n / 1e6,
             g_perf_wait_kind_ns[1].exchange(0) / n / 1e6, g_perf_wait_kind_ns[2].exchange(0) / n / 1e6,
             g_perf_wait_kind_ns[3].exchange(0) / n / 1e6, g_perf_wait_kind_ns[4].exchange(0) / n / 1e6,
             g_perf_wait_kind_ns[5].exchange(0) / n / 1e6, g_perf_sync_block.exchange(0) / n,
             g_perf_sync_late.exchange(0) / n);
        LOGR("perf: per frame %.0f draws (%.0f raw) %.0fk verts %.1f MB upload %.1f overflow | %.0f checks %.2f MB "
             "hashed | %.1f tex uploads %.2f MB, %.1f new tex, %.1f new targets, %.1f target uploads, %.1f shrinks, "
             "%.1f read-backs %.1f late | %llu shaders %llu psos this second",
             p.draws / n, p.raw / n, p.verts / n / 1000.0, p.upload / n / 1048576.0, p.overflow / n, p.sigs / n,
             p.sig_bytes / n / 1048576.0, p.texups / n, p.texup_bytes / n / 1048576.0, p.newtex / n,
             p.targets / n, p.rtups / n, p.shrinks / n, p.syncs / n, p.lates / n,
             (unsigned long long)p.shaders, (unsigned long long)p.psos);
        g_perf = Perf{};
        before = Perf{};
        perf_start = PerfClock::now();
        perf_frames = 0;
    }
}

void D3D12Backend::Present(const Surface& front) {
    PerfScope perf_present{g_perf.present};
    if (cap_) {
        FinishCapture(front);
    } else if (g_capture.exchange(false) && CaptureOn()) {
        StartCapture();
    }
    ResizeSwap();
    Target* t = EnsureTarget(front, false);
    if (t && LatchCpuWrite(*t)) UploadTarget(*t, front);
    if (t) Resolve(*t);
    frame_sides_ = g_frame_sides.exchange(1.0f);
    if (!g_movie) movie_on_ = false;  // first, so a screenshot just after a movie shows the game
    if (t && g_screenshot.exchange(false)) Screenshot(*t);
    // F7: this frame, then the next drawn with texture packs the other way round, as one pair of shots
    if (t && pair_step_ == 1) {
        Screenshot(*t, pair_name_ + (g_pack_off ? "_original.png" : "_pack.png"));
        g_pack_off = !g_pack_off;
        pair_step_ = 0;
    } else if (t && g_pack_pair.exchange(false)) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char name[64];
        std::snprintf(name, sizeof(name), "_%04u-%02u-%02u_%02u-%02u-%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
                      st.wMinute, st.wSecond);
        pair_name_ = name;
        Screenshot(*t, pair_name_ + (g_pack_off ? "_original.png" : "_pack.png"));
        g_pack_off = !g_pack_off;
        pair_step_ = 1;
        LOGR("texture pack: comparison pair (F7)");
    }
    if (t && g_movie) KeepMovieFrame(*t);
    // brightness changed in the in-game menu: the ramp again, the identity one for a game that never set its own
    if (float b = app::Get().brightness; b != brightness_) {
        if (!ramp_set_ && brightness_ == 1.0f)
            for (int i = 0; i < 256; i++) ramp_[i] = ramp_[256 + i] = ramp_[512 + i] = uint8_t(i);
        brightness_ = b;
        ramp_dirty_ = true;
    }
    if (ramp_dirty_) UploadGammaRamp();
    Transition(lut_.Get(), lut_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    uint32_t bi = swap_->GetCurrentBackBufferIndex();
    DrawFrame(t, bi);
    Submit(false);
    bool vsync = app::Get().vsync;
    auto perf_swap = PerfClock::now();
    HRESULT hr = swap_->Present(vsync ? 1 : 0, !vsync && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    g_perf.swap += PerfMs(perf_swap);
    // the driver gone (a GPU timeout on a card asked too much of, a driver crash or update): nothing drawn reaches the
    // screen again, so end with the reason rather than show a frozen picture
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
        HRESULT why = dev_->GetDeviceRemovedReason();
        LOGE("d3d12: the graphics device was lost (present %08lX, reason %08lX)", static_cast<unsigned long>(hr),
             static_cast<unsigned long>(why));
        app::Fail(app::kExitGpuLost,
                  L"The graphics driver stopped responding. This can happen when the graphics card is asked for more "
                  L"than it can do: try a lower Render resolution, Anti-aliasing off, or a lower frame rate. Updating "
                  L"the graphics driver may also help; the log has the details.");
    }

    if (t) {
        // the surfaces shown lately are the ones an upscaler takes
        if (std::find(shown_surf_.begin(), shown_surf_.end(), t->data) == shown_surf_.end()) {
            std::rotate(shown_surf_.rbegin(), shown_surf_.rbegin() + 1, shown_surf_.rend());
            shown_surf_[0] = t->data;
        }
        if (UpOn()) NextJitter(*t, up_ow_ ? up_ow_ : bb_w_);
    }
    // most of the frame unmatched a frame later: a cut (or a new place), the history goes
    if (mv_draws_ >= 16 && mv_found_ * 3 < mv_draws_) up_reset_ = true;
    if (UpOn() && presents_ % 600 == 300)
        LOGI("upscaler: frame %llu, %u draws with motion, %u matched a frame back, shown %08X", (unsigned long long)presents_,
             mv_draws_, mv_found_, t ? t->data : 0);
    if (!cur_draws_.empty()) {
        prev_index_.swap(cur_index_);
        prev_draws_.swap(cur_draws_);
    }
    cur_index_.clear();
    cur_draws_.clear();
    occ_.clear();
    mv_draws_ = mv_found_ = 0;
    presents_++;
    presents_seen_.store(presents_);
    frame_++;
    ReleaseOldTextures();
    ResetFrame();
    CountFps();
    if (diag::Perf()) LogPerf();
    else LogHitch();
    ApplySettings();
}

float D3D12Backend::TargetScale() {
    if (!UpWanted()) return app::RenderScale(guest_h_);
    float h = float(app::Output().height) / up::Ratio(up::Quality(std::min(app::Get().upscaler_quality, 4u)));
    return guest_h_ ? std::max(h / float(guest_h_), 0.1f) : 1.0f;
}

bool D3D12Backend::UpWanted() const {
    uint32_t k = app::Get().upscaler;
    return (k == up::kFsr || k == up::kDlss) && up_failed_ != k;
}

D3D12Backend::Motion* D3D12Backend::EnsureMotion(const Target& rt) {
    if (rt.depth || std::find(shown_surf_.begin(), shown_surf_.end(), rt.data) == shown_surf_.end()) return nullptr;
    Motion& m = motion_[rt.data];
    if (m.res && m.w == rt.w && m.h == rt.h) return &m;
    if (m.res) Release(m.res);
    // a slot of its own among the few after the back buffers
    std::array<bool, kMotionSlots> used{};
    for (auto& [d, o] : motion_)
        if (&o != &m && o.res) used[o.slot - (kRtvCount + kFrames)] = true;
    uint32_t slot = 0;
    while (slot < kMotionSlots && used[slot]) slot++;
    if (slot == kMotionSlots) {
        motion_.erase(rt.data);
        return nullptr;
    }
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = Texture2DDesc(kMotionFormat, rt.w, rt.h);
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = td.Format;
    m = Motion{};
    if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                                             IID_PPV_ARGS(&m.res)))) {
        motion_.erase(rt.data);
        return nullptr;
    }
    m.w = rt.w;
    m.h = rt.h;
    m.slot = kRtvCount + kFrames + slot;
    dev_->CreateRenderTargetView(m.res.Get(), nullptr, Cpu(rtv_heap_.Get(), m.slot, rtv_inc_));
    return &m;
}

// Halton (2, 3) over the upscaler's phase count, in render pixels from the pixel centre
float Halton(uint32_t i, uint32_t b) {
    float f = 1, r = 0;
    for (; i; i /= b) {
        f /= float(b);
        r += f * float(i % b);
    }
    return r;
}

void D3D12Backend::NextJitter(const Target& t, uint32_t ow) {
    int phases = up_ ? up_->Phases(t.w, ow) : 8;
    uint32_t i = uint32_t(jit_n_++ % uint64_t(std::max(phases, 1))) + 1;
    jit_px_[0] = Halton(i, 2) - 0.5f;
    jit_px_[1] = Halton(i, 3) - 0.5f;
    if (const char* e = std::getenv("XBE_UP_NO_JITTER"); e && *e == '1') jit_px_[0] = jit_px_[1] = 0;
    // as the projection takes it: +2x / w, -2y / h in clip space
    jit_[0] = 2.0f * jit_px_[0] / float(std::max(t.w, 1u));
    jit_[1] = -2.0f * jit_px_[1] / float(std::max(t.h, 1u));
    jit_[2] = jit_[3] = 0;
}

bool D3D12Backend::Upscale(Target& t, uint32_t ow, uint32_t oh, D3D12_CPU_DESCRIPTOR_HANDLE rh) {
    auto it = motion_.find(t.data);
    // nothing 3D drawn into this frame (menus, films): shown as it is, and the history starts again after
    if (it == motion_.end() || it->second.frame != presents_ || !it->second.depth) {
        up_reset_ = true;
        return false;
    }
    Motion& m = it->second;
    Target* depth = nullptr;
    for (auto& [key, tg] : targets_)
        if (&tg == m.depth) depth = &tg;
    if (!depth || depth->w != t.w || depth->h != t.h || m.w != t.w || m.h != t.h) {
        up_reset_ = true;
        return false;
    }
    const app::Settings& st = app::Get();
    bool hdr = t.fmt == DXGI_FORMAT_R16G16B16A16_FLOAT;
    // made (again) for this output and render size
    if (!up_ || up_ow_ != ow || up_oh_ != oh || up_rw_ != t.w || up_rh_ != t.h || up_hdr_ != hdr) {
        if (up_) {
            Submit(true);  // the old one's work done before it goes
            up_.reset();
        }
        std::string why;
        auto kind = up::Kind(st.upscaler);
        up_ = up::Create(kind, up::Quality(std::min(st.upscaler_quality, 4u)), dev_.Get(), queue_.Get(), adapter_.Get(), ow,
                         oh, t.w, t.h, hdr, why);
        if (!up_) {
            LOGR("upscaler: %s is not available: %s; the normal picture (Render resolution) instead", up::KindName(kind),
                 why.c_str());
            up_failed_ = st.upscaler;
            return false;
        }
        up_kind_ = st.upscaler;
        up_quality_ = st.upscaler_quality;
        up_ow_ = ow;
        up_oh_ = oh;
        up_rw_ = t.w;
        up_rh_ = t.h;
        up_hdr_ = hdr;
        up_reset_ = true;
        LOGR("upscaler: %s, %ux%u drawn, %ux%u shown", up_->Describe().c_str(), t.w, t.h, ow, oh);
        up_out_.Reset();
    }
    DXGI_FORMAT of = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!up_out_ || up_out_->GetDesc().Width != ow || up_out_->GetDesc().Height != oh || up_out_->GetDesc().Format != of) {
        if (up_out_) Release(up_out_);
        up_out_.Reset();
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC td = Texture2DDesc(of, ow, oh);
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        up_out_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, up_out_state_, nullptr, IID_PPV_ARGS(&up_out_))))
            return false;
    }
    const auto kRead = D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(t.res.Get(), t.state, kRead);
    Transition(depth->res.Get(), depth->state, kRead);
    Transition(m.res.Get(), m.state, kRead);
    Transition(up_out_.Get(), up_out_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    up::Frame f;
    f.cl = cl_.Get();
    f.color = t.res.Get();
    f.depth = depth->res.Get();
    f.motion = m.res.Get();
    f.output = up_out_.Get();
    f.render_w = t.w;
    f.render_h = t.h;
    f.out_w = ow;
    f.out_h = oh;
    f.jitter_x = jit_px_[0];
    f.jitter_y = jit_px_[1];
    // a test: XBE_UP_JITTER_SIGN=-x, -y or -xy flips what the upscaler is told (the drawn jitter stays)
    if (const char* e = std::getenv("XBE_UP_JITTER_SIGN")) {
        if (std::strchr(e, 'x')) f.jitter_x = -f.jitter_x;
        if (std::strchr(e, 'y')) f.jitter_y = -f.jitter_y;
    }
    f.reset = up_reset_;
    auto now = std::chrono::steady_clock::now();
    f.dt_ms = float(std::clamp(std::chrono::duration<double, std::milli>(now - up_t_).count(), 1.0, 100.0));
    up_t_ = now;
    f.hdr = hdr;
    bool ok = up_->Evaluate(f);
    // the upscalers leave their own root signature and heaps bound
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), smp_heap_.Get()};
    cl_->SetDescriptorHeaps(2, heaps);
    cl_->SetGraphicsRootSignature(root_.Get());
    cl_->OMSetRenderTargets(1, &rh, FALSE, nullptr);
    bound_rt_ = bound_ds_ = nullptr;
    bound_mv_ = nullptr;
    if (!ok) return false;
    up_reset_ = false;
    return true;
}

void D3D12Backend::ApplySettings() {
    // another upscaler or quality: made again for the next frame (a kind that failed is tried again once changed)
    const app::Settings& st = app::Get();
    if (up_ && (st.upscaler != up_kind_ || st.upscaler_quality != up_quality_)) {
        Flush();
        up_.reset();
        up_reset_ = true;
    }
    if (up_failed_ && st.upscaler != up_failed_) up_failed_ = 0;
    float want = TargetScale(), want_x = WideMul();
    bool remake = ext_ && ext_->Settings();
    uint32_t samples = PickSamples();
    if (std::fabs(want - scale_) > 1e-4f || std::fabs(want_x - xmul_) > 1e-4f || remake || samples != samples_) {
        Flush();
        for (auto& [key, tg] : targets_) {
            (tg.depth ? free_dsv_ : free_rtv_).push_back(tg.view);
            if (tg.ms) (tg.depth ? free_dsv_ : free_rtv_).push_back(tg.ms_view);
        }
        targets_.clear();
        motion_.clear();
        up_reset_ = true;
        scale_ = want;
        xmul_ = want_x;
        samples_ = samples;
        ms_ok_.clear();
        if (ext_) ext_->Reset(*this);
        bound_rt_ = bound_ds_ = nullptr;
    }
}

// the target resolved, then shrunk with o when it is not guest sized BGRA; res null on failure
D3D12Backend::CopySource D3D12Backend::PrepareCopySource(Target& t, const ShrinkOptions& o) {
    Resolve(t);
    CopySource out{t.res.Get(), nullptr};
    if (t.w != t.gw || t.h != t.gh || t.fmt != kBgra) {
        out.down = Shrink(t.res.Get(), t.state, t.fmt, t.gw, t.gh, o);
        out.res = out.down.Get();
    } else {
        Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    return out;
}

// long unused: the title freed it and other data is at this address now
bool D3D12Backend::TargetStale(uint32_t data) const {
    auto it = target_used_.find(data);
    uint64_t used = it == target_used_.end() ? 0 : it->second;
    return frame_ - used > kStaleFrames;
}

// the CPU reads a surface the GPU drew: bring the pixels back
void D3D12Backend::Sync(uint32_t data, uint32_t) {
    auto it = targets_.find(TargetMapKey(data, false));
    if (it == targets_.end() || !it->second.drawn || TargetStale(data)) return;
    PerfScope perf_sync{g_perf.sync};
    g_perf.syncs++;
    if (diag::Perf()) {
        LOG_FIRST(16, "perf: read-back of %08X (%ux%u guest, %ux%u host)", data, it->second.gw, it->second.gh,
                  it->second.w, it->second.h);
    }
    Target& t = it->second;
    // src.down, when there is one, stays alive until ReadBack is done
    CopySource src = PrepareCopySource(t, {});
    if (!src.res) return;
    std::vector<uint8_t> px;
    if (!ReadBack(src.res, t.gw, t.gh, px)) return;
    // color targets are 32 bit linear in guest memory, the pitch rounds to 64 bytes
    uint32_t pitch = GuestPitch(t.gw);
    uint8_t* guest = static_cast<uint8_t*>(kernel::HostPtr(data));
    for (uint32_t y = 0; y < t.gh; y++) std::memcpy(guest + size_t(y) * pitch, &px[size_t(y) * t.gw * 4], t.gw * 4);
    t.read = std::move(px);
    t.drawn = false;
    t.sig = MemSig(t.data, t.bytes);
}

// A NOOVERWRITE lock: the console reads whatever the GPU has finished, so a surface locked every frame gets the newest
// finished copy (a frame or two old) and the CPU never waits on the GPU; one locked now and then still waits in Sync
void D3D12Backend::SyncLate(uint32_t data) {
    if (!DeliverLate(data)) Sync(data, 0);
    RecordLate(data);
}

bool D3D12Backend::DeliverLate(uint32_t data) {
    std::lock_guard lock(late_lock_);
    auto it = late_.find(data);
    if (it == late_.end()) return false;
    Late& l = it->second;
    uint64_t done = fence_->GetCompletedValue();
    uint64_t now = presents_seen_.load();
    int best = -1;
    for (int i = 0; i < kFrames; i++) {
        bool finished = l.fence[i] && l.fence[i] <= done;
        bool recent = now - l.frame[i] <= uint64_t(kFrames) + 1;
        bool newest = best < 0 || l.fence[i] > l.fence[best];
        if (finished && recent && newest) best = i;
    }
    if (best < 0) {
        if (diag::Perf()) {
            LOG_FIRST(16, "perf: late %08X nothing finished: done %llu now %llu, slots %llu/%llu %llu/%llu %llu/%llu",
                      data, (unsigned long long)done, (unsigned long long)now, (unsigned long long)l.fence[0],
                      (unsigned long long)l.frame[0], (unsigned long long)l.fence[1], (unsigned long long)l.frame[1],
                      (unsigned long long)l.fence[2], (unsigned long long)l.frame[2]);
        }
        return false;
    }
    if (l.fence[best] > l.delivered) {
        uint8_t* p = nullptr;
        // the last row is not padded to the pitch: a range to the end of the pitch fails to map
        D3D12_RANGE rr{0, size_t(l.row_pitch) * (l.gh - 1) + l.gw * 4};
        if (FAILED(l.rb[best]->Map(0, &rr, reinterpret_cast<void**>(&p)))) return false;
        uint32_t pitch = GuestPitch(l.gw);
        uint8_t* dst = static_cast<uint8_t*>(kernel::HostPtr(data));
        for (uint32_t y = 0; y < l.gh; y++) std::memcpy(dst + size_t(y) * pitch, p + size_t(y) * l.row_pitch, l.gw * 4);
        D3D12_RANGE none{0, 0};
        l.rb[best]->Unmap(0, &none);
        l.delivered = l.fence[best];
    }
    return true;
}

// a slot set for another size: its buffers go with the frame
void D3D12Backend::ResetLate(Late& l, const Target& t) {
    auto& garbage = CurrentFrame().garbage;
    for (auto& r : l.rb) {
        if (r) garbage.push_back(r);
    }
    if (l.down) garbage.push_back(l.down);
    l = Late{};
    l.gw = t.gw;
    l.gh = t.gh;
}

// the target as a guest sized BGRA copy source, shrunk into l.down when it is not one already
ID3D12Resource* D3D12Backend::PrepareLateSource(Target& t, Late& l) {
    ShrinkOptions reuse{.into = l.down.Get(), .into_state = l.down_state};
    CopySource src = PrepareCopySource(t, reuse);
    if (src.down) {
        l.down = src.down;
        l.down_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    return src.res;
}

// src into the readback buffer of the slot, made on first use
void D3D12Backend::RecordLateCopy(Late& l, uint32_t slot, ID3D12Resource* src) {
    D3D12_RESOURCE_DESC rd = src->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT rows;
    UINT64 row_size;
    UINT64 bytes;
    dev_->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &row_size, &bytes);
    if (!l.rb[slot]) {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC bd = BufferDesc(bytes);
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                 nullptr, IID_PPV_ARGS(&l.rb[slot])))) {
            return;
        }
    }
    D3D12_TEXTURE_COPY_LOCATION dl{l.rb[slot].Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dl.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION sl{src, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    cl_->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    l.row_pitch = fp.Footprint.RowPitch;
    l.fence[slot] = fence_value_ + 1;  // the next Submit carries it
    l.frame[slot] = presents_;
    l.next = (slot + 1) % kFrames;
}

void D3D12Backend::RecordLate(uint32_t data) {
    auto it = targets_.find(TargetMapKey(data, false));
    if (it == targets_.end() || TargetStale(data)) return;
    Target& t = it->second;
    PerfScope perf_sync{g_perf.sync};
    g_perf.lates++;
    // what DeliverLate or Sync left in guest memory is no CPU write to upload back
    t.sig = MemSig(t.data, t.bytes);
    t.read.clear();
    std::lock_guard lock(late_lock_);
    Late& l = late_[data];
    if (l.gw != t.gw || l.gh != t.gh) {
        if (diag::Perf()) LOG_FIRST(16, "perf: late %08X ring reset, %ux%u -> %ux%u", data, l.gw, l.gh, t.gw, t.gh);
        ResetLate(l, t);
    }
    // a copy for a later lock, unless the GPU is a whole ring behind
    uint32_t slot = l.next;
    if (l.fence[slot] > fence_->GetCompletedValue()) return;
    ID3D12Resource* src = PrepareLateSource(t, l);
    if (!src) return;
    RecordLateCopy(l, slot, src);
}

// filters src into a new w x h target, left as a copy source
ComPtr<ID3D12Resource> D3D12Backend::Shrink(ID3D12Resource* src, D3D12_RESOURCE_STATES& st, DXGI_FORMAT fmt,
                                            uint32_t w, uint32_t h, const ShrinkOptions& o) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = Texture2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, w, h);
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = td.Format;
    ComPtr<ID3D12Resource> down = o.into;
    D3D12_RESOURCE_STATES down_state = o.into ? o.into_state : D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_perf.shrinks++;
    if (free_rtv_.empty()) return nullptr;
    if (down) {
        Transition(down.Get(), down_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    } else if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&down)))) {
        return nullptr;
    }
    // borrows the next free view without taking it, only for this recording
    D3D12_CPU_DESCRIPTOR_HANDLE rh = Cpu(rtv_heap_.Get(), free_rtv_.back(), rtv_inc_);
    dev_->CreateRenderTargetView(down.Get(), nullptr, rh);
    Transition(src, st, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    SetBlitTarget(rh, w, h);
    float uv[4] = {0, 0, 1, 1};
    Blit(src, fmt, uv, {.alpha = true, .taps = o.taps, .mul = o.mul});
    Transition(down.Get(), down_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    return down;
}

// a BGRA copy source to the CPU, rows packed; waits for the GPU
bool D3D12Backend::ReadBack(ID3D12Resource* res, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    D3D12_RESOURCE_DESC rd = res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT rows;
    UINT64 row_size;
    UINT64 bytes;
    dev_->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &row_size, &bytes);
    ComPtr<ID3D12Resource> rb;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd = BufferDesc(bytes);
    if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&rb)))) {
        return false;
    }
    D3D12_TEXTURE_COPY_LOCATION dl{rb.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dl.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION sl{res, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    cl_->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    Flush();
    uint8_t* p = nullptr;
    rb->Map(0, nullptr, reinterpret_cast<void**>(&p));
    out.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++) std::memcpy(&out[size_t(y) * w * 4], p + size_t(y) * fp.Footprint.RowPitch, w * 4);
    rb->Unmap(0, nullptr);
    return true;
}

// BMP file and info header: byte offsets of the fields CapSave writes
constexpr int kBmpHeaderBytes = 54;
constexpr int kBmpFileSize = 2;
constexpr int kBmpPixelOffset = 10;
constexpr int kBmpInfoSize = 14;
constexpr int kBmpWidth = 18;
constexpr int kBmpHeight = 22;  // negative: rows top down
constexpr int kBmpPlanes = 26;
constexpr int kBmpBitCount = 28;
constexpr int kBmpImageSize = 34;
constexpr uint32_t kBmpInfoBytes = 40;

void D3D12Backend::CapSave(const CapImage& img, const char* name) {
    ComPtr<ID3D12Resource> down = Shrink(img.res, *img.state, img.fmt, img.w, img.h, {.mul = img.mul});
    std::vector<uint8_t> px;
    if (!down || !ReadBack(down.Get(), img.w, img.h, px)) return;
    FILE* f = _wfopen((cap_dir_ / (std::string(name) + ".bmp")).c_str(), L"wb");
    if (!f) return;
    uint8_t hd[kBmpHeaderBytes] = {'B', 'M'};  // BITMAPFILEHEADER and BITMAPINFOHEADER, top down 32 bit
    auto put = [&](int o, uint32_t v) { std::memcpy(hd + o, &v, 4); };
    put(kBmpFileSize, kBmpHeaderBytes + uint32_t(px.size()));
    put(kBmpPixelOffset, kBmpHeaderBytes);
    put(kBmpInfoSize, kBmpInfoBytes);
    put(kBmpWidth, img.w);
    put(kBmpHeight, uint32_t(-int32_t(img.h)));
    hd[kBmpPlanes] = 1;
    hd[kBmpBitCount] = 32;
    put(kBmpImageSize, uint32_t(px.size()));
    fwrite(hd, 1, kBmpHeaderBytes, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
}

void D3D12Backend::CapSave(Target& t, const char* name) {
    ReadyRes(t);
    DXGI_FORMAT fmt = t.depth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : t.fmt;
    float mul = t.depth ? 1 : t.range;
    CapSave({.res = t.res.Get(), .state = &t.state, .fmt = fmt, .w = t.gw, .h = t.gh, .mul = mul}, name);
}

// the frame moves to another target: save the one it leaves
void D3D12Backend::CapStep(const Surface& rt) {
    uint32_t d = rt.header ? mem::VirtualFromPhysical(rt.data) : 0;
    if (d == cap_rt_) return;
    auto it = targets_.find(TargetMapKey(cap_rt_, false));
    if (cap_rt_ && it != targets_.end()) {
        char name[32];
        snprintf(name, sizeof(name), "%04u_%08X", cap_i_, cap_rt_);
        CapSave(it->second, name);
    }
    auto z = targets_.find(TargetMapKey(cap_rt_, true));
    if (cap_rt_ && z != targets_.end()) {
        char name[32];
        snprintf(name, sizeof(name), "%04u_%08X_z", cap_i_, cap_rt_);
        CapSave(z->second, name);
    }
    cap_rt_ = d;
}

void D3D12Backend::Capture(Target& t, const char* tag, float mul) {
    if (!cap_) return;
    ReadyRes(t);
    char name[32];
    snprintf(name, sizeof(name), "%04u_%s_%08X", cap_i_, tag, t.data);
    CapSave({.res = t.res.Get(), .state = &t.state, .fmt = t.fmt, .w = t.gw, .h = t.gh, .mul = mul}, name);
}

void D3D12Backend::SetGamma(const uint8_t* ramp) {
    std::memcpy(ramp_, ramp, sizeof(ramp_));
    ramp_set_ = true;
    ramp_dirty_ = true;
}

}  // namespace

std::unique_ptr<Backend> CreateBackend() { return std::make_unique<D3D12Backend>(); }

}  // namespace gpu
