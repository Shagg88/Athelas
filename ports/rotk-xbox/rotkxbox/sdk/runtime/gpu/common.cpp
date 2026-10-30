// API-neutral pieces of the renderer, shared by its backends: the pass shaders' HLSL, movie placement, matrix helpers
// and the draw constants filled from State alone
#include "gpu/common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"

namespace gpu {

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

// bytes per row of a 32 bit surface in guest memory, rounded to 64
uint32_t GuestPitch(uint32_t width) { return (width * 4 + 63) & ~63u; }

// kMovieFit's shape: the screen's (app::FrameAspect), from 16:9 up to g_movie_fit_max (a game whose films are wider
// than 16:9 raises it to their own shape)
float MovieFitAspect() { return std::clamp(app::FrameAspect(), 16.0f / 9.0f, std::max(g_movie_fit_max.load(), 16.0f / 9.0f)); }

// where movie_display puts a movie: the part of its frame shown (uv: left, top, width, height) and that part's shape
// on screen; stretch: the whole screen instead. Zoom (3) fills the screen's width with a 4:3 movie, its top and
// bottom cut off; the films (kMovieFit, kMovieWide) zoom as Fit shows them.
MoviePlace PlaceMovie(uint32_t display, uint32_t shape) {
    MoviePlace p;
    if (display == 3 && shape != kMovie43) display = 0;
    if (display == 0 && shape == kMovieFit) {
        // fit: the middle of the 4:3 frame, where the films are (their bars are outside it): 16:9, or up to the
        // films' own shape on wider screens, which then get bars at the sides rather than lose the film's top
        p.aspect = MovieFitAspect();
        p.uv[3] = (4.0f / 3.0f) / p.aspect;
        p.uv[1] = MovieFitTop(p.uv[3]);
        float z = std::clamp(g_movie_fit_zoom.load(), 1.0f, 2.0f);
        if (z > 1.0f) {  // zoomed further: the window's middle, same shape
            float c = p.uv[1] + p.uv[3] / 2;
            p.uv[2] = 1.0f / z;
            p.uv[0] = 0.5f - p.uv[2] / 2;
            p.uv[3] /= z;
            p.uv[1] = c - p.uv[3] / 2;
        }
    } else if (display == 0 && shape == kMovieWide) {
        p.aspect = 16.0f / 9.0f;  // a widescreen film squeezed into the 4:3 frame: the whole frame, unsqueezed
    } else if (display == 3) {
        p.aspect = std::max(app::FrameAspect(), 4.0f / 3.0f);  // the screen's shape, the frame's middle
        p.uv[3] = (4.0f / 3.0f) / p.aspect;
        p.uv[1] = 0.5f - p.uv[3] / 2;
    } else if (display == 1) {
        p.stretch = true;
    }
    return p;
}

uint32_t SimpleState(const State& s, uint32_t method) {
    return s.simple[method >> 2];
}


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

void FillCombinerConstants(const State& s, DrawConstants& c) {
    for (int i = 0; i < 8; i++) {
        Color(SimpleState(s, kMPsConstant0 + i * 4), c.pc[i * 2]);
        Color(SimpleState(s, kMPsConstant1 + i * 4), c.pc[i * 2 + 1]);
    }
    Color(SimpleState(s, kMPsFinalConstant0), c.pc[16]);
    Color(SimpleState(s, kMPsFinalConstant1), c.pc[17]);
    Color(s.rs[kRsTextureFactor], c.tfactor);
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
    // an all-zero world or view never means anything a draw could use (on the console it draws nothing either way):
    // the identity. The Third Age draws its movies with only the projection set and the other two cleared
    // (jobs 036-038: wvp all zeros, film black with its sound playing).
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto usable = [](const float* m) {
        for (int i = 0; i < 16; i++)
            if (m[i] != 0.0f) return m;
        LOG_FIRST(1, "gpu: an all-zero world or view transform drawn as the identity");
        return kIdentity;
    };
    MatMul(usable(s.transforms[kTsWorld].data()), usable(s.transforms[kTsView].data()), wv);
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

static void LightToView(const std::array<uint32_t, kLightFloats>& l, const float* vm, float (&o)[7][4]) {
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

// ---- the draw's shaders, chosen the same way by every backend

TexKind TextureKind(const TextureDesc& d) {
    if (d.cube) return kTexCube;
    if (d.depth > 1) return kTex3D;
    return kTex2D;
}

ShaderChoice ClassifyShaders(const State& s, const Assembled& a, uint32_t count) {
    ShaderChoice sh;
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

uint64_t VertexKey(const State& s, const Assembled& a, const ShaderChoice& sh) {
    uint64_t seed = sh.prog ? s.vs->hash ^ a.regs : Hash(&s.vs_handle, 4, a.regs | kDeclarationSalt);
    uint64_t vkey = Hash(&a.decode, 8, seed);
    if (!sh.prog && a.pretransformed) vkey ^= kPretransformedSalt;
    if (sh.quad) vkey ^= kQuadSalt;
    if (!sh.prog) vkey = Hash(&sh.fk, sizeof(sh.fk), vkey);
    return vkey;
}

std::string VertexHlsl(const State& s, const Assembled& a, const ShaderChoice& sh) {
    if (sh.prog) return VertexProgramHlsl(s.vs->program, a.regs, a.decode);
    return FixedVertexHlsl(a.regs, a.pretransformed, a.decode, sh.fk);
}

}  // namespace gpu
