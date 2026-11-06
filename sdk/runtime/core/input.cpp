// controllers through SDL: Xbox, PlayStation and Switch pads read into the console controller's report
#include "core/input.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>

#include <SDL3/SDL.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "app/app.h"
#include "core/keys.h"
#include "core/log.h"

namespace input {

namespace {

// [input] stick_deadzone: a stick inside the circle reads centred, the rest is stretched to the full range again so
// a small push still moves; out of range or 0 leaves the stick as it is
void Deadzone(int& x, int& y, uint32_t pct) {
    if (!pct || pct >= 100) return;
    float dz = pct / 100.0f;
    float fx = x / 32767.0f, fy = y / 32767.0f;
    float mag = std::sqrt(fx * fx + fy * fy);
    if (mag <= dz) {
        x = y = 0;
        return;
    }
    float k = (mag - dz) / (1 - dz) / mag;  // corners past the circle stay at the corners
    x = std::clamp(int(std::lround(fx * k * 32767)), -32768, 32767);
    y = std::clamp(int(std::lround(fy * k * 32767)), -32768, 32767);
}

// [input] trigger_threshold: below it released, above it stretched to the full 0 to 255 again
uint8_t Trigger(int raw, uint32_t pct) {
    int v = std::max(raw, 0) >> 7;
    if (!pct) return uint8_t(v);
    int t = int(std::min(pct, 90u) * 255 / 100);
    return v <= t ? 0 : uint8_t(std::min(255, (v - t) * 255 / (255 - t)));
}

// a controller takes the lowest free port when it is plugged in and keeps it until it is unplugged
std::mutex g_lock;
bool g_sdl = false;
bool g_wine = false;   // Init: running under Wine / Proton
bool g_steam_launched = false;   // Init: Steam started the game with Steam Input (Wine/Proton)
SDL_Gamepad* g_pads[4] = {};

// a pad that can stand in for Steam's virtual one (above)
bool IsSubstitute(SDL_Gamepad* p) {
    const char* path = SDL_GetGamepadPath(p);
    uint16_t v = SDL_GetGamepadVendor(p), pr = SDL_GetGamepadProduct(p);
    bool xin = path && std::strncmp(path, "XInput", 6) == 0;
    return (v == 0x28DE && pr == 0x11FF) || (g_steam_launched && xin && v != 0x28DE);
}
bool AnySubstitute() {
    for (SDL_Gamepad* p : g_pads)
        if (p && IsSubstitute(p)) return true;
    return false;
}

void AddPad(SDL_JoystickID id) {
    for (SDL_Gamepad* pad : g_pads)
        if (pad && SDL_GetGamepadID(pad) == id) return;
    // under Wine DirectInput is on (Init) and Wine's DirectInput also lists Xbox pads: the same pad seen twice (as
    // XInput and as DirectInput) would play two players at once. The XInput one is kept (review, 5 Nov).
    // Steam (Steam Deck, SteamOS, Linux): when Steam starts the game with Steam Input, it gives it a virtual Xbox pad
    // and expects the real controls (the Deck's own, a Steam Controller: vendor 28DE) left alone; SDL under Wine can't
    // tell the virtual pad apart, so one Deck could be two players. A real Steam controller is left out only while a
    // pad that stands in for it is open (Steam's virtual one: 28DE:11FF, or under Steam an XInput pad of another
    // vendor), and comes back when that one goes (review: never on Steam's settings alone - no controller at all).
    if (g_wine) {
        uint16_t v0 = SDL_GetGamepadVendorForID(id), p0 = SDL_GetGamepadProductForID(id);
        if (v0 == 0x28DE && p0 != 0x11FF && AnySubstitute()) {
            const char* n0 = SDL_GetGamepadNameForID(id);
            LOGR("input: %s left out: Steam Input gives it as its virtual pad (Wine/Proton)", n0 ? n0 : "a Steam controller");
            return;
        }
        const char* p = SDL_GetGamepadPathForID(id);
        bool xin = p && std::strncmp(p, "XInput", 6) == 0;
        uint16_t v = SDL_GetGamepadVendorForID(id), pr = SDL_GetGamepadProductForID(id);
        for (SDL_Gamepad*& pad : g_pads) {
            if (!pad || SDL_GetGamepadVendor(pad) != v || SDL_GetGamepadProduct(pad) != pr) continue;
            const char* op = SDL_GetGamepadPath(pad);
            bool oxin = op && std::strncmp(op, "XInput", 6) == 0;
            if (oxin && !xin) { LOGR("input: %s left out: the same pad as controller %d (Wine)", p ? p : "a pad", int(&pad - g_pads) + 1); return; }
            if (xin && !oxin) { SDL_CloseGamepad(pad); pad = nullptr; }   // the XInput one takes its place
        }
    }
    SDL_Gamepad** slot = std::find(std::begin(g_pads), std::end(g_pads), nullptr);
    if (slot == std::end(g_pads)) return;
    *slot = SDL_OpenGamepad(id);
    if (g_wine && *slot && IsSubstitute(*slot))   // the stand-in arrives: a real Steam controller read before gives way
        for (SDL_Gamepad*& pad : g_pads)
            if (pad && pad != *slot && SDL_GetGamepadVendor(pad) == 0x28DE && SDL_GetGamepadProduct(pad) != 0x11FF) {
                LOGR("input: controller %d (%s) gives way to Steam's virtual pad", int(&pad - g_pads) + 1, SDL_GetGamepadName(pad));
                SDL_CloseGamepad(pad);
                pad = nullptr;
            }
    const char* name = *slot ? SDL_GetGamepadName(*slot) : SDL_GetError();
    // in the release log: which driver took the pad decides how its triggers read
    const char* path = *slot ? SDL_GetGamepadPath(*slot) : nullptr;
    SDL_Joystick* js = *slot ? SDL_GetGamepadJoystick(*slot) : nullptr;
    LOGR("input: controller %d %s: %s (vendor %04X product %04X, %s, %s)", int(slot - g_pads) + 1,
         *slot ? "plugged in" : "failed to open", name ? name : "", *slot ? SDL_GetGamepadVendor(*slot) : 0,
         *slot ? SDL_GetGamepadProduct(*slot) : 0, path ? path : "no path",
         js && SDL_IsJoystickVirtual(id) ? "virtual" : *slot && SDL_GamepadHasAxis(*slot, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) ? "has triggers" : "no trigger axes");
}

void RemovePad(SDL_JoystickID id) {
    bool was_substitute = false;
    for (int i = 0; i < 4; i++) {
        if (!g_pads[i] || SDL_GetGamepadID(g_pads[i]) != id) continue;
        if (g_wine && IsSubstitute(g_pads[i])) was_substitute = true;
        SDL_CloseGamepad(g_pads[i]);
        g_pads[i] = nullptr;
        LOGI("input: controller %d unplugged", i + 1);
    }
    // Steam's virtual pad gone (Steam Input turned off mid-game): the real Steam controller back
    if (was_substitute && !AnySubstitute()) {
        int n = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
            for (int i = 0; i < n; i++)
                if (ids[i] != id && SDL_GetGamepadVendorForID(ids[i]) == 0x28DE) AddPad(ids[i]);
            SDL_free(ids);
        }
    }
}

// once a millisecond at most, however many ports and threads read
void UpdatePads() {
    static uint64_t last = 0;
    uint64_t now = SDL_GetTicks();
    if (!g_sdl || now == last) return;
    last = now;
    SDL_PumpEvents();
    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) AddPad(e.gdevice.which);
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) RemovePad(e.gdevice.which);
    }
}

}  // namespace

void Init() {
    // XInput takes the Xbox pads (with it on, SDL's WGI leaves them to it, so none is listed twice): WGI alone missed
    // an Xbox 360 wireless controller (#18). WGI takes the other pads, reading the triggers apart, and HIDAPI
    // PlayStation and Switch pads. Reads come from the guest's threads, so SDL's own thread takes the device notices.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    bool wine = false;
#ifdef _WIN32
    if (HMODULE nt = GetModuleHandleA("ntdll.dll")) wine = GetProcAddress(nt, "wine_get_version") != nullptr;
#endif
    g_wine = wine;
    // started by Steam with Steam Input (it sets these for the game it starts; children such as a port started by
    // the launcher inherit them)
    g_steam_launched = SDL_getenv("SteamVirtualGamepadInfo") || SDL_getenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD");
    // Under Wine / Proton (Steam Deck, Linux): SDL's own defaults, as the launcher has them, which sees the pads there
    // (GitHub #65: the launcher had the Steam Controller, the games no controller at all). With Steam Input on, Steam
    // gives the game a virtual Xbox pad that Wine shows through XInput only; with WGI on, SDL leaves XInput pads to
    // WGI, and Wine's WGI doesn't list Steam's virtual pad, so it was lost. WGI is off there (SDL's default),
    // DirectInput on (a pad Steam doesn't turn into an Xbox one), RAWINPUT off. SDL's HIDAPI Deck and Steam Controller
    // drivers (on by default) read them without Steam Input; AddPad keeps one pad per controller when Steam gives its
    // virtual one.
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, wine ? "0" : "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");
    // Windows: DirectInput off (XInput and WGI have every pad)
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, wine ? "1" : "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        LOGE("input: SDL failed to start, no controllers: %s", SDL_GetError());
        return;
    }
    int v = SDL_GetVersion();
    LOGR("input: SDL %d.%d.%d%s", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
         !wine ? "" : g_steam_launched ? " (Wine/Proton, started by Steam; WGI off, DirectInput on)" : " (Wine/Proton; WGI off, DirectInput on)");
    std::lock_guard lock(g_lock);
    g_sdl = true;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++) AddPad(ids[i]);
        SDL_free(ids);
    }
}

namespace {
// the controller on that port; remap: the Xbox buttons as [controls] sets them, else as on the Xbox
Pad Read(int port, bool remap) {
    Pad p;
    {
        std::lock_guard lock(g_lock);
        UpdatePads();
        if (SDL_Gamepad* pad = port >= 0 && port < 4 ? g_pads[port] : nullptr) {
            p.connected = true;
            constexpr SDL_GamepadButton kButtons[] = {
                SDL_GAMEPAD_BUTTON_DPAD_UP,   SDL_GAMEPAD_BUTTON_DPAD_DOWN,  SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_START,     SDL_GAMEPAD_BUTTON_BACK,
                SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
            for (int i = 0; i < 8; i++) p.buttons |= uint16_t(SDL_GetGamepadButton(pad, kButtons[i]) << i);
            const app::Settings& st = app::Get();
            auto axis = [&](SDL_GamepadAxis a) { return int(SDL_GetGamepadAxis(pad, a)); };
            // the controller's buttons as input::kSources, then each Xbox button from the one [controls] sets it to
            constexpr SDL_GamepadButton kFace[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                                                   SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                                                   SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
            uint8_t src[kSourceCount] = {};
            for (int i = 0; i < 6; i++) src[i] = SDL_GetGamepadButton(pad, kFace[i]) ? 0xFF : 0;
            src[6] = Trigger(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), st.trigger_threshold);
            src[7] = Trigger(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), st.trigger_threshold);
            src[8] = (p.buttons >> 6) & 1 ? 0xFF : 0;
            src[9] = (p.buttons >> 7) & 1 ? 0xFF : 0;
            bool moved[2] = {};
            for (int i = 0; i < 8; i++) {
                static constexpr uint32_t kXbox[8] = {0, 1, 2, 3, 5, 4, 6, 7};
                uint32_t from = remap ? std::min(st.buttons[i], kSourceCount - 1) : kXbox[i];
                p.analog[i] = src[from];
                if (from == 8 || from == 9) moved[from - 8] = true;
            }
            // a stick click set to an Xbox button no longer clicks the stick too
            if (moved[0]) p.buttons &= uint16_t(~(1u << 6));
            if (moved[1]) p.buttons &= uint16_t(~(1u << 7));
            // the first press of each trigger, and of each shoulder button, in the release log
            static bool seen[4][4] = {};
            const int lt = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), rt = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            const bool lb = src[4], rb = src[5];
            const bool now[4] = {lt > 4000, rt > 4000, lb, rb};
            static const char* kWhat[4] = {"left trigger", "right trigger", "left shoulder (White)", "right shoulder (Black)"};
            for (int k = 0; k < 4; k++)
                if (now[k] && !seen[port][k]) {
                    seen[port][k] = true;
                    LOGR("input: controller %d first %s press (triggers read %d and %d of 32767)", port + 1, kWhat[k], lt, rt);
                }
            // SDL's sticks have y down
            // not plain negation: -32768 has no positive
            int lx = axis(SDL_GAMEPAD_AXIS_LEFTX), ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
            int rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
            Deadzone(lx, ly, st.stick_deadzone);
            Deadzone(rx, ry, st.stick_deadzone);
            p.lx = int16_t(lx);
            p.ly = int16_t(-1 - ly);
            p.rx = int16_t(rx);
            p.ry = int16_t(-1 - ry);
        }
    }
    return p;
}
}  // namespace

Pad Raw(int port) { return Read(port, false); }

bool Connected(int port) { return Read(port, false).connected || keys::Claims(port); }

Pad Poll(int port) {
    Pad p = Read(port, true);
    // [window] background_input off: the controller stays plugged in but presses nothing without the focus
    if (!app::Get().background_input && !app::Focused()) {
        bool connected = p.connected;
        p = Pad{};
        p.connected = connected;
    }
    app::OverlayPad(port, p);  // both stick clicks open the in-game menu; nothing reaches the game while it is open
    if (app::kGame.pad) app::kGame.pad(port, p);
    keys::Merge(port, p);
    return p;
}

// the motors run until set again
void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    // [input] vibration scales both motors, 0 keeps them still
    uint32_t pct = std::min(app::Get().vibration, 100u);
    left = uint16_t(left * pct / 100);
    right = uint16_t(right * pct / 100);
    static bool logged = false;
    if (!logged && (left || right)) {
        logged = true;
        LOGR("input: rumble on controller %d (vibration %u%%)", port + 1, pct);
    }
    std::lock_guard lock(g_lock);
    if (port >= 0 && port < 4 && g_pads[port]) SDL_RumbleGamepad(g_pads[port], left, right, 0);
}

int Style(int port) {
    std::lock_guard lock(g_lock);
    if (port < 0 || port >= 4 || !g_pads[port]) return 0;
    switch (SDL_GetGamepadType(g_pads[port])) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5: return 1;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return 2;
        default: return 0;
    }
}

}  // namespace input
