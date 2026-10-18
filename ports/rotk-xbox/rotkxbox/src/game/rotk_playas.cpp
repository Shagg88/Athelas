// ROTK Xbox: [Gameplay] AllHeroes in ROTKFix.ini, every hero on the character select of a level already completed.
//
// The character select asks 0x9E870(hero, level, co-op) about each hero (0 Aragorn, 1 Legolas, 2 Gimli, 3 Gandalf,
// 4 Sam, 5 Frodo, 6 Pippin, 7 Merry, 8 Faramir; the level an index into the level table 0x16CF00): 0 not offered,
// 2 to be chosen, 1 otherwise (0x9EA60 is "is it 2"; the select screen 0x74950 makes a hero choosable only then).
// The developer unlock (0x10AA10, [Gameplay] UnlockAll) makes it 2 for everyone everywhere.
//
// Once the game is beaten (progress flag 0xC, 0x9E310) the game itself makes it 2 for every hero in every level but
// Helm's Deep, which stays Gandalf's. With AllHeroes on, every hero is 2 the same way in a level already completed
// the way the game meant it, by any hero, alone or in co-op: the profile in play keeps a bit a level for each hero
// (rotk_achievements.cpp; the bit is the level's index, 0 Helm's Deep to 12 the Crack of Doom). The scenes after
// those in the level table (13 and on) count once every level is completed. A level not completed yet keeps the
// game's own choice, and Helm's Deep is left as the game has it, as are the levels with no co-op (9 Osgiliath to 12
// the Crack of Doom: 0x9E870 says 0 for them in co-op); UnlockAll still opens everything.
//
// Gimli can't play Helm's Deep (the game crashes in its opening, a read through a bad pointer at 0x7E400), so he is
// kept off its select screen even with UnlockAll.
//
// The test build: ROTK_PLAY_AS=<0..8> sets the first player's hero (0x16D584) as the level loads (0x9C090 gives the
// scene loader his character file), to load any level as any hero without the select screen.
#include <cstdint>
#include <cstdlib>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace rotk {
bool g_all_heroes = false;  // [Gameplay] AllHeroes
}

X86_EXTERN_FUNC(__imp__sub_0009C090);
X86_EXTERN_FUNC(__imp__sub_0009E870);

namespace {
constexpr uint32_t kHero = 0x16D584, kLevel = 0x16D554;
constexpr uint32_t kSlot = 0x1C4FE0 + 2 * 0x988;  // the profile in play
constexpr uint32_t kAllLevels = 0x1FFF;
const char* const kHeroes[] = {"Aragorn", "Legolas", "Gimli", "Gandalf", "Sam", "Frodo", "Pippin", "Merry", "Faramir"};

bool Broken(uint32_t level, uint32_t hero) { return level == 0 && hero == 2; }

bool Completed(uint32_t level) {
    if (!mem::IsCommitted(kSlot) || !mem::IsCommitted(kSlot + 0x988 - 4)) return false;
    uint32_t any = 0;
    for (uint32_t h = 0; h < 9; h++)
        any |= kernel::R32(kSlot + 0x244 + h * 0x3C) | kernel::R32(kSlot + 0x244 + h * 0x3C + 4);
    any &= kAllLevels;
    return level < 13 ? (any >> level) & 1 : any == kAllLevels;
}
}  // namespace

X86_FUNC(sub_0009E870) {  // (hero, level, co-op): 0 not offered, 1 shown, 2 to be chosen
    uint32_t hero = kernel::R32(ctx.esp + 4), level = kernel::R32(ctx.esp + 8), coop = kernel::R32(ctx.esp + 12);
    __imp__sub_0009E870(ctx, base);
    if (Broken(level, hero)) {
        ctx.eax = 0;
        return;
    }
    if (ctx.eax != 2 && rotk::g_all_heroes && hero < 9 && level != 0 && !(coop && level >= 9) && Completed(level)) {
        LOG_FIRST(40, "rotk: %s offered for level %d (AllHeroes; the game said %u)", kHeroes[hero], int32_t(level),
                  ctx.eax);
        ctx.eax = 2;
    }
}

X86_FUNC(sub_0009C090) {  // the first player's character file, as a level loads
    static const uint32_t env = [] {
        const char* e = std::getenv("ROTK_PLAY_AS");
        return e ? uint32_t(std::atoi(e) + 1) : 0u;
    }();
    if (env >= 1 && env <= 9) {
        uint32_t was = kernel::R32(kHero);
        if (was != env - 1) {
            kernel::W32(kHero, env - 1);
            LOGI("rotk: level %d played as %s (ROTK_PLAY_AS) instead of %s", int32_t(kernel::R32(kLevel)),
                 kHeroes[env - 1], was < 9 ? kHeroes[was] : "?");
        }
    }
    __imp__sub_0009C090(ctx, base);
}
