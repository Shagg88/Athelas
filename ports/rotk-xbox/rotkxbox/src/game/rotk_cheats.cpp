// ROTK Xbox: [Cheats] in ROTKFix.ini, the game's own secret codes that are switches.
//
// The game takes its codes on the pause page with both triggers held and four presses (0xF9460; the table 0x178138,
// 98 codes; most of the shared ones only once the game is beaten, 0x9E4D0 flag 0xC). The switches among them flip a
// bit of each player's +0x634 (0xF9030; the players are [0x17F758], [0x17F75C]):
//   Invulnerable 0x04, InfiniteMissiles 0x08, AlwaysDevastating 0x10, PerfectMode 0x20
// A player object lasts a level, so the bits are put back each frame while a switch is on (in a level, for each
// player there); turning one off clears it once. ROTK_CHEATS=Invulnerable,PerfectMode,... turns them on in test
// builds.
#include <cstdint>
#include <cstdlib>
#include <string>

#include "core/log.h"
#include "kernel/kernel.h"

namespace rotk {
bool g_cheat_invulnerable = false, g_cheat_missiles = false, g_cheat_devastating = false, g_cheat_perfect = false;
bool InMenus();  // rotk_controls.cpp
}  // namespace rotk

namespace {
bool Env(const char* name) {
    static const std::string list = [] {
        const char* e = std::getenv("ROTK_CHEATS");
        return e ? "," + std::string(e) + "," : std::string();
    }();
    return list.find("," + std::string(name) + ",") != std::string::npos;
}
}  // namespace

namespace rotk {
// once a frame, from the time step (rotk_framerate.cpp)
void CheatsFrame() {
    if (InMenus()) return;
    struct Bit {
        bool on;
        uint32_t mask;
    };
    const Bit bits[] = {{g_cheat_invulnerable || Env("Invulnerable"), 0x04},
                        {g_cheat_missiles || Env("InfiniteMissiles"), 0x08},
                        {g_cheat_devastating || Env("AlwaysDevastating"), 0x10},
                        {g_cheat_perfect || Env("PerfectMode"), 0x20}};
    static uint32_t was = 0;  // the bits on last frame
    uint32_t want = 0;
    for (const Bit& b : bits)
        if (b.on) want |= b.mask;
    uint32_t clear = was & ~want;
    if (want || clear)
        for (uint32_t slot : {0x17F758u, 0x17F75Cu}) {
            uint32_t p = kernel::R32(slot);
            if (p < 0x10000 || p >= 0x4000000) continue;
            uint32_t v = kernel::R32(p + 0x634), nv = (v | want) & ~clear;
            if (nv != v) kernel::W32(p + 0x634, nv);
        }
    if (want != was) LOGI("rotk: cheats now %02X (ROTKFix.ini [Cheats])", want);
    was = want;
}
}  // namespace rotk
