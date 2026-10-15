#pragma once
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/settings_file.h"

namespace input {
struct Pad;
}

// the host side: window, settings
namespace app {

enum class DisplayMode : uint32_t { Fullscreen, Borderless, Windowed };

struct Settings {
    DisplayMode display_mode = DisplayMode::Borderless;
    std::string screen = "main";     // the screen the game opens on: "main", or a Windows display name, "DISPLAY2"
    uint32_t refresh = 0;            // fullscreen refresh rate in Hz; 0 the highest the screen has for the size
    uint32_t width = 0, height = 0;  // 0 is the desktop mode
    uint32_t render_percent = 100;   // of the output height
    uint32_t fps_cap = 0;            // frames a second the window shows at most, 0 no limit; set by the game's frame rate setting
    bool vsync = true;
    uint32_t aniso = 0;              // 0 as the game asks
    std::string join_ip;                   // co-op: a host past the LAN the system link search also asks
    bool widescreen = false;               // 16:9 picture on a wide display; false keeps the console's 4:3 with side bars
    bool texture_dump = false;             // write each texture as it is first seen to textures/dump (texture packs)
    bool texture_replace = true;           // draw texture pack files from textures/replace in place of the game's
    float brightness = 1.0f;               // a curve over the picture as shown: 1 the game's own, more lifts the darks
    float lod_bias = 0.0f;                 // added to the game's mip level bias: below 0 picks sharper texture levels
    uint32_t sharpening = 0;               // 0 to 100: contrast adaptive sharpening over the picture as shown
    bool smooth_2d = true;                 // unfiltered 2D pictures (menus, HUD) upscaled bicubic past 1.2x the console's lines
    uint32_t msaa = 0;                     // anti-aliasing samples: 0 off, 2, 4 or 8
    bool hdr = false;                      // HDR output on an HDR screen (Windows HDR on), read at start
    uint32_t colour_look = 0;              // 0 original, 1 film, 2 vivid, 3 dark fantasy
    uint32_t look_strength = 100;          // 0 to 100, how much of the look
    // [input], as ROTK Fix's controller options; 0 deadzone and threshold leave it to the game, as on the console
    uint32_t vibration = 100;              // 0 to 100, rumble strength
    uint32_t stick_deadzone = 0;           // 0 to 50, % of a stick's travel read as centred
    uint32_t trigger_threshold = 0;        // 0 to 90, % of a trigger's pull read as released
    // [controls]: for each Xbox button the game reads (A B X Y black white left trigger right trigger), the
    // controller button that presses it, as input::kSources; as on the Xbox by default
    uint32_t buttons[8] = {0, 1, 2, 3, 5, 4, 6, 7};
    uint32_t movie_display = 2;            // movies: 0 fit (the middle 16:9), 1 stretch, 2 4:3 (the console's frame)
    uint32_t movie_sharpening = 75;        // 0 to 100, sharpening at the movie's own pixel size (75 by default, the player's choice)
    uint32_t movie_delay = 0;              // ms the movie picture is held back, for late (Bluetooth) sound
};

Settings& Get();
void Load();  // a settings file that is not TOML ends the run (kExitBadSettings); a missing one is made with defaults
void Save();
void LogSettings();  // every setting, as the settings file has them
std::vector<cfg::Desc> SettingsList();  // every setting the file holds, the host's and the game's
std::filesystem::path SettingsFile();   // --config, or GameInfo::settings beside the exe

// The command line, for a launcher:
//   --game <folder with the xbe, or a disc image>   --config <settings file>   --textures <folder>   --log <file>
//   --saves <folder>: the console's hard disk (save games, the game's cache)   --shader-cache <folder>
//   --screenshots <folder>: where \ saves a PNG of the picture
//   With all of them given nothing is written beside the exe, which may then be read-only.
//   --no-dialogs: failures end the run with their exit code and a line in the log, without a message box
//   --version, --describe-settings: one line, or the settings as JSON, on stdout; then exit 0
struct CommandLine {
    std::filesystem::path game, config, textures, log, saves, shader_cache, screenshots;
    bool no_dialogs = false, version = false, describe = false;
    int instance = 0;  // --instance N: a second copy on this PC for system link tests
};
const CommandLine& Args();
// exit codes a launcher turns into a message; a crash or anything else is another code
enum ExitCode : int {
    kExitOk = 0,
    kExitNoGame = 2,        // the game files are not where --game (or assets) says, or are another game
    kExitWrongRelease = 3,  // the game, but not the release this build was made from
    kExitNoD3D12 = 4,       // the graphics card or driver cannot run the renderer
    kExitBadSettings = 5,   // the settings file is not readable
    kExitGpuLost = 6,       // the graphics driver stopped or reset while playing (Windows' GPU timeout, a driver crash)
};
// logs the reason, shows it unless --no-dialogs, and ends the process with code
[[noreturn]] void Fail(int code, const std::wstring& message);
std::filesystem::path TextureDir();  // --textures, or textures beside the exe: replace/ holds packs, dump/ dumps
std::filesystem::path SavesDir();        // --saves, or hdd beside the exe (hddN for --instance N above 1)
std::filesystem::path ShaderCacheDir();  // --shader-cache, or shadercache beside the exe
std::filesystem::path ScreenshotDir();   // --screenshots, or Screenshots beside the exe
std::filesystem::path LogFolder();       // --log's folder, or logs beside the exe; crash dumps and F3 captures go by it
void LogSystem();    // the machine and the settings, at a run's start
void ApplyDisplay();  // any thread
struct Mode {
    uint32_t width, height;
};
Mode Desktop();  // the chosen screen's desktop mode
// the screens Windows has, "main" first then each display name ("DISPLAY1"...), as the screen setting takes them
const std::vector<std::string>& ScreenNames();
void* ScreenMonitor();  // HMONITOR of the screen setting's screen, the main one when that screen is not there
Mode Output();
float RenderScale(uint32_t guest_height);
// the output is 3:2 or wider: the console reports a widescreen TV and the frame is shown 16:9, else 4:3 as on a 4:3 TV
bool Widescreen();
// the shape the frame is shown at: 4:3 without widescreen, else the output's shape, at least 16:9 and at most
// g_max_frame_aspect (16:9 unless the game handles wider screens: its 3D, 2D and movies then follow FrameAspect)
float FrameAspect();
extern float g_max_frame_aspect;

std::filesystem::path ExeDir();
// the game in assets (game_files.cpp): its folder and this executable's xbe, from the extracted files or a disc
// image (.iso), which it mounts as D:; false after telling the player what is missing
struct GameFiles {
    std::filesystem::path dir;
    std::vector<uint8_t> xbe;
};
bool FindGame(GameFiles& game);
int Instance();  // Args().instance
void* Window();  // HWND
// boot splash: GameInfo::splash .bmp or .png beside the exe, up before the game window
bool OpenSplash();        // window thread, at launch; false without one, and the game window opens at once
void SplashFirstFrame();  // the game's first Swap: waits out the splash's minimum, then opens the game window
void CloseSplash();       // window thread
void OpenGameWindow(bool apply = true);  // any thread but the window thread's; returns once the window is up
// relaunched: the previous instance keeps its window up until this one has shown its first frames (handover.cpp)
bool StartHandover();   // window thread, at launch; true: the game window starts hidden
bool HandingOver();
void FinishHandover();  // any thread but the window thread's: opens the game window, lets the old instance go

// held around ImGui: the window thread's messages and the renderer's frame
std::recursive_mutex& UiLock();

// what the game repo defines once
using Option = cfg::Desc;  // a game setting in the settings file, described for a launcher
struct GameInfo {
    const wchar_t* title;        // window and splash
    const wchar_t* short_title;  // message boxes
    const wchar_t* name;         // window classes, crash dumps, handover event
    const char* log;             // log prefix
    const char* settings;        // settings file beside the exe, unless --config names another
    const char* version;         // --version: one line, "<program> <version>"
    const wchar_t* splash;       // .bmp or .png beside the exe
    const wchar_t* missing;      // message when neither the game files nor a disc image are found
    uint32_t title_id;           // the xbe's title id: another one is another game (kExitNoGame), not another release
    std::vector<std::pair<const char*, const char*>> images;  // xbe file name, executable name
    std::vector<Option> options;
    std::vector<cfg::Preset> presets;  // --describe-settings: a launcher's preset menu
    // [controls]: the game's name for what each Xbox button does (A B X Y black white left trigger right trigger),
    // the launcher's labels; empty ones show the button's name
    std::vector<const char*> button_actions;
    // the settings file's toml::table, for what options cannot hold
    void (*load)(const void* table);
    void (*save)(void* table);
    void (*init)();                // after the settings load, before the window and boot
    void (*loaded)();              // image in memory, before its entry runs
    void (*window)(void* hwnd);    // the game window is up, before boot
    // window thread, each message after ImGui's; true returns result instead of the host's handling
    bool (*message)(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
    void (*ui)(double fps);        // each frame inside the ImGui frame, UiLock held
    void (*pad)(int port, input::Pad& pad);  // a polled pad before the game gets it
    void (*rumble)(int port, uint16_t& left, uint16_t& right);
    // [controls] in the settings (the overlay, --describe-settings): off for a game that remaps its buttons another way
    bool remap_controls = true;
};
extern const GameInfo kGame;

// sleeps out the rest of the frame for the fps cap
void Pace();

}  // namespace app
