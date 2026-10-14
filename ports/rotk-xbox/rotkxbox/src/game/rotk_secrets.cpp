// ROTK Xbox: the game's own hidden switches (found 2026-10-13, notes/rotkx-08-hidden-settings.md). All off unless set;
// in the test build each can be turned on from the environment (ROTK_UNLOCK, ROTK_DEVBUTTONS, ROTK_CODES,
// ROTK_DAMAGE=<percent>) to compare captures.
//
// - 0xF9660 (not used): the player-damage function (0x2A1E0) asks it whether to skip the hit, compiled down to "no";
//   answering yes crashes the game a few seconds into a fight (the code behind it is gone), so it is left alone.
// - 0x10AA10: reads the developer "unlock all" byte 0x1DFD40 (the front end's boot script sets it to 0): every level
//   and character selectable (0x9E560, 0x9E870).
// - 0x10AA40: each frame hides the pause pages' developer buttons "Debug Game", "Debug Win", "Try Again" (word +0x74
//   of the three elements it looks up once).
// - 0x9E4D0: whether a progress flag is set (0xC: the final level beaten), asked before secret codes are accepted.
// - 0x29530: copies the difficulty table's damage multipliers (upgd "DIFF") to 0x17F550..0x17F57C at load: easy,
//   normal, hard x single player, co-op x min, max (damage the player takes).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace rotk {
bool g_unlock_all = false, g_dev_buttons = false, g_codes_anytime = false;
uint32_t g_damage_percent = 100;

namespace {
#ifndef _WIN32
const bool kEnvRead = [] {
    auto on = [](const char* n) { return std::getenv(n) != nullptr; };
    g_unlock_all |= on("ROTK_UNLOCK");
    g_dev_buttons |= on("ROTK_DEVBUTTONS");
    g_codes_anytime |= on("ROTK_CODES");
    if (const char* d = std::getenv("ROTK_DAMAGE")) g_damage_percent = uint32_t(std::atoi(d));
    return true;
}();
#endif
}  // namespace
}  // namespace rotk

X86_EXTERN_FUNC(__imp__sub_0010AA10);
X86_FUNC(sub_0010AA10) {
    __imp__sub_0010AA10(ctx, base);
    LOG_FIRST(1, "rotk: unlock-all byte 0x1DFD40 = %u, debug respawn byte 0x1DFD41 = %u", ctx.eax & 0xFF,
              ::kernel::R32(0x1DFD40) >> 8 & 0xFF);
    if (rotk::g_unlock_all) ctx.eax = (ctx.eax & ~0xFFu) | 1;
}

X86_EXTERN_FUNC(__imp__sub_0010AA40);
X86_FUNC(sub_0010AA40) {
    __imp__sub_0010AA40(ctx, base);
    if (!rotk::g_dev_buttons) return;
    for (uint32_t slot : {0x1DB32Cu, 0x1DB330u, 0x1DB334u})
        if (uint32_t e = ::kernel::R32(slot)) ::kernel::W16(e + 0x74, 1);
}

X86_EXTERN_FUNC(__imp__sub_0009E4D0);
X86_FUNC(sub_0009E4D0) {
    __imp__sub_0009E4D0(ctx, base);
    if (rotk::g_codes_anytime && ::kernel::R32(ctx.esp + 4) == 0xC) ctx.eax = (ctx.eax & ~0xFFu) | 1;
}

X86_EXTERN_FUNC(__imp__sub_00029530);
X86_FUNC(sub_00029530) {
    __imp__sub_00029530(ctx, base);
    float v[12];
    for (int i = 0; i < 12; i++) {
        uint32_t u = ::kernel::R32(0x17F550 + i * 4);
        std::memcpy(&v[i], &u, 4);
    }
    LOGR("rotk: damage taken: easy %.2f-%.2f (co-op %.2f-%.2f), normal %.2f-%.2f (%.2f-%.2f), hard %.2f-%.2f (%.2f-%.2f)",
         v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
    if (rotk::g_damage_percent == 100) return;
    for (int i = 0; i < 12; i++) {
        float f = v[i] * float(rotk::g_damage_percent) / 100.0f;
        uint32_t u;
        std::memcpy(&u, &f, 4);
        ::kernel::W32(0x17F550 + i * 4, u);
    }
    LOGR("rotk: damage taken scaled to %u%%", rotk::g_damage_percent);
}

// 0x102D50: the main menu, as it opens, hides three of its items with 0x10A550(0, name, visible = 0): "Online" (Xbox
// Live, gone), "Exit" and "PlayTrailer". The Xbox release cut Exit's text and action (its string table has no
// "mainMenuExit", and no code handles the item); on PC it quits the game, so the port shows it again, always: its
// label is the unused "Online" entry of the string table (GlobScen: "key\0text\0" pairs) renamed to "mainMenuExit" /
// "Exit" (again whenever the table is loaded anew), and choosing it (see 0xFE010 below) ends the game.
namespace {
void LabelExit() {
    // the table is loaded again (elsewhere) with the front end after a level, so it is looked for each time the menu
    // opens; the old copy may still hold the renamed entry, so only the unrenamed one counts
    static const char kFrom[] = "mainMenuOnline\0Online";
    static const char kTo[] = "mainMenuExit\0\0\0Exit\0\0";  // same length: the table's layout is kept
    static_assert(sizeof(kFrom) == sizeof(kTo));
    static uint32_t at = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (uint32_t p = 0x10000; p < 0x4000000; p += 0x1000) {
        if (!mem::IsCommitted(p)) continue;
        bool next = mem::IsCommitted(p + 0x1000);  // a match may run on into the next page
        const char* page = static_cast<const char*>(::kernel::HostPtr(p));
        uint32_t end = next ? 0x1000 : 0x1000 - uint32_t(sizeof(kFrom) - 1);
        for (uint32_t i = 0; i < end; i++) {
            if (page[i] != 'm' || std::memcmp(page + i, kFrom, sizeof(kFrom) - 1) != 0) continue;
            std::memcpy(::kernel::HostPtr(p + i), kTo, sizeof(kTo) - 1);
            LOGR("rotk: main menu Exit labelled (string table at %08X, %.1f ms)", p + i,
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            at = p + i;
            return;
        }
    }
    if (!at) LOGR("rotk: main menu Exit: the string table's Online entry was not found, the item has no label");
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00102D50);
X86_FUNC(sub_00102D50) {
    __imp__sub_00102D50(ctx, base);
    LabelExit();
    ::kernel::CallGuest(ctx, 0x10A550, {0u, 0x15A318u, 1u});  // MainMenuScreen.Exit
    static std::atomic<int> logged{0};
    if (!logged++) LOGR("rotk: main menu shows Exit");
}

// 0xFE010(event): the menus' input events (jump table 0xFE0B8; 2/3 down, 7 accept, 6 START). Accept runs the focused
// element's script (+0x8C; Exit's is an "rpn!" program whose quit does nothing on Xbox), then the page's; START falls
// back to the same. On the main menu's Exit item both end the game, as on PC.
X86_EXTERN_FUNC(__imp__sub_000FE010);
X86_FUNC(sub_000FE010) {
    uint32_t event = ::kernel::R32(ctx.esp + 4);
    if (event == 6 || event == 7) {
        uint32_t focus = ::kernel::R32(0x1DB2E0);
        if (focus && focus == ::kernel::CallGuest(ctx, 0x1017F0, {0u, 0x15A318u})) {  // MainMenuScreen.Exit
            LOGR("rotk: Exit chosen on the main menu");
            ::kernel::Shutdown(0);
        }
    }
    __imp__sub_000FE010(ctx, base);
}
