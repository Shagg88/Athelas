// ROTK Xbox: the controller. [input] second_controller off leaves player 2's port empty.
//
// The buttons are remapped by the shared [controls] settings, as in the other ports. Up to Athelas 1.5 the Xbox port
// read ROTK Fix's layout from ROTKFix.ini instead ([GamepadButtons]: each of the game's virtual pad buttons, "slots",
// named by what it does, driven by a physical button named by Xbox position); ImportControls brings player 1's layout
// over into [controls] once (rotk_settings.cpp). On the Xbox pad LB is the white button and RB the black one.
#include <atomic>
#include <cctype>
#include <cstdint>
#include <string>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace rotk {
bool g_second_controller = true;

// ROTKFix.ini [GamepadButtons]: get(section, key) gives the value or null. Each of the eight Xbox buttons the game
// reads (as [controls] orders them: A B X Y Black White LT RT) by its ROTK Fix name; its source by input::kSources'
// names ("A" .. "RT", "LS", "RS"). ROTK Fix's Special (the left stick click's slot) and the Back, Start and d-pad
// sources have no [controls] counterpart and are left as they are.
void ImportControls(const std::string* (*get)(void*, const char*, const char*), void* ctx) {
    static const char* const kSlots[8] = {"SpeedAttack", "PhysicalAttack", "Parry", "FierceAttack",
                                          "Action", "JumpBack", "RangedAttack", "KillingMove"};
    app::Settings& s = app::Get();
    std::string list;
    for (int i = 0; i < 8; i++) {
        const std::string* v = get(ctx, "GamepadButtons", kSlots[i]);
        if (!v) continue;
        bool found = false;
        for (uint32_t k = 0; k + 1 < input::kSourceCount && !found; k++) {
            const char* n = input::kSources[k];
            size_t j = 0;
            while (j < v->size() && n[j] && ((*v)[j] | 0x20) == (n[j] | 0x20)) j++;
            if (j == v->size() && !n[j]) {
                s.buttons[i] = k;
                found = true;
                list += std::string(" ") + kSlots[i] + "=" + n;
            }
        }
        if (!found) LOGW("rotk: %s on \"%s\" can't be brought over, it keeps its own button", kSlots[i], v->c_str());
    }
    if (!list.empty()) LOGR("rotk: ROTK Fix's button layout brought over:%s", list.c_str());
}

void SkipPress(int port, input::Pad& pad);  // rotk_movies.cpp: skip_logos

// GameInfo::pad: a polled pad before the game sees it
void Pad(int port, input::Pad& pad) {
    if (port == 1 && !g_second_controller) pad = input::Pad{};
    SkipPress(port, pad);
}

// the front end (title, menus, the upgrade shop) or a level, from the scene the game loads last:
// Data\Game\Fro\Fro03.scx is the front end, Data\Game\<abc>\<Abc01>.scx a level (GlobScen.scx, beside them, is
// loaded with either). For the keyboard: in the front end's menus Esc goes back and Enter presses Start, which they
// take to go on ("press start" takes nothing else).
std::atomic<bool> g_front_end{true};
void AchievementsFile(const std::string& lower_name);  // rotk_achievements.cpp
void LangFile(const std::string& lower_name);  // rotk_lang.cpp
void FileOpened(const char* name) {
    std::string n = name;
    for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    AchievementsFile(n);
    LangFile(n);
    size_t at = n.find("\\data\\game\\");
    if (at == std::string::npos || n.size() < 4 || n.compare(n.size() - 4, 4, ".scx") != 0) return;
    std::string rest = n.substr(at + 11);  // "fro\fro03.scx", "globscen.scx"
    size_t slash = rest.find('\\');
    if (slash == std::string::npos) return;
    bool fe = rest.compare(0, slash, "fro") == 0;
    if (g_front_end.exchange(fe) != fe) LOGI("rotk: %s", fe ? "front end" : "in a level");
}
bool InMenus() { return g_front_end.load(); }

}  // namespace rotk
