// The in-game menu: F10 (or both stick clicks together) opens it over the game, which stands still behind it. It
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
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <imgui.h>

#include "app/app.h"
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
ImFont* g_font = nullptr;
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
    "display.show_fps", "display.vsync", "display.render_percent", "graphics.anti_aliasing",
    "graphics.anisotropic_filtering", "graphics.lod_bias", "graphics.console_textures", "graphics.sharpening", "graphics.smooth_2d",
    "graphics.colour_look", "graphics.look_strength", "graphics.brightness", "graphics.movie_display",
    "graphics.movie_sharpening", "graphics.cutscene_audio_delay", "input.vibration", "input.stick_deadzone",
    "input.trigger_threshold", "window.pause_unfocused", "window.mute_unfocused", "window.background_input", "keyboard.mouse_aim", "keyboard.mouse_sensitivity", "keyboard.mouse_menus", "keyboard.enabled",
    "keyboard.player", "achievements.notify", "discord.enabled"};

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

// one setting's control; true when it changed
bool Control(const cfg::Desc& d) {
    ImGui::PushID(d.key);
    ImGui::PushID(d.section);
    bool changed = false;
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(d.label);
    if (d.help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.help);
    ImGui::SameLine(w * 0.45f);
    ImGui::SetNextItemWidth(w * 0.55f);
    switch (d.type) {
        case cfg::Type::Bool:
            if (d.flag) changed = ImGui::Checkbox("##v", d.flag);
            break;
        case cfg::Type::Int:
            if (d.value) {
                int v = int(*d.value);
                if (ImGui::SliderInt("##v", &v, int(d.min), int(d.max))) {
                    *d.value = uint32_t(std::clamp<int64_t>(v, d.min, d.max));
                    changed = true;
                }
            }
            break;
        case cfg::Type::Float:
            if (d.real) {
                float v = *d.real;
                if (ImGui::SliderFloat("##v", &v, float(d.min) / 100.0f, float(d.max) / 100.0f, "%.2f")) {
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
            if (ImGui::BeginCombo("##v", cur >= 0 ? label(cur) : "")) {
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
    auto pressed = [&](uint16_t bit) { return (p.buttons & bit) && !(last.buttons & bit); };
    bool b_now = p.analog[1] > 30, b_was = last.analog[1] > 30;
    bool both = (p.buttons & 0xC0) == 0xC0, both_was = (last.buttons & 0xC0) == 0xC0;
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
        // B with nothing open in the menu, Start, or both stick clicks again: back to the game
        if (pressed(0x10) || (both && !both_was) || (b_now && !b_was && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)))
            Close();
    }
    last = p;
}

}  // namespace

bool OverlayOpen() { return g_open.load(); }
bool Paused() { return g_open.load() || (Get().pause_unfocused && !g_focus.load()); }
bool Held() { return g_held.load(); }
bool Muted() { return g_held.load() || (Get().mute_unfocused && !g_focus.load()); }

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
    kernel::Freeze(true);
    g_held = true;
    LOG_FIRST(4, "pause: the game waits (%s)", g_open ? "menu" : "in the background");
    auto t0 = std::chrono::steady_clock::now();
    while (Paused()) {
        if (g_open) FeedPads();
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
    bool both = (p.buttons & 0xC0) == 0xC0;
    if (both && !g_combo[port].exchange(true)) {
        Open();
        g_mask_buttons[port] = uint16_t(g_mask_buttons[port] | 0xC0);
    }
    if (!both) g_combo[port] = false;
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
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig fc;
    fc.OversampleH = 2;
    // Latin with its accents (the Latin-1 and Extended-A letters: French, German, Spanish, Italian, Portuguese,
    // Polish, Dutch, Swedish), general punctuation, and Cyrillic: the menu's own text and the subtitles' languages
    static const ImWchar kRanges[] = {0x0020, 0x024F, 0x0400, 0x052F, 0x2000, 0x206F, 0x20AC, 0x20AC, 0x2122, 0x2122, 0};
    g_font = io.Fonts->AddFontFromMemoryCompressedTTF(overlay_font_compressed_data, int(overlay_font_compressed_size),
                                                      kFontPixels, &fc, kRanges);
    io.FontDefault = g_font;
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
    DrawSubtitles(UiScale());
    if (!g_open) {
        shown = false;
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float scale = UiScale();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x / 2, vp->Pos.y + vp->Size.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(vp->Size.x * 0.9f, kFontPixels * scale * 34), vp->Size.y * 0.8f), ImGuiCond_Always);
    if (!shown) ImGui::SetNextWindowFocus();
    ImGui::Begin("Settings  -  F10, Esc or B to go back to the game", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    shown = true;
    ImGui::SetWindowFontScale(scale);
    static const char* const kPages[4] = {"Picture", "Controls", "Game", "Achievements"};
    std::vector<cfg::Desc> list = SettingsList();
    bool has[4] = {false, Get().keyboard, false, !kGame.achievements.empty()};
    for (const cfg::Desc& d : list)
        if (IsLive(d)) has[PageOf(d)] = true;
    // the controller's shoulder buttons, or Page Up and Down, step through the pages there are
    static int page = 0, want = -1;
    static bool lb = false, rb = false;
    {
        input::Pad p = input::Raw(0);
        bool l = p.analog[5] > 30, r = p.analog[4] > 30;
        int step = ((l && !lb) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) ? 3
                 : ((r && !rb) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) ? 1 : 0;
        if (step) {
            want = page;
            do want = (want + step) % 4;
            while (!has[want] && want != page);
        }
        lb = l;
        rb = r;
    }
    if (ImGui::BeginTabBar("##pages")) {
        for (int i = 0; i < 4; i++) {
            if (!has[i]) continue;
            ImGuiTabItemFlags f = want == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kPages[i], nullptr, f)) {
                page = i;
                ImGui::BeginChild("##list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.4f), ImGuiChildFlags_NavFlattened);
                for (const cfg::Desc& d : list)
                    if (IsLive(d) && PageOf(d) == i && Control(d)) g_dirty = true;
                if (i == 1) KeyList();
                if (i == 3) ach::DrawList();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        want = -1;
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    if (ImGui::Button("Back to the game")) Close();
    ImGui::SameLine();
    ImGui::TextDisabled("LB / RB or Page Up / Down: pages");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Quit the game").x - ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Quit the game")) ImGui::OpenPopup("##quit");
    if (ImGui::BeginPopupModal("##quit", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::SetWindowFontScale(scale);
        ImGui::TextUnformatted("Quit the game? Progress since the last save is lost.");
        if (ImGui::Button("Quit")) {
            if (g_dirty.exchange(false)) {
                if (kGame.save_settings) kGame.save_settings();
                else Save();
            }
            PostMessageW(static_cast<HWND>(Window()), WM_CLOSE, 0, 0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

}  // namespace app
