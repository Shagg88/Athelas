#pragma once
// API-neutral pieces of the renderer, shared by its backends (defined in gpu/common.cpp)
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "gpu/shaders.h"
#include "gpu/state.h"
#include "gpu/texture.h"
#include "gpu/texture_pack.h"
#include "gpu/vertex.h"

namespace gpu {

// the pass shaders (HLSL)
extern const char* kBlitHlsl;
extern const char* kClearHlsl;
extern const char* kMotionShowHlsl;
extern const char* kDepthHlsl;
extern const char* kMsUpHlsl;
extern const char* kMsUpDepthHlsl;
extern const char* kMsDepthResolveHlsl;

// the render target's console size in pixels, or the depth buffer's without one
struct GuestExtent {
    uint32_t w;
    uint32_t h;
};

// a register's bits as a float
float Bits(uint32_t v);
// a D3DCOLOR (ARGB) as rgba floats
void Color(uint32_t argb, float* o);
// bytes per row of a 32 bit surface in guest memory, rounded to 64
uint32_t GuestPitch(uint32_t width);
// a push buffer method's value in s.simple
uint32_t SimpleState(const State& s, uint32_t method);

// kMovieFit's shape on this screen
float MovieFitAspect();
// where movie_display puts a movie: the part of its frame shown (uv: left, top, width, height) and that part's shape
// on screen; stretch: the whole screen instead
struct MoviePlace {
    float uv[4] = {0, 0, 1, 1};
    float aspect = 4.0f / 3.0f;
    bool stretch = false;
};
MoviePlace PlaceMovie(uint32_t display, uint32_t shape);

// 4x4 row major matrices
void MatMul(const float* x, const float* y, float* o);
void Transpose(const float* m, float* o);
void InverseTranspose3x3(const float* wv, float* n);

// draw constants that come from State alone
void FillFogConstants(const State& s, bool vertex_program, DrawConstants& c);
void FillCombinerConstants(const State& s, DrawConstants& c);
void FillTransformConstants(const State& s, GuestExtent size, DrawConstants& c, float place_a, float place_b);
void FillLightConstants(const State& s, const FixedKey& fk, DrawConstants& c);

// the draw's shaders: what they are made from and the vertex shader's cache key
constexpr uint32_t kQuadVertices = 6;  // a quad drawn as two triangles
struct ShaderChoice {
    bool prog = false;  // a vertex program, else the fixed function pipeline
    bool quad = false;  // up to a quad with no depth test: QuadHlsl
    bool fixed_transform = false;  // fixed function on untransformed vertices: the shader reads matrices and lights
    FixedKey fk;
    uint64_t vkey = 0;
};
ShaderChoice ClassifyShaders(const State& s, const Assembled& a, uint32_t count);
// the program's hash, cached in the guest shader object
void EnsureProgramHash(const State& s);
uint64_t VertexKey(const State& s, const Assembled& a, const ShaderChoice& sh);
std::string VertexHlsl(const State& s, const Assembled& a, const ShaderChoice& sh);
TexKind TextureKind(const TextureDesc& d);

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

// a small draw whose float2 inputs all lie in 0..1 (sprites, HUD, text)
bool UnitTexcoords(const Assembled& a);
// such a draw's wrapped textures are read clamped (no bleed from the far edge when enlarged), a bit per stage, but
// only on a stage whose coordinates really are those inputs: not one the fixed pipeline moves with a texture matrix
// or generates, and not one a vertex program computes (only a plain copy of a float2 input to oT counts). A
// scrolling picture on a unit quad wraps, as on the console.
uint32_t ClampStages(const State& s, const Assembled& a);

// SMAA's three passes' HLSL (entries vs; edges, weights, blend)
const std::string& SmaaSource();

// F3 frame captures, both backends: on with XBE_CAPTURE_DIR; XBE_CAPTURE_EVERY etc. take them without a key; the
// targets and textures written as 32-bit top-down BMPs (px: BGRA rows)
bool CaptureOn();
bool AutoCaptureDue();
void WriteCaptureBmp(const std::filesystem::path& file, uint32_t w, uint32_t h, const std::vector<uint8_t>& px);

// a surface's signature: hashed whole up to 64 KB, a bigger one as 64 evenly spaced samples of 64 bytes (both
// backends: one copy, so they can't drift apart; the D3D12 backend counts them for its perf lines)
constexpr uint32_t kSigWholeMax = 65536;
constexpr uint32_t kSigSamples = 64;
constexpr uint32_t kSigSampleBytes = 64;
uint64_t MemSig(uint32_t data, uint32_t total);

// shaders.seed (ASS1, then entry, profile and source as length-prefixed strings): the shaders to compile ahead
struct SeedShader {
    std::string entry, profile, src;
};
std::vector<SeedShader> ReadShaderSeed(const std::filesystem::path& file);

// gpu::g_art_shot: a movie frame read back (BGRA rows, w x h) cropped of its black borders and written as the PNG
// asked for, unless it is nearly black (a later frame then) or another was asked for meanwhile
void ArtShotSave(const std::string& path, const std::vector<uint8_t>& px, uint32_t w, uint32_t h);

// the movie ring's clock, in seconds
double MovieClock();

// Texture packs, the backends' shared part: a texture's pack hash (its file name's), its level 0 dumped as a PNG once
// (dumped: the hashes done), and a picture the game makes for it (GameInfo builtins, the 4x redraw) when no file has
// one. pf: how the texture's bytes are laid out on the host.
uint64_t PackHash(const TextureDesc& d, const uint8_t* p, const uint32_t* colours);
void PackDump(pack::Format pf, const TextureDesc& d, uint64_t h, const std::vector<std::vector<uint8_t>>& levels,
              std::unordered_set<uint64_t>& dumped);
bool PackMake(pack::Format pf, const TextureDesc& d, uint64_t h, const uint8_t* p, const uint32_t* colours,
              pack::Image& img);

}  // namespace gpu
