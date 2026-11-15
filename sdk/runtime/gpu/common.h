#pragma once
// API-neutral pieces of the renderer, shared by its backends (defined in gpu/common.cpp)
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gpu/shaders.h"
#include "gpu/state.h"
#include "gpu/texture.h"
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

// a small draw whose float2 inputs all lie in 0..1 (sprites, HUD, text): its wrapped textures are read clamped
bool UnitTexcoords(const Assembled& a);

// SMAA's three passes' HLSL (entries vs; edges, weights, blend)
const std::string& SmaaSource();

// F3 frame captures, both backends: on with XBE_CAPTURE_DIR; XBE_CAPTURE_EVERY etc. take them without a key; the
// targets and textures written as 32-bit top-down BMPs (px: BGRA rows)
bool CaptureOn();
bool AutoCaptureDue();
void WriteCaptureBmp(const std::filesystem::path& file, uint32_t w, uint32_t h, const std::vector<uint8_t>& px);

}  // namespace gpu
