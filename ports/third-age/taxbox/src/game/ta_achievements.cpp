// The Third Age: achievements, the Journey's records and kill deeds, as the other three ports have them.
//
// The story walks nine areas in order, each a scene the game opens (d:\Data\Game\<group>\<scene>.scx; the area names
// from the program's own map names, ta_state.cpp). As in The Fellowship of the Ring's port, a scene of an area opening
// means every area before it was completed, so a save further on earns what it has passed the first time it is played.
// Areas' other scenes (e01c03 and the like) are left as they are.
//
// Enemies defeated: an HP change (0x1F4E30, ta_gameplay.cpp) from a party member to someone outside the party with
// the change's "kill" byte set.
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace {
struct Area {
    const char *scene, *id, *title, *desc;
};
const Area kAreas[] = {
    {"e01c01", "area_eregion", "Eregion", "Come through Eregion."},
    {"e01c02", "area_western_moria", "Western Moria", "Come through Western Moria."},
    {"e01c04", "area_eastern_moria", "Eastern Moria", "Come through Eastern Moria."},
    {"e02c01", "area_eastemnet_gullies", "Eastemnet Gullies", "Come through the Eastemnet Gullies."},
    {"e02c03", "area_plains_of_rohan", "Plains of Rohan", "Cross the Plains of Rohan."},
    {"e02c05", "area_helms_deep", "Helm's Deep", "Hold Helm's Deep."},
    {"e03c01", "area_osgiliath", "Osgiliath", "Come through Osgiliath."},
    {"e03c02", "area_minas_tirith", "Minas Tirith", "Come through Minas Tirith."},
    {"e03c03", "area_pelennor_fields", "Pelennor Fields", "Reach the Pelennor Fields."},
};
constexpr int kCount = int(sizeof kAreas / sizeof kAreas[0]);

int Index(const std::string& scene) {
    for (int i = 0; i < kCount; i++)
        if (scene == kAreas[i].scene) return i;
    return -1;
}
}  // namespace

namespace ta {
std::vector<ach::Def> Achievements() {
    std::vector<ach::Def> v;
    // the last area's own deed is reaching it (its end isn't known yet), the others' leaving them for the next
    for (const Area& a : kAreas) v.push_back({a.id, a.title, a.desc});
    ach::SharedDeeds(v);  // the kill deeds and Fate's
    return v;
}

// ta_gameplay.cpp: a party member's hit that defeated an enemy
void EnemyDefeated() {
    static std::once_flag once;
    std::call_once(once, [] { LOGR("ta: first enemy defeated counted (kill deeds)"); });
    ach::Defeated();
}

// ta_state.cpp's FileOpened: a new scene (lower case, no folder or extension)
void AchievementsScene(const std::string& scene) {
    int i = Index(scene);
    if (i < 0) return;
    static std::mutex m;
    static int last = -1;
    std::lock_guard lock(m);
    if (i == last) return;
    if (last >= 0 && last == i - 1) {  // the area before, completed by leaving it for this one
        ach::LevelDone(kAreas[last].id);
        bool fate_all = true;
        for (int k = 0; k < kCount - 1; k++) fate_all = fate_all && ach::FateDone(kAreas[k].id);
        if (fate_all) ach::Unlock("fate_all");
    }
    last = i;
    ach::LevelStarted(kAreas[i].id);
    for (int k = 0; k < i; k++)
        if (!ach::Has(kAreas[k].id)) {
            LOGR("ta: %s completed", kAreas[k].title);
            ach::Unlock(kAreas[k].id);
        }
    if (i == kCount - 1 && !ach::Has(kAreas[i].id)) {
        LOGR("ta: %s reached", kAreas[i].title);
        ach::Unlock(kAreas[i].id);
    }
}
}  // namespace ta
