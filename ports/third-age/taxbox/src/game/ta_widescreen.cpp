// The Third Age (Xbox): widescreen, as the ROTK port does it (rotk_widescreen.cpp). The game builds its perspective
// projections for 4:3; with [graphics] widescreen on and a wide display the host shows the frame at 16:9
// (app::FrameAspect), so the horizontal term _11 is scaled by 4:3 / 16:9: the same vertical view, more at the sides.
// Only perspective matrices (_34 = 1, _44 = 0) at about 4:3 are changed, never 2D ones, and only the renderer's copy
// (SDK gpu::g_projection_hook, which also sees the projections the game stores itself through its inlined
// SetTransform, d3d8.cpp TakeTransform). Menus, text and the HUD fill the 16:9 picture, as ROTK's do.
//
// [graphics] fov (70 to 150, percent of the game's own): the camera's view angle (radians at camera+0xE0) is widened
// through the tangent of its half angle while the game builds its view from it: 0xC8060 (ecx the view, its camera at
// [ecx+4]; the projection and the values it culls with, +0x318 tan, +0x31C its inverse) and 0x11F450 (eax the
// camera; its tangents at +0xEC, +0xF4). The game's own value is put back after, so nothing else sees the change.
// Narrow cameras (zoomed shots, under 25 degrees) are left alone, as in the ROTK port.
//
// [graphics] draw_distance (50 to 800, percent of the game's own): the far clip plane of the 3D cameras moves out, and
// the fog distances with it (gpu::g_fog_scale), as the ROTK port does. [graphics] alpha_edges: with anti-aliasing,
// cut-out textures (leaves, grass, fences) get smooth edges (alpha to coverage, gpu::g_alpha_edges).
//
// The vertex programs (characters) carry their own 4:3 projection in their constants, which the hook doesn't see: their
// 3D positions are narrowed by the same factor in the shader (gpu::g_program_widen). Each new camera is logged once.
#include <atomic>
#include <cmath>
#include <cstring>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/vertex.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace ta {
uint32_t g_fov_percent = 100;   // [graphics] fov
uint32_t g_draw_percent = 100;  // [graphics] draw_distance
bool g_alpha_edges = true;      // [graphics] alpha_edges (acts only with anti-aliasing on)
}  // namespace ta

namespace {
constexpr float kDeg = 57.29578f;

// the far plane f out to f * percent. Standard depth: _33 = f/(f-n), _43 = -n*f/(f-n); reversed depth (1 at the camera,
// 0 at f = -_43/_33): _33 divided by the percent
void DrawDistance(float* m) {
    uint32_t dd = ta::g_draw_percent;
    if (dd == 100) return;
    float k = float(dd) / 100.0f;
    if (m[10] < 0.0f && m[10] > -0.01f && m[14] > 0.0f) {
        m[10] /= k;
    } else if (m[10] > 1.0f && m[14] < 0.0f) {
        float n = -m[14] / m[10], f = m[14] / (1.0f - m[10]);
        if (f > n) {
            float f2 = f * k;
            m[10] = f2 / (f2 - n);
            m[14] = -n * f2 / (f2 - n);
        }
    }
}

void Projection(float* m) {
    // the vertex programs (characters, skinned meshes) carry the same 4:3 projection in their constants: their 3D
    // positions are narrowed by the same amount in the shader (gpu::g_program_widen), 2D ones are left
    gpu::g_program_widen = app::Widescreen() ? (4.0f / 3.0f) / app::FrameAspect() : 0.0f;
    // row major: _11 m[0], _22 m[5], _34 m[11], _44 m[15]
    bool persp = m[11] > 0.99f && m[11] < 1.01f && std::fabs(m[15]) < 0.001f;
    if (!persp || m[0] == 0.0f || m[5] == 0.0f) return;
    float game_aspect = std::fabs(m[5] / m[0]);  // a mirrored camera keeps its sign
    bool widened = app::Widescreen() && game_aspect > 1.30f && game_aspect < 1.36f;
    if (widened) m[0] *= game_aspect / app::FrameAspect();
    DrawDistance(m);
    // each distinct camera once (the first 8): its vertical angle and shape, and whether it was widened
    static float seen[8][2];
    static std::atomic<int> nseen{0};
    float vf = 2.0f * std::atan(1.0f / std::fabs(m[5])) * kDeg;
    int n = nseen;
    for (int i = 0; i < n && i < 8; i++)
        if (std::fabs(seen[i][0] - vf) < 0.5f && std::fabs(seen[i][1] - game_aspect) < 0.01f) return;
    if (n >= 8) return;
    seen[n][0] = vf;
    seen[n][1] = game_aspect;
    nseen = n + 1;
    LOGR("ta: 3D camera %.1f deg at aspect %.4f: %s", vf, game_aspect,
         widened ? "widened" : app::Widescreen() ? "left as it is (not 4:3)" : "widescreen off");
}
}  // namespace

namespace ta {
void ApplyCameraSettings();
// GameInfo::init: after the settings load, before the window and boot
void InitWidescreen() {
    gpu::g_projection_hook = Projection;
    gpu::g_wide_targets = true;  // drawn at the screen's own width, not stretched sideways
    // 16:9 at most: the game's 2D is stretched to the frame, and wider than 16:9 text would be more than twice as wide
    // as it is tall (ROTK holds its menus at 16:9 inside a wider frame; this game has no such placement yet)
    app::g_max_frame_aspect = 16.0f / 9.0f;
    LOGR("ta: widescreen %s", app::Widescreen() ? "on (16:9)" : "off (4:3)");
    ApplyCameraSettings();
}

// at start and when the in-game menu changes one (GameInfo::setting_changed)
void ApplyCameraSettings() {
    gpu::g_fog_scale = float(g_draw_percent) / 100.0f;
    gpu::g_alpha_edges = g_alpha_edges;
    if (g_draw_percent != 100) LOGR("ta: draw distance %u%%", g_draw_percent);
    if (g_alpha_edges) LOGR("ta: smooth cut-out edges with anti-aliasing");
}
}  // namespace ta

// ---- field of view
namespace {
float GuestF(uint32_t va) {
    float v;
    uint32_t u = ::kernel::R32(va);
    std::memcpy(&v, &u, 4);
    return v;
}
void GuestWF(uint32_t va, float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    ::kernel::W32(va, u);
}
// the camera's angle widened; returns the game's own to put back, or a negative number when left alone
float WidenFov(uint32_t camera) {
    constexpr float kNarrow = 25.0f / 57.29578f;
    uint32_t pct = ta::g_fov_percent;
    if (pct == 100 || !camera) return -1.0f;
    float f = GuestF(camera + 0xE0);
    if (!(f > kNarrow && f < 3.0f)) return -1.0f;
    float wide = 2.0f * std::atan(std::tan(f * 0.5f) * float(pct) / 100.0f);
    GuestWF(camera + 0xE0, wide);
    return f;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000C8060);
X86_FUNC(sub_000C8060) {
    uint32_t camera = ::kernel::R32(ctx.ecx + 4);
    float own = WidenFov(camera);
    __imp__sub_000C8060(ctx, base);
    if (own >= 0.0f) GuestWF(camera + 0xE0, own);
}

X86_EXTERN_FUNC(__imp__sub_0011F450);
X86_FUNC(sub_0011F450) {
    uint32_t camera = ctx.eax;
    float own = WidenFov(camera);
    __imp__sub_0011F450(ctx, base);
    if (own >= 0.0f) GuestWF(camera + 0xE0, own);
}

// [[hook]] 0xD900F, 0xD906D, 0xD90D2: the game has just set its TV-safe border, 64 pixels across ([0x304E70]) and 32
// down ([0x30AF98]; 16 in field mode). Every viewport (0xC26C0) and the HUD and menu layout read it, which leaves a
// black frame round the picture: 32 pixels at the sides and 16 at the top and bottom of the 640x480 frame (job 097).
// Without [graphics] safe_area the border is zero, so the game lays out the world, HUD and menus for the full frame
// itself (the same fix as Return of the King's).
namespace ta {
bool g_safe_area = false;
}
void TA_SafeArea(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    if (ta::g_safe_area) return;
    ::kernel::W16(0x304E70, 0);
    ::kernel::W16(0x30AF98, 0);
    static std::atomic<int> logged{0};
    if (!logged++) LOGR("ta: no TV-safe border, the picture fills the screen");
}
