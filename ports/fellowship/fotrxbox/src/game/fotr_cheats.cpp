// FOTR Xbox: [cheats], the game's own secret codes as switches. The game object ([0x1ECBD4]) keeps four codes at
// +0x199C, 0x10 bytes each (built at 0xD29E9: six buttons, then the on flag at +6): the Ring (+0x19A2, Y B A B Y X),
// health (+0x19B2, Y A X B A Y), spirit (+0x19C2, X Y A X B X) and ammunition (+0x19D2, X B Y A X B); +0x19E2 says a
// code was entered (the pause menu's "cheat enabled" line and the saves' cheat note read it). The game reads the
// flags as it plays (the Ring 0xB5B92, ammunition 0xAA7AF), so a switch on sets its flag each frame, as entering the
// code would. Off clears a flag the switch set; a code entered on the controller is left alone.
#include <cstdint>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

namespace fotr {
bool g_cheat_ring = false, g_cheat_health = false, g_cheat_spirit = false, g_cheat_ammo = false;  // [cheats]
}

namespace {
constexpr uint32_t kGame = 0x1ECBD4, kRing = 0x19A2, kHealth = 0x19B2, kSpirit = 0x19C2, kAmmo = 0x19D2, kUsed = 0x19E2;

bool g_ours[4];          // flags this switch set (so switching it off clears only what we turned on)
uint32_t g_ours_game;    // ... in this game object (a new one, e.g. after loading, starts clean)

void Apply() {
    using namespace fotr;
    uint32_t g = ::kernel::R32(kGame);
    if (!g) return;
    if (g != g_ours_game) { g_ours_game = g; for (bool& o : g_ours) o = false; }
    const bool want[4] = {g_cheat_ring, g_cheat_health, g_cheat_spirit, g_cheat_ammo};
    const uint32_t at[4] = {kRing, kHealth, kSpirit, kAmmo};
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        uint8_t* f = static_cast<uint8_t*>(::kernel::HostPtr(g + at[i]));
        if (want[i]) { *f = 1; g_ours[i] = true; any = true; }
        else if (g_ours[i]) { *f = 0; g_ours[i] = false; }   // switched off in-game: the cheat stops
    }
    if (!any) return;
    *static_cast<uint8_t*>(::kernel::HostPtr(g + kUsed)) = 1;
    LOG_FIRST(1, "fotr: cheats on:%s%s%s%s", g_cheat_ring ? " Ring" : "", g_cheat_health ? " health" : "",
              g_cheat_spirit ? " spirit" : "", g_cheat_ammo ? " ammunition" : "");
}
}  // namespace

// the application's frame (its vtable 0x1C5B28 +0xC)
X86_FUNC(__imp__sub_000D66E0);
namespace fotr {
void CameraFrame(X86Context& ctx, uint8_t* base);  // fotr_camera.cpp
void PromptsFrame();                               // fotr_prompts.cpp
}
X86_FUNC(sub_000D66E0) {
    Apply();
    fotr::CameraFrame(ctx, base);
    fotr::PromptsFrame();
    __imp__sub_000D66E0(ctx, base);
}
