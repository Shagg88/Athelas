// ROTK Xbox: ROTK Fix's settings file. The launcher keeps one Return of the King page for the PC and Xbox versions;
// started with --ini <ROTKFix.ini> (ROTK_INI on the headless harness) the port reads that file's keys, the same ones
// ROTK Fix reads on PC, over its own settings.toml. Keys the Xbox version has no use for (Renderer, DxvkFolder,
// HideRealControllers, HideMousePointer, LogInput, [Gamepad] Enabled, SkipOpeningLogos) are left alone.
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/backend.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // std::min and std::max, not Windows' macros
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace rotk {
extern bool g_skip_all_videos;                // rotk_movies.cpp
extern uint32_t g_fps;                         // rotk_framerate.cpp
extern uint32_t g_fov_percent, g_draw_percent;  // rotk_widescreen.cpp
extern bool g_alpha_edges, g_hud_edges;          // rotk_widescreen.cpp
extern bool g_second_controller;                 // rotk_controls.cpp
extern bool g_unlock_all;                        // rotk_secrets.cpp
extern uint32_t g_damage_percent, g_enemy_damage_percent;  // rotk_secrets.cpp
void LoadControls(const std::string* (*get)(void*, const char*, const char*), void* ctx);
void InitFrameRate();
void InitCamera();
void InitPrompts();
extern uint32_t g_button_pictures;  // rotk_prompts.cpp
extern bool g_sharp_text;           // rotk_fonts.cpp
std::filesystem::path g_ini_file;   // the ROTKFix.ini read, for rotk_fonts.cpp (ROTK PC's Data folder is beside it)

namespace {
std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// [section] key = value, keys and sections in lower case; ';' and '#' start comments
using Ini = std::map<std::string, std::map<std::string, std::string>>;
bool ReadIni(const std::filesystem::path& file, Ini& ini) {
    std::ifstream in(file);
    if (!in) return false;
    std::string line, section;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Lower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string value = Trim(line.substr(eq + 1));
        size_t c = value.find_first_of(";#");
        if (c != std::string::npos) value = Trim(value.substr(0, c));
        ini[section][Lower(Trim(line.substr(0, eq)))] = value;
    }
    return true;
}

std::filesystem::path IniArgument() {
#ifdef _WIN32
    int n = 0;
    wchar_t** v = CommandLineToArgvW(GetCommandLineW(), &n);
    std::filesystem::path p;
    for (int i = 1; v && i + 1 < n; i++)
        if (std::wstring(v[i]) == L"--ini") p = v[i + 1];
    if (v) LocalFree(v);
    return p;
#else
    const char* e = std::getenv("ROTK_INI");
    return e ? std::filesystem::path(e) : std::filesystem::path();
#endif
}

struct Reader {
    Ini& ini;
    int applied = 0;
    const std::string* Get(const char* section, const char* key) {
        auto s = ini.find(Lower(section));
        if (s == ini.end()) return nullptr;
        auto k = s->second.find(Lower(key));
        return k == s->second.end() ? nullptr : &k->second;
    }
    void Int(const char* section, const char* key, uint32_t& out, long lo, long hi) {
        if (const std::string* v = Get(section, key)) {
            out = uint32_t(std::clamp(std::strtol(v->c_str(), nullptr, 10), lo, hi));
            applied++;
        }
    }
    void Bool(const char* section, const char* key, bool& out) {
        if (const std::string* v = Get(section, key)) {
            out = std::strtol(v->c_str(), nullptr, 10) != 0;
            applied++;
        }
    }
    void Float(const char* section, const char* key, float& out, float lo, float hi) {
        if (const std::string* v = Get(section, key)) {
            out = std::clamp(float(std::atof(v->c_str())), lo, hi);
            applied++;
        }
    }
    // a word from the list, its place stored; an unknown word changes nothing
    void Word(const char* section, const char* key, uint32_t& out, std::initializer_list<const char*> words) {
        if (const std::string* v = Get(section, key)) {
            uint32_t i = 0;
            for (const char* w : words) {
                if (Lower(*v) == w) {
                    out = i;
                    applied++;
                    return;
                }
                i++;
            }
        }
    }
};

// ROTKFix.ini [Keyboard]: each keys::Action by the job it does in this game, as [GamepadButtons] names them
constexpr const char* kKeyboardKeys[keys::kCount] = {
    "MoveForward", "MoveBack", "MoveLeft", "MoveRight", "Walk",
    "SpeedAttack", "PhysicalAttack", "Parry", "FierceAttack", "Action", "JumpBack", "RangedAttack", "KillingMove",
    "Pause", "Back", "Special", "RightStick",
    "MenuUp", "MenuDown", "MenuLeft", "MenuRight"};

void ApplyIni() {
    std::filesystem::path file = IniArgument();
    if (file.empty()) return;
    Ini ini;
    if (!ReadIni(file, ini)) {
        LOGR("rotk: settings file %s not found, using settings.toml", file.string().c_str());
        return;
    }
    g_ini_file = file;
    app::Settings& s = app::Get();
    Reader r{ini};
    // [Graphics]
    if (const std::string* v = r.Get("Graphics", "Screen")) {
        s.screen = *v;
        r.applied++;
    }
    uint32_t mode = uint32_t(s.display_mode);
    r.Word("Graphics", "DisplayMode", mode, {"fullscreen", "borderless", "windowed"});
    s.display_mode = app::DisplayMode(mode);
    r.Int("Graphics", "Width", s.width, 0, 16384);
    r.Int("Graphics", "Height", s.height, 0, 16384);
    r.Int("Graphics", "RefreshRate", s.refresh, 0, 500);
    r.Bool("Graphics", "VSync", s.vsync);
    // RenderScale: percent of the chosen resolution drawn, fitted to the screen (render_percent, 50 to 300)
    r.Int("Graphics", "RenderScale", s.render_percent, 50, 300);
    r.Int("Graphics", "MSAA", s.msaa, 0, 8);
    if (s.msaa != 2 && s.msaa != 4 && s.msaa != 8) s.msaa = 0;
    r.Int("Graphics", "AnisotropicFiltering", s.aniso, 0, 16);
    r.Float("Graphics", "TextureLODBias", s.lod_bias, -2.0f, 2.0f);
    r.Int("Graphics", "Sharpening", s.sharpening, 0, 100);
    r.Float("Graphics", "Brightness", s.brightness, 0.5f, 2.0f);
    r.Word("Graphics", "ColourLook", s.colour_look, {"original", "film", "vivid", "dark"});
    r.Int("Graphics", "ColourLookStrength", s.look_strength, 0, 100);
    r.Bool("Graphics", "WidescreenFix", s.widescreen);
    r.Int("Graphics", "FieldOfView", g_fov_percent, 70, 150);
    r.Int("Graphics", "DrawDistance", g_draw_percent, 50, 800);
    r.Bool("Graphics", "AlphaEdgeSmoothing", g_alpha_edges);
    r.Bool("Graphics", "HUDEdges", g_hud_edges);       // the Xbox port's own keys, for now
    r.Bool("Graphics", "SharperText", g_sharp_text);
    // the shared SDK's smooth menus: off under ROTK Fix (its own SharperText redraws the text and menu pictures)
    s.smooth_2d = false;
    r.Bool("Graphics", "SmoothMenus", s.smooth_2d);
    // [Frame]: ROTK Fix's limiter at TargetFPS (0 no cap); Limiter = 0 leaves the game's own pace, the console's 30
    uint32_t target = g_fps, limiter = 1;
    r.Int("Frame", "TargetFPS", target, 0, 1000);
    r.Int("Frame", "Limiter", limiter, 0, 1);
    g_fps = limiter ? target : 30;
    r.Bool("Frame", "ShowFPS", s.show_fps);  // the frame rate counter, top right
    // [Video]
    r.Word("Video", "MovieDisplay", s.movie_display, {"auto", "stretch", "4:3"});  // auto = fit, as the port calls it
    r.Int("Video", "SharperMovies", s.movie_sharpening, 0, 100);
    r.Int("Video", "CutsceneAudioDelay", s.movie_delay, 0, 500);
    r.Bool("Video", "AllowSkippingAllVideos", g_skip_all_videos);
    // SkipOpeningLogos: on the Xbox the logos open the Helm's Deep intro movie, so AllowSkippingAllVideos covers them
    // [Gamepad]
    r.Int("Gamepad", "Vibration", s.vibration, 0, 100);
    r.Int("Gamepad", "StickDeadzone", s.stick_deadzone, 0, 50);
    r.Int("Gamepad", "TriggerThreshold", s.trigger_threshold, 0, 90);
    r.Bool("Gamepad", "SecondController", g_second_controller);
    // ButtonNames: auto, xbox, playstation, nintendo; original is the game's own pictures, the Xbox ones
    if (const std::string* v = r.Get("Gamepad", "ButtonNames")) {
        std::string w = Lower(*v);
        g_button_pictures = w == "xbox" || w == "original" ? 1 : w == "playstation" ? 2 : w == "nintendo" ? 3
                          : w == "keyboard" ? 4 : 0;
        r.applied++;
    }
    // [Gamepad] Enabled is the PC's DirectInput gamepad layer; the Xbox version always reads the controller
    LoadControls([](void* c, const char* section, const char* key) { return static_cast<Reader*>(c)->Get(section, key); }, &r);
    // [Gameplay]: damage the player takes and enemies take, in percent of the game's (The Two Towers' damage_taken and
    // enemy_damage), and every level and character open (the game's own developer switch)
    r.Int("Gameplay", "DamageTaken", g_damage_percent, 10, 500);
    r.Int("Gameplay", "EnemyDamage", g_enemy_damage_percent, 10, 500);
    r.Bool("Gameplay", "UnlockAll", g_unlock_all);
    // [Window]: the Xbox port's own (ROTK on PC slows itself down in the background)
    r.Bool("Window", "PauseInBackground", s.pause_unfocused);
    r.Bool("Window", "MuteInBackground", s.mute_unfocused);
    // [Keyboard]: the Xbox port's keyboard and mouse play (on PC the game's own keyboard controls)
    r.Bool("Keyboard", "Enabled", s.keyboard);
    r.Int("Keyboard", "Player", s.keyboard_player, 1, 2);
    for (uint32_t i = 0; i < keys::kCount; i++)
        if (const std::string* v = r.Get("Keyboard", kKeyboardKeys[i])) {
            s.keys[i] = *v;
            r.applied++;
        }
    // [Achievements], [Discord]: shared with ROTK on PC (ROTK Fix's achieve.c)
    r.Bool("Achievements", "Notify", s.achievement_notes);
    r.Bool("Discord", "Enabled", s.discord);
    if (const std::string* v = r.Get("Discord", "AppId")) {
        s.discord_app_id = *v;
        r.applied++;
    }
    // [Textures]
    r.Bool("Textures", "Dump", s.texture_dump);
    r.Bool("Textures", "Replace", s.texture_replace);
    LOGR("rotk: %d settings from %s (ROTK Fix's file)", r.applied, file.string().c_str());
}
}  // namespace

// GameInfo::load: settings.toml as read. Two shared-SDK settings default on for The Two Towers but were not in this
// port's 1.0.0, so a file that doesn't set them keeps 1.0.0's picture: smooth_2d off, movie_sharpening 0.
void LoadTable(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    app::Settings& s = app::Get();
    if (!t["graphics"]["smooth_2d"]) s.smooth_2d = false;
    if (!t["graphics"]["movie_sharpening"]) s.movie_sharpening = 0;
}

// GameInfo::setting_changed: the in-game menu changed one; what the settings feed at start, fed again
void SettingChanged(const cfg::Desc& d) {
    std::string k = std::string(d.section) + "." + d.key;
    if (k == "graphics.draw_distance") gpu::g_fog_scale = float(g_draw_percent) / 100.0f;
}

// GameInfo::save_settings: the in-game menu closed after a change. Its settings go into ROTKFix.ini (the file the
// launcher and ROTK on PC read) under ROTK Fix's names; without --ini, into settings.toml.
void SaveIni() {
    if (g_ini_file.empty()) {
        app::Save();
        return;
    }
#ifdef _WIN32
    const app::Settings& s = app::Get();
    std::string file = g_ini_file.string();
    auto put = [&](const char* section, const char* key, const std::string& v) {
        WritePrivateProfileStringA(section, key, v.c_str(), file.c_str());
    };
    auto num = [](double v, int places) {
        char b[32];
        std::snprintf(b, sizeof b, "%.*f", places, v);
        return std::string(b);
    };
    auto i = [](uint32_t v) { return std::to_string(v); };
    put("Frame", "ShowFPS", i(s.show_fps));
    put("Graphics", "VSync", i(s.vsync));
    put("Graphics", "RenderScale", i(s.render_percent));
    put("Graphics", "MSAA", i(s.msaa));
    put("Graphics", "AnisotropicFiltering", i(s.aniso));
    put("Graphics", "TextureLODBias", num(s.lod_bias, 2));
    put("Graphics", "Sharpening", i(s.sharpening));
    put("Graphics", "SmoothMenus", i(s.smooth_2d));
    static const char* const kLooks[] = {"original", "film", "vivid", "dark"};
    put("Graphics", "ColourLook", kLooks[std::min(s.colour_look, 3u)]);
    put("Graphics", "ColourLookStrength", i(s.look_strength));
    put("Graphics", "Brightness", num(s.brightness, 2));
    put("Graphics", "FieldOfView", i(g_fov_percent));
    put("Graphics", "DrawDistance", i(g_draw_percent));
    put("Graphics", "HUDEdges", i(g_hud_edges));
    static const char* const kMovie[] = {"auto", "stretch", "4:3"};
    put("Video", "MovieDisplay", kMovie[std::min(s.movie_display, 2u)]);
    put("Video", "SharperMovies", i(s.movie_sharpening));
    put("Video", "CutsceneAudioDelay", i(s.movie_delay));
    put("Gamepad", "Vibration", i(s.vibration));
    put("Gamepad", "StickDeadzone", i(s.stick_deadzone));
    put("Gamepad", "TriggerThreshold", i(s.trigger_threshold));
    put("Window", "PauseInBackground", i(s.pause_unfocused));
    put("Window", "MuteInBackground", i(s.mute_unfocused));
    put("Keyboard", "Enabled", i(s.keyboard));
    put("Keyboard", "Player", i(s.keyboard_player));
    put("Achievements", "Notify", i(s.achievement_notes));
    if (app::kGame.discord_app_id) put("Discord", "Enabled", i(s.discord));
    LOGR("rotk: in-game menu settings written to %s", file.c_str());
#endif
}

// GameInfo::init: after settings.toml loads, before the window and boot
void Init() {
    ApplyIni();
    InitFrameRate();
    InitCamera();
    InitPrompts();
}
}  // namespace rotk
