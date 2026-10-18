// The Two Towers: [cheats] in settings.toml, the game's own secret codes as switches.
//
// The game takes them on the pause screen with both triggers held and four presses (0xC5B50; the table 0x1B13D0,
// 24-byte records: four buttons, the code's function, and whether it needs the game beaten first). What the switch
// codes set (all cleared as a level starts, 0x99125):
//   invulnerable        bit 0 of the player's +0x110 (the player [0x43B15C]; 0xC5D60)
//   always_devastating  byte 0x43B214 (0x98F10), read by the player's attacks
//   infinite_missiles   byte 0x43B215 (0x98F20): the missile count [player+0x304] is not taken down (0x83FCE)
//   slow_motion         the clock's fixed step (0x4B8B80, 0xE3530 on, 0xE3610 off; tt_framerate.cpp keeps it the
//                       same slow motion at any frame rate)
// (Code 2, Up Up Down Down, toggles byte 0x8D24DC through 0xF5750 for every object; it showed no change to the
// enemies in a test at Amon Hen, so it is not offered.)
// Each is put back every frame while it is on, so a new level (which clears them) keeps it; turning one off clears
// it once. Only in a level. TT_CHEATS=invulnerable,slow_motion,... turns them on in test builds.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

namespace tt {
bool g_cheat_invulnerable = false, g_cheat_devastating = false, g_cheat_missiles = false,
     g_cheat_slow = false;
bool InMenus();  // tt_extras.cpp
}  // namespace tt

namespace {
constexpr uint32_t kPlayer = 0x43B15C, kDevastating = 0x43B214, kMissiles = 0x43B215, kFixedStep = 0x4B8B80;

bool Env(const char* name) {
    static const std::string list = [] {
        const char* e = std::getenv("TT_CHEATS");
        return e ? "," + std::string(e) + "," : std::string();
    }();
    return list.find("," + std::string(name) + ",") != std::string::npos;
}

// a cheat's switch: on puts the game's value back each frame, off clears it once after being on
struct Switch {
    bool was = false;
    template <class Set>
    void Apply(bool on, Set set) {
        if (on) set(true);
        else if (was) set(false);
        was = on;
    }
};
Switch g_inv, g_dev, g_mis, g_slow;
}  // namespace

namespace tt {
// once a frame, from the frame wait (tt_extras.cpp), on the game's thread
void CheatsFrame(X86Context& ctx) {
    (void)ctx;
    if (InMenus()) return;
    uint32_t player = kernel::R32(kPlayer);
    bool have_player = player >= 0x10000 && player < 0x4000000;
    g_inv.Apply(g_cheat_invulnerable || Env("invulnerable"), [&](bool on) {
        if (!have_player) return;
        uint32_t v = kernel::R32(player + 0x110);
        kernel::W32(player + 0x110, on ? v | 1u : v & ~1u);
    });
    g_dev.Apply(g_cheat_devastating || Env("always_devastating"),
                [](bool on) { kernel::G<uint8_t>(kDevastating) = on ? 1 : 0; });
    g_mis.Apply(g_cheat_missiles || Env("infinite_missiles"),
                [](bool on) { kernel::G<uint8_t>(kMissiles) = on ? 1 : 0; });
    g_slow.Apply(g_cheat_slow || Env("slow_motion"), [](bool on) { kernel::W32(kFixedStep, on ? 1 : 0); });
}
}  // namespace tt
