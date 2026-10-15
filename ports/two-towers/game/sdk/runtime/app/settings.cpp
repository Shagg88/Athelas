// the settings file: beside the exe, or where --config says (core/settings_file.h for its form)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"

namespace app {

namespace {

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace

Settings& Get() {
    static Settings s;
    return s;
}

std::filesystem::path SettingsFile() {
    return Args().config.empty() ? ExeDir() / kGame.settings : Args().config;
}

void Load() {
    std::filesystem::path file = SettingsFile();
    std::error_code ec;
    bool fresh = !std::filesystem::exists(file, ec);
    std::string error;
    bool old_layout = false;
    if (!cfg::Read(file.string(), SettingsList(), error, &old_layout))
        Fail(kExitBadSettings, L"The settings file " + file.wstring() + L" could not be read: " + Wide(error) +
                                   L"\n\nFix it, or delete it to start again with the defaults.");
    if (kGame.load) {
        try {
            toml::table t = fresh ? toml::table{} : toml::parse_file(file.string());
            kGame.load(&t);
        } catch (...) {
        }
    }
    // a launcher's new settings file starts with every setting at its default; an older flat file is written again
    // in sections at once, its values kept, so a launcher finds the layout it edits
    if ((fresh && !Args().config.empty()) || old_layout) {
        if (old_layout) LOGR("settings: %s rewritten in sections, values kept", file.string().c_str());
        Save();
    }
}

void Save() {
    if (!cfg::Write(SettingsFile().string(), SettingsList()))
        LOGW("settings: could not write %s", SettingsFile().string().c_str());
}

void LogSettings() {
    std::istringstream lines(cfg::Text(SettingsList()));
    LOGR("Settings: %s", SettingsFile().string().c_str());
    for (std::string line; std::getline(lines, line);)
        if (!line.empty()) LOGR("  %s", line.c_str());
}

namespace {

// "\\.\DISPLAY2" as the setting names it: DISPLAY2
std::string ScreenName(const wchar_t* device) {
    std::wstring d = device;
    if (d.rfind(L"\\\\.\\", 0) == 0) d = d.substr(4);
    return std::string(d.begin(), d.end());
}

struct Screen {
    HMONITOR mon;
    std::string name;
    bool primary;
};

std::vector<Screen> Screens() {
    std::vector<Screen> out;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(m, &mi))
            reinterpret_cast<std::vector<Screen>*>(p)->push_back({m, ScreenName(mi.szDevice), (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(), [](const Screen& a, const Screen& b) { return a.name < b.name; });
    return out;
}

}  // namespace

const std::vector<std::string>& ScreenNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n{"main"};
        for (const Screen& s : Screens()) n.push_back(s.name);
        return n;
    }();
    return names;
}

// read each time: a screen plugged in or out while the game runs is seen at the next display change
void* ScreenMonitor() {
    const std::string& want = Get().screen;
    HMONITOR main = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (want.empty() || want == "main") return main;
    for (const Screen& s : Screens())
        if (_stricmp(s.name.c_str(), want.c_str()) == 0) return s.mon;
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGR("display: screen %s is not connected, using the main one", want.c_str());
    }
    return main;
}

Mode Desktop() {
    HMONITOR mon = static_cast<HMONITOR>(ScreenMonitor());
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
        return {dm.dmPelsWidth, dm.dmPelsHeight};
    return {uint32_t(GetSystemMetrics(SM_CXSCREEN)), uint32_t(GetSystemMetrics(SM_CYSCREEN))};
}

Mode Output() {
    const Settings& s = Get();
    if (s.width && s.height) return {s.width, s.height};
    return Desktop();
}

namespace {
std::atomic<float> g_wide_aspect{16.0f / 9.0f};
}

bool Widescreen() {
    // asked per frame and per HUD quad: the desktop mode lookup only runs when the resolution setting changes
    static std::atomic<uint64_t> key{~0ull};
    static std::atomic<bool> wide{true};
    const Settings& s = Get();
    if (!s.widescreen) return false;  // the console's 4:3, side bars on a wide display
    uint64_t k = uint64_t(s.width) << 32 | s.height;
    if (key.exchange(k) != k) {
        Mode m = Output();
        wide = m.width * 2 >= m.height * 3;  // 3:2 or wider
        float a = float(m.width) / float(std::max(m.height, 1u));
        g_wide_aspect = std::clamp(a, 16.0f / 9.0f, 32.0f / 9.0f);
    }
    return wide;
}

float WideAspect() {
    Widescreen();  // brings the aspect up to date
    return g_wide_aspect.load();
}

float RenderScale(uint32_t guest_height) {
    float h = Output().height * (Get().render_percent / 100.0f);
    return guest_height ? std::max(h / guest_height, 0.1f) : 1.0f;
}

}  // namespace app
