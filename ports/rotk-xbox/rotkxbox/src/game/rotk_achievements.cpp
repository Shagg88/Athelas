// ROTK Xbox: achievements and the Discord status.
//
// The game's progress (found 2026-10-16, notes/rotkx-11-achievements.md) is three 0x988-byte profile slots from
// 0x1C4FE0; the game asks slot 2, the profile in play (0x9E310 passes 2 to the stats getter 0x9A3C0, whose record
// lookup is 0x9A310). In a slot:
//   +0x244 + hero * 0x3C   one record a hero (0 Aragorn, 1 Legolas, 2 Gimli, 3 Gandalf, 4 Sam, 5 Frodo, 6 Pippin,
//                          7 Merry, 8 Faramir: the char_* table 0x16CF48)
//     +0x00  levels completed alone, a bit a level (the level table 0x16CF00: bit 0 Helm's Deep ... 12 Crack of Doom)
//     +0x04  levels completed in co-op
//     +0x18  the hero's level (float, 1 at the start); +0x1C, +0x20 experience
// "Debug Win" on Helm's Deep as Gandalf set +0x00 bit 0 of record 3 and his level 1 -> 2 (harness, 2026-10-16).
// The block is read every second from a host thread: what a save already holds counts too.
//
// Enemies defeated: a hit a player lands (0x2A1B0, rotk_secrets.cpp) that takes an enemy's health (+0x90) from
// above 0 to 0 or below.
//
// The Discord status: the level from the scene the game loads (Data\Game\<abc>\<Abc01>.scx), the hero from the
// character file (Data\Player\Char_<Abc>.scx).
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace rotk {

// the 13 levels by bit, in the order the game plays them
struct Level {
    int bit;
    const char *code, *name, *id;
};
const Level kLevels[] = {
    {0, "hel01", "Helm's Deep", "level_helms_deep"},
    {6, "ise01", "Road to Isengard", "level_road_to_isengard"},
    {9, "osg01", "Escape from Osgiliath", "level_osgiliath"},
    {1, "pat01", "Paths of the Dead", "level_paths_of_the_dead"},
    {2, "pat03", "King of the Dead", "level_king_of_the_dead"},
    {3, "min02", "The Southern Gate", "level_southern_gate"},
    {7, "min00", "Top of the Wall", "level_top_of_the_wall"},
    {8, "min04", "The Courtyard", "level_courtyard"},
    {4, "pel02", "Pelennor Fields", "level_pelennor_fields"},
    {10, "she01", "Shelob's Lair", "level_shelobs_lair"},
    {11, "cir00", "Cirith Ungol", "level_cirith_ungol"},
    {5, "bla01", "The Black Gate", "level_black_gate"},
    {12, "cra01", "The Crack of Doom", "level_crack_of_doom"},
};
const char* const kHeroes[9] = {"Aragorn", "Legolas", "Gimli", "Gandalf", "Sam", "Frodo", "Pippin", "Merry", "Faramir"};
const char* const kHeroCodes[9] = {"ara", "leg", "gim", "gan", "sam", "fro", "pip", "mer", "far"};
const char* const kHeroIds[9] = {"hero_aragorn", "hero_legolas", "hero_gimli", "hero_gandalf", "hero_sam",
                                 "hero_frodo", "hero_pippin", "hero_merry", "hero_faramir"};

namespace {
constexpr uint32_t kSlot = 0x1C4FE0 + 2 * 0x988;
constexpr uint32_t kAllLevels = 0x1FFF;  // bits 0 to 12

std::mutex g_lock;
std::string g_level, g_hero;  // the Discord status: the scene's level name, the hero's name

uint32_t Done(int hero, bool coop) { return ::kernel::R32(kSlot + 0x244 + uint32_t(hero) * 0x3C + (coop ? 4 : 0)); }
float HeroLevel(int hero) {
    uint32_t u = ::kernel::R32(kSlot + 0x244 + uint32_t(hero) * 0x3C + 0x18);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

void Check() {
    if (!mem::IsCommitted(kSlot) || !mem::IsCommitted(kSlot + 0x988 - 4)) return;
    uint32_t any = 0, all_heroes_all = kAllLevels;
    bool coop = false;
    for (int h = 0; h < 9; h++) {
        uint32_t m = (Done(h, false) | Done(h, true)) & kAllLevels;
        if (m) ach::Unlock(kHeroIds[h]);
        if (Done(h, true) & kAllLevels) coop = true;
        any |= m;
        all_heroes_all &= m;
        float lv = HeroLevel(h);
        if (lv >= 10.0f && lv < 100.0f) ach::Unlock("hero_level_10");
    }
    for (const Level& l : kLevels)
        if (any & (1u << l.bit)) ach::Unlock(l.id);
    if (coop) ach::Unlock("coop");
    if ((any & kAllLevels) == kAllLevels) ach::Unlock("every_level");
    if (all_heroes_all == kAllLevels) ach::Unlock("every_level_every_hero");
}
}  // namespace

// the list, for GameInfo::achievements
std::vector<ach::Def> Achievements() {
    std::vector<ach::Def> v;
    static std::string descs[sizeof kLevels / sizeof kLevels[0]];
    static std::string hero_desc[9];
    int i = 0;
    for (const Level& l : kLevels) {
        descs[i] = std::string("Complete ") + (std::strncmp(l.name, "The ", 4) ? l.name : std::string("the ") + (l.name + 4)) + ".";
        v.push_back({l.id, l.name, descs[i].c_str()});
        i++;
    }
    for (int h = 0; h < 9; h++) {
        hero_desc[h] = std::string("Complete a level as ") + kHeroes[h] + (h >= 6 ? " (a hero you unlock)." : ".");
        v.push_back({kHeroIds[h], kHeroes[h], hero_desc[h].c_str()});
    }
    v.push_back({"coop", "Side by Side", "Complete a level in co-op."});
    v.push_back({"hero_level_10", "Seasoned Hero", "Raise a hero to level 10."});
    v.push_back({"every_level", "The Return of the King", "Complete every level."});
    v.push_back({"every_level_every_hero", "Every Path Walked", "Complete every level with every hero."});
    v.push_back({"kills_100", "A Hundred Foes", "Defeat 100 enemies."});
    v.push_back({"kills_1000", "A Thousand Foes", "Defeat 1,000 enemies."});
    v.push_back({"kills_10000", "The Host of Mordor", "Defeat 10,000 enemies."});
    return v;
}

// 0x2A1B0, rotk_secrets.cpp: a player's hit that took an enemy's health to 0
void EnemyDefeated() {
    uint64_t n = ach::Add("enemies_defeated", 1);
    if (n >= 100) ach::Unlock("kills_100");
    if (n >= 1000) ach::Unlock("kills_1000");
    if (n >= 10000) ach::Unlock("kills_10000");
}

// GameInfo::file_opened, from rotk_controls.cpp's: the level and hero the Discord status shows, and the progress
// checks, started with the first file
void AchievementsFile(const std::string& n) {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                Check();
            }
        }).detach();
    });
    size_t at = n.find("\\data\\game\\");
    if (at != std::string::npos && n.size() > 4 && n.compare(n.size() - 4, 4, ".scx") == 0) {
        std::string file = n.substr(n.find_last_of('\\') + 1);  // "hel01.scx"
        std::string code = file.substr(0, file.size() - 4);
        std::string name;
        if (code == "fro03") name = "";
        else if (code == "pat02") name = "Paths of the Dead";
        else if (code == "cir01") name = "Cirith Ungol";
        else if (code.rfind("pal", 0) == 0) name = "A secret level";
        else
            for (const Level& l : kLevels)
                if (code == l.code) name = l.name;
        if (code != "globscen") {
            std::lock_guard lock(g_lock);
            g_level = name;
            if (name.empty()) g_hero.clear();
        }
    }
    size_t c = n.find("\\data\\player\\char_");
    if (c != std::string::npos) {
        std::string code = n.substr(c + 18, 3);
        for (int h = 0; h < 9; h++)
            if (code == kHeroCodes[h]) {
                std::lock_guard lock(g_lock);
                g_hero = kHeroes[h];
            }
    }
}

std::string Activity() {
    std::lock_guard lock(g_lock);
    return g_level;
}
std::string ActivityState() {
    std::lock_guard lock(g_lock);
    return g_level.empty() || g_hero.empty() ? std::string() : "As " + g_hero;
}

}  // namespace rotk
