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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

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

// The game's clock 0xE3540, once a frame: the step [0x4B8BC0] (seconds, float) from the TSC (0xF8F3F, 733 MHz) since
// the last frame, [0x4B8BB8] the same in ms, and the time since the clock was reset (each level start, 0xE32A0)
// summed in the float [0x4B8BBC] (and x1000 in ms, [0x4B8BB4]); then the copies the game reads ([0x4B8BA0..] in
// play, 0 while paused or in slow motion; [0x4B8BA4..] always) are filled from them.
// A float sum loses the step once the time is large: past 2^13 s (2.3 hours) a 240 fps step (4.2 ms) is only 4 or 5
// units of the float, so the clock runs some percent slow or fast; past 2^17 s (1.5 days) at 240 fps, 2^19 s (6 days)
// at 60 or 2^20 s (12 days) at 30 the step rounds to nothing and the clock stops. The sum is kept here in double
// instead: what the game reads is the nearest float to the true time, never off by more than half a unit.
//
// Test builds: TT_FIXED_FPS=<n> makes each step exactly 1/n s (the clock's TSC read is a virtual count), so the
// headless harness plays the game as it would run at n frames a second, whatever its own speed.
namespace {
constexpr uint32_t kStep = 0x4B8BC0, kStepMs = 0x4B8BB8, kSum = 0x4B8BBC, kSumMs = 0x4B8BB4;
constexpr uint32_t kFixedMode = 0x4B8B80;
constexpr uint32_t kPlaySum = 0x4B8B9C, kPlaySumMs = 0x4B8B94, kAllSum = 0x4B8BAC, kAllSumMs = 0x4B8BA4;
double g_sum = 0;
uint32_t g_sum_bits = 0;  // the float last written to [kSum]
uint64_t g_virtual_tsc = 0;
const uint32_t kFixedFps = [] {
    const char* e = std::getenv("TT_FIXED_FPS");
    return e ? uint32_t(std::atoi(e)) : 0u;
}();
float F(uint32_t a) {
    uint32_t u = ::kernel::R32(a);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
float AsFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
uint32_t Bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
}  // namespace

// rdtsc into a guest 64-bit value: the clock's own reads (its reset 0xE3292, 0xE3333 and each frame 0xE354A) count
// virtually with TT_FIXED_FPS
X86_EXTERN_FUNC(__imp__sub_000F8F3F);
X86_FUNC(sub_000F8F3F) {
    uint32_t ret = ::kernel::R32(ctx.esp);
    if (kFixedFps && (ret == 0xE329C || ret == 0xE3338 || ret == 0xE354F)) {
        if (!g_virtual_tsc) g_virtual_tsc = 1ull << 32;
        if (ret == 0xE354F) g_virtual_tsc += 733333333ull / kFixedFps;
        uint32_t out = ::kernel::R32(ctx.esp + 4);
        ::kernel::W32(out, uint32_t(g_virtual_tsc));
        ::kernel::W32(out + 4, uint32_t(g_virtual_tsc >> 32));
        ctx.eax = 1;
        ctx.esp += 8;  // ret 4
        return;
    }
    __imp__sub_000F8F3F(ctx, base);
}

namespace tt {
bool InMenus();  // tt_extras.cpp
}
// test builds with TT_FIXED_FPS: TT_SNAP_EVERY=<s> asks the headless harness for a picture (its snap file) each <s>
// seconds of a level's clock, kept as snap_t<seconds>.tga, so runs at different frame rates compare at the same times
void SnapTest() {
    static const double every = [] {
        const char* e = std::getenv("TT_SNAP_EVERY");
        return e && kFixedFps ? std::atof(e) : 0.0;
    }();
    if (every <= 0) return;
    static double next = 0;
    static int pending = -1;
    namespace fs = std::filesystem;
    fs::path dir = app::ExeDir();
    if (pending >= 0 && !fs::exists(dir / "snap") && fs::exists(dir / "snap_1.tga")) {
        std::error_code ec;
        fs::rename(dir / "snap_1.tga", dir / ("snap_t" + std::to_string(pending) + ".tga"), ec);
        pending = -1;
    }
    // times from the level's start (the front end ends: tt_extras.cpp), whatever the movies before it took
    static double origin = -1;
    if (tt::InMenus() || tt::g_movie_playing) {  // the level's time starts after any movie
        origin = -1;
        return;
    }
    if (origin < 0) {
        origin = g_sum;
        next = 0;
        LOGI("tt: level clock starts at %.2f s", g_sum);
    }
    double t = g_sum - origin;
    if (pending < 0 && t >= next) {
        if (next > 0) {
            LOGI("tt: level clock %.2f s: snap", t);
            std::FILE* f = std::fopen((dir / "snap").string().c_str(), "w");
            if (f) std::fclose(f);
            pending = int(next + 0.5);
        }
        next += every;
    }
}

X86_EXTERN_FUNC(__imp__sub_000E3540);
X86_FUNC(sub_000E3540) {
    uint32_t before = ::kernel::R32(kSum);
    __imp__sub_000E3540(ctx, base);
    uint32_t after = ::kernel::R32(kSum);
    // reset since the last frame (the sum went back), or the first frame: start again from the game's value
    if (before != g_sum_bits || AsFloat(after) < AsFloat(before)) g_sum = AsFloat(after);
    else g_sum += double(F(kStep));
    float s = float(g_sum), ms = float(g_sum * 1000.0);
    uint32_t sb = Bits(s), msb = Bits(ms), old_ms = ::kernel::R32(kSumMs);
    ::kernel::W32(kSum, sb);
    ::kernel::W32(kSumMs, msb);
    // the copies made from the float sum this frame
    if (::kernel::R32(kPlaySum) == after) ::kernel::W32(kPlaySum, sb);
    if (::kernel::R32(kAllSum) == after) ::kernel::W32(kAllSum, sb);
    if (::kernel::R32(kPlaySumMs) == old_ms) ::kernel::W32(kPlaySumMs, msb);
    if (::kernel::R32(kAllSumMs) == old_ms) ::kernel::W32(kAllSumMs, msb);
    g_sum_bits = sb;
    // the fixed step (0x4B8B80 on: the Slo-Mo code, 0xE3530) gives every frame [0x1C3594] = 6 ms, 0.18 of the
    // console's 30 fps frame. Above 30 fps a fixed step a frame plays faster the more frames there are (1.44 times
    // real speed at 240), so there it is 0.18 of the real step instead: the same slow motion at any frame rate.
    if (::kernel::R32(kFixedMode) && tt::g_fps_mode > 30 && !kFixedFps) {
        float step = F(kStep) * 0.18f, step_ms = step * 1000.0f;
        for (uint32_t a : {0x4B8BA0u, 0x4B8BB0u}) ::kernel::W32(a, Bits(step));
        for (uint32_t a : {0x4B8B98u, 0x4B8BA8u}) ::kernel::W32(a, Bits(step_ms));
        LOG_FIRST(1, "tt: slow motion at %u fps: 0.18 of the real step", tt::g_fps_mode);
    }
    SnapTest();
}
