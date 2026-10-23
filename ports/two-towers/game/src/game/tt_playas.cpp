// The Two Towers: [gameplay] all_heroes in settings.toml, every hero on the level select of a level already completed.
//
// The game's progress: the byte 0x4AB68F + hero * 15 + level (0xD2420(level, hero); heroes 1 Aragorn, 2 Legolas,
// 3 Gimli, 4 Isildur; levels 0 the Prologue, 1 Weathertop, 2 the Gates of Moria ... 11 the Hornburg Courtyard, 13
// the Tower of Orthanc) is 1 once that hero has completed that level. The level select (the screen object; its level
// at +0x14, its hero at +0x18, an index into the table 0x1A40F4) keeps two rules of the game's:
//   - a hero who has completed a level can't play it again until the game is beaten ("is it beaten", 0x73960, the
//     Hornburg Courtyard completed by Aragorn, Legolas or Gimli; asked when a hero is chosen, 0x77895, and inlined
//     where the heroes' labels are drawn, 0x75542);
//   - Isildur is offered (four heroes instead of three) only once the game is beaten and the Tower of Orthanc
//     completed: 0x73960 then 0xD2420(13, hero) wherever the screen counts its heroes (0x74F20, 0x75250, 0x75430,
//     0x758A0, 0x760E2..0x7625D, the left and right moves 0x76740 and 0x76800, the screen's set-up 0x76CCA).
// With all_heroes on, both answers are "yes" there while the level picked is one already completed by any hero, so
// every hero, Isildur too, can play it: what the game itself allows everywhere once it is beaten and Orthanc done.
// The same questions asked anywhere else (which menu items and bonus levels are open, 0x739A0, 0x74198..0x743B2)
// keep the game's answers. The Prologue and Weathertop are left as they are: the game picks their hero itself, and
// Legolas and Gimli don't load in them.
//
// The test build: TT_PLAY_AS=aragorn|legolas|gimli|isildur sets the hero as each level loads (0xD3C00, the mission
// loader, opens Worlds\<level>.bfa, .bfl, .bfg or .bfi by the hero), to load any level as any hero without the
// select screen.
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

namespace tt {
bool g_all_heroes = false;  // [gameplay] all_heroes
bool g_unlock_all = false;  // [gameplay] unlock_all
bool InMenus();             // tt_extras.cpp
}

X86_EXTERN_FUNC(__imp__sub_000D3C00);
X86_EXTERN_FUNC(__imp__sub_00073960);
X86_EXTERN_FUNC(__imp__sub_000D2420);

namespace {
constexpr uint32_t kHero = 0x4AB69D, kProgress = 0x4AB68F;
const char* const kNames[] = {"?", "Aragorn", "Legolas", "Gimli", "Isildur"};

uint32_t g_screen = 0;  // the level select, from its last "is it beaten"

// the return addresses on the level select whose questions are about its heroes
bool HeroQuestion(uint32_t ret) {
    struct Range {
        uint32_t lo, hi;
    };
    static const Range kSites[] = {
        {0x74F20, 0x74F65}, {0x75080, 0x75105}, {0x75250, 0x752A0}, {0x75430, 0x75490}, {0x75540, 0x75578},
        {0x758A0, 0x758F3}, {0x75917, 0x75970}, {0x760DE, 0x762A0}, {0x76740, 0x76790}, {0x76800, 0x76850},
        {0x76CCA, 0x76D41}, {0x77893, 0x7789B},
    };
    for (const Range& r : kSites)
        if (ret >= r.lo && ret < r.hi) return true;
    return false;
}

// a level after Weathertop that some hero has completed
bool Opened(uint32_t level) {
    if (!tt::g_all_heroes || level < 2 || level > 14) return false;
    for (uint32_t h = 1; h <= 4; h++)
        if (kernel::G<uint8_t>(kProgress + h * 15 + level) == 1) return true;
    return false;
}

uint32_t ScreenLevel(uint32_t screen) { return screen >= 0x10000 ? kernel::R32(screen + 0x14) : 0xFFFFFFFF; }
}  // namespace

X86_FUNC(sub_00073960) {  // the level select's "is the game beaten" (ecx the screen)
    uint32_t ret = kernel::R32(ctx.esp);
    if (HeroQuestion(ret)) {
        g_screen = ctx.ecx;
        uint32_t level = ScreenLevel(ctx.ecx);
        if (Opened(level)) {
            LOG_FIRST(8, "tt: level %u completed: every hero offered (all_heroes)", level);
            ctx.eax = 1;
            ctx.esp += 4;  // its ret
            return;
        }
    }
    __imp__sub_00073960(ctx, base);
}

X86_FUNC(sub_000D2420) {  // (level, hero): 1 once that hero has completed that level
    uint32_t ret = kernel::R32(ctx.esp), level = kernel::R32(ctx.esp + 4) & 0xFF, hero = kernel::R32(ctx.esp + 8);
    // unlock_all: in the front end every level reads as completed by every hero, so the game counts itself beaten
    // and opens what it opens then: every level for every hero, Isildur, the bonus levels and extras. Only the
    // answers change; the progress in the save stays the player's own, and in a level the game asks as before
    if (tt::g_unlock_all && level <= 14 && hero >= 1 && hero <= 4 && tt::InMenus()) {
        LOG_FIRST(1, "tt: every level open (unlock_all)");
        ctx.eax = (ctx.eax & ~0xFFu) | 1;
        ctx.esp += 4;
        return;
    }
    if ((level == 11 || level == 13) && hero >= 1 && hero <= 3 && tt::g_all_heroes && HeroQuestion(ret)) {
        // the heroes' labels ask with the level picked in esi; the screen's set-up with the screen in esi
        uint32_t picked = ret >= 0x75540 && ret < 0x75578   ? ctx.esi
                          : ret >= 0x76CCA && ret < 0x76D41 ? ScreenLevel(ctx.esi)
                                                            : ScreenLevel(g_screen);
        if (Opened(picked)) {
            ctx.eax = (ctx.eax & ~0xFFu) | 1;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_000D2420(ctx, base);
}

X86_FUNC(sub_000D3C00) {  // load a mission file: (name, kind), kind 0 the world
    static const uint32_t want = [] {
        const char* e = std::getenv("TT_PLAY_AS");
        if (!e) return 0u;
        for (uint32_t h = 1; h <= 4; h++) {
            const char *a = e, *b = kNames[h];
            while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b)))
                a++, b++;
            if (!*a && !*b) return h;
        }
        return 0u;
    }();
    if (want && kernel::R32(ctx.esp + 8) == 0) {
        uint8_t was = kernel::G<uint8_t>(kHero);
        kernel::G<uint8_t>(kHero) = uint8_t(want);
        if (was != want) LOGI("tt: playing as %s (TT_PLAY_AS) instead of %s", kNames[want], kNames[was <= 4 ? was : 0]);
    }
    __imp__sub_000D3C00(ctx, base);
}
