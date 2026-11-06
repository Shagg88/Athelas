// Pads.cpp - controllers of the third and fourth player, read by ROTKPlus through SDL3 (the same SDL3.dll as ROTK Fix)
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// ROTK Fix presents only two virtual controllers to the game. It opens the first two controllers in SDL's list in the
// order it finds them, and the order of that list changes over time (the "second" controller was once the first
// player's Steam Controller). So the extra players take the first controller that nobody has opened yet
// (SDL_GetGamepadFromID returns null), that is, the ones ROTK Fix leaves free; for testing, [Coop] PadName picks the
// third player's controller by name (part of the name, e.g. DualSense).
//
// Input records per player at 0x99CBA0 + n*0x58 (0x49D490(n)): right stick at +0x00/+0x04, left stick at
// +0x10/+0x14 (floats -1..1, like JSPOSX/JSPOSY), buttons at +0x40 (word: OR of 0x99CB60[id] for the pressed
// buttons) and +0x42 (those of the previous frame). Button ids (table 0x569C90): 0..3 d-pad up/down/left/right,
// 4 triangle, 5 cross, 6 square, 7 circle, 8 L1, 9 L2, 10 R1, 11 R2, 12 Start, 13 Select, 14 L3, 15 R3. The mapping from
// Xbox-style buttons is the same as ROTK Fix's ([input_JS_BASE_1] in ROTK.ini): A cross, B circle, X square, Y triangle,
// LB L2, RB R1, LT L1, RT R2, Back Select, Start Start, L3 L3.
// Update: after 0x4AAC10 (axes), called every frame from 0x4ABE67, right after the buttons.
#include "ROTKPlus.h"
#include <string.h>
#include <stdio.h>

typedef uint32_t SDL_JoystickID;
struct SDL_Gamepad;
enum { AX_LX, AX_LY, AX_RX, AX_RY, AX_LT, AX_RT };
enum { BT_SOUTH, BT_EAST, BT_WEST, BT_NORTH, BT_BACK, BT_GUIDE, BT_START, BT_LSTICK, BT_RSTICK, BT_LSHOULDER,
       BT_RSHOULDER, BT_UP, BT_DOWN, BT_LEFT, BT_RIGHT };

static SDL_JoystickID* (__cdecl* pGetGamepads)(int*);
static SDL_Gamepad*    (__cdecl* pOpenGamepad)(SDL_JoystickID);
static bool            (__cdecl* pGamepadConnected)(SDL_Gamepad*);
static int16_t         (__cdecl* pGetAxis)(SDL_Gamepad*, int);
static bool            (__cdecl* pGetButton)(SDL_Gamepad*, int);
static const char*     (__cdecl* pGetName)(SDL_Gamepad*);
static SDL_Gamepad*    (__cdecl* pFromId)(SDL_JoystickID);
static const char*     (__cdecl* pNameForId)(SDL_JoystickID);
static void            (__cdecl* pFree)(void*);
static const char*     (__cdecl* pGetSerial)(SDL_Gamepad*);   // optional: SDL_GetGamepadSerial
static const char*     (__cdecl* pGetPath)(SDL_Gamepad*);     // optional: SDL_GetGamepadPath

// two extra controllers: the third player (input slot 2) and the fourth (slot 3, searched only when 4 players are chosen)
struct ExtraPad { SDL_Gamepad* pad; SDL_JoystickID id; DWORD nextScan; uint16_t lastBits; };
static ExtraPad g_ep[2];
static char     g_padName[64];    // [Coop] PadName: pick the third player's controller by name (testing), empty = first free one
static const uintptr_t kRecordBase = 0x99CBA0;
static const uintptr_t kButtonBit = 0x99CB60;   // uint16 per id
static const uintptr_t kUpdateAxes = 0x4AAC10;

static bool LoadSdl()
{
    HMODULE m = GetModuleHandleA("SDL3.dll");
    if (!m) return false;
    pGetGamepads      = (decltype(pGetGamepads))GetProcAddress(m, "SDL_GetGamepads");
    pOpenGamepad      = (decltype(pOpenGamepad))GetProcAddress(m, "SDL_OpenGamepad");
    pGamepadConnected = (decltype(pGamepadConnected))GetProcAddress(m, "SDL_GamepadConnected");
    pGetAxis          = (decltype(pGetAxis))GetProcAddress(m, "SDL_GetGamepadAxis");
    pGetButton        = (decltype(pGetButton))GetProcAddress(m, "SDL_GetGamepadButton");
    pGetName          = (decltype(pGetName))GetProcAddress(m, "SDL_GetGamepadName");
    pFree             = (decltype(pFree))GetProcAddress(m, "SDL_free");
    pFromId           = (decltype(pFromId))GetProcAddress(m, "SDL_GetGamepadFromID");
    pNameForId        = (decltype(pNameForId))GetProcAddress(m, "SDL_GetGamepadNameForID");
    pGetSerial        = (decltype(pGetSerial))GetProcAddress(m, "SDL_GetGamepadSerial");
    pGetPath          = (decltype(pGetPath))GetProcAddress(m, "SDL_GetGamepadPath");
    return pGetGamepads && pOpenGamepad && pGamepadConnected && pGetAxis && pGetButton && pGetName && pFree &&
           pFromId && pNameForId;
}

// Looks for the extra player's controller (slot 2 or 3), at most once a second until it is found.
static void FindPad(int slot)
{
    ExtraPad& e = g_ep[slot - 2];
    if (e.pad && pGamepadConnected(e.pad)) return;
    if (e.pad) { Log("pad of player %d disconnected", slot + 1); e.pad = nullptr; }
    const DWORD now = GetTickCount();
    if (now < e.nextScan) return;
    e.nextScan = now + 1000;
    if (!pGetGamepads && !LoadSdl()) return;
    int n = 0;
    SDL_JoystickID* ids = pGetGamepads(&n);
    static int listed;   // the list of controllers SDL sees, the first few times
    if (ids && listed++ < 3)
        for (int i = 0; i < n; ++i)
            Log("pad: SDL sees controller %d \"%s\" (id %u, %s)", i + 1, pNameForId(ids[i]) ? pNameForId(ids[i]) : "?",
                ids[i], pFromId(ids[i]) ? "already open" : "free");
    const char* want = slot == 2 ? g_padName : "";
    for (int i = 0; ids && i < n && !e.pad; ++i) {
        const char* name = pNameForId(ids[i]);
        if (want[0] ? !(name && strstr(name, want)) : pFromId(ids[i]) != nullptr) continue;
        e.id = ids[i];
        e.pad = pOpenGamepad(e.id);
        if (e.pad) Log("pad of player %d: \"%s\" (%d controllers connected%s)", slot + 1, name ? name : "?", n,
                       want[0] ? ", chosen by name" : ", the first free one");
        if (e.pad) PlayersPadOpened(slot, PadKeyOfSlot(slot));   // a known controller brings back its player's name
    }
    if (ids) pFree(ids);
}

// A key that stays the same for a controller (Players.cpp remembers names by it): SDL's serial number if the controller
// has one (DualSense, DualShock 4, Switch Pro), otherwise its device path (the same as long as it is plugged into the same
// port or paired the same way). Hashed (FNV-1a, 64 bit), because paths have characters an ini key cannot hold. "" if the
// slot has no controller.
std::string PadKeyOfSlot(int slot)
{
    if (slot < 2 || slot > 3) return "";
    SDL_Gamepad* pad = g_ep[slot - 2].pad;
    if (!pad) return "";
    const char* serial = pGetSerial ? pGetSerial(pad) : nullptr;
    const char* path = pGetPath ? pGetPath(pad) : nullptr;
    const char* src = serial && serial[0] ? serial : path && path[0] ? path : nullptr;
    if (!src) return "";
    uint64_t h = 1469598103934665603ull;
    for (const char* p = src; *p; ++p) { h ^= (uint8_t)*p; h *= 1099511628211ull; }
    char key[32];
    sprintf_s(key, "%s%016llx", serial && serial[0] ? "S" : "P", (unsigned long long)h);
    return key;
}

static float Axis(SDL_Gamepad* pad, int a)
{
    const int v = pGetAxis(pad, a);
    return v < 0 ? v / 32768.0f : v / 32767.0f;
}

// The shoulder buttons of every open controller (ROTK Fix's two and ours), for the pages of the main menu
// (Players.cpp): on SteamOS the game's input records had no shoulder buttons in the menus (test of
// 04/11/2026). A button counts when it goes down; each controller is remembered by its SDL id.
int PadsShoulder(bool& sdl)
{
    sdl = false;
    if (!pGetGamepads && !LoadSdl()) return 0;
    struct Seen { SDL_JoystickID id; uint8_t held; };
    static Seen was[8];
    Seen now[8] = {};
    int n = 0, dir = 0, kept = 0;
    SDL_JoystickID* ids = pGetGamepads(&n);
    if (!ids) return 0;
    sdl = true;
    for (int i = 0; i < n && kept < 8; ++i) {
        SDL_Gamepad* p = pFromId(ids[i]);
        if (!p) continue;
        uint8_t held = 0;
        if (pGetButton(p, BT_LSHOULDER) || Axis(p, AX_LT) > 0.5f) held |= 1;
        if (pGetButton(p, BT_RSHOULDER) || Axis(p, AX_RT) > 0.5f) held |= 2;
        uint8_t before = 0;
        for (const Seen& s : was) if (s.id == ids[i]) before = s.held;
        const uint8_t down = held & ~before;
        if (down & 1) dir = -1;
        else if (down & 2) dir = 1;
        now[kept++] = Seen{ ids[i], held };
    }
    pFree(ids);
    memcpy(was, now, sizeof was);
    return dir;
}

static void UpdatePad(int slot)
{
    FindPad(slot);
    ExtraPad& e = g_ep[slot - 2];
    SDL_Gamepad* pad = e.pad;
    uint8_t* rec = (uint8_t*)(kRecordBase + slot * 0x58);
    if (!pad) {
        // No controller: unless [Coop] KeyboardFallback=1, the extra player stands still. The game feeds the keyboard
        // (player 1's keys, copied for the extra players' input sections) into every player without a pad, which made one
        // person drive several characters at once.
        if (!g_cfg.kbFallback) {
            memset(rec, 0, 0x20);
            *(uint16_t*)(rec + 0x40) = 0;
            *(uint16_t*)(rec + 0x42) = e.lastBits;
            e.lastBits = 0;
        }
        return;
    }
    // sticks with a 12% radial dead zone (like ROTK Fix)
    for (int s = 0; s < 2; ++s) {
        float x = Axis(pad, s ? AX_RX : AX_LX), y = Axis(pad, s ? AX_RY : AX_LY);
        const float m2 = x * x + y * y;
        if (m2 < 0.12f * 0.12f) x = y = 0.0f;
        float* out = (float*)(rec + (s ? 0x00 : 0x10));
        out[0] = x; out[1] = y;
    }
    struct { int sdl; int id; } const kMap[] = {
        { BT_SOUTH, 5 }, { BT_EAST, 7 }, { BT_WEST, 6 }, { BT_NORTH, 4 }, { BT_LSHOULDER, 9 }, { BT_RSHOULDER, 10 },
        { BT_BACK, 13 }, { BT_START, 12 }, { BT_LSTICK, 14 }, { BT_RSTICK, 15 },
        { BT_UP, 0 }, { BT_DOWN, 1 }, { BT_LEFT, 2 }, { BT_RIGHT, 3 },
    };
    const uint16_t* bit = (const uint16_t*)kButtonBit;
    auto bitOf = [&](int id) { return bit[id] ? bit[id] : (uint16_t)(1u << id); };
    uint16_t bits = 0;
    for (const auto& m : kMap)
        if (pGetButton(pad, m.sdl)) bits |= bitOf(m.id);
    if (Axis(pad, AX_LT) > 0.30f) bits |= bitOf(8);    // LT -> L1
    if (Axis(pad, AX_RT) > 0.30f) bits |= bitOf(11);   // RT -> R2
    // +0x1C (and +0x0C for the right stick): digital directions. With the keyboard the game turns it on and the
    // character ignores the stick (0x4453A0 uses the stick only if +0x1C is 0): with a pad it must be off
    rec[0x1C] = 0;
    rec[0x0C] = 0;
    *(uint16_t*)(rec + 0x42) = e.lastBits;
    *(uint16_t*)(rec + 0x40) = bits;
    e.lastBits = bits;
}

void SupportFrame();   // Support.cpp
void ScreensFrame();   // Screens.cpp
void PvpFrame();       // Pvp.cpp

static bool RealPad(int slot);

static void __cdecl UpdateAxesAndPads()
{
    ((void(__cdecl*)())kUpdateAxes)();
    UpdatePad(2);
    if (CoopPlayers() >= 4) UpdatePad(3);   // the fourth controller is opened only if needed
    SimFrame(&RealPad);
    SupportFrame();
    ScreensFrame();
    PvpFrame();
}

static bool RealPad(int slot) { return (slot == 2 || slot == 3) && g_ep[slot - 2].pad != nullptr; }
// with the keyboard-simulated pads (Sim.cpp) the extra players count as having a pad
bool PadConnected(int slot)
{
    return RealPad(slot) || (SimActive() && (slot == 2 || slot == 3) && CoopPlayers() >= slot + 1);
}

bool PadsInstall(const char* padName)
{
    strncpy_s(g_padName, padName ? padName : "", _TRUNCATE);
    const bool ok = PatchCall(0x4ABE67, kUpdateAxes, (const void*)&UpdateAxesAndPads);
    Log("pads of players 3 and 4: %s (%s%s)", ok ? "active" : "NOT installed",
        g_padName[0] ? "third player chosen by name: " : "first free controller", g_padName);
    return ok;
}
