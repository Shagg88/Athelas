// The Two Towers: achievements and the Discord status, from the files the game opens (tt_extras.cpp's FileOpened):
// a level is the world it loads (Worlds\<code>.bfa), and it was completed when the end-of-level screen
// (UI\endlvl.BFF, the stats and rating the game shows only after a level is won) opens after it.
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace tt {

namespace {
struct Level {
    const char *code, *name, *id;
};
// the order the game plays them; Orthanc (both parts) is the secret mission
const Level kLevels[] = {
    {"00isul1", "Prologue", "level_prologue"},
    {"01weat1", "Weathertop", "level_weathertop"},
    {"02gate2", "Gates of Moria", "level_gates_of_moria"},
    {"04balin1", "Balin's Tomb", "level_balins_tomb"},
    {"13amon1", "Amon Hen", "level_amon_hen"},
    {"14fang2", "Fangorn Forest", "level_fangorn"},
    {"16rohan1", "Plains of Rohan", "level_plains_of_rohan"},
    {"16rohan2", "The Westfold", "level_westfold"},
    {"16rohan3", "Gap of Rohan", "level_gap_of_rohan"},
    {"17top1", "Helm's Deep: The Deeping Wall", "level_deeping_wall"},
    {"18hole1", "Helm's Deep: Breached Wall", "level_breached_wall"},
    {"20yard1", "Helm's Deep: Hornburg Courtyard", "level_hornburg_courtyard"},
};
constexpr int kMain = int(sizeof kLevels / sizeof kLevels[0]);

std::mutex g_lock;
std::string g_world;  // the level being played (a Level code, or "orth"), empty in the front end

const Level* Find(const std::string& code) {
    for (const Level& l : kLevels)
        if (code == l.code) return &l;
    return nullptr;
}

void Completed(const std::string& code) {
    if (code == "orth") {
        ach::Unlock("secret_mission");
        return;
    }
    const Level* l = Find(code);
    if (!l) return;
    ach::Unlock(l->id);
    int done = 0;
    for (const Level& m : kLevels) done += ach::Has(m.id);
    if (done == kMain) ach::Unlock("every_level");
}
}  // namespace

std::vector<ach::Def> Achievements() {
    static std::string descs[kMain];
    std::vector<ach::Def> v;
    for (int i = 0; i < kMain; i++) {
        descs[i] = std::string("Complete ") + (std::strncmp(kLevels[i].name, "The ", 4) ? kLevels[i].name : std::string("the ") + (kLevels[i].name + 4)) + ".";
        v.push_back({kLevels[i].id, kLevels[i].name, descs[i].c_str()});
    }
    v.push_back({"every_level", "The Two Towers", "Complete every level of the story."});
    v.push_back({"secret_mission", "Tower of Orthanc", "Complete the secret mission."});
    return v;
}

// tt_extras.cpp: each file the game opens, lower case
void AchievementsFile(const std::string& n) {
    auto ends = [&](const char* e) { size_t k = std::strlen(e); return n.size() >= k && n.compare(n.size() - k, k, e) == 0; };
    size_t w = n.find("\\worlds\\");
    if (w != std::string::npos && ends(".bfa")) {
        std::string code = n.substr(w + 8, n.size() - w - 8 - 4);
        if (code.rfind("21orth", 0) == 0) code = "orth";
        std::lock_guard lock(g_lock);
        g_world = Find(code) || code == "orth" ? code : std::string();
        return;
    }
    if (ends("\\ui\\endlvl.bff")) {
        std::string code;
        {
            std::lock_guard lock(g_lock);
            code = g_world;
        }
        if (!code.empty()) {
            LOGR("tt: %s completed", code.c_str());
            Completed(code);
        }
    }
    // the front end's own screens: no level any more (the upgrade screens after a level are the front end too)
    if (n.find("\\ui\\mainmenu") != std::string::npos || n.find("\\ui\\startup") != std::string::npos) {
        std::lock_guard lock(g_lock);
        g_world.clear();
    }
}

std::string Activity() {
    std::lock_guard lock(g_lock);
    if (g_world == "orth") return "Tower of Orthanc";
    const Level* l = Find(g_world);
    return l ? l->name : std::string();
}

}  // namespace tt
