// The Two Towers: the game's named developer settings ("CAMERA/Max Follow Distance", "GRAPHICS/Fog/Enable" ...).
// Each is registered through 0xE4870 (name, type, data, size): type 1 an int (0xE4D70), 2 a float (0xE4DC0), 3 a
// bool byte (0xE4E00); data starts with the guest address of the value. 0xE4E60 (name, address) points a setting
// at a new object's field (a new level's camera). Names group by '/', a folder per part.
//
// Settings that use them (game_info.cpp):
//   [gameplay] damage_taken       "DIFFICULTY/Good Guy Easy|Normal|Hard" (0.7, 1, 1.3): the damage your side takes
//   [gameplay] enemy_damage       "DIFFICULTY/Bad Guy Easy|Normal|Hard" (1.3, 1, 0.7): the damage enemies take
//   The two damage settings are percents of the chosen difficulty's own (100 leaves it as it is).
//
// "GRAPHICS/Floor reflections" is not a setting: each level's script turns the mirrored floor on or off with its own
// mirror height (0x8D2484 -> +0x480D on, +0x4810 height, set at 0xB24C0). Orthanc has it on already; forcing it on
// elsewhere drew a hidden pass that showed nothing (Balin's Tomb), so it was dropped.
//
// Test switches, for finding out what the settings do:
//   TT_DEV_LOG=1                 log each registration with its value, and every value again 20 s in
//   TT_DEV="Name=v;Name=v"       hold those settings at those values (written each frame)
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
struct Known {
    std::string name;
    uint32_t type = 0, addr = 0;
    float original = 0;  // a float's value as the game set it, before any change here
};
// a setting registered on a record that isn't there yet (a null base plus the field's offset, seen as 0x0C when the
// prologue is loaded for Gimli) has no value to read
bool Readable(uint32_t addr) { return addr >= 0x10000; }
float ReadFloat(uint32_t addr) {
    float f = 0;
    if (Readable(addr)) std::memcpy(&f, ::kernel::HostPtr(addr), 4);
    return f;
}
std::vector<Known> g_known;

std::string Value(uint32_t type, uint32_t addr) {
    if (!Readable(addr)) return "?";
    char b[64];
    if (type == 2) {
        float f;
        std::memcpy(&f, ::kernel::HostPtr(addr), 4);
        std::snprintf(b, sizeof(b), "%g", f);
    } else if (type == 3) {
        std::snprintf(b, sizeof(b), "%u", *static_cast<uint8_t*>(::kernel::HostPtr(addr)));
    } else {
        std::snprintf(b, sizeof(b), "%d", int32_t(::kernel::R32(addr)));
    }
    return b;
}

bool Log() {
    static const bool on = std::getenv("TT_DEV_LOG") != nullptr;
    return on;
}

Known* Find(const std::string& n) {
    for (auto& k : g_known)
        if (k.name == n) return &k;
    return nullptr;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000E4870);
X86_FUNC(sub_000E4870) {
    uint32_t name = ::kernel::R32(ctx.esp + 4), type = ::kernel::R32(ctx.esp + 8), data = ::kernel::R32(ctx.esp + 12);
    __imp__sub_000E4870(ctx, base);
    if (!name || !data || type < 1 || type > 3) return;
    std::string n = static_cast<const char*>(::kernel::HostPtr(name));
    while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.pop_back();
    uint32_t addr = ::kernel::R32(data);
    if (Known* k = Find(n)) {
        k->addr = addr;
        k->original = type == 2 ? ReadFloat(addr) : 0;
    } else {
        g_known.push_back({n, type, addr, type == 2 ? ReadFloat(addr) : 0});
    }
    if (Log()) LOGI("dev: + %s (type %u) at %08x = %s", n.c_str(), type, addr, Value(type, addr).c_str());
}

X86_EXTERN_FUNC(__imp__sub_000E4E60);
X86_FUNC(sub_000E4E60) {
    uint32_t name = ::kernel::R32(ctx.esp + 4), addr = ::kernel::R32(ctx.esp + 8);
    __imp__sub_000E4E60(ctx, base);
    if (!name) return;
    std::string n = static_cast<const char*>(::kernel::HostPtr(name));
    while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.pop_back();
    if (Known* k = Find(n)) {
        k->addr = addr;
        if (k->type == 2) k->original = ReadFloat(addr);
        if (Log()) LOGI("dev: > %s now at %08x = %s", n.c_str(), addr, Value(k->type, addr).c_str());
    }
}

namespace tt {
uint32_t g_damage_taken = 100;     // [gameplay] damage_taken, percent
uint32_t g_enemy_damage = 100;     // [gameplay] enemy_damage, percent
}

namespace {
// each frame: the in-game menu may change the percent while playing, back to 100 too
void Scale(const char* side, uint32_t percent) {
    static bool touched[2] = {};
    bool& t = touched[side[0] == 'B'];
    if (percent == 100 && !t) return;
    t = true;
    for (const char* level : {"Easy", "Normal", "Hard"}) {
        Known* k = Find(std::string("DIFFICULTY/") + side + " " + level);
        if (!k || !Readable(k->addr) || k->type != 2) continue;
        float f = k->original * float(percent) / 100.0f;
        std::memcpy(::kernel::HostPtr(k->addr), &f, 4);
    }
}
}  // namespace

namespace tt {
// once a frame (tt_extras.cpp)
void DevSettingsFrame() {
    // (Virtues, opt-in: the launcher's bonus on top)
    Scale("Good Guy", std::clamp(g_damage_taken * ach::VirtueTaken() / 100, 10u, 500u));
    Scale("Bad Guy", std::clamp(g_enemy_damage * ach::VirtueDealt() / 100, 10u, 500u));
    static uint64_t frames = 0;
    frames++;
    if (Log() && frames % 600 == 0) {
        uint32_t o = ::kernel::R32(0x8D2484);
        if (o) LOGI("dev: reflection object %08x: plane %g on %u", o, ReadFloat(o + 0x4810), *static_cast<uint8_t*>(::kernel::HostPtr(o + 0x480D)));
    }
    if (Log() && frames == 600) {
        for (auto& k : g_known) LOGI("dev: = %s = %s", k.name.c_str(), Value(k.type, k.addr).c_str());
    }
    static const std::vector<std::pair<std::string, std::string>> holds = [] {
        std::vector<std::pair<std::string, std::string>> v;
        const char* e = std::getenv("TT_DEV");
        std::string s = e ? e : "";
        size_t i = 0;
        while (i < s.size()) {
            size_t j = s.find(';', i);
            if (j == std::string::npos) j = s.size();
            std::string item = s.substr(i, j - i);
            size_t eq = item.find('=');
            if (eq != std::string::npos) v.push_back({item.substr(0, eq), item.substr(eq + 1)});
            i = j + 1;
        }
        return v;
    }();
    for (auto& [n, val] : holds) {
        Known* k = Find(n);
        if (!k || !Readable(k->addr)) continue;
        if (k->type == 2) {
            float f = float(std::atof(val.c_str()));
            std::memcpy(::kernel::HostPtr(k->addr), &f, 4);
        } else if (k->type == 3) {
            *static_cast<uint8_t*>(::kernel::HostPtr(k->addr)) = uint8_t(std::atoi(val.c_str()));
        } else {
            ::kernel::W32(k->addr, uint32_t(std::atoi(val.c_str())));
        }
    }
}
}  // namespace tt
