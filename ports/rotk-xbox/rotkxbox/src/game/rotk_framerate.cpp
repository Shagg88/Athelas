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
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
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
namespace rotk {
void CheatsFrame();  // rotk_cheats.cpp
}

X86_EXTERN_FUNC(__imp__sub_0009EB20);
X86_FUNC(sub_0009EB20) {
    rotk::CheatsFrame();
    if (!rotk::g_unlocked) {
        __imp__sub_0009EB20(ctx, base);
        return;
    }
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    clock::time_point now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (dt > 0.1f) dt = 0.1f;  // a load or a stall: at most a tenth of a second, as ROTK PC clamps it (0x3DCCCCCD)
#ifndef _WIN32
    static const float fixed_dt = [] {  // test build: ROTK_DT=180 steps 1/180 s a frame, however slow the harness is
        const char* e = std::getenv("ROTK_DT");
        return e ? 1.0f / float(std::atof(e)) : 0.0f;
    }();
    if (fixed_dt > 0) dt = fixed_dt;
    static const bool jitter = std::getenv("ROTK_DT_JITTER") != nullptr;  // steps alternate 0.5 and 1.5 times it
    static uint32_t step_n = 0;
    if (fixed_dt > 0 && jitter) dt = fixed_dt * ((step_n++ & 1) ? 1.5f : 0.5f);
    // test build: ROTK_MEMDUMP="dir,step,step,..." writes the guest's memory below 64 MB at those time steps
    static uint32_t steps = 0;
    steps++;
    if (const char* md = std::getenv("ROTK_MEMDUMP")) {
        std::string spec = md, dir = spec.substr(0, spec.find(','));
        if (steps % 100 == 0) LOGR("rotk: step %u", steps);
        for (size_t at = spec.find(','); at != std::string::npos; at = spec.find(',', at + 1)) {
            if (uint32_t(std::atoi(spec.c_str() + at + 1)) != steps) continue;
            if (FILE* f = std::fopen((dir + "/mem_" + std::to_string(steps) + ".bin").c_str(), "wb")) {
                static const char zero[0x1000] = {};
                for (uint32_t p = 0; p < 0x4000000; p += 0x1000)
                    std::fwrite(p >= 0x10000 && mem::IsCommitted(p) ? ::kernel::HostPtr(p) : zero, 1, 0x1000, f);
                std::fclose(f);
                LOGR("rotk: memory dump at step %u", steps);
            }
        }
    }
#endif
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

// [[hook]] 0xFDB13, 0xFDC84, 0xFDDE3, 0xFDEC8: the menus (0xFD730) count frames while a direction is held
// ([0x1DA4A8]: a step at 10, then every 2 frames), which on the console is a third of a second before repeating and
// 15 steps a second after. Above 30 fps the count only goes up once per console frame (1/30 s of real time), so the
// menus move at the console's speed at any frame rate.
void ROTK_MenuRepeat(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    if (!rotk::g_unlocked) return;
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    static float owed = 0;
    clock::time_point now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    owed += dt > 0.1f ? 1.0f / 30.0f : dt;  // a fresh hold (or a stall) counts as one console frame
    if (owed >= 1.0f / 30.0f) {
        owed -= 1.0f / 30.0f;
        if (owed > 1.0f / 30.0f) owed = 1.0f / 30.0f;
        return;  // the game counts this frame
    }
    W32(0x1DA4A8, R32(0x1DA4A8) - 1);  // undone by the game's own increment
}

// The player's move and slide (0x5EBD0: owner in esi, the frame's step at owner+0x6CC) tests its moves against fixed
// 0.001 and 0.1, sized for the console's 1/30 s steps. ROTK PC (0x440CE0, the same function) takes them from the step
// instead: 0.001 becomes clamp(step, 0.001, 0.1) x 0.03, 0.1 becomes clamp(step, 0.001, 0.1) x 3; both the same at
// 1/30. With the fixed ones, shorter steps catch the player on walls and slow him (Path of the Wizard's opening cave
// at fixed 1/90 and 1/120 steps). Unlocked, these hooks take the place of the six instructions, as ROTK PC has them;
// at 30 fps the game's own run.
namespace {
float SlideStep(const X86Context& ctx) {
    uint32_t u = R32(ctx.esi + 0x6CC);
    float dt;
    std::memcpy(&dt, &u, 4);
    return dt < 0.001f ? 0.001f : dt > 0.1f ? 0.1f : dt;
}
float SlideEps(const X86Context& ctx) { return SlideStep(ctx) * 0.03f; }
}  // namespace

// [[hook]] 0x5EC2D, 0x5F14D: fcom [0.001] -> against the step's epsilon
bool ROTK_SlideCompare(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (!rotk::g_unlocked) return false;
    x87_compare(ctx, X87_ST(0), double(SlideEps(ctx)));
    return true;
}
// [[hook]] 0x5F1D0: fcomp [0.001]
bool ROTK_SlideComparePop(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (!rotk::g_unlocked) return false;
    x87_compare(ctx, X87_ST(0), double(SlideEps(ctx)));
    x87_pop(ctx);
    return true;
}
// [[hook]] 0x5EC3E, 0x5F15E: fld [0.001]
bool ROTK_SlideLoad(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (!rotk::g_unlocked) return false;
    x87_push(ctx, double(SlideEps(ctx)));
    return true;
}
// [[hook]] 0x5F0E2: fcomp [0.1] -> against the step x 3
bool ROTK_SlideRest(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (!rotk::g_unlocked) return false;
    x87_compare(ctx, X87_ST(0), double(SlideStep(ctx) * 3.0f));
    x87_pop(ctx);
    return true;
}
