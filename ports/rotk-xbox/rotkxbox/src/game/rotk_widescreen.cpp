// ROTK Xbox: widescreen and field of view, as ROTK Fix does them on PC (graphics.c, SetTransform).
// The game builds its perspective projections for 4:3. With widescreen on and a wide display the host shows the
// frame at 16:9, or at the screen's own shape on an ultrawide one (app::FrameAspect, up to 32:9; gpu BlitFrame), so the
// horizontal term _11 is scaled by 4:3 / that shape: the same vertical view, more at the sides. FieldOfView (70 to 150, percent of the game's own angle) widens the view angle through the tangent of
// the half angle; narrow cameras (zoomed shots under 25 degrees) are left alone. Only perspective matrices
// (_34 = 1, _44 = 0) are changed, never the 2D ones, and only the renderer's copy (SDK gpu::g_projection_hook), as
// ROTK Fix changes only what reaches Direct3D. Menus, text and the HUD fill the picture up to 16:9 and are held at
// 16:9 in the middle of a wider one, as on PC with ROTK Fix (Place2D).
//
// DrawDistance (50 to 800, percent of the game's own): the far clip plane of the 3D cameras moves out, and the fog
// distances with it (gpu::g_fog_scale), as ROTK Fix does. AlphaEdgeSmoothing: with anti-aliasing, cut-out textures
// (leaves, grass, fences) get smooth edges (alpha to coverage, gpu::g_alpha_edges).
//
// hud_edges (off by default; ROTKFix.ini [Graphics] HUDEdges): with widescreen, HUD pictures keep their 4:3 shape and
// the pieces near a side move out to that side of the 16:9 picture, the rest stays centred. The game draws its 2D
// through an orthographic projection (FVF XYZ|DIFFUSE|TEX1), so each draw is placed by its extent in clip space
// (SDK gpu::g_ortho_place), with ROTK Fix's tests for what keeps the full width: untextured draws (fades), screen
// copies, small textures stretched across, and bands along the top or bottom. A full-screen picture keeps its shape.
#include <atomic>
#include <algorithm>
#include <vector>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/state.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace rotk {
uint32_t g_fov_percent = 100;   // [graphics] fov, ROTKFix.ini [Graphics] FieldOfView
uint32_t g_draw_percent = 100;  // [graphics] draw_distance, ROTKFix.ini [Graphics] DrawDistance
bool g_alpha_edges = false;     // [graphics] alpha_edges, ROTKFix.ini [Graphics] AlphaEdgeSmoothing
bool g_hud_edges = false;       // [graphics] hud_edges, ROTKFix.ini [Graphics] HUDEdges
bool g_safe_area = false;       // [graphics] safe_area: the console's black TV border around the picture

// after the settings load
void InitCamera() {
    app::g_max_frame_aspect = 32.0f / 9.0f;  // ultrawide screens get their full width (see Place2D)
    gpu::g_wide_targets = true;              // drawn at the screen's own width, not stretched sideways
    gpu::g_alpha_edges = g_alpha_edges;
    gpu::g_fog_scale = float(g_draw_percent) / 100.0f;
    if (g_draw_percent != 100) LOGR("rotk: draw distance %u%%", g_draw_percent);
    if (g_alpha_edges) LOGR("rotk: smooth cut-out edges with anti-aliasing");
    if (g_hud_edges) LOGR("rotk: HUD at 4:3 with its pieces at the screen edges (with widescreen)");
}
}

namespace {
constexpr float kDeg = 57.29578f;

void Projection(float* m) {
    // row major: _11 m[0], _22 m[5], _34 m[11], _44 m[15]
    bool persp = m[11] > 0.99f && m[11] < 1.01f && std::fabs(m[15]) < 0.001f;
    if (!persp || m[0] == 0.0f || m[5] <= 0.0f) return;
    // the gameplay camera can be mirrored (_11 negative): magnitudes, sign kept
    float game_aspect = std::fabs(m[5] / m[0]);
    if (app::Widescreen() && game_aspect > 1.30f && game_aspect < 1.36f) {
        float screen = app::FrameAspect();
        m[0] *= game_aspect / screen;
    }
    uint32_t pct = rotk::g_fov_percent;  // applied to the camera (CameraFov), so its culling follows
    // draw distance: the far plane f out to f * percent. Standard depth: _33 = f/(f-n), _43 = -n*f/(f-n); reversed
    // depth (z/w = _43/z + _33, 1 at the camera, 0 at f = -_43/_33): _33 divided by the percent
    uint32_t dd = rotk::g_draw_percent;
    float raw33 = m[10], raw43 = m[14];
    if (dd != 100) {
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
    // each distinct camera once (the first 8)
    static float seen[8][2];
    static std::atomic<int> nseen{0};
    float vf = 2.0f * std::atan(1.0f / (m[5] != 0 ? m[5] : 1.0f)) * kDeg;
    int n = nseen;
    for (int i = 0; i < n && i < 8; i++)
        if (std::fabs(seen[i][0] - vf) < 0.5f && std::fabs(seen[i][1] - raw43) <= std::fabs(raw43) * 0.01f) return;
    if (n >= 8) return;
    seen[n][0] = vf;
    seen[n][1] = raw43;
    nseen = n + 1;
        LOGR("rotk: 3D camera %.1f deg at aspect %.4f (_33 %g _43 %g); widescreen %s, field of view %u%%, draw distance %u%%",
             vf, game_aspect, raw33, raw43,
             app::Widescreen() ? "on" : "off", pct, dd);
}

// Where a 2D draw goes (the game draws all its 2D through an orthographic projection). The frame is shown at
// app::FrameAspect: 16:9, or the screen's own shape on an ultrawide one. As ROTK Fix does on PC, menus, text and the
// HUD fill the picture up to 16:9 and are held at 16:9 in the middle of a wider one (stretched further, text would be
// more than twice as wide as it is tall). Left full width: untextured draws (fades, flat overlays), copies of the
// screen, movie frames (movie_display shapes those), and gradients or shading bands stretched across.
//
// hud_edges: in play (frames without a full-screen menu picture), HUD pieces keep their 4:3 shape and each group of
// pieces near a side moves out to that edge of the picture; groups in the middle (subtitles, messages) stay centred.
// The game draws the HUD a piece at a time (portrait, bar segments, digits, the ring...), so a piece can't tell on its
// own which group it belongs to: the pieces of the frame before are joined into groups of touching boxes, and each
// piece follows the group its centre falls in (the HUD barely moves from one frame to the next). A group reaching
// from the left quarter to no further than just past the middle goes left; the mirror goes right.
struct Box {
    float x0, x1, y0, y1;
    int side = 0;  // groups: -1 left, 1 right, 0 middle
};
struct Groups {
    uint32_t frame = ~0u;
    std::vector<Box> drawn;  // this frame's pieces
    std::vector<Box> groups;       // the frame before's groups
    uint32_t menu_frame = ~0u;     // the last frame with a full-screen menu picture
} g_groups;

constexpr uint32_t kFrameCount = 0x1E2F04;  // the game's frame counter (0xBF1A0)

// the frame before's pieces into groups of touching boxes, and which reach a side
void MakeGroups() {
    std::vector<Box>& d = g_groups.drawn;
    std::vector<int> group(d.size());
    for (size_t i = 0; i < d.size(); i++) group[i] = int(i);
    std::function<int(int)> find = [&](int i) { return group[size_t(i)] == i ? i : group[size_t(i)] = find(group[size_t(i)]); };
    constexpr float kGap = 0.04f;
    for (size_t i = 0; i < d.size(); i++)
        for (size_t j = i + 1; j < d.size(); j++)
            if (d[i].x0 <= d[j].x1 + kGap && d[j].x0 <= d[i].x1 + kGap && d[i].y0 <= d[j].y1 + kGap && d[j].y0 <= d[i].y1 + kGap)
                group[size_t(find(int(i)))] = find(int(j));
    std::vector<Box> box(d.size(), Box{1e9f, -1e9f, 1e9f, -1e9f});
    for (size_t i = 0; i < d.size(); i++) {
        Box& g = box[size_t(find(int(i)))];
        g = {std::min(g.x0, d[i].x0), std::max(g.x1, d[i].x1), std::min(g.y0, d[i].y0), std::max(g.y1, d[i].y1)};
    }
    g_groups.groups.clear();
    for (size_t i = 0; i < d.size(); i++) {
        if (find(int(i)) != int(i)) continue;
        Box g = box[i];
        g.side = g.x0 < -0.5f && g.x1 < 0.1f ? -1 : g.x1 > 0.5f && g.x0 > -0.1f ? 1 : 0;
        g_groups.groups.push_back(g);
    }
    d.clear();
}

bool Place2D(const gpu::State& s, float x0, float x1, float y0, float y1, float& a, float& b) {
    if (!app::Widescreen()) return false;
    float frame = app::FrameAspect();
    bool ultra = frame > 16.0f / 9.0f + 0.01f;
    if (!rotk::g_hud_edges && !ultra) return false;  // stretched with the frame, as on PC
    uint32_t w, h;
    gpu::SurfaceSize(s.rt, w, h);
    if (w != 640 || h != 480) return false;  // the screen, not a texture the game draws into
    uint32_t now = ::kernel::R32(kFrameCount);
    if (now != g_groups.frame) {
        g_groups.frame = now;
        MakeGroups();
    }
    float wide = ultra ? (16.0f / 9.0f) / frame : 1.0f;  // 16:9 in the middle of an ultrawide picture
#ifndef _WIN32
    if (std::getenv("HUD_BOXES")) {  // test build: each 2D draw's box
        gpu::TextureDesc t;
        bool tex = s.textures[0] && gpu::Describe(s.textures[0], t);
        LOGI("hud box %u %.3f %.3f %.3f %.3f tex %ux%u", now, x0, x1, y0, y1, tex ? t.width : 0, tex ? t.height : 0);
    }
#endif
    gpu::TextureDesc d;
    if (!s.textures[0] || !gpu::Describe(s.textures[0], d)) {
        // untextured: a fade over the whole screen keeps the whole width, anything smaller is held at 16:9 on an
        // ultrawide screen
        if (x0 <= -0.98f && x1 >= 0.98f) return false;
        if (!ultra) return false;
        a = wide;
        b = 0;
        return true;
    }
    if (d.width >= 640 && d.height >= 480) return false;                  // a copy of the screen
    // a movie frame (its Y plane, 768x544, in stage 2): the movie keeps the whole frame, movie_display shapes it
    gpu::TextureDesc y;
    if (s.textures[2] && gpu::Describe(s.textures[2], y) && y.width == 768 && y.height == 544) return false;
    // gradients and shading bands stretched across the screen (small textures over the whole screen, bands along the
    // top or bottom) keep the whole width at any shape: held at 16:9 they would end in a visible edge
    bool spans = x0 <= -0.98f && x1 >= 0.98f;
    bool tall = y0 <= -0.98f && y1 >= 0.98f;
    bool edge_y = y0 <= -0.98f || y1 >= 0.98f;
    bool band = (y1 - y0) < 0.5f && edge_y;
    if (spans && ((tall && (d.width <= 128 || d.height <= 128)) || band)) return false;  // not a light streak
    if (!spans && (x0 <= -0.98f || x1 >= 0.98f) && edge_y && band && (x1 - x0) >= 0.8f) return false;  // in pieces
    // a full-screen menu picture: held at 16:9 on an ultrawide screen, where the menus' 3D scenes, drawn behind it,
    // would show beside it, so the sides are painted black
    if (spans && tall) {
        g_groups.menu_frame = now;
        if (!ultra) return false;
        a = wide;
        b = 0;
        gpu::g_frame_sides = wide;
        return true;
    }
    if (!rotk::g_hud_edges || g_groups.menu_frame == now) {  // menus, or the HUD without hud_edges
        if (!ultra) return false;
        a = wide;
        b = 0;
        return true;
    }
    // hud_edges, in play
    a = (4.0f / 3.0f) / frame;

    g_groups.drawn.push_back({x0, x1, y0, y1});
    // the group its centre falls in, else one it overlaps (a piece new this frame, such as the flashing low-health
    // bar, joins it), else its own extent by the same rule
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int side = 2;
    for (const Box& g : g_groups.groups)
        if (cx >= g.x0 && cx <= g.x1 && cy >= g.y0 && cy <= g.y1) side = g.side;
    if (side == 2)
        for (const Box& g : g_groups.groups)
            if (x0 <= g.x1 && g.x0 <= x1 && y0 <= g.y1 && g.y0 <= y1) side = g.side;
    if (side == 2) side = x0 < -0.5f && x1 < 0.1f ? -1 : x1 > 0.5f && x0 > -0.1f ? 1 : 0;
    b = side < 0 ? a - 1 : side > 0 ? 1 - a : 0;  // a left group's left side stays at the screen's left
    return true;
}

const int kRegistered = (gpu::g_projection_hook = Projection, gpu::g_ortho_place = Place2D, 0);
}  // namespace

// 0xA2B50 (Viewport.c) makes a viewport: its screen shape is 1 x 0.75 (+0x10/+0x14, +0x18/+0x1C) and its aspect 4:3
// (+0x24, +0x2C). The cameras build their projections and their visibility tests from it, so with widescreen the
// viewport is made 16:9 here: the world is drawn and culled for the wide picture (only widening the projection
// left the ground at the sides undrawn: black edges, objects floating). Projection() then sees 16:9 and leaves _11.
X86_EXTERN_FUNC(__imp__sub_000A2B50);
X86_FUNC(sub_000A2B50) {
    __imp__sub_000A2B50(ctx, base);
    if (!app::Widescreen() || !ctx.eax) return;
    auto f = [&](uint32_t off, float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        ::kernel::W32(ctx.eax + off, u);
    };
    float kAspect = app::FrameAspect();
    f(0x14, 1.0f / kAspect);
    f(0x1C, 1.0f / kAspect);
    f(0x24, kAspect);
    f(0x2C, kAspect);
    static std::atomic<int> logged{0};
    if (!logged++) LOGR("rotk: viewports made %.2f:1 (widescreen)", kAspect);
}

// 0xA32F0 (ctx): a camera's frustum and projection from its horizontal field of view (camera = [ctx + 4], angle in
// radians at +0xE0) and its viewport's aspect. The game keeps the horizontal angle, so on a 16:9 viewport the view
// would lose height; the angle is widened for the call so the vertical view stays the console's (more is seen at
// the sides, and culled for), and FieldOfView scales the vertical angle as ROTK Fix does (cameras under 25 degrees
// left alone, 150 at most). The camera's own value is put back afterwards.
X86_EXTERN_FUNC(__imp__sub_000A32F0);
X86_FUNC(sub_000A32F0) {
    uint32_t cam = ::kernel::R32(::kernel::R32(ctx.esp + 4) + 4);
    uint32_t at = cam + 0xE0, saved = 0;
    bool wide = app::Widescreen();
    uint32_t pct = rotk::g_fov_percent;
    if (cam && (wide || pct != 100)) {
        saved = ::kernel::R32(at);
        float h;
        std::memcpy(&h, &saved, 4);
        if (h > 0.0f && h < 3.1f) {
            float tv = std::tan(h * 0.5f) / (4.0f / 3.0f);  // the vertical half angle on the console's 4:3
            float v = std::atan(tv);
            if (pct != 100 && 2.0f * v > 25.0f / kDeg) v = std::fmin(v * float(pct) / 100.0f, 75.0f / kDeg);
            float nh = 2.0f * std::atan(std::tan(v) * (wide ? app::FrameAspect() : 4.0f / 3.0f));
            uint32_t u;
            std::memcpy(&u, &nh, 4);
            ::kernel::W32(at, u);
        } else {
            cam = 0;
        }
    } else {
        cam = 0;
    }
    __imp__sub_000A32F0(ctx, base);
    if (cam) ::kernel::W32(at, saved);
}

// [[hook]] 0xBF143, 0xBF20E, 0xBF26E: the game has just set its TV-safe border, 64 pixels across ([0x1E2F08]) and 32
// down ([0x1E2F18]; 16 in field mode), which every viewport is inset by (0xBE720) and which leaves a black frame
// round the picture on a monitor: 32 pixels at the sides and 16 at the top and bottom of the 640x480 frame. Without
// [graphics] safe_area the border is zero, so the world, menus and movies fill the frame as on PC.
void ROTK_SafeArea(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    if (rotk::g_safe_area) return;
    ::kernel::W16(0x1E2F08, 0);
    ::kernel::W16(0x1E2F18, 0);
    static std::atomic<int> logged{0};
    if (!logged++) LOGR("rotk: no TV-safe border, the picture fills the screen");
}
