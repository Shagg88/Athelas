// the host side without a window: settings from settings.toml in the run folder when there is one (else the
// defaults), the run folder as the exe folder
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "app/app.h"
#include "core/log.h"

namespace app {

Settings& Get() {
    static Settings s;
    return s;
}
void Load() {
    std::string error;
    if (!cfg::Read(SettingsFile().string(), SettingsList(), error)) LOGE("settings: %s", error.c_str());
}
void Save() {}
void ApplyDisplay() {}
Mode Desktop() { return {1280, 720}; }
Mode Output() { return Desktop(); }
float RenderScale(uint32_t) { return 1.0f; }
bool Widescreen() { return true; }
float g_max_frame_aspect = 16.0f / 9.0f;
// ASPECT=2.37 (in the environment; TT_ASPECT in older scripts) tests an ultrawide screen
float FrameAspect() {
    static const float a = [] {  // read once: this is asked per HUD quad
        const char* e = std::getenv("ASPECT");
        if (!e) e = std::getenv("TT_ASPECT");
        return e ? float(std::atof(e)) : 16.0f / 9.0f;
    }();
    return std::clamp(a, 16.0f / 9.0f, std::max(g_max_frame_aspect, 16.0f / 9.0f));
}

std::filesystem::path ExeDir() {
    static const std::filesystem::path dir = std::filesystem::current_path();
    return dir;
}
int Instance() {
    static const int n = [] {
        const char* e = std::getenv("INSTANCE");
        return e ? std::atoi(e) : 0;
    }();
    return n;
}
void* Window() { return nullptr; }
// achievements: logged, nothing kept
}  // namespace app
namespace ach {
void Unlock(const char* id) {
    static std::map<std::string, bool> seen;
    if (!seen[id]) LOGR("achievements: earned %s (harness)", id);
    seen[id] = true;
}
bool Has(const char*) { return false; }
std::map<std::string, uint64_t>& Counters() {
    static std::map<std::string, uint64_t> m;
    return m;
}
uint64_t Count(const char* n) { return Counters()[n]; }
uint64_t Add(const char* n, uint64_t v) {
    uint64_t t = Counters()[n] += v;
    LOGR("achievements: %s now %llu (harness)", n, (unsigned long long)t);
    return t;
}
void Flush() {}
void Start() {}
}  // namespace ach
namespace app {
// no in-game menu or window focus here
bool OverlayOpen() { return false; }
bool Paused() { return false; }
bool Held() { return false; }
bool Muted() { return false; }
void Hold(const std::function<void()>&) {}

// no command line: the run folder holds everything
const CommandLine& Args() {
    static const CommandLine c = [] {
        CommandLine c;
        c.no_dialogs = true;
        c.instance = Instance();
        return c;
    }();
    return c;
}
std::filesystem::path TextureDir() { return ExeDir() / "textures"; }
std::filesystem::path SavesDir() { return ExeDir() / (Instance() > 1 ? "hdd" + std::to_string(Instance()) : std::string("hdd")); }
std::filesystem::path ShaderCacheDir() { return ExeDir() / "shadercache"; }
std::filesystem::path ScreenshotDir() { return ExeDir() / "Screenshots"; }
const std::vector<std::string>& ScreenNames() {
    static const std::vector<std::string> n{"main"};
    return n;
}
void* ScreenMonitor() { return nullptr; }
std::filesystem::path LogFolder() { return ExeDir() / "logs"; }
std::filesystem::path SettingsFile() { return ExeDir() / "settings.toml"; }
void Fail(int code, const std::wstring& message) {
    LOGE("Exit (%d): %s", code, std::string(message.begin(), message.end()).c_str());
    logx::Flush();
    std::_Exit(code);
}

bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}
void OpenGameWindow(bool) {}
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

// as the window's: frames at most fps_cap a second (a game's frame rate setting), so game time can be checked
void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    std::this_thread::sleep_until(next);
}

}  // namespace app
