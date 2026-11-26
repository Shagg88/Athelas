// The Third Age (Xbox): frame rate, as the ROTK port does it (rotk_framerate.cpp); the two games share the design.
//
// The frame end 0x1C09E0 waits (the loop at 0x1C0A70) until at least one vertical blank has passed since the last
// frame: the blank count [0x30AF68] (set by the blank callback 0xBF1E0) against the last frame's [0x2FD284]. The
// game's time step 0x11F640 (called right after the frame's Swap, 0x1C0AC5) is the blanks since the last step times
// 1/59.94 s ([0x25934C]), clamped to 0..0.1 s, into [0x2FD288], with a 1200 Hz tick count ([0x2FD290], remainder
// [0x2FD294]) beside it; the first step after a reset ([0x2FD28C] set) is 1/29.97 s ([0x2595D8]) with the ticks at 0.
//
// [graphics] fps (30, the console's, by default): 30 waits for two blanks a frame (TA_FrameWait); any other rate skips the blank wait, lets the window's frame cap
// (Settings::fps_cap) pace the frames, and gives the game the real time since its last step. Loading screens (the
// other frame loop, 0x1C0FA0, which waits on the blank event itself) keep their pace.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

namespace ta {
uint32_t g_fps = 30;  // settings.toml [graphics] fps
bool g_unlocked = false;

// GameInfo::init (ta::Init in game_info.cpp): after the settings load
void InitFrameRate() {
    g_unlocked = g_fps != 30;
    if (!g_unlocked) {
        LOGR("ta: frame rate 30 (two vertical blanks a frame, as the console)");
    } else {
        app::Get().fps_cap = g_fps;
        LOGR("ta: frame rate %s (the console's is 30)", g_fps ? std::to_string(g_fps).c_str() : "unlimited");
    }
}
}  // namespace ta

using kernel::R32;
using kernel::W32;

// [[hook]] 0x1C0A6E: just before the frame end's wait for a vertical blank (once a frame; at 0x1C0A70, the loop's own
// start, it ran on every poll of the wait: the HUD's frame count and battle speed's tick hundreds of times a frame);
// true skips the wait, to 0x1C0A81
namespace ta {
extern std::atomic<uint32_t> g_frame_no;  // ta_hud.cpp
void BattleTick();  // ta_battle.cpp
}
bool TA_FrameWait(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    ta::g_frame_no.fetch_add(1);
    ta::BattleTick();  // [gameplay] battle_speed (ta_battle.cpp)
    if (ta::g_unlocked) return true;
    // 30 fps: the game's own wait is for one vertical blank since the last frame (its 1.0 at 0x259224, a constant
    // shared with hundreds of other places), so on a fast PC it ran at 60 with 30 chosen (measured, to-do 1.9.3; the
    // console's 30 came from its slower drawing). Two blanks are waited for here, then the game's wait is skipped:
    // 30, in step with the blanks as on the console (a 30 fps cap on its own clock beside the 60 Hz blanks gave
    // steps of 1 and 3 blanks when the two lined up: judder; review). Never more than 100 ms (the blanks stopped).
    const uint32_t last = ::kernel::R32(0x2FD284);  // kLastBlank
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (::kernel::R32(0x30AF68) - last < 2 && std::chrono::steady_clock::now() < until)  // kBlanks
        d3d::WaitVBlank(4);  // asleep until the next blank (4 ms at most: a blank counted just before this read is
                             // waited out, not slept through; review), as ROTK's wait, not spinning a core
    return true;
}

namespace {
constexpr uint32_t kBlanks = 0x30AF68, kLastBlank = 0x2FD284, kStep = 0x2FD288, kReset = 0x2FD28C,
                   kTicks = 0x2FD290, kTickRest = 0x2FD294;

float F(uint32_t va) {
    float v;
    uint32_t u = R32(va);
    std::memcpy(&v, &u, 4);
    return v;
}
void WF(uint32_t va, float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    W32(va, u);
}
}  // namespace

// the time step, as 0x11F640 computes it, from the real time since the last step instead of counted blanks
X86_EXTERN_FUNC(__imp__sub_0011F640);
X86_FUNC(sub_0011F640) {
    if (!ta::g_unlocked) {
        __imp__sub_0011F640(ctx, base);
        return;
    }
    // the guest clock: fast forward (F9 held) already counted in it, pauses left out, so a frame across a press is right
    static int64_t last = ::kernel::GuestNs();
    int64_t now = ::kernel::GuestNs();
    float dt = float(now - last) * 1e-9f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;  // a load or a stall: at most a tenth of a second, as the game clamps it
    if (dt < 0.0f) dt = 0.0f;
    W32(kLastBlank, R32(kBlanks));  // the frame end and others still compare blank counts
    float rest;
    int32_t ticks;
    if (::kernel::G<uint8_t>(kReset)) {  // the first step after a reset: 1/29.97 s, as the console
        dt = 1.0f / 29.97f;
        rest = 0;
        ticks = 0;
        ::kernel::G<uint8_t>(kReset) = 0;
    } else {
        rest = F(kTickRest);
        ticks = int32_t(R32(kTicks));
    }
    WF(kStep, dt);
    float x = dt * 1200.0f;
    int32_t t = int32_t(x);  // _ftol truncates
    ticks += t;
    rest += x - float(t);
    int32_t t2 = int32_t(rest);
    ticks += t2;
    W32(kTicks, uint32_t(ticks));
    WF(kTickRest, rest - float(t2));
    ::kernel::Return(ctx, ctx.eax, 0);  // no arguments: pops the return address
    (void)base;
}
