// ROTK Xbox: [Gameplay] Eowyn in ROTKFix.ini, Éowyn in Faramir's place: on the character select (his figure dressed
// as her, her name) and in every level he can play, with his moves, combos, bow and upgrades on her.
//
// Éowyn is a hero in the game's own data: Pelennor Fields (Data\Game\pel\pel02.scx) has her as the player of its
// scenes, with a mesh, textures and a skeleton of her own (rotk_eowyn_build.cpp). As the game opens Faramir's files,
// they are served from memory with her pieces in them (kernel::SetFileProvider): his hero files (Data\Player\
// Char_Far.scx, Foll_Far.scx for co-op) rebuilt with her mesh, bound to his skeleton, his bow and arrow added to it,
// her textures (and his for the bow), and his voice quiet (his shouts as he fights are his; she has none); the menus'
// file (Data\Game\Fro\Fro03.scx) with his figure's mesh and textures replaced where they are (a level's file keeps
// its streamed zones where its table says). They are built on a thread of ours from the start, from the player's own
// files; nothing is written to disk. Faramir's face on the HUD (GlobScen.scx) is made hers from her face texture. When something is not as expected, Faramir stays Faramir (the log says why).
// His name is hers where the game shows it (GetText, rotk_lang.cpp).
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "game/rotk_eowyn_build.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace rotk {
bool g_eowyn = false;  // [Gameplay] Eowyn
}

namespace {
std::mutex g_lock;
std::condition_variable g_built;
bool g_done = false;
kernel::FileData g_char, g_follower, g_menu, g_glob;  // null: Faramir's own

bool Ends(const std::string& s, const char* tail) {
    size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

void Publish(kernel::FileData hero, kernel::FileData follower, kernel::FileData menu, kernel::FileData portrait) {
    std::lock_guard lock(g_lock);
    g_char = hero;
    g_follower = follower;
    g_menu = menu;
    g_glob = portrait;
    g_done = true;
    g_built.notify_all();
}

void BuildAll() {
    using namespace rotk::eowyn;
    kernel::FileData hero, follower, menu, portrait;
    std::string why;
    std::vector<uint8_t> pel;
    Pieces her;
    // the game's drive is there once the title starts (a moment after GameInfo::loaded)
    bool read = false;
    for (int tries = 0; tries < 200 && !(read = kernel::ReadGameFile("d:\\Data\\Game\\pel\\pel02.scx", pel)); tries++)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!read) {
        why = "Data\\Game\\pel\\pel02.scx not read";
    } else if (FindPieces(pel, her, why)) {
        pel = {};
        auto hero_file = [&](const char* name, kernel::FileData& out) {
            std::vector<uint8_t> f, built;
            std::string w;
            if (!kernel::ReadGameFile(std::string("d:\\Data\\Player\\") + name, f)) {
                LOGR("rotk: Eowyn: Data\\Player\\%s not read", name);
            } else if (!BuildHero(f, her, built, w)) {
                LOGR("rotk: Eowyn: %s stays Faramir's (%s)", name, w.c_str());
            } else {
                if (!w.empty()) LOGR("rotk: Eowyn: %s: %s", name, w.c_str());
                out = std::make_shared<const std::vector<uint8_t>>(std::move(built));
            }
        };
        hero_file("Char_Far.scx", hero);
        hero_file("Foll_Far.scx", follower);
        std::vector<uint8_t> fro;
        if (!kernel::ReadGameFile("d:\\Data\\Game\\Fro\\Fro03.scx", fro)) {
            LOGR("rotk: Eowyn: Data\\Game\\Fro\\Fro03.scx not read");
        } else if (!BuildMenu(fro, her, why)) {
            LOGR("rotk: Eowyn: the character select keeps Faramir's figure (%s)", why.c_str());
        } else {
            menu = std::make_shared<const std::vector<uint8_t>>(std::move(fro));
        }
        std::vector<uint8_t> glob;
        std::string w;
        if (!kernel::ReadGameFile("d:\\Data\\Game\\GlobScen.scx", glob)) {
            LOGR("rotk: Eowyn: Data\\Game\\GlobScen.scx not read");
        } else if (!BuildPortrait(glob, her, w)) {
            LOGR("rotk: Eowyn: the HUD keeps Faramir's face (%s)", w.c_str());
        } else {
            portrait = std::make_shared<const std::vector<uint8_t>>(std::move(glob));
        }
        why.clear();
    }
    if (!why.empty()) LOGR("rotk: Eowyn: Faramir stays Faramir (%s)", why.c_str());
    else LOGR("rotk: Eowyn in Faramir's place: hero file %s, co-op file %s, character select %s, HUD face %s",
              hero ? "ready" : "his", follower ? "ready" : "his", menu ? "ready" : "his", portrait ? "ready" : "his");
    Publish(hero, follower, menu, portrait);
}

// on a thread of ours: whatever happens, the files are given (his own if not hers), never waited for forever
void Build() {
    try {
        BuildAll();
    } catch (const std::exception& e) {
        LOGR("rotk: Eowyn: Faramir stays Faramir (%s)", e.what());
        Publish(nullptr, nullptr, nullptr, nullptr);
    } catch (...) {
        LOGR("rotk: Eowyn: Faramir stays Faramir (the build failed)");
        Publish(nullptr, nullptr, nullptr, nullptr);
    }
}

kernel::FileData Provide(const std::string& path) {
    int which = Ends(path, "\\data\\player\\char_far.scx")   ? 0
                : Ends(path, "\\data\\player\\foll_far.scx") ? 1
                : Ends(path, "\\data\\game\\fro\\fro03.scx") ? 2
                : Ends(path, "\\data\\game\\globscen.scx")  ? 3
                                                             : -1;
    if (which < 0) return nullptr;
    std::unique_lock lock(g_lock);
    g_built.wait(lock, [] { return g_done; });
    return which == 0 ? g_char : which == 1 ? g_follower : which == 2 ? g_menu : g_glob;
}
}  // namespace

namespace rotk {
// GameInfo::loaded (rotk_movies.cpp): the game's files can be read, the game has not started
void InitEowyn() {
    if (!g_eowyn) return;
    try {
        std::thread(Build).detach();
    } catch (...) {
        LOGR("rotk: Eowyn: Faramir stays Faramir (no thread to build her on)");
        return;
    }
    kernel::SetFileProvider(Provide);
}

// GetText (rotk_lang.cpp): his name as hers ("faramir", the name shown on the character select and the screens
// after); 0 for the game's own. The game's fonts have no É
uint32_t EowynText(const std::string& key) {
    if (!g_eowyn || key != "faramir") return 0;
    {
        std::lock_guard lock(g_lock);
        if (!g_done || !g_char) return 0;  // he stays Faramir
    }
    static const uint32_t text = [] {  // made once, before any thread sees it
        uint32_t t = mem::AllocSystem(16, 16);
        if (t) std::memcpy(kernel::HostPtr(t), "Eowyn", 6);
        return t;
    }();
    return text;
}
}  // namespace rotk
