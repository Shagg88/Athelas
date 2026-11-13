// The in-game menu: F10 (or Back + Start on a controller) opens it over the game, which stands still behind it. It
// holds the settings that take effect at once; the rest stay in the launcher. Also [window] pause_unfocused and
// mute_unfocused: the same standstill, or silence, while another window has the focus.
//
// Standing still: the title's thread waits in Hold at its next Swap (xdk/d3d8.cpp), the last picture shown again
// with the menu over it; the guest's clocks stop (kernel::Freeze) and the sound output stops taking samples, so
// nothing in the game moves on, and it carries on from the same moment.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <imgui.h>
#include <imgui_internal.h>

#include "app/app.h"
#include "app/menu_look.h"
#include "app/overlay_font.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

namespace keys {
void Swallow();  // keyboard.cpp: keys down now are not read again until released
}

namespace app {

namespace {

std::atomic<bool> g_open{false}, g_focus{true}, g_held{false};
std::atomic<bool> g_dirty{false};
// controller buttons held as the menu opened or closed, kept from the game until released: buttons, analog
std::atomic<uint16_t> g_mask_buttons[4];
std::atomic<uint8_t> g_mask_analog[4];
std::atomic<bool> g_combo[4];
std::atomic<bool> g_ff_combo[4];  // Back + right stick click held: fast forward toggled once per press
ImFont* g_font = nullptr;
look::Fonts g_look;                   // the menu's own fonts (the launcher's)
std::atomic<bool> g_feed_fresh{true};
std::atomic<uint8_t> g_shoulders{0};   // FeedPads: LB (1) / RB (2) held on any controller
bool g_b_was = false;
bool g_ctrl_open = false;              // Control: its list is open (the row stays lit)
int g_first_page = 0;  // XBE_MENU_PAGE: the page shown first (pictures of each)  // FeedPads: what is held as the menu opens is not a press in it
constexpr float kFontPixels = 48.0f;  // the atlas size; drawn smaller with UiScale

void MaskHeld() {
    for (int port = 0; port < 4; port++) {
        input::Pad p = input::Raw(port);
        uint8_t a = 0;
        for (int i = 0; i < 8; i++)
            if (p.analog[i] > 30) a |= uint8_t(1u << i);
        g_mask_buttons[port] = p.buttons;
        g_mask_analog[port] = a;
    }
    keys::Swallow();
}

void Open() {
    if (g_open.exchange(true)) return;
    MaskHeld();
    g_feed_fresh = true;
    LOGR("menu: open");
}

void Close() {
    if (!g_open.load()) return;
    MaskHeld();
    if (g_dirty.exchange(false)) {
        if (kGame.save_settings) kGame.save_settings();
        else Save();
        LOGR("menu: settings saved");
    }
    g_open = false;
    LOGR("menu: closed");
}

void Toggle() {
    if (g_open) Close();
    else Open();
}

// the host's settings the menu shows: they all take effect at once
constexpr const char* kLive[] = {
    "display.show_fps", "display.vsync", "display.render_percent", "graphics.upscaler", "graphics.upscaler_quality", "graphics.anti_aliasing",
    "graphics.anisotropic_filtering", "graphics.lod_bias", "graphics.console_textures", "graphics.sharpening", "graphics.smooth_2d",
    "graphics.colour_look", "graphics.look_strength", "graphics.brightness", "graphics.movie_display",
    "graphics.movie_sharpening", "graphics.cutscene_audio_delay", "input.vibration", "input.stick_deadzone",
    "input.trigger_threshold", "window.pause_unfocused", "window.mute_unfocused", "window.background_input", "keyboard.mouse_aim", "keyboard.mouse_sensitivity", "keyboard.mouse_invert_y", "keyboard.mouse_menus", "keyboard.enabled",
    "keyboard.player", "achievements.notify", "discord.enabled", "system.fast_forward_speed", "system.fast_forward_toggle"};

bool IsLive(const cfg::Desc& d) {
    std::string k = std::string(d.section) + "." + d.key;
    for (const char* l : kLive)
        if (k == l) return true;
    for (const char* l : kGame.live_settings)
        if (k == l) return true;
    return false;
}

// which page a setting goes on: 0 picture, 1 controls, 2 game
int PageOf(const cfg::Desc& d) {
    std::string s = d.section;
    if (s == "display" || s == "graphics" || s == "textures") return 0;
    if (s == "input" || s == "keyboard" || s == "controls" || s == "window") return 1;
    if (s == "achievements" || s == "discord") return 2;
    return 2;
}

bool NumberChoices(const cfg::Desc& d) {
    for (const char* c : d.choices) {
        char* end = nullptr;
        std::strtol(c, &end, 10);
        if (!*c || *end) return false;
    }
    return !d.choices.empty();
}

// one setting's control, without its label (the menu's row draws that), at the cursor and the next item width;
// true when it changed
bool Control(const cfg::Desc& d, bool label = true);
bool ControlOnly(const cfg::Desc& d) { return Control(d, false); }

// one setting's control, with its label before it; true when it changed
bool Control(const cfg::Desc& d, bool label) {
    const bool label_only = label;   // (the old look: ImGui's own arrow)
    ImGui::PushID(d.key);
    ImGui::PushID(d.section);
    bool changed = false;
    g_ctrl_open = false;
    if (label) {
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(d.label);
        if (d.help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.help);
        ImGui::SameLine(w * 0.45f);
        ImGui::SetNextItemWidth(w * 0.55f);
    }
    switch (d.type) {
        case cfg::Type::Bool:
            if (d.flag) changed = ImGui::Checkbox("##v", d.flag);
            break;
        case cfg::Type::Int:
            if (d.value) {
                int v = int(*d.value);
                if (ImGui::SliderInt("##v", &v, int(d.min), int(d.max), "%d", ImGuiSliderFlags_AlwaysClamp)) {
                    *d.value = uint32_t(std::clamp<int64_t>(v, d.min, d.max));
                    changed = true;
                }
            }
            break;
        case cfg::Type::Float:
            if (d.real) {
                float v = *d.real;
                if (ImGui::SliderFloat("##v", &v, float(d.min) / 100.0f, float(d.max) / 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
                    *d.real = std::round(v * 100.0f) / 100.0f;
                    changed = true;
                }
            }
            break;
        case cfg::Type::Choice: {
            int cur = -1;
            bool numbers = NumberChoices(d);
            for (size_t i = 0; i < d.choices.size(); i++) {
                if (d.flag) {
                    if (int(*d.flag) == int(i)) cur = int(i);
                } else if (d.value) {
                    uint32_t want = numbers ? uint32_t(std::atoi(d.choices[i])) : uint32_t(i);
                    if (*d.value == want) cur = int(i);
                }
            }
            auto label = [&](int i) { return size_t(i) < d.labels.size() ? d.labels[size_t(i)] : d.choices[size_t(i)]; };
            // (measured before the list opens: once it has, the "last item" is the list's own)
            ImDrawList* row_dl = ImGui::GetWindowDrawList();
            const ImVec2 mn = ImGui::GetCursorScreenPos();
            const ImVec2 mx(mn.x + ImGui::CalcItemWidth(), mn.y + ImGui::GetFrameHeight());
            const bool open = ImGui::BeginCombo("##v", cur >= 0 ? label(cur) : "", label_only ? 0 : ImGuiComboFlags_NoArrowButton);
            g_ctrl_open = open;
            if (!label_only) {   // the menu's own arrow: a small gold chevron at the right end
                float h = mx.y - mn.y, cx = mx.x - h * 0.5f, cy = (mn.y + mx.y) * 0.5f, r = h * 0.16f;
                bool hov = open || ImGui::IsMouseHoveringRect(mn, mx);
                row_dl->AddLine(ImVec2(mx.x - h, mn.y + h * 0.22f), ImVec2(mx.x - h, mx.y - h * 0.22f), look::Gold(0.35f), 1.0f);
                ImVec2 pts[3] = {ImVec2(cx - r * 1.4f, cy - r * 0.6f), ImVec2(cx, cy + r * 0.8f), ImVec2(cx + r * 1.4f, cy - r * 0.6f)};
                row_dl->AddPolyline(pts, 3, hov ? look::GoldLight() : look::Gold(), 0, std::max(1.5f, h * 0.06f));
            }
            if (open) {
                for (int i = 0; i < int(d.choices.size()); i++) {
                    if (ImGui::Selectable(label(i), i == cur) && i != cur) {
                        if (d.flag) *d.flag = i != 0;
                        else if (d.value) *d.value = numbers ? uint32_t(std::atoi(d.choices[size_t(i)])) : uint32_t(i);
                        changed = true;
                    }
                    if (i == cur) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case cfg::Type::String: break;
    }
    ImGui::PopID();
    ImGui::PopID();
    if (changed) {
        std::string k = std::string(d.section) + "." + d.key;
        if (k == "graphics.colour_look") gpu::g_colour_look = std::min(Get().colour_look, gpu::kColourLooks - 1);
        if (kGame.setting_changed) kGame.setting_changed(d);
        LOGR("menu: %s = %s", k.c_str(), cfg::ValueText(d).c_str());
    }
    return changed;
}

void KeyList() {
    const Settings& s = Get();
    if (!s.keyboard) return;
    ImGui::SeparatorText("Keyboard and mouse");
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingStretchProp)) {
        for (uint32_t i = 0; i < keys::kCount; i++) {
            if (s.keys[i].empty()) continue;
            const char* label = keys::kLabels[i];
            if (i >= keys::kA && i <= keys::kRightTrigger && size_t(i - keys::kA) < kGame.button_actions.size() &&
                kGame.button_actions[i - keys::kA])
                label = kGame.button_actions[i - keys::kA];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", s.keys[i].c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("The keys can be changed in the launcher.");
}

// the controllers while the menu is open, into ImGui's gamepad keys (the game reads none of it)
void FeedPads() {
    static input::Pad last{};
    input::Pad p{};
    for (int port = 0; port < 4; port++) {
        input::Pad q = input::Raw(port);
        if (!q.connected) continue;
        p.buttons |= q.buttons;
        for (int i = 0; i < 8; i++) p.analog[i] = std::max(p.analog[i], q.analog[i]);
        if (std::abs(q.lx) > std::abs(p.lx)) p.lx = q.lx;
        if (std::abs(q.ly) > std::abs(p.ly)) p.ly = q.ly;
    }
    // what is held as the menu opens (Back + Start, or A still down from the game) is no press in it: kept from
    // ImGui too until let go (review: A held while opening pressed the row the cursor was on)
    static uint16_t held_b = 0;
    static uint8_t held_a = 0;
    if (g_feed_fresh.exchange(false)) {
        last = p;
        held_b = p.buttons;
        held_a = 0;
        for (int i = 0; i < 8; i++)
            if (p.analog[i] > 30) held_a |= uint8_t(1u << i);
    }
    held_b &= p.buttons;
    for (int i = 0; i < 8; i++)
        if (p.analog[i] <= 30) held_a &= uint8_t(~(1u << i));
    input::Pad raw = p;   // (Start and Back + Start below go by the pad itself)
    p.buttons = uint16_t(p.buttons & ~held_b);
    for (int i = 0; i < 8; i++)
        if (held_a & (1u << i)) p.analog[i] = 0;
    g_shoulders = uint8_t((p.analog[5] > 30 ? 1 : 0) | (p.analog[4] > 30 ? 2 : 0));   // LB / RB of any controller: the pages
    auto pressed = [&](uint16_t bit) { return (raw.buttons & bit) && !(last.buttons & bit); };
    bool b_now = p.analog[1] > 30, b_was = g_b_was;
    g_b_was = b_now;
    bool both = (raw.buttons & 0x30) == 0x30, both_was = (last.buttons & 0x30) == 0x30;
    {
        std::lock_guard lock(UiLock());
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, p.buttons & 0x01);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown, p.buttons & 0x02);
        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, p.buttons & 0x04);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, p.buttons & 0x08);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, p.analog[0] > 30);
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, b_now);
        io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, p.analog[2] > 30);
        io.AddKeyEvent(ImGuiKey_GamepadFaceUp, p.analog[3] > 30);
        auto stick = [&](ImGuiKey neg, ImGuiKey pos, int v) {
            float f = std::clamp(v / 32767.0f, -1.0f, 1.0f);
            f = std::fabs(f) < 0.3f ? 0.0f : f;
            io.AddKeyAnalogEvent(neg, f < 0, f < 0 ? -f : 0.0f);
            io.AddKeyAnalogEvent(pos, f > 0, f > 0 ? f : 0.0f);
        };
        stick(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, p.lx);
        stick(ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, p.ly);
        // B with nothing open in the menu, Start, or Back + Start again: back to the game
        // (B not while a list is open or a slider is being moved: there it only ends that)
        if (pressed(0x10) || (both && !both_was) ||
            (b_now && !b_was && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && ImGui::GetCurrentContext()->ActiveId == 0))
            Close();
    }
    last = raw;
}

}  // namespace

bool OverlayOpen() { return g_open.load(); }
bool Paused() { return g_open.load() || (Get().pause_unfocused && !g_focus.load()); }
bool Held() { return g_held.load(); }
bool Muted() { return g_held.load() || FastForward() || (Get().mute_unfocused && !g_focus.load()); }

bool Focused() { return g_focus.load(); }

void SetFocus(bool focus) {
    if (g_focus.exchange(focus) == focus) return;
    // keys held across the switch (Alt-Tab's Alt) wait to be let go, and taps from before it are dropped
    keys::Swallow();
    if (Get().pause_unfocused || Get().mute_unfocused || !Get().background_input)
        LOGR("window: %s", focus ? "back in front" : "in the background");
}

void Hold(const std::function<void()>& present) {
    if (!Paused()) return;
    // normal speed first: fast forward's extra time would otherwise keep running while the clock is frozen, and the
    // game's time jump on resume (review, F9 held while pausing)
    kernel::SetSpeed(1);
    kernel::Freeze(true);
    g_held = true;
    LOG_FIRST(4, "pause: the game waits (%s)", g_open ? "menu" : "in the background");
    auto t0 = std::chrono::steady_clock::now();
    while (Paused()) {
        if (g_open && (g_focus || Get().background_input)) FeedPads();
        present();
        // a menu at the screen's pace (V-Sync waits too); behind another window, slower
        std::this_thread::sleep_for(std::chrono::milliseconds(g_focus ? 8 : 50));
    }
    g_held = false;
    kernel::Freeze(false);
    LOG_FIRST(4, "pause: %.1f s", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}

void OverlayPad(int port, input::Pad& p) {
    if (port < 0 || port > 3) return;
    // Back + Start (Share + Options on a PlayStation pad) opens the menu, as in most PC ports; both are kept from the
    // game until let go, so the press does nothing in it
    const uint16_t rawb = p.buttons;   // as the pad has them, before a lone Start or Back is held back below
    bool both = (p.buttons & 0x30) == 0x30;
    // (not while one of the two is still held from closing the menu - Start closed it, Back landed a moment later, or
    // it was opened with F10 and closed with the pair: that would open it again at once - review)
    if (both && (g_mask_buttons[port] & 0x30)) g_combo[port] = true;
    if (both && !g_combo[port].exchange(true)) {
        Open();
        g_mask_buttons[port] = uint16_t(g_mask_buttons[port] | 0x30);
    }
    if (!both) g_combo[port] = false;
    // 2: Start or Back pressed on its own waits a moment before the game sees it, in case the other follows (they
    // rarely land in the same instant): the game's own pause menu, or ROTK's Back, would open under ours. Let go
    // before the wait is over, it is passed on as a short press.
    {
        using clock = std::chrono::steady_clock;
        static clock::time_point since[4], pulse_until[4];
        static uint16_t pending[4], pulse[4], was[4];
        const auto now = clock::now();
        const uint16_t pair = uint16_t(p.buttons & 0x30), fresh = uint16_t(pair & ~was[port]);
        was[port] = pair;
        if (!g_open && !both) {
            if (fresh && !pending[port] && !(g_mask_buttons[port] & fresh)) { pending[port] = fresh; since[port] = now; }
            if (pending[port]) {
                const bool waited = now - since[port] >= std::chrono::milliseconds(90);
                if (!(pair & pending[port])) {
                    // let go while still held back: the game has not seen it at all, so it gets a short press of it
                    // now - whenever that is (review: only "before the 90 ms were over" lost a tap whose release
                    // was first seen at a poll after them, one in a few at 30 polls a second)
                    pulse[port] = pending[port];
                    pulse_until[port] = now + std::chrono::milliseconds(70);
                    pending[port] = 0;
                } else if (waited) pending[port] = 0;    // held on its own: the game's from here
                else p.buttons = uint16_t(p.buttons & ~pending[port]);
            }
            if (pulse[port]) {
                if (now < pulse_until[port]) p.buttons = uint16_t(p.buttons | pulse[port]);
                else pulse[port] = 0;
            }
        } else {
            pending[port] = pulse[port] = 0;
        }
    }
    // Back + right stick click: fast forward on or off, for controller players (Steam Deck) who have no F9 to hold.
    // Neither game uses this pair together (ROTK has Back and the right stick click each on its own, the others leave
    // the right stick click unused), and both buttons are kept from the game until let go so the press does nothing in it
    bool ff = (rawb & 0xA0) == 0xA0 && !(rawb & 0x50);   // (not with Start: that is the menu)
    if (ff && !g_open && !g_ff_combo[port].exchange(true)) {
        ToggleFastForward();
        g_mask_buttons[port] = uint16_t(g_mask_buttons[port] | 0xA0);
    }
    if (!ff) g_ff_combo[port] = false;
    if (g_open) {
        bool c = p.connected;
        p = input::Pad{};
        p.connected = c;
        return;
    }
    // what was held as the menu opened or closed stays away from the game until let go
    uint16_t mb = g_mask_buttons[port];
    if (mb) {
        g_mask_buttons[port] = uint16_t(mb & p.buttons);
        p.buttons = uint16_t(p.buttons & ~mb);
    }
    uint8_t ma = g_mask_analog[port];
    if (ma) {
        uint8_t still = 0;
        for (int i = 0; i < 8; i++)
            if (ma & (1u << i)) {
                if (p.analog[i] > 30) still |= uint8_t(1u << i);
                p.analog[i] = 0;
            }
        g_mask_analog[port] = still;
    }
}

bool OverlayMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    bool repeat = lp & (intptr_t(1) << 30);
    // a press the keyboard player's pad may not be read during (a tap shorter than a frame)
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: keys::Latch(uint32_t(wp)); break;
        case WM_LBUTTONDOWN: keys::Latch(VK_LBUTTON); break;
        case WM_RBUTTONDOWN: keys::Latch(VK_RBUTTON); break;
        case WM_MBUTTONDOWN: keys::Latch(VK_MBUTTON); break;
        case WM_XBUTTONDOWN: keys::Latch(HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2); break;
    }
    if (msg == WM_INPUT || msg == WM_MOUSEWHEEL || msg == WM_CREATE || msg == WM_ACTIVATE) keys::Mouse(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            if (wp == VK_F10) {  // a system key: also keeps Windows from starting menu mode
                if (!repeat) Toggle();
                *result = 0;
                return true;
            }
            if (wp == VK_ESCAPE && g_open) {
                bool popup;
                {
                    std::lock_guard lock(UiLock());
                    popup = ImGui::GetCurrentContext() && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                }
                if (!popup && !repeat) Close();
                *result = 0;
                return true;
            }
            break;
        case WM_SYSKEYUP:
            if (wp == VK_F10) {
                *result = 0;
                return true;
            }
            break;
        case WM_SYSCOMMAND:
            // Alt or F10 alone would put the window in menu mode (it has none) and hold the next key: Alt is the
            // walk key. Alt+Space, Alt+F4 and Alt+Tab are other commands and still work.
            if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0) {
                *result = 0;
                return true;
            }
            break;
        case WM_SETCURSOR:
            // the pointer is hidden over the game, shown with the menu (ImGui sets it then)
            if (LOWORD(lp) == HTCLIENT && !g_open) {
                SetCursor(nullptr);
                *result = TRUE;
                return true;
            }
            break;
    }
    (void)hwnd;
    return false;
}

void OverlayInit() {
    // XBE_MENU_OPEN=1: the menu open from the first frame (a picture of it for testing, without pressing anything)
    if (const char* e = std::getenv("XBE_MENU_OPEN"); e && *e == '1') g_open = true;
    if (const char* e = std::getenv("XBE_MENU_PAGE"); e && *e >= '0' && *e <= '3') g_first_page = *e - '0';
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig fc;
    fc.OversampleH = 2;
    // Latin with its accents (the Latin-1 and Extended-A letters: French, German, Spanish, Italian, Portuguese,
    // Polish, Dutch, Swedish), general punctuation, and Cyrillic: the menu's own text and the subtitles' languages
    static const ImWchar kRanges[] = {0x0020, 0x024F, 0x0400, 0x052F, 0x2000, 0x206F, 0x20AC, 0x20AC, 0x2122, 0x2122, 0};
    g_font = io.Fonts->AddFontFromMemoryCompressedTTF(overlay_font_compressed_data, int(overlay_font_compressed_size),
                                                      kFontPixels, &fc, kRanges);
    io.FontDefault = g_font;
    g_look = look::AddFonts(io.Fonts, g_font);
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;  // FeedPads; the Win32 backend's XInput reading is compiled out
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 6;
    st.FrameRounding = 4;
    st.GrabRounding = 4;
    st.WindowBorderSize = 1;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.06f, 0.05f, 0.94f);
    c[ImGuiCol_Border] = ImVec4(0.55f, 0.45f, 0.25f, 0.6f);
    c[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.17f, 0.12f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.32f, 0.26f, 0.16f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.32f, 0.18f, 1.0f);
    c[ImGuiCol_SliderGrab] = c[ImGuiCol_CheckMark] = ImVec4(0.85f, 0.70f, 0.40f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.80f, 0.50f, 1.0f);
    c[ImGuiCol_Button] = c[ImGuiCol_Header] = c[ImGuiCol_Tab] = ImVec4(0.26f, 0.21f, 0.13f, 1.0f);
    c[ImGuiCol_ButtonHovered] = c[ImGuiCol_HeaderHovered] = c[ImGuiCol_TabHovered] = ImVec4(0.42f, 0.33f, 0.18f, 1.0f);
    c[ImGuiCol_ButtonActive] = c[ImGuiCol_HeaderActive] = c[ImGuiCol_TabSelected] = ImVec4(0.52f, 0.40f, 0.20f, 1.0f);
    c[ImGuiCol_NavHighlight] = ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.55f, 0.45f, 0.25f, 0.5f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.26f, 0.21f, 0.13f, 1.0f);
}

float UiScale() {
    float h = ImGui::GetMainViewport()->Size.y;
    return std::max(16.0f, h / 720.0f * 20.0f) / kFontPixels;
}

void DrawOverlay(double) {
    ImGuiIO& io = ImGui::GetIO();
    // the game's own pointer is hidden; ImGui's shows with the menu
    if (g_open) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    static bool shown = false;  // drawn last frame: a new opening takes the keyboard and controller focus
    ach::DrawNotes(UiScale());
    if (gpu::g_pack_off) {  // F8: a reminder in the corner (screenshots are taken before it is drawn)
        const ImGuiViewport* v = ImGui::GetMainViewport();
        float f = ImGui::GetFontSize() * UiScale();
        ImGui::GetForegroundDrawList()->AddText(nullptr, f, ImVec2(v->Pos.x + f, v->Pos.y + f * 0.8f), IM_COL32(255, 220, 90, 230),
                                                "Texture packs off (F8)");
    }
    if (uint32_t times = kernel::Speed(); times > 1 && !g_open) {  // sped up (fast forward, a game's battle speed)
        const ImGuiViewport* v = ImGui::GetMainViewport();
        float f = ImGui::GetFontSize() * UiScale();
        char text[16];
        std::snprintf(text, sizeof text, ">> %ux", times);
        float w = ImGui::GetFont()->CalcTextSizeA(f, 1e9f, 0.0f, text).x;
        ImGui::GetForegroundDrawList()->AddText(nullptr, f, ImVec2(v->Pos.x + v->Size.x - w - f, v->Pos.y + f * 0.8f),
                                                IM_COL32(255, 220, 90, 230), text);
    }
    DrawSubtitles(UiScale());
    if (!g_open) {
        shown = false;
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // a controller in use: its buttons in the hints (else the keys)
    bool pad = false;
    for (int port = 0; port < 4; port++)
        if (input::Raw(port).connected) pad = true;
    const look::Look L = look::MakeLook(vp->Size, g_look, pad);
    const float u = L.u;
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
    if (!shown) ImGui::SetNextWindowFocus();
    look::PushStyle(L);
    ImGui::PushFont(g_look.body, 34 * u);
    ImGui::Begin("##athelas_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    if (!shown && pad) ImGui::SetNavCursorVisible(true);   // A works at once, not only after a move (review)
    shown = true;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    look::Backdrop(dl, L);
    char game[256] = "";
    if (kGame.title) WideCharToMultiByte(CP_UTF8, 0, kGame.title, -1, game, int(sizeof game), nullptr, nullptr);
    const float rule = look::Header(dl, L, game, "The game waits");

    static const char* const kPages[4] = {"Picture", "Controls", "Game", "Achievements"};
    std::vector<cfg::Desc> list = SettingsList();
    bool has[4] = {false, Get().keyboard, false, !kGame.achievements.empty()};
    for (const cfg::Desc& d : list)
        if (IsLive(d)) has[PageOf(d)] = true;
    static int page = g_first_page;
    if (!has[page])
        for (int i = 0; i < 4; i++)
            if (has[i]) { page = i; break; }
    // the controller's shoulder buttons, or Page Up and Down, step through the pages there are
    static bool lb = false, rb = false;
    {
        const uint8_t sh = g_shoulders.load();
        bool l = sh & 1, r = sh & 2;
        int step = ((l && !lb) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) ? 3
                 : ((r && !rb) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) ? 1 : 0;
        if (step) {
            int want = page;
            do want = (want + step) % 4;
            while (!has[want] && want != page);
            page = want;
        }
        lb = l;
        rb = r;
    }
    std::vector<std::string> names;
    std::vector<int> idx;
    int cur = 0;
    for (int i = 0; i < 4; i++)
        if (has[i]) {
            if (i == page) cur = int(names.size());
            names.push_back(kPages[i]);
            idx.push_back(i);
        }
    const float x0 = L.c0.x + 64 * u, x1 = L.c1.x - 64 * u;
    const float tabs_top = rule + 24 * u, tabs_h = 60 * u;
    int clicked = look::Tabs(L, ImVec2(x0, tabs_top), ImVec2(x1, tabs_top + tabs_h), names, cur, pad ? "LB" : "PgUp",
                             pad ? "RB" : "PgDn");
    if (clicked >= 0) page = idx[size_t(clicked)];

    // two columns: the settings (left); what the chosen one does, and the buttons (right)
    const float body_top = tabs_top + tabs_h + 28 * u, body_bot = vp->Size.y - 96 * u;
    const float gap = 30 * u, right_w = std::clamp((x1 - x0) * 0.32f, 340 * u, 520 * u);
    const ImVec2 la(x0, body_top), lb2(x1 - right_w - gap, body_bot), ra(x1 - right_w, body_top), rb2(x1, body_bot);
    look::Panel(dl, la, lb2, L);
    const float btn_h = 68 * u, btn_gap = 16 * u;
    const ImVec2 da(ra.x, ra.y), db(rb2.x, rb2.y - 2 * btn_h - 2 * btn_gap - 12 * u);
    look::Panel(dl, da, db, L);

    const cfg::Desc* about = nullptr;  // the setting the cursor or the mouse is on: its help on the right
    static std::string about_key;
    ImGui::SetCursorScreenPos(ImVec2(la.x + 18 * u, la.y + 18 * u));
    ImGui::BeginChild("##rows", ImVec2(lb2.x - la.x - 36 * u, lb2.y - la.y - 36 * u), ImGuiChildFlags_NavFlattened);
    {
        ImDrawList* cl = ImGui::GetWindowDrawList();
        const float rw = ImGui::GetContentRegionAvail().x, row_h = 84 * u;
        const float ctrl_w = std::clamp(rw * 0.42f, 260 * u, 480 * u);
        const float frame_h = 34 * u + 18 * u;
        if (page == 3) {
            // the deeds as cards: a gold seal for one earned (with when), a hollow one for one still ahead
            std::vector<ach::Entry> all = ach::Entries();
            size_t got = 0;
            for (auto& e : all) got += e.earned;
            {
                const ImVec2 a = ImGui::GetCursorScreenPos();
                char t[64];
                std::snprintf(t, sizeof t, "%zu of %zu earned", got, all.size());
                cl->AddText(g_look.head, 32 * u, ImVec2(a.x + 12 * u, a.y + 2 * u), look::GoldLight(), t);
                const float bx = a.x + 12 * u + g_look.head->CalcTextSizeA(32 * u, FLT_MAX, 0, t).x + 24 * u, bw = rw - (bx - a.x) - 16 * u;
                const float by = a.y + 22 * u;
                cl->AddRectFilled(ImVec2(bx, by - 4 * u), ImVec2(bx + bw, by + 4 * u), IM_COL32(0, 0, 0, 120), 4 * u);
                if (!all.empty() && got)
                    cl->AddRectFilled(ImVec2(bx, by - 4 * u), ImVec2(bx + bw * float(got) / float(all.size()), by + 4 * u), look::Gold(), 4 * u);
                cl->AddRect(ImVec2(bx, by - 4 * u), ImVec2(bx + bw, by + 4 * u), look::Gold(0.5f), 4 * u, 0, std::max(1.f, u));
                ImGui::Dummy(ImVec2(rw, 60 * u));
            }
            int k = 0;
            for (auto& e : all) {
                ImGui::PushID(k++);
                const float card_h = 116 * u;
                const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + rw, a.y + card_h - 8 * u);
                ImGui::InvisibleButton("##deed", ImVec2(rw, card_h), ImGuiButtonFlags_EnableNav);   // the pad scrolls through them
                const bool lit = (ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible) || ImGui::IsItemHovered();
                cl->AddRectFilled(a, b, e.earned ? IM_COL32(110, 84, 40, lit ? 90 : 60) : IM_COL32(255, 240, 210, lit ? 14 : 6), 6 * u);
                cl->AddRect(a, b, e.earned ? look::Gold(lit ? 0.9f : 0.55f) : look::Gold(lit ? 0.6f : 0.2f), 6 * u, 0, std::max(1.f, u));
                const ImVec2 c(a.x + 50 * u, (a.y + b.y) * 0.5f);
                const float r = 28 * u;
                if (e.earned) {
                    cl->AddCircleFilled(c, r, look::Gold(), 32);
                    cl->AddCircle(c, r - 4 * u, IM_COL32(90, 64, 24, 255), 32, std::max(1.f, 1.5f * u));
                    ImVec2 pts[3] = {ImVec2(c.x - r * 0.42f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.34f), ImVec2(c.x + r * 0.45f, c.y - r * 0.32f)};
                    cl->AddPolyline(pts, 3, IM_COL32(60, 40, 12, 255), 0, std::max(2.f, 3 * u));
                } else {
                    cl->AddCircle(c, r, look::Gold(0.45f), 32, std::max(1.f, 1.5f * u));
                    cl->AddCircle(c, r * 0.45f, look::Gold(0.25f), 24, std::max(1.f, u));
                }
                const float tx = a.x + 104 * u;
                cl->AddText(g_look.body, 38 * u, ImVec2(tx, a.y + 10 * u), e.earned ? look::Text() : look::Text(0.75f), e.title);
                cl->AddText(g_look.body, 30 * u, ImVec2(tx, a.y + 56 * u), look::Dim(e.earned ? 1.f : 0.8f), e.desc);
                if (e.earned && !e.when.empty()) {
                    ImVec2 ws = g_look.body->CalcTextSizeA(28 * u, FLT_MAX, 0, e.when.c_str());
                    cl->AddText(g_look.body, 28 * u, ImVec2(b.x - ws.x - 22 * u, a.y + 16 * u), look::Gold(0.85f), e.when.c_str());
                }
                ImGui::PopID();
            }
        } else {
            int n = 0;
            for (const cfg::Desc& d : list) {
                if (!IsLive(d) || PageOf(d) != page) continue;
                ImGui::PushID(d.section);
                ImGui::PushID(d.key);
                const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + rw, a.y + row_h);
                std::string key = std::string(d.section) + "." + d.key;
                // the row behind its control (drawn first, under it)
                ImDrawListSplitter split;
                split.Split(cl, 2);
                split.SetCurrentChannel(cl, 1);
                ImGui::SetCursorScreenPos(ImVec2(b.x - ctrl_w - 10 * u, a.y + (row_h - frame_h) * 0.5f));
                bool changed = false;
                if (d.type == cfg::Type::Bool && d.flag) {
                    changed = look::Toggle(L, "##v", d.flag, ImVec2(ctrl_w, frame_h));
                    if (changed) {
                        if (std::string(d.section) + "." + d.key == "graphics.colour_look") gpu::g_colour_look = std::min(Get().colour_look, gpu::kColourLooks - 1);
                        if (kGame.setting_changed) kGame.setting_changed(d);
                        LOGR("menu: %s = %s", key.c_str(), cfg::ValueText(d).c_str());
                    }
                } else {
                    // the label is drawn by the row: Control's own is pushed out of sight
                    ImGui::SetNextItemWidth(ctrl_w);
                    changed = ControlOnly(d);
                }
                if (changed) g_dirty = true;
                // lit: the controller's or keyboard's cursor on it, its list open, or the mouse over the row (not through
                // a list or the quit question, and not while the controller is steering - review)
                ImGuiContext* gc = ImGui::GetCurrentContext();
                const bool open_list = d.type != cfg::Type::Bool && g_ctrl_open;
                const bool lit = open_list || (!open_list && ((ImGui::IsItemFocused() && gc->NavCursorVisible) || ImGui::IsItemActive())) ||
                                 (!gc->NavCursorVisible && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                                  ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b));
                if (lit) about_key = key;
                if (about_key == key) about = &d;
                split.SetCurrentChannel(cl, 0);
                if (about_key == key) {
                    cl->AddRectFilledMultiColor(a, b, look::Gold(0.16f), look::Gold(0.02f), look::Gold(0.02f), look::Gold(0.16f));
                    cl->AddRectFilled(ImVec2(a.x, a.y + 8 * u), ImVec2(a.x + 3 * u, b.y - 8 * u), look::GoldLight());
                } else if (n % 2) {
                    cl->AddRectFilled(a, b, IM_COL32(255, 240, 210, 6));
                }
                const float fs = 38 * u;
                ImVec2 ts = g_look.body->CalcTextSizeA(fs, FLT_MAX, 0, d.label);
                cl->AddText(g_look.body, fs, ImVec2(a.x + 22 * u + u, a.y + (row_h - ts.y) * 0.5f + 1.5f * u), IM_COL32(0, 0, 0, 160), d.label);
                cl->AddText(g_look.body, fs, ImVec2(a.x + 22 * u, a.y + (row_h - ts.y) * 0.5f),
                            about_key == key ? look::Text() : look::Text(0.86f), d.label);
                split.Merge(cl);
                ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
                ImGui::Dummy(ImVec2(rw, 0));
                ImGui::PopID();
                ImGui::PopID();
                n++;
            }
            if (page == 1) {
                KeyList();
                ImGui::InvisibleButton("##keys_end", ImVec2(rw, 6 * u), ImGuiButtonFlags_EnableNav);   // the cursor can go down to it: the list scrolls into sight
            }
        }
    }
    ImGui::EndChild();

    // the description
    {
        dl->PushClipRect(ImVec2(da.x + 6 * u, da.y + 6 * u), ImVec2(db.x - 6 * u, db.y - 8 * u), true);   // never past its panel
        float y = da.y + 26 * u;
        const float tx = da.x + 28 * u, tw = db.x - da.x - 56 * u;
        if (page == 3) {
            y += look::Heading(dl, L, ImVec2(tx, y), "Achievements");
            dl->AddText(g_look.body, 32 * u, ImVec2(tx, y + 6 * u), look::Dim(),
                        "Deeds done in this game, and those still ahead. They are kept with your saves.", nullptr, tw);
        } else if (about) {
            y += look::Heading(dl, L, ImVec2(tx, y), about->label, 24, tw);
            if (about->help && *about->help) {
                // a long description: smaller until it fits above the two lines under it (review: it ran into the button)
                const float room = db.y - y - 150 * u;
                float hs = 32 * u;
                while (hs > 20 * u && g_look.body->CalcTextSizeA(hs, FLT_MAX, tw, about->help).y > room) hs -= 1.5f * u;
                dl->AddText(g_look.body, hs, ImVec2(tx, y + 6 * u), look::Text(0.9f), about->help, nullptr, tw);
                y += g_look.body->CalcTextSizeA(hs, FLT_MAX, tw, about->help).y + 6 * u;
            }
            y += 22 * u;
            dl->AddLine(ImVec2(tx, y), ImVec2(tx + tw, y), look::Gold(0.25f), std::max(1.f, u));
            y += 14 * u;
            std::string now = "Now: " + cfg::ValueText(*about);
            for (size_t k = 0; k < about->choices.size(); k++)   // the choice's own words where it has them
                if (now == std::string("Now: ") + about->choices[k] || now == std::string("Now: \"") + about->choices[k] + "\"") {
                    now = std::string("Now: ") + (k < about->labels.size() ? about->labels[k] : about->choices[k]);
                    break;
                }
            if (now.size() > 6 && now[5] == '"' && now.back() == '"') now = "Now: " + now.substr(6, now.size() - 7);
            if (now == "Now: true") now = "Now: On";
            if (now == "Now: false") now = "Now: Off";
            dl->AddText(g_look.body, 29 * u, ImVec2(tx, y), look::Dim(), now.c_str());
            y += 34 * u;
            dl->AddText(g_look.body, 29 * u, ImVec2(tx, y), look::Dim(), "Takes effect at once, and is kept when the menu closes.", nullptr, tw);
        } else {
            y += look::Heading(dl, L, ImVec2(tx, y), kPages[page]);
            dl->AddText(g_look.body, 32 * u, ImVec2(tx, y + 6 * u), look::Dim(),
                        "These take effect at once. The rest are in the launcher.", nullptr, tw);
        }
        dl->PopClipRect();
    }
    // the buttons
    ImGui::SetCursorScreenPos(ImVec2(ra.x, rb2.y - 2 * btn_h - btn_gap));
    if (look::Button(L, "Back to the game", ImVec2(right_w, btn_h), true)) Close();
    ImGui::SetCursorScreenPos(ImVec2(ra.x, rb2.y - btn_h));
    if (look::Button(L, "Quit the game", ImVec2(right_w, btn_h), false)) ImGui::OpenPopup("##quit");

    // the hints along the bottom
    if (pad)
        look::Hints(dl, L, {{"A", "Choose"}, {"B", "Back to the game"}, {"LB", "Previous page"}, {"RB", "Next page"},
                            {"Back + Start", "Close"}});
    else
        look::Hints(dl, L, {{"Enter", "Choose"}, {"Esc", "Back to the game"}, {"PgUp", "Previous page"}, {"PgDn", "Next page"},
                            {"F10", "Close"}});

    // quitting: asked first
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620 * u, 250 * u), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("##quit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove)) {
        ImDrawList* pl = ImGui::GetWindowDrawList();
        const ImVec2 pa = ImGui::GetWindowPos(), pb(pa.x + 620 * u, pa.y + 250 * u);
        look::Panel(pl, ImVec2(pa.x + 8 * u, pa.y + 16 * u), ImVec2(pb.x - 8 * u, pb.y - 8 * u), L);
        look::Heading(pl, L, ImVec2(pa.x + 40 * u, pa.y + 40 * u), "Quit the game?");
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) ImGui::CloseCurrentPopup();   // B: stay
        pl->AddText(g_look.body, 30 * u, ImVec2(pa.x + 40 * u, pa.y + 96 * u), look::Text(0.9f),
                    "Progress since the last save is lost.");
        const float bw = 250 * u, bh = 54 * u;
        ImGui::SetCursorScreenPos(ImVec2(pa.x + 40 * u, pb.y - bh - 34 * u));
        if (look::Button(L, "Quit", ImVec2(bw, bh), false)) {
            if (g_dirty.exchange(false)) {
                if (kGame.save_settings) kGame.save_settings();
                else Save();
            }
            PostMessageW(static_cast<HWND>(Window()), WM_CLOSE, 0, 0);
        }
        ImGui::SetCursorScreenPos(ImVec2(pb.x - bw - 40 * u, pb.y - bh - 34 * u));
        if (look::Button(L, "Stay", ImVec2(bw, bh), true)) ImGui::CloseCurrentPopup();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);   // Stay: the safe one first
        ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::PopFont();
    look::PopStyle();
}

}  // namespace app
