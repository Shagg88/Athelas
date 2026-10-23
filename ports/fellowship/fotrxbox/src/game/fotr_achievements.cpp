// FOTR Xbox: achievements, from the files the game opens (GameInfo::file_opened, via fotr_movies.cpp's FileOpened).
// The story goes through eight regions in order (Scenes\<region>\<scene>): a scene of a region opening
// (Scenes\<region>\<scene>\Media\Textures.xpr) means every region before it was completed, so a save further on earns
// what it has passed the first time it is played. Rivendell's combat ranges are left out (practice, not the story).
// The story is complete when its last film, Movies\English\fmv_6.xmv, plays (Anduin_aragorn's script starts it).
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace {
struct Region {
    const char *folder, *id, *title, *desc;
};
const Region kRegions[] = {
    {"shire", "region_shire", "The Shire", "Leave the Shire."},
    {"old_forest", "region_old_forest", "The Old Forest", "Come through the Old Forest and the Barrow-downs."},
    {"bree", "region_bree", "Bree", "Leave Bree with Strider."},
    {"weathertop", "region_weathertop", "Weathertop", "Come through Weathertop and the Trollshaws."},
    {"rivendell", "region_rivendell", "Rivendell", "Set out from Rivendell."},
    {"moria", "region_moria", "The Mines of Moria", "Come through the Mines of Moria."},
    {"lothlorien", "region_lothlorien", "Lothlórien", "Leave Lothlórien."},
};
constexpr int kCount = int(sizeof kRegions / sizeof kRegions[0]);
constexpr const char* kStory = "story_complete";

std::mutex g_lock;
std::string g_region;  // the region being played, lower case

int Index(const std::string& folder) {
    for (int i = 0; i < kCount; i++)
        if (folder == kRegions[i].folder) return i;
    return folder == "river_anduin" ? kCount : -1;
}
}  // namespace

namespace fotr {
// GameInfo::activity, for the Discord status as the other ports have it: the region being played ("Bree")
std::string Activity() {
    std::lock_guard lock(g_lock);
    if (g_region == "river_anduin") return "The River Anduin";
    int i = Index(g_region);
    return i >= 0 && i < kCount ? kRegions[i].title : std::string();
}

std::vector<ach::Def> Achievements() {
    std::vector<ach::Def> v;
    for (const Region& r : kRegions) v.push_back({r.id, r.title, r.desc});
    v.push_back({kStory, "The Fellowship of the Ring", "Complete the story."});
    return v;
}

// each file the game opens
void AchievementsFile(const char* name) {
    std::string n = name;
    for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    size_t s = n.find("\\scenes\\");
    if (s != std::string::npos && n.size() > 13 && n.compare(n.size() - 13, 13, "\\textures.xpr") == 0) {
        size_t a = s + 8, b = n.find('\\', a);
        if (b == std::string::npos) return;
        std::string region = n.substr(a, b - a);
        size_t c = n.find('\\', b + 1);
        std::string scene = c == std::string::npos ? std::string() : n.substr(b + 1, c - b - 1);
        int i = Index(region);
        if (i < 0 || scene.rfind("combatrange", 0) == 0) return;
        {
            std::lock_guard lock(g_lock);
            if (g_region == region) return;
            g_region = region;
        }
        for (int k = 0; k < i; k++)
            if (!ach::Has(kRegions[k].id)) {
                LOGR("fotr: %s completed", kRegions[k].title);
                ach::Unlock(kRegions[k].id);
            }
        return;
    }
    if (n.size() >= 18 && n.compare(n.size() - 18, 18, "\\english\\fmv_6.xmv") == 0) {
        {
            std::lock_guard lock(g_lock);
            if (g_region != "river_anduin") return;
        }
        if (!ach::Has(kStory)) LOGR("fotr: the story completed");
        for (const Region& r : kRegions) ach::Unlock(r.id);
        ach::Unlock(kStory);
    }
}
}  // namespace fotr
