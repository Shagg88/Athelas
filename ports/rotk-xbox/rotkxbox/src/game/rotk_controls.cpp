// ROTK Xbox: ROTK Fix's controller settings (ROTKFix.ini [Gamepad], [GamepadButtons], [GamepadButtons2]).
//
// ROTK Fix gives the PC game a virtual pad whose buttons ("slots") the player drives with any physical button,
// named by Xbox position: SpeedAttack is the A slot, PhysicalAttack B, Parry X, FierceAttack Y, JumpBack LB,
// Action RB, Special LS (left stick click), RangedAttack LT, KillingMove RT. The same slots are remapped here, on the
// pad the Xbox game reads, so a layout made in the launcher moves the same buttons on both versions. On the Xbox pad
// LB is the white button and RB the black one (the SDK's mapping). The labels are ROTK Fix's names for the slots.
//
// SecondController = 0 leaves player 2's port empty. Enabled (the PC's DirectInput gamepad layer) has no use here:
// on the Xbox version the controller is the game's own input.
#include <atomic>
#include <cctype>
#include <cstdint>
#include <string>

#include "core/input.h"
#include "core/log.h"

namespace rotk {

// the physical controls by Xbox position, as ROTKFix.ini names them
enum Phys { kA, kB, kX, kY, kLB, kRB, kBack, kStart, kLS, kRS, kLT, kRT, kUp, kDown, kLeft, kRight, kPhysCount };
const char* const kPhysNames[kPhysCount] = {"A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS", "LT", "RT",
                                            "Up", "Down", "Left", "Right"};
// the remappable actions and the slot each one is
struct Action {
    const char* key;
    int slot;
};
const Action kActions[] = {{"SpeedAttack", kA}, {"PhysicalAttack", kB}, {"Parry", kX},       {"FierceAttack", kY},
                           {"JumpBack", kLB},   {"Action", kRB},        {"Special", kLS},    {"RangedAttack", kLT},
                           {"KillingMove", kRT}};
constexpr int kActionCount = int(sizeof kActions / sizeof kActions[0]);

// per player: the physical control driving each action's slot; default, the slot's own button
int g_source[2][kActionCount] = {{kA, kB, kX, kY, kLB, kRB, kLS, kLT, kRT}, {kA, kB, kX, kY, kLB, kRB, kLS, kLT, kRT}};
bool g_remapped[2] = {false, false};
bool g_second_controller = true;

namespace {
// a control's value on the Xbox pad, 0 to 255
uint8_t Read(const input::Pad& p, int c) {
    switch (c) {
        case kA: return p.analog[0];
        case kB: return p.analog[1];
        case kX: return p.analog[2];
        case kY: return p.analog[3];
        case kRB: return p.analog[4];  // black
        case kLB: return p.analog[5];  // white
        case kLT: return p.analog[6];
        case kRT: return p.analog[7];
        case kUp: return p.buttons & 0x01 ? 255 : 0;
        case kDown: return p.buttons & 0x02 ? 255 : 0;
        case kLeft: return p.buttons & 0x04 ? 255 : 0;
        case kRight: return p.buttons & 0x08 ? 255 : 0;
        case kStart: return p.buttons & 0x10 ? 255 : 0;
        case kBack: return p.buttons & 0x20 ? 255 : 0;
        case kLS: return p.buttons & 0x40 ? 255 : 0;
        case kRS: return p.buttons & 0x80 ? 255 : 0;
    }
    return 0;
}
void Write(input::Pad& p, int c, uint8_t v) {
    auto bit = [&](uint16_t b) { p.buttons = v >= 128 ? uint16_t(p.buttons | b) : uint16_t(p.buttons & ~b); };
    switch (c) {
        case kA: p.analog[0] = v; break;
        case kB: p.analog[1] = v; break;
        case kX: p.analog[2] = v; break;
        case kY: p.analog[3] = v; break;
        case kRB: p.analog[4] = v; break;
        case kLB: p.analog[5] = v; break;
        case kLT: p.analog[6] = v; break;
        case kRT: p.analog[7] = v; break;
        case kLS: bit(0x40); break;
        case kRS: bit(0x80); break;
    }
}
}  // namespace

int PhysFromName(const std::string& v) {
    for (int i = 0; i < kPhysCount; i++) {
        const char* n = kPhysNames[i];
        size_t k = 0;
        while (k < v.size() && n[k] && ((v[k] | 0x20) == (n[k] | 0x20))) k++;
        if (k == v.size() && !n[k]) return i;
    }
    return -1;
}

// GameInfo::pad: a polled pad before the game sees it
void Pad(int port, input::Pad& pad) {
    if (port == 1 && !g_second_controller) {
        pad = input::Pad{};
        return;
    }
    if (port > 1 || !g_remapped[port]) return;
    const input::Pad in = pad;
    uint8_t v[kActionCount];
    for (int a = 0; a < kActionCount; a++) v[a] = Read(in, g_source[port][a]);
    for (int a = 0; a < kActionCount; a++) Write(pad, kActions[a].slot, 0);
    // a physical control that drives no slot any more stops doing its own job; one driving a slot does that slot's
    for (int a = 0; a < kActionCount; a++)
        if (v[a] > Read(pad, kActions[a].slot)) Write(pad, kActions[a].slot, v[a]);
}

// the front end (title, menus, the upgrade shop) or a level, from the scene the game loads last:
// Data\Game\Fro\Fro03.scx is the front end, Data\Game\<abc>\<Abc01>.scx a level (GlobScen.scx, beside them, is
// loaded with either). For the keyboard: in the front end's menus Esc goes back and Enter presses Start, which they
// take to go on ("press start" takes nothing else).
std::atomic<bool> g_front_end{true};
void AchievementsFile(const std::string& lower_name);  // rotk_achievements.cpp
void FileOpened(const char* name) {
    std::string n = name;
    for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    AchievementsFile(n);
    size_t at = n.find("\\data\\game\\");
    if (at == std::string::npos || n.size() < 4 || n.compare(n.size() - 4, 4, ".scx") != 0) return;
    std::string rest = n.substr(at + 11);  // "fro\fro03.scx", "globscen.scx"
    size_t slash = rest.find('\\');
    if (slash == std::string::npos) return;
    bool fe = rest.compare(0, slash, "fro") == 0;
    if (g_front_end.exchange(fe) != fe) LOGI("rotk: %s", fe ? "front end" : "in a level");
}
bool InMenus() { return g_front_end.load(); }

// ROTKFix.ini: get(section, key) gives the value or null
void LoadControls(const std::string* (*get)(void*, const char*, const char*), void* ctx) {
    for (int p = 0; p < 2; p++) {
        const char* section = p ? "GamepadButtons2" : "GamepadButtons";
        std::string list;
        for (int a = 0; a < kActionCount; a++) {
            int src = p ? g_source[0][a] : kActions[a].slot;  // player 2 without a layout of its own uses player 1's
            if (const std::string* v = get(ctx, section, kActions[a].key)) {
                int c = PhysFromName(*v);
                if (c >= 0) src = c;
                else LOGW("rotk: unknown button \"%s\" for %s, keeping %s", v->c_str(), kActions[a].key, kPhysNames[src]);
            }
            g_source[p][a] = src;
            if (src != kActions[a].slot) {
                g_remapped[p] = true;
                list += std::string(" ") + kActions[a].key + "=" + kPhysNames[src];
            }
        }
        if (g_remapped[p]) LOGR("rotk: player %d buttons remapped:%s", p + 1, list.c_str());
    }
}
}  // namespace rotk
