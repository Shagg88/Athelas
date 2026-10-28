// FOTR Xbox: [gameplay] start_chapter, a new game started at the beginning of a later chapter.
//
// The game has no chapter or level select and nothing to unlock (no menu entry, flag or cheat for one in its
// executable or its 64 scene scripts). A new game copies the name of its first scene into the current scene
// (0x129CB9 loads "Bag_End" into eax for the copy 0x11E800). With start_chapter set, that name is the first scene of
// the chosen chapter instead, as the previous chapter's last scene names it when it moves the player on
// (TransitionToArea in the scene scripts). The scene's own script sets up who is played and the party. Items and
// quest steps from the chapters skipped are not given: it is a way to replay a chapter, not a save.
//
// Testing, in the Linux test build only: FOTR_SCENE=<any scene folder's name> starts there.
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace fotr {
uint32_t g_start_chapter = 0;  // [gameplay] start_chapter, an index into kChapters
}

namespace {
struct Chapter {
    const char* scene;
    const char* what;
};
// in the order the game plays them; each scene as the previous chapter's exit spells it
const Chapter kChapters[] = {
    {"Bag_End", "the Shire"},                  // the game's own start
    {"Tree_Labyrinth", "the Old Forest"},      // from Green_Hill_Country_Night
    {"Prancing_Pony", "Bree"},                 // from Barrow_downs_backside
    {"Weathertop_Newest", "Weathertop"},       // from Prancing_pony
    {"Bilbos_Room", "Rivendell"},              // from Troll_shaws
    {"Gates_of_Moria", "Moria"},               // from House_of_elrond
    {"IGC_forest", "Lothlorien"},              // from Abyss
    {"Orc_Dam_Camp", "the River Anduin"},      // from IGC_mirror
};
}  // namespace

void FOTR_StartScene(X86Context& ctx, uint8_t* base) {
    (void)base;
    const char* want = nullptr;
    const char* what = nullptr;
#ifndef _WIN32
    static const char* test = std::getenv("FOTR_SCENE");
    if (test && *test) want = what = test;
#endif
    if (!want && fotr::g_start_chapter > 0 && fotr::g_start_chapter < sizeof kChapters / sizeof kChapters[0]) {
        want = kChapters[fotr::g_start_chapter].scene;
        what = kChapters[fotr::g_start_chapter].what;
    }
    if (!want) return;
    // one name for the whole run (the setting is read at start)
    static uint32_t text = 0;
    if (!text && (text = mem::AllocSystem(64, 16)) != 0) std::strncpy(static_cast<char*>(kernel::HostPtr(text)), want, 63);
    if (!text) return;
    ctx.eax = text;
    LOGR("fotr: new game started at %s (%s) instead of Bag_End", want, what);
}
