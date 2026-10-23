// FOTR Xbox: [cheats], the game's own secret codes as switches. The game object ([0x1ECBD4]) keeps four codes at
// +0x199C, 0x10 bytes each (built at 0xD29E9: six buttons, then the on flag at +6): the Ring (+0x19A2, Y B A B Y X),
// health (+0x19B2, Y A X B A Y), spirit (+0x19C2, X Y A X B X) and ammunition (+0x19D2, X B Y A X B); +0x19E2 says a
// code was entered (the pause menu's "cheat enabled" line and the saves' cheat note read it). The game reads the
// flags as it plays (the Ring 0xB5B92, ammunition 0xAA7AF), so a switch on sets its flag each frame, as entering the
// code would. Off leaves the game's own (a code entered on the controller still works).
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

void Apply() {
    using namespace fotr;
    if (!g_cheat_ring && !g_cheat_health && !g_cheat_spirit && !g_cheat_ammo) return;
    uint32_t g = ::kernel::R32(kGame);
    if (!g) return;
    auto on = [&](bool want, uint32_t at) {
        if (want) *static_cast<uint8_t*>(::kernel::HostPtr(g + at)) = 1;
    };
    on(g_cheat_ring, kRing);
    on(g_cheat_health, kHealth);
    on(g_cheat_spirit, kSpirit);
    on(g_cheat_ammo, kAmmo);
    on(true, kUsed);
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
