// The Two Towers: field of view and HUD toggle, through the game's own developer settings.
// - "CAMERA/Desired FOV" (degrees, 45 in the prologue) is a field of the level's camera tuning, [[0x43B164] + 0x18]
//   + 0x14. Gameplay cameras follow it; cutscene cameras set their own, so only gameplay changes. [graphics] fov in
//   settings.toml scales it (100 is the game's).
// - "GRAPHICS/Draw HUD" is the byte 0x4A8C74 (1 by default, read at 0xAD57B and 0xCF451). F1 hides and shows the
//   HUD, for screenshots.
// Both are applied once a frame from the frame wait 0xE00E0.
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace tt {
uint32_t g_fov_percent = 100;  // settings.toml [graphics] fov
std::atomic<bool> g_hud_hidden{false};

// window messages first (game_info.cpp): F1 toggles the HUD
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t*) {
    constexpr uint32_t kKeyDown = 0x100;  // WM_KEYDOWN
    constexpr uintptr_t kF1 = 0x70;      // VK_F1
    bool repeat = lp & (intptr_t(1) << 30);  // held down: the key was already down
    if (msg == kKeyDown && wp == kF1 && !repeat) g_hud_hidden = !g_hud_hidden;
    return false;
}
}  // namespace tt

namespace {
constexpr uint32_t kDrawHud = 0x4A8C74;
constexpr uint32_t kCameraTuningRoot = 0x43B164;

bool Guest(uint32_t va) { return va >= 0x10000 && va < 0x84000000; }

void ApplyFov() {
    static uint32_t field = 0;
    static float original = 0, written = 0;
    static const uint32_t percent = [] {
        const char* e = std::getenv("TT_FOV_PERCENT");  // a test override
        return e ? uint32_t(std::atoi(e)) : tt::g_fov_percent;
    }();
    if (percent == 100 || percent < 50 || percent > 200) return;
    uint32_t root = ::kernel::R32(kCameraTuningRoot);
    uint32_t tuning = Guest(root) ? ::kernel::R32(root + 0x18) : 0;
    if (!Guest(tuning)) return;
    uint32_t va = tuning + 0x14;
    float v;
    std::memcpy(&v, ::kernel::HostPtr(va), 4);
    if (va != field || v != written) {  // a new level's tuning, or the game set it again
        field = va;
        original = v;
    }
    if (!(original > 1 && original < 170)) return;
    written = std::min(original * float(percent) / 100.0f, 170.0f);
    std::memcpy(::kernel::HostPtr(va), &written, 4);
}
}  // namespace

namespace tt {
void WorldFrame();       // tt_widescreen.cpp
void FrameRateFrame();  // tt_framerate.cpp
}

X86_EXTERN_FUNC(__imp__sub_000E00E0);
X86_FUNC(sub_000E00E0) {
    static const bool hide_env = std::getenv("TT_HUD_HIDDEN") != nullptr;  // a test override
    *static_cast<uint8_t*>(::kernel::HostPtr(kDrawHud)) = (tt::g_hud_hidden || hide_env) ? 0 : 1;
    ApplyFov();
    tt::WorldFrame();
    tt::FrameRateFrame();
    __imp__sub_000E00E0(ctx, base);
}
