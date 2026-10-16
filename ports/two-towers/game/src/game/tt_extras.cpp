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
#include <cctype>
#include <cstring>
#include <string>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace tt {
// the front end (title, main menu, the screens between levels) or a level, from the files the game opens: a world
// (Worlds\*.bfa) starts a level; the front end's own screens (UI\*.BFF but loading pictures and options, which a
// pause in a level may open too) end one. For the keyboard: Esc goes back in the front end's menus.
std::atomic<bool> g_front_end{true};
void FileOpened(const char* name) {
    std::string n = name;
    for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    auto ends = [&](const char* e) { size_t k = std::strlen(e); return n.size() >= k && n.compare(n.size() - k, k, e) == 0; };
    bool was = g_front_end;
    if (n.find("\\worlds\\") != std::string::npos && ends(".bfa")) g_front_end = false;
    else if (n.find("\\ui\\") != std::string::npos && ends(".bff") && n.find("\\ui\\load") == std::string::npos &&
             n.find("\\ui\\options") == std::string::npos)
        g_front_end = true;
    if (was != g_front_end) LOGI("tt: %s (%s)", g_front_end ? "front end" : "in a level", name);
}
bool InMenus() { return g_front_end.load(); }

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
    static const char* env = std::getenv("TT_FOV_PERCENT");  // a test override
    // read each frame: the in-game menu may change it while playing
    const uint32_t percent = env ? uint32_t(std::atoi(env)) : tt::g_fov_percent;
    static uint32_t last = 100;
    if ((percent == 100 && last == 100) || percent < 50 || percent > 200) return;
    last = percent;
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
void DevSettingsFrame();  // tt_devsettings.cpp
}

X86_EXTERN_FUNC(__imp__sub_000E00E0);
X86_FUNC(sub_000E00E0) {
    static const bool hide_env = std::getenv("TT_HUD_HIDDEN") != nullptr;  // a test override
    *static_cast<uint8_t*>(::kernel::HostPtr(kDrawHud)) = (tt::g_hud_hidden || hide_env) ? 0 : 1;
    ApplyFov();
    tt::WorldFrame();
    tt::FrameRateFrame();
    tt::DevSettingsFrame();
    __imp__sub_000E00E0(ctx, base);
}
