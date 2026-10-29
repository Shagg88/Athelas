// The Two Towers: widescreen. The game only renders 4:3 and never asks for the console's widescreen setting, but
// the host shows the picture at 16:9 on a widescreen display (gpu BlitFrame), which stretched the world sideways.
// Every camera projection goes through 0x4E890 (this, fov, aspect, near, far): with a widescreen display the 4:3
// aspect becomes 16:9, so the same vertical view is kept and more is seen at the sides. The frustum culling built
// from the same arguments follows.
//
// The HUD, menus and text are 2D quads in 640x480 screen pixels, drawn with the game's pass-through vertex
// programs; on a widescreen display their x is pulled into the middle 4:3 of the picture so they keep their shape.
// Quads as wide as the screen (fades, full screen effects) stay as they are (gpu::SqueezeScreenX). With hud_edges
// (on by default, and only with widescreen = true) the pieces near the sides stay at the sides (HudPlace).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/state.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_0004E890);
X86_FUNC(sub_0004E890) {
    if (app::Widescreen()) {
        uint32_t at = ctx.esp + 8;  // aspect
        float aspect;
        std::memcpy(&aspect, ::kernel::HostPtr(at), 4);
        if (std::fabs(aspect - 4.0f / 3.0f) < 0.01f) {
            aspect = app::FrameAspect();
            std::memcpy(::kernel::HostPtr(at), &aspect, 4);
        }
    }
    __imp__sub_0004E890(ctx, base);
}

namespace tt {
bool g_hud_edges = true;  // settings.toml [graphics] hud_edges
bool InMenus();           // tt_extras.cpp: the front end, the screens between levels too
}

namespace {
// the shared SDK draws wider frames only for a game that handles them: this one does, up to 32:9 (menus at 16:9
// with side bars, see the panel zoom below), drawn at the screen's own width rather than stretched sideways
const bool g_wide_setup = [] {
    app::g_max_frame_aspect = 32.0f / 9.0f;
    gpu::g_wide_targets = true;
    return true;
}();
}

namespace {
// The game's 2D vertex programs only copy inputs to outputs: mov oPos, v0 first, then any of mov oD0 / oT0, vN
// (HUD, text, menus, fades, panels). In 4928 microcode each such instruction is 0, 0x0020001B | vN << 9,
// 0x0836106C, 0x2070F8xx (the low byte picks the output and marks the last instruction).
bool Is2D(const std::vector<uint32_t>& p) {
    if (p.empty() || p.size() % 4 || p.size() > 4 * 6) return false;
    for (size_t i = 0; i < p.size(); i += 4) {
        if (p[i] != 0 || (p[i + 1] & ~0x1E00u) != 0x0020001Bu || p[i + 2] != 0x0836106Cu ||
            (p[i + 3] & 0xFFFFFF00u) != 0x2070F800u)
            return false;
    }
    return p[1] == 0x0020001Bu && (p[3] & 0xFEu) == 0;  // v0 to oPos first
}

// the last draw of the 3D world (a vertex program other than the 2D ones): HUD pieces go to the edges only while
// the world is on screen, so screens of only 2D (legal text, menus) stay centred as they were laid out
// A frame with the world in it draws hundreds of 3D things; a movie frame or a loading screen draws one or two
// (the movie quad uses a 3D-looking vertex program). The edges need such frames for half a second (a loading frame
// behind a 2D screen does not count), and stay until they have been gone for a second and a half (a stalled frame
// does not end it). tt::WorldFrame counts, once a frame, from the frame wait (tt_extras.cpp).
std::atomic<int64_t> g_world_ms{-1000000}, g_world_since{-1000000};
std::atomic<int> g_world_draws{0};
constexpr int64_t kWorldSteadyMs = 500, kWorldGoneMs = 1500;
constexpr int kWorldDrawsPerFrame = 100;  // the copyright screen once reached 30, gameplay draws 900 and more
constexpr int kWorldDrawsNow = 20;        // this frame's 3D draws before the HUD: a loading screen has 0 or 1, play 180 and up
int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

float HudSqueeze(const gpu::State& s) {
    if (!s.vs || !(s.vs_handle & 1) || !app::Widescreen()) return 1.0f;
    if (!Is2D(s.vs->program)) {
        g_world_draws++;
        return 1.0f;
    }
    uint32_t w, h;
    gpu::SurfaceSize(s.rt, w, h);
    if (w != 640 || h != 480) return 1.0f;  // the screen, not a texture the game draws into
    // screen effects that sample a copy of the screen (heat haze over lava) must line up with the 3D picture
    for (uint32_t t : s.textures) {
        gpu::TextureDesc d;
        if (t && gpu::Describe(t, d) && d.width >= 640 && d.height >= 480) return 1.0f;
    }
    return (4.0f / 3.0f) / app::FrameAspect();
}

// hud_edges: a HUD piece near the left or right edge keeps its distance to that edge of the 16:9 picture (in
// 4:3 pixels) instead of moving into the middle 4:3; the rest stays centred. The game draws text a letter at a
// time and bars a piece at a time, so a draw that touches the one just before it (side by side, same rows) goes
// where that one went: a word or a bar moves as one. Only the last draw counts, never a union of them, and a wide
// draw (a centred panel) starts no chain: a box that only grew, kept from frame to frame, once took in the whole
// screen and pulled every HUD piece into the middle.
struct Chain {
    float x0 = 0, x1 = -1, y0 = 0, y1 = -1;
    float b = 0;
    bool edge = false;  // the chain began with a piece that hugs a side (only those are remembered in g_placed)
} g_chain;
std::atomic<uint64_t> g_chain_frame{0}, g_frame{0};  // the frame the chain was made in
// the pieces sent to a side, in this frame and the one before: a piece that touches one of them goes where it went,
// whatever was drawn just before it (#33). The health bar is drawn in layers: frame, segments, then the green fill
// from 105 to the health left. A fill that ended short of the last segment didn't touch it, started a new chain in
// the middle, and every layer drawn after it (the bar again, its frame) went to the middle 4:3 while the first
// layer stayed at the edge: measured in 235 of the sampled frames of two levels, 0 with this (and the meter's
// pieces likewise, 5 frames to 0)
struct Placed { float x0, x1, y0, y1, b; };
std::vector<Placed> g_placed, g_placed_before;
uint64_t g_placed_frame = 0;
std::atomic<uint64_t> g_panel_frame{~0ull >> 1};     // the last frame with a menu's inset panel

void HudPlace(float x0, float x1, float y0, float y1, float w, float f, float& a, float& b, float& c, float& d) {
    int64_t now = NowMs();
    // and the world drawn in this very frame (it comes before the HUD): a loading screen drawn just as a level ends
    // (none or one 3D draw) is laid out centred, not as HUD. The game then loads for seconds without a new frame,
    // so that one frame stays on screen; the time rule alone placed its tip text and picture at the left edge
    // The screens between levels count as no world either: the save screen's map is 3D (200 draws a frame), which
    // once sent its dialog boxes and their frames to the screen's sides
    bool world = now - g_world_ms <= kWorldGoneMs && g_world_ms - g_world_since >= kWorldSteadyMs &&
                 g_world_draws >= kWorldDrawsNow && !tt::InMenus();
    // menus and the title screen: their picture is one panel inset from the edges of the 640 x 480 screen (24 to 616
    // across, 18 to 462 down: a TV's safe area), with black around it. In a frame that has that panel everything is
    // enlarged by the same 8% about the middle, so the panel meets the screen's edges and the menu's text, which is
    // drawn on the panel's art (the ring around the main menu is part of it), stays where it was on it. No game
    // screen draws a quad exactly there
    {  // the main menu counts as the world (its moving mist is 3D), so this goes by the panel alone
        auto near = [](float v, float want) { return std::fabs(v - want) <= 2; };
        float s = w / 640;
        if (f == 1 && near(x0, 24 * s) && near(x1, 616 * s) && near(y0, 18 * s) && near(y1, 462 * s))
            g_panel_frame = g_frame.load();
        if (g_frame - g_panel_frame <= 1) {
            constexpr float kZoom = 640.0f / 592.0f;
            float h = w * 3 / 4;
            // wider than 16:9 (ultrawide): the menu as on a 16:9 screen, in the middle with bars at the sides, as
            // filling the width would stretch the art far more or cut off the menu's top and bottom
            float wide = (16.0f / 9.0f) / app::FrameAspect();
            a = (f == 1 ? wide : f) * kZoom;
            // the screen-wide shade over the panel stays within the 16:9 part (on 16:9 its enlarged edges are off
            // the screen)
            if (f == 1 && wide < 1 && x0 <= 1 && x1 >= w - 1) a = wide;
            b = w / 2 * (1 - a);
            c = kZoom;
            d = h / 2 * (1 - c);
            return;
        }
    }
    if (f == 1) return;  // a wide draw elsewhere: left as it is
    a = f;
    float centre = w / 2 * (1 - f), right = w * (1 - f);
    if (!tt::g_hud_edges || !world) {
        b = centre;
        return;
    }
    constexpr float kGap = 16, kRows = 8, kWide = 0.4f;
    if (g_chain_frame != g_frame) g_chain = {};  // no chain carries over from the frame before
    g_chain_frame = g_frame.load();
    bool touches = x0 <= g_chain.x1 + kGap && x1 >= g_chain.x0 - kGap && y0 <= g_chain.y1 + kRows &&
                   y1 >= g_chain.y0 - kRows;
    if (touches) {
        b = g_chain.b;
        bool edge = g_chain.edge;
        g_chain = {x0, x1, y0, y1, b, edge};
        if (edge && b != centre && g_placed_frame == g_frame && g_placed.size() < 512) g_placed.push_back({x0, x1, y0, y1, b});
        return;
    }
    if (g_placed_frame != g_frame) {
        g_placed_before.swap(g_placed);
        g_placed.clear();
        g_placed_frame = g_frame.load();
    }
    // only what hugs a side: the HUD sits within 96 of 640 pixels of the edge, centred text and menus start further in
    constexpr float kEdge = 0.15f;
    bool edge = true;
    if (x0 < w * kEdge && x1 < w / 2) b = 0.0f;
    else if (x1 > w * (1 - kEdge) && x0 > w / 2) b = right;
    else {
        b = centre;
        edge = false;
        // overlapping a piece that hugs a side (a layer of the same bar): with it. Only real overlap, and a piece
        // moved this way is not remembered itself, so centred text drawn near the HUD can't be pulled along, or
        // keep itself at the side from frame to frame
        auto with = [&](const std::vector<Placed>& v) {
            for (const Placed& p : v)
                if (x0 <= p.x1 && x1 >= p.x0 && y0 <= p.y1 && y1 >= p.y0) {
                    b = p.b;
                    return true;
                }
            return false;
        };
        if (x1 - x0 <= w * kWide && !with(g_placed)) with(g_placed_before);
    }
    if (edge && b != centre && x1 - x0 <= w * kWide && g_placed.size() < 512) g_placed.push_back({x0, x1, y0, y1, b});
    if (x1 - x0 > w * kWide) g_chain = {};
    else g_chain = {x0, x1, y0, y1, b, edge};
}

}  // namespace

namespace tt {
// once a frame: was the world in the frame just drawn
void WorldFrame() {
    g_frame++;
    int n = g_world_draws.exchange(0);
    static const bool log_draws = std::getenv("TT_WORLD_LOG") != nullptr;  // testing: 3D draws per frame
    if (log_draws) LOGI("tt: frame %llu: %d 3D draws", (unsigned long long)g_frame.load(), n);
    if (n < kWorldDrawsPerFrame) return;
    int64_t now = NowMs();
    if (now - g_world_ms > kWorldGoneMs) {
        g_world_since = now;  // a new stretch of the world on screen
        static std::atomic<int> logged{0};
        if (logged++ < 16) LOGR("tt: world on screen (%d 3D draws a frame): HUD to the screen edges", n);
    }
    g_world_ms = now;
}
}  // namespace tt

namespace {
// the game's 2D vertex programs, for texture dumps to name the HUD's textures by
bool ScreenSpace(const gpu::State& s) { return s.vs && Is2D(s.vs->program); }

const int kRegistered = (gpu::g_screen_space_hook = HudSqueeze, gpu::g_screen_space_place = HudPlace,
                         gpu::g_screen_space_test = ScreenSpace, 0);
}  // namespace
