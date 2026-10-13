// The Two Towers: frame rate. The frame wait 0xE00E0 waits for vertical blanks (the count at 0x4B7618): two a
// frame while the developer setting "GRAPHICS/Cap to 30 fps" (byte 0x4B75F1, registered at 0xE03CC) is on, one
// otherwise. Gameplay and menus turn the cap on through the setter 0xDFFE0. The frame step (0x4B8BC0, made at
// 0xE3540) is the real time since the last frame from QueryPerformanceCounter, so the game keeps its speed at any
// frame rate.
//
// [graphics] fps (the same choices as ROTK Fix, with the Xbox's 30 first), 30 by default as on the console:
//   30          the cap as the game sets it: the original
//   60          the cap kept off: one vertical blank a frame
//   90 .. 240   no vertical blank wait at all: the developer setting "GRAPHICS/DMA Issue Mode" (0x4B75F4, registered
//               at 0xE03BD, 0 is "VBLANK") is set past 0, and the window's frame limit (app::Settings::fps_cap) is
//               that many frames a second. V-Sync still holds frames to the screen's refresh.
// A movie keeps the pacing it had: the cap call from the movie routine (0xE8CB0) is let through, and no wait is
// skipped while one plays.
#include <algorithm>
#include <atomic>
#include <cstdint>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace tt {
uint32_t g_fps_mode = 30;  // settings.toml [graphics] fps: frames a second
std::atomic<int> g_movie_playing{0};  // tt_movies.cpp: inside the movie player
}

namespace {
constexpr uint32_t kCap30 = 0x4B75F1;
constexpr uint32_t kIssueMode = 0x4B75F4;
constexpr uint32_t kMovieCall = 0x000E8CB0;  // return address of the movie routine's cap call
void SetCap(uint8_t on) { *static_cast<uint8_t*>(::kernel::HostPtr(kCap30)) = on; }
}  // namespace

namespace tt {
// before each frame wait (tt_extras.cpp): above 60, skip the vertical blank wait, not during a movie
void FrameRateFrame() {
    uint32_t fps = std::clamp(g_fps_mode, 30u, 1000u);
    app::Get().fps_cap = fps > 60 ? fps : 0;  // 30 and 60: the game's own vertical blank wait
    if (fps <= 60) return;
    uint32_t want = g_movie_playing ? 0 : 1;
    if (::kernel::R32(kIssueMode) != want) ::kernel::W32(kIssueMode, want);
    static bool logged = false;
    if (!logged && want) {
        logged = true;
        LOGR("tt: frame rate up to %u (no vertical blank wait)", fps);
    }
}
}  // namespace tt

// the settings registration 0xE03B0 sets the developer default; uncapped from the start
X86_EXTERN_FUNC(__imp__sub_000E03B0);
X86_FUNC(sub_000E03B0) {
    __imp__sub_000E03B0(ctx, base);
    if (tt::g_fps_mode > 30) SetCap(0);
}

// (bool cap)
X86_EXTERN_FUNC(__imp__sub_000DFFE0);
X86_FUNC(sub_000DFFE0) {
    if (tt::g_fps_mode > 30 && ::kernel::R32(ctx.esp) != kMovieCall) ::kernel::W32(ctx.esp + 4, 0);
    __imp__sub_000DFFE0(ctx, base);
}
