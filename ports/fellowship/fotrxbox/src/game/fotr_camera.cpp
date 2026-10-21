// FOTR Xbox: [graphics] fov (the gameplay camera's field of view, percent of the game's) and screens wider than 16:9
// (21:9, 32:9).
//
// A view (the camera's at +0x434) takes its horizontal angle through 0xD68B0 (esi the view, one argument: the angle in
// radians; it keeps the angle, its half and the half's sine and cosine, which the culling uses). 0xD6A60 (eax the
// view) then makes the vertical angle from it and the screen's shape, 16:9 (0x3FE38E39) with the console's widescreen
// setting ([0x1ECBA8]+0x30) else 4:3, and passes it to 0xD6840. The picture's projection is built by 0x5843E (thiscall,
// the matrix; the vertical angle, the shape, near, far), called by the gameplay camera's 0x10DBC0 and by 0x9A580.
//
// fov: the gameplay camera's angles are its "Horizontal field of view" (+0x5A4, 87 degrees) and "Wide Cinema FOV"
// (+0x5A8), which its modes pass to 0xD68B0 from 0x10E1C0 to 0x112C8A; those calls (by their return addresses) get
// the percent. The copy of another camera's view (0x10E3B8) is left as it is (it is the changed angle already), as are
// the scripted and cutscene cameras' own angles.
//
// Wider screens: with widescreen on and a frame wider than 16:9 (app::FrameAspect), every horizontal angle is widened
// so the vertical one stays as at 16:9 (tan(h/2) times the shape's share of 16:9), 0xD6A60 makes the vertical angle
// with the real shape, and 0x5843E builds the projection with it: the same picture as at 16:9, with more at the sides,
// and the culling follows. The HUD is flat: its pieces are drawn with one vertex program (kHudProgram) through an
// orthographic matrix the game lays out for 16:9 (constants 45 to 48, the last 0 0 0 1); on a wider screen its first
// row (x) is divided by the shape's share of 16:9 just before each such draw, so the HUD keeps its shape in the middle
// 16:9, and put back at the next draw if the game left it (WideHud, from the draw observer). Menus, text and
// screens are drawn in 640 x 480 pixels with another program (kScreenProgram): pulled into the middle 16:9 too
// (gpu::g_screen_space_hook), and so are pictures as wide as the screen while no 3D world is being drawn (menus and
// loading screens, the pause and inventory screens get bars at the sides); in a frame that drew the 3D world first
// (fades, flashes) they stay as wide as the screen. Movie frames are left to the movie settings.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iterator>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "gpu/backend.h"
#include "gpu/state.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"

namespace fotr {
uint32_t g_fov_percent = 100;  // [graphics] fov
}

namespace {
constexpr float k169 = 16.0f / 9.0f;
constexpr uint32_t kApp = 0x1ECBA8;

// screens up to 32:9 drawn at their own width (the 3D, the HUD through its projection); films stay 16:9 at most
const bool g_wide_setup = [] {
    app::g_max_frame_aspect = 32.0f / 9.0f;
    gpu::g_wide_targets = true;
    return true;
}();

float F(uint32_t a) {
    float f;
    std::memcpy(&f, ::kernel::HostPtr(a), 4);
    return f;
}
void SetF(uint32_t a, float f) { std::memcpy(::kernel::HostPtr(a), &f, 4); }

// the frame's shape over 16:9 when the game draws widescreen, else 1
float Wider() {
    uint32_t app = ::kernel::R32(kApp);
    if (!app || !*static_cast<uint8_t*>(::kernel::HostPtr(app + 0x30))) return 1.0f;
    float k = app::FrameAspect() / k169;
    return k > 1.001f ? k : 1.0f;
}

// the gameplay camera's last angle as the game gave it, to set it again when the setting or the screen's shape
// changes (the camera only sets its angle as a level starts and when it changes mode, not each frame)
struct Cam {
    uint32_t view = 0, ret = 0;
    float game = 0, set = 0;  // the game's angle, and the one this file made of it
    uint32_t fov = 100;
    float wider = 1;
} g_cam;

bool Gameplay(uint32_t ret) {
    switch (ret) {
        case 0x10E1D3: case 0x10E21E: case 0x10E292: case 0x110767: case 0x110A66:
        case 0x110D86: case 0x110E2E: case 0x110ECA: case 0x112C8F:
            return true;
    }
    return false;
}
}  // namespace

// a view's horizontal angle
X86_FUNC(__imp__sub_000D68B0);
X86_FUNC(sub_000D68B0) {
    uint32_t ret = ::kernel::R32(ctx.esp), at = ctx.esp + 4;
    if (ret != 0x10E3BD) {  // not the copy of a view's angle
        float h = F(at), was = h;
        bool gameplay = Gameplay(ret);
        if (gameplay && fotr::g_fov_percent != 100)
            h = std::min(h * float(std::clamp(fotr::g_fov_percent, 50u, 200u)) / 100.0f, 2.9f);
        float k = Wider();
        if (k > 1.0f) h = std::min(2.0f * std::atan(std::tan(h / 2) * k), 3.05f);
        if (gameplay) g_cam = {ctx.esi, ret, was, h, fotr::g_fov_percent, k};
        if (h != was) {
            SetF(at, h);
            LOG_FIRST(1, "fotr: camera angle %.1f -> %.1f degrees (fov %u%%, %.2f of 16:9)", was * 57.2958f,
                      h * 57.2958f, fotr::g_fov_percent, k);
        }
    }
    __imp__sub_000D68B0(ctx, base);
}

// the view's vertical angle from its horizontal one: with the real shape on a wider screen
X86_FUNC(__imp__sub_000D6A60);
X86_FUNC(sub_000D6840);
X86_FUNC(sub_000D6A60) {
    float k = Wider();
    if (k <= 1.0f) {
        __imp__sub_000D6A60(ctx, base);
        return;
    }
    uint32_t view = ctx.eax, esi = ctx.esi;
    float v = 2.0f * std::atan(std::tan(F(view + 8)) / (k169 * k));  // +8: half the horizontal angle
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    ctx.esi = view;
    X86_PUSH32(bits);
    X86_PUSH32(0x000D6AD6u);  // as the game's call returns (0xD6840 pops it and the angle)
    sub_000D6840(ctx, base);
    ctx.esi = esi;
    ctx.esp += 4;  // its own ret
}

// a projection: the 16:9 shape becomes the frame's
X86_FUNC(__imp__sub_0005843E);
X86_FUNC(sub_0005843E) {
    float k = Wider();
    uint32_t at = ctx.esp + 8;  // the shape (after the return address and the vertical angle)
    if (k > 1.0f && std::fabs(F(at) - k169) < 0.001f) SetF(at, k169 * k);
    __imp__sub_0005843E(ctx, base);
}

namespace {
// the HUD's vertex program (oT0/oT1 from v3, v0 through c45-c48, oD0 from v2, the viewport c58/c59)
const uint32_t kHudProgram[] = {
    0x00000000, 0x00806615, 0x097E1854, 0x3070C848, 0x00000000, 0x00808615, 0x097E1854, 0x3070C850,
    0x00000000, 0x0020001B, 0x0836106C, 0x2EA00FF8, 0x00000000, 0x0020041B, 0x0836106C, 0x2070F818,
    0x00000000, 0x00C5A01B, 0xA436186C, 0x20708800, 0x00000000, 0x00C5C01B, 0xA436186C, 0x20704800,
    0x00000000, 0x00C5E01B, 0xA436186C, 0x20702800, 0x00000000, 0x00C6001B, 0xA436186C, 0x20701800,
    0x00000000, 0x0647401B, 0xC4361BFF, 0x1078E800, 0x00000000, 0x0087601B, 0xC400286C, 0x3070E801};
const uint32_t kScreenProgram[] = {
    0x00000000, 0x0220201B, 0x0C3613FC, 0x2F203804, 0x00000000, 0x0020041B, 0x0836106C, 0x2070F818,
    0x00000000, 0x00600015, 0x08361454, 0x9C300FF8, 0x00000000, 0x00200615, 0x0836106C, 0x2070C848,
    0x00000000, 0x00400015, 0x357E486C, 0x2070C800, 0x00000000, 0x0647401B, 0xC4361BFF, 0x1078E800,
    0x00000000, 0x0087601B, 0xC400286C, 0x3070E801};
template <size_t N>
bool Is(const gpu::State& s, const uint32_t (&prog)[N]) {
    return s.vs && (s.vs_handle & 1) && s.vs->program.size() == N &&
           std::memcmp(s.vs->program.data(), prog, sizeof prog) == 0;
}
std::atomic<int64_t> g_world_ms{-100000};  // the last 3D draw
// frames shown (the renderer's ui callback, after each frame's draws, in order with them); 0 where it never comes
// (the Linux test build), and then the last 3D draw's time stands in
std::atomic<uint64_t> g_frame{0}, g_world_frame{~0ull};
int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
float ScreenSqueeze(const gpu::State& s) {
    if (!Is(s, kScreenProgram)) return 1.0f;
    if (gpu::g_movie) return 1.0f;  // a film's frame: the movie settings show it (the title screen's moving picture
                                    // is not one: it gets bars as the menu pictures do)
    float k = Wider();
    return k > 1.0f ? 1.0f / k : 1.0f;
}
// x' = a x + b: into the middle; a picture as wide as the screen too, unless it is over the world
void ScreenPlace(float, float, float, float, float w, float f, float& a, float& b, float&, float&) {
    if (f == 1.0f) {
        bool world = g_frame ? g_world_frame == g_frame : NowMs() - g_world_ms < 250;
        if (world) return;  // over the 3D world drawn this frame (fades, flashes): as it is
        float k = Wider();
        if (k <= 1.0f) return;
        f = 1.0f / k;
    }
    a = f;
    b = w / 2 * (1 - f);
}
const int kScreenHooks = (gpu::g_screen_space_hook = ScreenSqueeze, gpu::g_screen_space_place = ScreenPlace, 0);
constexpr int kRow = 45;  // the matrix's x row, hardware constant numbering
bool g_squeezed = false;
std::array<float, 4> g_orig{}, g_put{};
}  // namespace

namespace fotr {
// the draw observer, before each draw (the renderer's thread)
void WideHud(const gpu::State& cs) {
    auto& s = const_cast<gpu::State&>(cs);
#ifndef _WIN32
    static const char* probe = std::getenv("FOTR_VS_PROBE");  // test build: a vertex program's words, once
    if (probe && s.vs && s.vs_handle == std::strtoul(probe, nullptr, 16)) {
        static bool once = false;
        if (!once) {
            once = true;
            std::string t;
            for (uint32_t v : s.vs->program) { char b[12]; std::snprintf(b, sizeof b, "0x%08X, ", v); t += b; }
            LOGI("fotr: vertex program %X: %s", s.vs_handle, t.c_str());
        }
    }
#endif
    auto& row = s.vsc[kRow];
    // the draw before was a squeezed HUD piece: each number of the row the game has not set since is put back (the
    // render thread's copy of the state takes only what changed, 64 bytes at a time, so a row can come back half
    // the game's new numbers and half the squeezed ones)
    if (g_squeezed) {
        for (int j = 0; j < 4; j++)
            if (row[j] == g_put[j]) row[j] = g_orig[j];
        g_squeezed = false;
    }
    if (!Is(s, kHudProgram)) {
        if (s.vs && (s.vs_handle & 1) && !Is(s, kScreenProgram)) {  // a 3D draw
            g_world_ms = NowMs();
            g_world_frame = g_frame.load();
        }
        return;
    }
    const auto& w = s.vsc[kRow + 3];
    if (w[0] != 0 || w[1] != 0 || w[2] != 0 || w[3] != 1) return;  // orthographic only
    float k = Wider();
    if (k <= 1.0f) return;
    g_orig = row;
    for (float& v : row) v /= k;
    g_put = row;
    g_squeezed = true;
    LOG_FIRST(1, "fotr: HUD kept 16:9 in the middle (%.2f of 16:9)", k);
}
}  // namespace fotr

X86_FUNC(sub_0010DBC0);

namespace fotr {
// each frame, on the game's thread (fotr_cheats.cpp's frame): the gameplay camera's angle set again after the field
// of view (the in-game menu) or the screen's shape changed, as its modes set it: the angle, the vertical angle, the
// projection (0x10DBC0, the camera). Only while the view still holds the angle set here (the same camera, not
// changed by the game since).
void CameraFrame(X86Context& ctx, uint8_t* base) {
#ifndef _WIN32
    // test build: FOTR_FOV_SWITCH=<frame>:<percent> changes the field of view that many frames into play, as the
    // in-game menu would, and logs the camera's view and projection then
    static uint64_t frame = 0;
    static const char* sw = std::getenv("FOTR_FOV_SWITCH");
    bool dump = false;
    if (sw && g_cam.view && ++frame == std::strtoull(sw, nullptr, 10)) {  // frames since the gameplay camera
        g_fov_percent = uint32_t(std::atoi(std::strchr(sw, ':') + 1));
        dump = true;
    }
    struct Dump {
        uint32_t v;
        ~Dump() {
            if (!v) return;
            std::string t;
            for (int i = 0; i < 8; i++) { char b[24]; std::snprintf(b, sizeof b, " %.5f", F(v + 4 * i)); t += b; }
            t += " |";
            for (int i = 0; i < 16; i++) { char b[24]; std::snprintf(b, sizeof b, " %.5f", F(v - 0x434 + 0x4A0 + 4 * i)); t += b; }
            LOGI("fotr: test: view%s", t.c_str());
        }
    } d{dump ? g_cam.view : 0};
#endif
    if (!g_cam.view) return;
    float k = Wider();
    if (g_cam.fov == g_fov_percent && std::fabs(g_cam.wider - k) < 1e-4f) return;
    uint32_t view = g_cam.view;
    if (F(view) != g_cam.set) {
        g_cam.view = 0;
        return;
    }
    X86Context saved = ctx;
    uint32_t bits;
    std::memcpy(&bits, &g_cam.game, 4);
    ctx.esi = view;
    X86_PUSH32(bits);
    X86_PUSH32(g_cam.ret);  // as from the camera's own call (the field of view applies)
    sub_000D68B0(ctx, base);
    ctx.eax = view;
    X86_PUSH32(g_cam.ret);
    sub_000D6A60(ctx, base);
    X86_PUSH32(view - 0x434);  // the camera
    X86_PUSH32(g_cam.ret);
    sub_0010DBC0(ctx, base);
    ctx = saved;
    LOGI("fotr: camera set again: fov %u%%, %.2f of 16:9", g_fov_percent, k);
}
}  // namespace fotr

namespace fotr {
// GameInfo::ui: once a frame shown, after its draws
void FrameShown(double) { g_frame++; }
}
