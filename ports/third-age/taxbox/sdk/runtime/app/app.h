#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/keys.h"
#include "core/settings_file.h"

namespace input {
struct Pad;
}

// achievements (app/achievements.cpp): a game lists them in GameInfo::achievements and earns them with Unlock
namespace ach {
struct Def {
    const char* id;     // kept in achievements.txt; never changed once released
    const char* title;
    const char* desc;
};
void Start();                                 // at start: what's earned so far, and the list for the launcher
void Unlock(const char* id);                  // any thread; once
bool Has(const char* id);
uint64_t Count(const char* name);             // a running total the game keeps (saved with the achievements)
uint64_t Add(const char* name, uint64_t n);   // adds and returns the new total
void Flush();                                 // write what's not written yet (at exit)
void DrawNotes(float scale);                  // the renderer's ImGui frame: "Achievement earned" in the corner
void DrawList();                              // the in-game menu's page
// Virtues (opt-in, the launcher's Journey page): ATHELAS_VIRTUES=<dealt>,<taken>, percents the launcher works out
// from the deeds earned and sets for the game it starts. 100 each when it isn't set.
uint32_t VirtueDealt();                       // the damage the player's hits do, percent
uint32_t VirtueTaken();                       // the damage the player takes, percent
// Fate (opt-in, the Journey page): ATHELAS_FATE=1. The launcher makes enemies tougher through ATHELAS_VIRTUES; here
// every enemy counts twice towards the kill deeds, and Fate's own deeds are earned.
bool Fate();
// the deeds every game shares: the kill deeds (100 to 5,000) and Fate's two; a game adds them to its list
void SharedDeeds(std::vector<Def>& v);
// an enemy defeated by a player: the total, the count for the level being played ("kills@<id>"), the kill deeds
void Defeated();
// a level's run: Started with its deed id when it begins, Done when it's completed: a line in records.txt beside
// achievements.txt (when, level, seconds, enemies defeated, Fate), the counters completed@<id> and
// levels_completed, and on Fate fate@<id> and the deed fate_one; only for the level being played, once. FateDone:
// completed on Fate before.
void LevelStarted(const char* id);
void LevelDone(const char* id);
bool FateDone(const char* id);
// Fellowship (opt-in, ATHELAS_FELLOWSHIP=1): in co-op, while both players are fighting, their hits do 10% more
bool FellowshipOn();
}  // namespace ach

// Discord status (app/discord.cpp), started once the window is up
namespace discord {
void Start();
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
    bool console_textures = false;         // 3D textures pick the detail level the console did at its own 480 lines
    uint32_t sharpening = 0;               // 0 to 100: contrast adaptive sharpening over the picture as shown
    bool smaa = false;                     // SMAA over the frame (edges smoothed after drawing, on top of or instead of MSAA)
    bool smooth_2d = true;                 // unfiltered 2D pictures (menus, HUD) upscaled bicubic past 1.2x the console's lines
    bool show_fps = false;                 // [display] show_fps: the frames per second in the screen's top right corner
    uint32_t msaa = 0;                     // anti-aliasing samples: 0 off, 2, 4 or 8
    uint32_t upscaler = 0;                 // 0 off, 1 FSR (AMD, any card), 2 DLSS (NVIDIA RTX): drawn smaller, upscaled temporally
    uint32_t upscaler_quality = 1;         // 0 native (DLAA / FSR native AA), 1 quality, 2 balanced, 3 performance, 4 ultra performance
    bool hdr = false;                      // HDR output on an HDR screen (Windows HDR on), read at start
    uint32_t renderer = 0;                 // 0 Direct3D 12, 1 Vulkan (in development; Direct3D 12 if it can't start), read at start
    uint32_t colour_look = 0;              // 0 original, 1 film, 2 vivid, 3 dark fantasy
    uint32_t look_strength = 100;          // 0 to 100, how much of the look
    // [input], as ROTK Fix's controller options; 0 deadzone and threshold leave it to the game, as on the console
    uint32_t vibration = 100;              // 0 to 100, rumble strength
    uint32_t stick_deadzone = 0;           // 0 to 50, % of a stick's travel read as centred
    uint32_t trigger_threshold = 0;        // 0 to 90, % of a trigger's pull read as released
    // [controls]: for each Xbox button the game reads (A B X Y black white left trigger right trigger), the
    // controller button that presses it, as input::kSources; as on the Xbox by default
    uint32_t buttons[8] = {0, 1, 2, 3, 5, 4, 6, 7};
    uint32_t movie_display = 0;            // movies: 0 fit (the middle 16:9, the default), 1 stretch, 2 4:3 (the console's frame), 3 zoom (4:3 movies fill the width too)
    uint32_t movie_sharpening = 75;        // 0 to 100, sharpening at the movie's own pixel size (75 by default, the player's choice)
    uint32_t movie_delay = 0;              // ms the movie picture is held back, for late (Bluetooth) sound
    bool movie_subtitles = true;           // a movie's subtitles from subtitles\<movie>.srt beside the exe, when there
    // [keyboard]: keys and mouse buttons press the controller's buttons on one player's port (keys.h)
    bool keyboard = true;
    uint32_t keyboard_player = 1;          // 1 or 2
    std::string keys[keys::kCount];        // each action's keys, comma separated; keys::kDefaults and the game's
    bool mouse_aim = true;                 // [keyboard] mouse_aim: while the ranged attack is held, the mouse aims (the left stick)
    uint32_t mouse_sensitivity = 100;      // [keyboard] mouse_sensitivity, percent
    bool mouse_invert_y = false;           // [keyboard] mouse_invert_y: mouse aiming up and down reversed
    bool mouse_menus = true;               // [keyboard] mouse_menus: in the game's menus the wheel moves, right click goes back
    // [window]: when the game window loses the focus (another window in front, Alt-Tab)
    bool pause_unfocused = false;          // the game stops, picture and sound, until it has the focus again
    bool mute_unfocused = false;           // the sound goes quiet while it runs on
    bool background_input = true;          // [window] background_input: controllers still play while another window has the focus
    bool achievement_notes = true;         // [achievements] notify: a note in the corner when one is earned
    bool discord = false;                  // [discord] enabled: what the game is doing on the player's Discord profile
    std::string discord_app_id;            // [discord] app_id: another Discord application than the game's own
    bool one_core = false;                 // [system] one_core: the game's threads take turns on one core, as on the Xbox
};

Settings& Get();
void Load();  // a settings file that is not TOML ends the run (kExitBadSettings); a missing one is made with defaults
void Save();
// a note Save writes at the top of the settings file, as comment lines (a game whose settings live elsewhere says where)
void SetSettingsNote(const std::string& comment_lines);
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
    bool any_release = false;  // --any-release: start another release of the game without asking (the launcher asked)
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
std::filesystem::path LogFolder();       // --log's folder, or logs beside the exe; crash dumps and M captures go by it
void LogSystem();    // the machine and the settings, at a run's start
uint64_t OneCoreMask();  // the fastest processor core, as an affinity mask (for [system] one_core)
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
    // [keyboard]: the game's own default keys where they differ from keys::kDefaults ({keys::kBlack, "Tab"})
    std::vector<std::pair<uint32_t, const char*>> key_defaults;
    // the in-game menu: the game's settings (options) that take effect while playing, "section.key"; the host's
    // own are listed in overlay.cpp
    std::vector<const char*> live_settings;
    void (*save_settings)() = nullptr;                // the menu closed after a change: null saves the settings file
    void (*setting_changed)(const cfg::Desc&) = nullptr;  // the menu changed one, at once
    // the keyboard in the game's own menus: in_menus says when they are up (the front end, not a pause in a level);
    // there the keys bound to Start press B, so Esc goes back, and with menu_accept_start the keys bound to A press
    // Start (a game whose menus take Start to go on, "press start" among them); with menu_start_with_a they press
    // both (a game whose menus go on with A but whose title screen takes only Start)
    void (*file_opened)(const char* name) = nullptr;  // each file the game opens (any thread), to follow its state
    void (*file_read)(const char* name) = nullptr;    // each read from a file (any thread): a game's movie streaming
    // a file the game cannot play without (true): when it is missing the run ends with kExitNoGame, naming it, instead
    // of the game crashing on it (an incomplete copy of the game)
    bool (*required_file)(const char* name) = nullptr;
    bool (*in_menus)() = nullptr;
    bool menu_accept_start = false;
    bool menu_start_with_a = false;
    // with menu_start_with_a: when it says so only (a title screen), A's keys press Start too; elsewhere in the menus
    // they press A alone (The Two Towers' Yes/No pop-ups took the Start as their answer, #17)
    bool (*start_with_a_now)() = nullptr;
    // achievements: every one the game has, in the order the lists show them (empty: none)
    std::vector<ach::Def> achievements;
    // Discord status: the game's Discord application id (its name is what Discord shows as the game), and what the
    // game is doing for the status's two lines, asked every few seconds from another thread (empty: "In the menus")
    const char* discord_app_id = nullptr;
    std::string (*activity)() = nullptr;        // "Helm's Deep"
    std::string (*activity_state)() = nullptr;  // "As Gandalf", "Co-op"
};
extern const GameInfo kGame;

// sleeps out the rest of the frame for the fps cap
void Pace();

// the in-game menu (overlay.cpp): F10 or both stick clicks open it, and the game stands still behind it
bool OverlayOpen();
void OverlayPad(int port, input::Pad& pad);  // input::Poll: the menu's stick-click combination, and its masks
void DrawOverlay(double fps);                // the renderer's ImGui frame, UiLock held
bool OverlayMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);  // window thread first
void SetFocus(bool focus);                   // window thread: WM_ACTIVATE
void OverlayInit();                          // after ImGui's context is made: the menu's font
float UiScale();                             // ImGui font scale for text about 18 pixels high at 720 lines
// subtitles (app/subtitles.cpp): a line at the bottom of the screen, over the game (a game's translations)
void Subtitle(const std::string& utf8, double seconds);  // any thread; the same line again keeps it showing
void ClearSubtitle();
void DrawSubtitles(float scale);                          // the renderer's ImGui frame (DrawOverlay)
// a movie starting (its name without folder or extension, "fmv_2"): its subtitles\<name>.srt, when there is one, shown
// as subtitles while the movie's frames come (gpu::g_movie), timed from its first frame; any thread
void MovieSubtitles(const std::string& name);
// the game stands still (the menu, or [window] pause_unfocused): the title's thread waits at its next Swap, the
// guest's clocks stop and the sound pauses
bool Paused();
bool Focused();  // the game window has the focus
bool Muted();  // the sound is silent (paused, or [window] mute_unfocused without the focus)
bool Held();   // the game is waiting in Hold now: its sound stands still too
// the title's thread at Swap, while Paused: the guest's clocks stop, present shows the last picture again with the
// menu over it, about 60 times a second, until the pause ends
void Hold(const std::function<void()>& present);

}  // namespace app
