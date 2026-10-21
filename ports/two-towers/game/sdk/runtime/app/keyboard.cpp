// Keyboard and mouse play ([keyboard] in the settings file): keys press the console controller's buttons and move
// its left stick, on player 1's port (or player 2's), together with any controller there. The keys are read only
// while the game window has the focus, and the mouse buttons only with the pointer over the picture.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"

namespace keys {

std::atomic<bool> g_used{false};  // a key or mouse button has pressed something since the start

namespace {

// each action's keys, as virtual-key codes, from the settings at the first read
struct Bindings {
    std::vector<uint8_t> vk[kCount];
};

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

const Bindings& Get() {
    static std::once_flag once;
    static Bindings b;
    std::call_once(once, [] {
        const app::Settings& s = app::Get();
        std::string list;
        for (uint32_t a = 0; a < kCount; a++) {
            const std::string& text = s.keys[a];
            size_t start = 0;
            while (start <= text.size()) {
                size_t comma = text.find(',', start);
                std::string name = Trim(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                start = comma == std::string::npos ? text.size() + 1 : comma + 1;
                if (name.empty()) continue;
                bool found = false;
                for (const Key& k : kKeys)
                    if (_stricmp(k.name, name.c_str()) == 0) {
                        b.vk[a].push_back(k.vk);
                        found = true;
                    }
                if (!found) LOGW("keyboard: unknown key \"%s\" for %s", name.c_str(), kSettingKeys[a]);
            }
        }
        LOGR("keyboard: %s, player %u", s.keyboard ? "on" : "off", s.keyboard_player);
    });
    return b;
}

// keys held as the in-game menu opened or closed: not read again until let go (Esc closing it would pause the game)
std::atomic<uint64_t> g_swallowed[4];

// keys pressed since the keyboard player's pad was last read (window thread: Latch): a tap shorter than the game's
// frame still counts
std::atomic<uint64_t> g_latched[4];

// the mouse since the pad was last read (window thread: Mouse): raw movement in counts, wheel notches
std::atomic<int32_t> g_mouse_dx{0}, g_mouse_dy{0}, g_wheel{0};
float g_aim_x = 0, g_aim_y = 0;  // the aim the mouse holds the stick at, easing back when it stops

bool Down(uint8_t vk, bool mouse_ok) {
    bool mouse = vk <= 0x06 && vk != 0x03;
    if (mouse && !mouse_ok) return false;
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0 || (g_latched[vk >> 6].load() & (1ull << (vk & 63)));
    uint64_t bit = 1ull << (vk & 63);
    std::atomic<uint64_t>& w = g_swallowed[vk >> 6];
    if (w.load() & bit) {
        if (down) return false;
        w &= ~bit;
    }
    return down;
}

}  // namespace

void Latch(uint32_t vk) {
    auto one = [](uint32_t v) { g_latched[(v >> 6) & 3] |= 1ull << (v & 63); };
    // the window's messages name Shift, Ctrl and Alt without a side: both sides count
    if (vk == VK_SHIFT) one(VK_LSHIFT), one(VK_RSHIFT);
    else if (vk == VK_CONTROL) one(VK_LCONTROL), one(VK_RCONTROL);
    else if (vk == VK_MENU) one(VK_LMENU), one(VK_RMENU);
    else if (vk < 256) one(vk);
}

void Mouse(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp) {
    static bool registered = false;
    if (!registered && hwnd) {
        RAWINPUTDEVICE d{0x01, 0x02, 0, static_cast<HWND>(hwnd)};  // generic desktop, mouse; no RIDEV_NOLEGACY
        registered = RegisterRawInputDevices(&d, 1, sizeof d) != FALSE;
    }
    if (msg == WM_INPUT) {
        RAWINPUT r;
        UINT size = sizeof r;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &r, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            r.header.dwType == RIM_TYPEMOUSE && !(r.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            g_mouse_dx += r.data.mouse.lLastX;
            g_mouse_dy += r.data.mouse.lLastY;
        }
    } else if (msg == WM_MOUSEWHEEL) {
        g_wheel += GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
    }
    (void)wp;
}

void Swallow() {
    for (auto& w : g_latched) w = 0;
    for (const Key& k : kKeys)
        if (GetAsyncKeyState(k.vk) & 0x8000) g_swallowed[k.vk >> 6] |= 1ull << (k.vk & 63);
}

bool Claims(int port) {
    const app::Settings& s = app::Get();
    return s.keyboard && uint32_t(port) + 1 == s.keyboard_player;
}

bool Over() {
    HWND w = static_cast<HWND>(app::Window());
    if (!w || GetForegroundWindow() != w) return false;
    POINT p;
    RECT r;
    if (!GetCursorPos(&p) || !ScreenToClient(w, &p) || !GetClientRect(w, &r)) return false;
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

// input::Poll, after the game's own changes to the pad (kGame.pad): the keys go on top, so a layout made for the
// controller does not move them
void Merge(int port, input::Pad& p) {
    const app::Settings& s = app::Get();
    if (!s.keyboard || uint32_t(port) + 1 != s.keyboard_player) return;
    const Bindings& b = Get();
    p.connected = true;  // the game sees a controller on the port, so the keys work without one plugged in
    HWND w = static_cast<HWND>(app::Window());
    if (!w || GetForegroundWindow() != w || app::OverlayOpen()) return;
    bool mouse_ok = Over();
    bool menus = app::kGame.in_menus && app::kGame.in_menus();
    bool held[kCount] = {};
    bool any = false;
    bool right_click = false;  // the right button held, in the menus going back instead of its action
    for (uint32_t a = 0; a < kCount; a++)
        for (uint8_t vk : b.vk[a]) {
            if (menus && s.mouse_menus && vk == VK_RBUTTON) {
                right_click = right_click || Down(vk, mouse_ok);
                continue;
            }
            if (Down(vk, mouse_ok)) {
                held[a] = true;
                any = true;
            }
        }
    for (auto& w : g_latched) w = 0;  // read once
    int32_t dx = g_mouse_dx.exchange(0), dy = g_mouse_dy.exchange(0), wheel = g_wheel.exchange(0);
    if (!mouse_ok) dx = dy = wheel = 0;
    if (menus && s.mouse_menus) {
        if (right_click) held[kB] = any = true;
        if (wheel > 0) held[kDpadUp] = any = true;
        if (wheel < 0) held[kDpadDown] = any = true;
    }
    // mouse aiming: while the ranged attack's keys are held, the mouse's movement holds the left stick over, as far as
    // it moved (about 40 counts for full tilt at 100%), easing back over a few frames when it stops
    float ax = 0, ay = 0;
    if (s.mouse_aim && !menus && held[kLeftTrigger]) {
        float k = float(s.mouse_sensitivity) / 100.0f / 40.0f;
        auto toward = [](float cur, float want) {
            float v = want != 0 ? want : cur * 0.6f;
            return std::fabs(v) < 0.02f ? 0.0f : std::max(-1.0f, std::min(1.0f, v));
        };
        g_aim_x = toward(g_aim_x, float(dx) * k);
        g_aim_y = toward(g_aim_y, float(-dy) * k);
        ax = g_aim_x;
        ay = g_aim_y;
        if (ax != 0 || ay != 0) any = true;
    } else {
        g_aim_x = g_aim_y = 0;
    }
    if (!any) return;
    if (!g_used.exchange(true)) LOGR("keyboard: first key press");
    // in the game's menus Esc (Start's keys) goes back, and Enter (A's) goes on the way those menus take it
    if (menus) {
        bool start = held[kStart];
        if (app::kGame.menu_accept_start) {
            held[kStart] = held[kA];
            held[kA] = false;
        } else {
            held[kStart] = app::kGame.menu_start_with_a && held[kA];
        }
        held[kB] = held[kB] || start;
    }
    // the left stick: the keys' direction at full tilt (a diagonal no further than a straight push), half with walk
    int mx = int(held[kMoveRight]) - int(held[kMoveLeft]), my = int(held[kMoveUp]) - int(held[kMoveDown]);
    if (mx || my) {
        float len = held[kWalk] ? 0.5f : 1.0f;
        if (mx && my) len *= 0.70710678f;
        p.lx = int16_t(std::lround(mx * len * 32767));
        p.ly = int16_t(std::lround(my * len * 32767));
    } else if (ax != 0 || ay != 0) {
        p.lx = int16_t(std::lround(ax * 32767));
        p.ly = int16_t(std::lround(ay * 32767));
    }
    // the analog buttons, A B X Y black white left trigger right trigger, as input::Pad::analog
    for (uint32_t i = 0; i < 8; i++)
        if (held[kA + i]) p.analog[i] = 255;
    // dpad up/down/left/right, start, back, left thumb, right thumb, as input::Pad::buttons
    const Action bits[8] = {kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kStart, kBack, kLeftStick, kRightStick};
    for (int i = 0; i < 8; i++)
        if (held[bits[i]]) p.buttons |= uint16_t(1u << i);
}

// the first key bound to an action, for the game's button prompts and the overlay; empty when none
std::string FirstKey(Action a) {
    const std::string& text = app::Get().keys[a];
    size_t comma = text.find(',');
    return Trim(comma == std::string::npos ? text : text.substr(0, comma));
}

}  // namespace keys
