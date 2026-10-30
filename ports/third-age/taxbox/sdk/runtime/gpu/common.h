#pragma once
// API-neutral pieces of the renderer, shared by its backends (defined in gpu/common.cpp)
#include <cstdint>

#include <string>

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

}  // namespace gpu
