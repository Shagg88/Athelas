// ROTK Xbox: frame rate.
//
// The console paces a frame at two vertical blanks (30 fps): the frame end 0xD03A0 waits until at least one blank has
// passed since the last frame (the loop at 0xD03D3 on the blank count [0x1E2F14] from the callback 0xBEDE0), then
// 0xBF1A0 waits for the next one (0xBF1CA, D3DDevice_BlockUntilVerticalBlank).
// The game's time step 0x9EB20 is the blanks since the last frame times 1/59.94 s ([0x154338]) into [0x1E3344],
// with a 1200 Hz tick count ([0x1E334C], remainder [0x1E3350]) beside it; ROTK PC has the same design on a real
// clock (0x4828E0), which is why it runs at any frame rate at the right speed.
//
// [graphics] fps (30, the console's, by default): any other rate skips both waits, lets the window's frame cap
// (Settings::fps_cap) pace the frames, and gives the game the real time since its last step.
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace rotk {
uint32_t g_fps = 30;  // settings.toml [graphics] fps; 0 no cap
bool g_unlocked = false;

void InitFrameRate() {
    g_unlocked = g_fps != 30;
    if (g_unlocked) {
        app::Get().fps_cap = g_fps;
        LOGR("rotk: frame rate %s (the console's is 30)", g_fps ? std::to_string(g_fps).c_str() : "unlimited");
    }
}
}  // namespace rotk

using kernel::R32;
using kernel::W32;

// [[hook]] 0xD03D3: the frame end's wait for a vertical blank; true skips to 0xD03F1
bool ROTK_FrameWait(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    return rotk::g_unlocked;
}
// 0xBF1A0, called by the frame end at 0xD03FB: unbinds the four texture stages, counts the frame ([0x1E2F04]) and
// tail-jumps to BlockUntilVerticalBlank. Unlocked, the frame end's call does the same without the wait; the other
// callers (loading screens) keep it.
X86_EXTERN_FUNC(__imp__sub_000BF1A0);
X86_FUNC(sub_000BF1A0) {
    if (!rotk::g_unlocked || R32(ctx.esp) != 0xD0400) {
        __imp__sub_000BF1A0(ctx, base);
        return;
    }
    for (uint32_t stage = 0; stage < 4; stage++) ::kernel::CallGuest(ctx, 0x12D630, {stage, 0});  // SetTexture(stage, null)
    W32(0x1E2F04, R32(0x1E2F04) + 1);
    ::kernel::Return(ctx, ctx.eax, 0);
}

// the time step, as 0x9EB20 computes it, from the real time since the last step instead of counted blanks
X86_EXTERN_FUNC(__imp__sub_0009EB20);
X86_FUNC(sub_0009EB20) {
    if (!rotk::g_unlocked) {
        __imp__sub_0009EB20(ctx, base);
        return;
    }
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    clock::time_point now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (dt > 0.25f) dt = 0.25f;  // a load or a stall: as a few dozen blanks at most
    constexpr uint32_t kBlanks = 0x1E2F14, kLastBlank = 0x1E3340, kStep = 0x1E3344, kReset = 0x1E3348,
                       kTicks = 0x1E334C, kTickRest = 0x1E3350;
    W32(kLastBlank, R32(kBlanks));  // the frame end and others still compare blank counts
    auto f = [](uint32_t va) {
        float v;
        uint32_t u = R32(va);
        std::memcpy(&v, &u, 4);
        return v;
    };
    auto wf = [](uint32_t va, float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        W32(va, u);
    };
    float rest;
    int32_t ticks;
    wf(kStep, dt);
    if (::kernel::G<uint8_t>(kReset)) {  // first step after a reset (0x9EC00): 1/29.97 s as the console
        rest = 0;
        ticks = 0;
        W32(kStep, 0x3D08AB86);
        ::kernel::G<uint8_t>(kReset) = 0;
    } else {
        rest = f(kTickRest);
        ticks = int32_t(R32(kTicks));
    }
    float x = f(kStep) * 1200.0f;
    int32_t t = int32_t(x);
    ticks += t;
    rest += x - float(t);
    int32_t t2 = int32_t(rest);
    ticks += t2;
    W32(kTicks, uint32_t(ticks));
    wf(kTickRest, rest - float(t2));
    ::kernel::Return(ctx, ctx.eax, 0);  // cdecl, no arguments: pops the return address
    (void)base;
}
