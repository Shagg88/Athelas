#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <vector>

#include "gpu/state.h"

namespace gpu {

enum class Topology : uint8_t { Points, Lines, Triangles };

// how the vertex shader finishes an element the input assembler reads raw, 4 bits per register in Assembled::decode
enum VertexDecode : uint8_t {
    kDecodeNone,
    kDecodeW1,       // 3 components read as 4: w = 1
    kDecodeInt,      // SHORT: integers to float
    kDecodeIntW1,    // SHORT3
    kDecodePacked,   // NORMPACKED3, 11:11:10 signed in a uint
    kDecodeFloat2H,  // x y w as 3 floats
};

// a buffer the draw reads: guest vertices as they are, or the float4 copy converted on the CPU
struct VertexBuffer {
    const uint8_t* src = nullptr;
    uint32_t bytes = 0, stride = 0;
};

// where input register r comes from: host vertex format (a DXGI_FORMAT), buffer, byte offset in the vertex
struct VertexElement {
    uint8_t format = 0, buffer = 0;
    uint16_t offset = 0;
};

// one draw's vertices and indices as the input assembler takes them
struct Assembled {
    std::vector<float> vertices;    // converted path: vertex_count * regs * 4
    std::vector<uint32_t> indices;  // converted path, or the list rebuilt for fans, quads and loops
    uint16_t regs = 0;              // input registers present, bit per v register
    uint32_t vertex_count = 0;
    Topology topology = Topology::Triangles;
    bool strip = false;             // line or triangle strip, drawn as a strip
    bool pretransformed = false;    // FVF XYZRHW
    uint64_t decode = 0;            // VertexDecode per register, 4 bits each
    std::array<VertexElement, 16> elements{};
    std::array<VertexBuffer, 16> buffers{};
    uint32_t buffer_count = 0;
    const void* index_src = nullptr;  // the title's 16 bit indices, or indices
    bool index16 = false;
    uint32_t index_count = 0;         // 0: an unindexed draw of draw_count vertices
    uint32_t draw_count = 0;
    int32_t base_vertex = 0;
    std::array<std::vector<uint8_t>, 16> gathered;  // raw vertices copied in draw order when the indices are sparse
};

// the FVF bits translated to the register layout the XDK's declarations use
void FvfInputs(uint32_t fvf, VertexShader& out);

// for a draw, how much a game wants its screen space x (v0, pixels) pulled towards the target's centre: 1 leaves
// it, 0.75 fits a 4:3 layout into the middle of a 16:9 picture. Set by game code; null for none.
using ScreenSpaceHook = float (*)(const State& s);
extern ScreenSpaceHook g_screen_space_hook;
// where a squeezed draw goes: given its x and y extent (pixels), the target width and the squeeze, the game sets
// x' = a * x + b. Null, or not set: pulled towards the centre (a = squeeze, b = width / 2 * (1 - squeeze)).
using ScreenSpacePlace = void (*)(float x0, float x1, float y0, float y1, float width, float squeeze, float& a,
                                  float& b);
extern ScreenSpacePlace g_screen_space_place;
// a draw in screen space (HUD, menus, text), as the game tells: texture dumps name the textures such draws use once
// in the log, so the pieces of a HUD are easy to find for a texture pack. Null for none.
using ScreenSpaceTest = bool (*)(const State& s);
extern ScreenSpaceTest g_screen_space_test;
// the projection a game sets (SetTransform, Direct3D 8 layout, row major _11.._44), as the renderer will use it: the
// game may change it (widescreen, field of view). The device copy GetTransform reads stays the game's. Null for none.
using ProjectionHook = void (*)(float* m);
extern ProjectionHook g_projection_hook;
// a fixed function draw under an orthographic projection (the HUD and menus of a game that draws its 2D through one):
// given its extent in clip space (x and y, -1 to 1), the game may move it sideways, x' = a * x + b. Return false to
// leave it. Null for none.
using OrthoPlace = bool (*)(const State& s, float x0, float x1, float y0, float y1, float& a, float& b);
extern OrthoPlace g_ortho_place;
// set by game code for the frame being drawn: the share of the frame's width, centred, that is shown; the sides are
// painted black (a full-screen menu picture held narrower than an ultrawide screen, with a 3D scene behind it that
// would show at the sides). 1 shows it all; put back to 1 after each frame is shown.
extern std::atomic<float> g_frame_sides;
// the clip space extent of a fixed function draw with XYZ positions in stream 0 (false when it has none)
bool OrthoExtent(const State& s, uint32_t start, uint32_t count, const uint8_t* indices, float& x0, float& x1, float& y0,
                 float& y1);

// false when there is nothing to draw
// indices: the title's 16 bit indices, null for an unindexed draw
bool Assemble(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices, Assembled& out);

}  // namespace gpu
