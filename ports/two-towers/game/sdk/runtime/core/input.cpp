// controllers through SDL: Xbox, PlayStation and Switch pads read into the console controller's report
#include "core/input.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <mutex>

#include <SDL3/SDL.h>

#include "app/app.h"
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
SDL_Gamepad* g_pads[4] = {};

void AddPad(SDL_JoystickID id) {
    for (SDL_Gamepad* pad : g_pads)
        if (pad && SDL_GetGamepadID(pad) == id) return;
    SDL_Gamepad** slot = std::find(std::begin(g_pads), std::end(g_pads), nullptr);
    if (slot == std::end(g_pads)) return;
    *slot = SDL_OpenGamepad(id);
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
    for (int i = 0; i < 4; i++) {
        if (!g_pads[i] || SDL_GetGamepadID(g_pads[i]) != id) continue;
        SDL_CloseGamepad(g_pads[i]);
        g_pads[i] = nullptr;
        LOGI("input: controller %d unplugged", i + 1);
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
    // WGI reads the triggers apart and HIDAPI takes PlayStation and Switch pads. Reads come from the guest's threads,
    // so SDL's own thread takes the device notices.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        LOGE("input: SDL failed to start, no controllers: %s", SDL_GetError());
        return;
    }
    int v = SDL_GetVersion();
    LOGI("input: SDL %d.%d.%d", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    std::lock_guard lock(g_lock);
    g_sdl = true;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++) AddPad(ids[i]);
        SDL_free(ids);
    }
}

Pad Poll(int port) {
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
                uint32_t from = std::min(st.buttons[i], kSourceCount - 1);
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
    if (app::kGame.pad) app::kGame.pad(port, p);
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

}  // namespace input
