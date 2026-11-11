// Athelas - what the launcher's pages share (split out of main.cpp, 1.7.7): the App, the look, and the functions
// one page's file calls in another's. Each file says what it holds; main.cpp is the start and the window.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <string>
#include <vector>

// where Athelas lives: GitHub (home again since 11 Nov 2026), with GitLab as a copy of every release
static const char kProjectPage[] = "https://github.com/Shagg88/Athelas";
static const char kTickets[] = "https://github.com/Shagg88/Athelas/issues";
static const char kTicketsNew[] = "https://github.com/Shagg88/Athelas/issues/new";
#include <map>
#include <algorithm>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "config.h"
#include "library.h"
#include "port.h"
#include "ornament.h"
#include "gpu.h"
#include "gameart.h"
#include "update.h"
#include "report.h"
#include "music.h"
#include "userdata.h"
#include "i18n.h"
#include "hwrec.h"
#include "builder.h"
#include <shlobj.h>
#ifndef ROTKFIX_VERSION
#define ROTKFIX_VERSION "dev"
#endif
#define LAUNCHER_VERSION ROTKFIX_VERSION   // from the VERSION file (build.sh)

inline ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }

// Dark fantasy: deep brown-black, parchment text and old gold.
static const ImVec4 C_TEXT   = rgb(234, 223, 200);
static const ImVec4 C_DIM    = rgb(168, 152, 124);
static const ImVec4 C_GOLD   = rgb(206, 166, 78);
static const ImVec4 C_GOLD_H = rgb(232, 196, 112);
static const ImVec4 C_GOOD   = rgb(150, 190, 110);
static const ImVec4 C_WARN   = rgb(222, 150, 80);
static const ImVec4 C_BAD    = rgb(220, 95, 80);
static const ImVec4 C_PANEL  = rgb(16, 12, 9, 0.85f);
static const ImVec4 C_LINE   = rgb(150, 118, 60, 0.6f);

// w, h: the size it is shown at (w = the texture's width); lum: average brightness 0-255;
// blur: a tiny copy, stretched over the window behind pictures too small to fill it sharply
struct Picture { SDL_Texture *tex = nullptr, *blur = nullptr; int w = 0, h = 0; float lum = 0; };

struct BgJob;

struct Backgrounds {
    std::vector<std::string> files;
    int index = -1;
    Picture cur, prev;
    double changedAt = 0;
    BgJob *job = nullptr;          // picture being read in the background
    int group = -1;                // whose pictures are listed: 0 The Return of the King, 1 The Two Towers, 2 The Fellowship of the Ring
    bool artIn = false;            // the game art was ready when the list was made
    int gen = 0;                   // bumped when the list is made again (a picture still being read is dropped)
    std::string dir;               // the game it was listed from (groups 1 and 2)
    bool fresh = false;            // listed again: show its first picture now rather than at the next change
};

// [Background] Show in ROTKFix\launcher.ini: what is shown behind the launcher.
enum { BG_ART_AND_MINE, BG_ART, BG_MINE, BG_NONE, BG_SHOW_COUNT };

// Pictures are read and shrunk on a separate thread, so the launcher opens
// at once and never pauses when the background changes (screenshots can be
// 4K PNGs of 10 MB or more).
struct BgJob {
    std::vector<std::string> files;
    int gen = 0;
    int start = 0, found = -1;
    unsigned char *px = nullptr;
    int w = 0, h = 0, showH = 0;
    float lum = 0;                     // showH: the height to show it at (game art is square, shown at 4:3)
    SDL_AtomicInt done;
};

// ---------------------------------------------------------------------------
// App state

struct App {
    std::string runningExe, runningArgs;   // the port playing, as it was started (to start it again)
    std::string dir;
    // the player's games (this folder's PC game first); cur = the one shown, -1 = none yet
    std::vector<LibGame> lib;
    int cur = -1;
    GameInfo other;                    // inspect_game() of a PC game in another folder, when that one is shown
    std::string otherFor;
    GameInfo game;
    Settings s;
    std::vector<Monitor> monitors;
    std::string controller;
    int controllers = 0;
    SDL_GamepadType controllerType = SDL_GAMEPAD_TYPE_UNKNOWN;
    std::vector<SDL_Gamepad *> pads;   // opened so their button presses arrive as events
    int capture = -1;                  // move waiting for "press a button", or -1
    int padPlayer = 0;                 // whose buttons the Controls tab shows (0 = player 1)
    bool trigDown[2] = { false, false };   // trigger edge detection while mapping
    // first-run help when the launcher is not in the game folder
    bool searched = false;
    std::vector<FoundGame> found;
    std::string setupErr, deniedDir;
    double captureAt = 0;
    int captureFrame = 0;              // last frame a Controls tab was drawn (a capture ends when it isn't)
    bool captureTT = false;            // the move waiting is on The Two Towers' Controls tab (its [controls] buttons)
    // keyboard keys for the Xbox ports: the action waiting for "press a key", and the key pressed for it
    int kbCapture = -1, kbCaptureFrame = 0, kbResultFor = -1;
    double kbCaptureAt = 0;
    std::string kbResult;
    std::string message;
    bool messageBad = false;
    double messageAt = 0;
    bool quit = false;
    Backgrounds bg;
    SDL_Window *win = nullptr;
    // The Two Towers (Xbox) port
    Port tt;
    bool ttLooked = false;             // looked for the port program and asked it for its settings
    PortSettingsFile ttFile;
    bool ttLoaded = false;
    // The Fellowship of the Ring and The Return of the King (Xbox) ports: the same kind of port and page as The Two
    // Towers' (a settings.toml of its own). The page works on the tt fields above; the ports not shown wait in these,
    // by k_tomlPorts' order (tp_use moves them in and out).
    int ttWho = 0;                     // whose port the tt fields hold: 0 The Two Towers, 1 Fellowship, 2 Return of the King, 3 The Third Age
    Port ttPark[4];
    bool ttParkLooked[4] = {};
    PortSettingsFile ttParkFile[4];
    bool ttParkLoaded[4] = {};
    bool firstRunPreset = false;       // no ROTKFix.ini yet: the preset may still follow the graphics card
    HANDLE running = NULL;             // a port that is playing; the launcher waits, hidden
    std::string runningLog, runningGame;
    bool front = false;                // the front screen (the carousel of games) instead of a game's page
    bool journey = false;              // the Journey page (journey.inc) in place of the front screen
    unsigned long long runningSince = 0;   // when it started (GetTickCount64), for its play time
    std::string runningKey;            // its key in Athelas.ini's [PlayTime]: fotr, tt, rotk-xbox, rotk-pc, ta
    bool runningPc = false;            // the one playing is the PC game (rotk.exe), not a port
    Session session;                   // this play, for its folder in Logs (userdata.h)
};

enum { NAMES_AUTO, NAMES_XBOX, NAMES_PS, NAMES_NINTENDO, NAMES_ORIGINAL };

// The ports with a settings.toml of their own share one page (the tt_ functions): who it is for
struct TomlPortId {
    GameKind kind;
    const char *dir, *oldDir;          // the data folder beside Athelas, the one earlier test builds used
    const char *exe, *iniKey, *log;    // the program, its [Ports] key in Athelas.ini, its log's name
    const char *game;                  // "The Two Towers"
    const char *original;              // the Original preset's description
};

inline const int k_tomlPortCount = 4;

inline const TomlPortId k_tomlPorts[k_tomlPortCount] = {
    { GK_TT_XBOX, DIR_TT_XBOX, "TwoTowers", "TwoTowers.exe", "TwoTowers", "tt.log", "The Two Towers",
      "as it looked on the Xbox in 2003: 4:3, 30 fps, no anti-aliasing, filtering or sharpening." },
    { GK_FOTR_XBOX, DIR_FOTR_XBOX, nullptr, "FOTRXbox.exe", "FOTRXbox", "fotrx.log", "The Fellowship of the Ring",
      "as it looked on the Xbox in 2002: 4:3, 30 fps, no anti-aliasing, filtering or sharpening." },
    // its own page since Athelas 1.5.1 (it shared the PC version's ROTKFix.ini before: rx_import brings that over)
    { GK_ROTK_XBOX, DIR_ROTK_XBOX, "ROTKXbox", "ROTKXbox.exe", "ROTKXbox", "rotkx.log", "The Return of the King",
      "as it looked on the Xbox in 2003: 4:3, 30 fps, no anti-aliasing, filtering or sharpening." },
    { GK_TA_XBOX, DIR_TA_XBOX, nullptr, "TAXbox.exe", "TAXbox", "ta.log", "The Third Age",
      "as it looked on the Xbox in 2004: 4:3, 30 fps, no anti-aliasing, filtering or sharpening." },
};

// ui.cpp
extern ImFont *F_BODY, *F_SMALL, *F_HEAD, *F_TITLE, *F_PLAY, *F_TAB;
extern float g_scale;
extern float g_scale0;
extern int g_baseW, g_baseH;
void setup_style();
extern float g_labelW;
void row_begin(const char *label, const char *desc);
void row_end();
void section(const char *title);
bool segmented(const char *id, int *v, const char *const *names, int n, const bool *enabled = nullptr);
bool dialog_back();
bool gold_button(const char *label, ImVec2 size, ImFont *font);
void set_message(App &a, const std::string &m, bool bad);
void status_dot(bool ok, bool warn, const char *label, const char *tip);
void tip(const char *t);
int pad_tab_step(const App &a);
void pad_tab_focus();
float tabs_begin(const char *const *labels, int n, int sel);
void tabs_end();

// backgrounds.cpp
extern std::vector<ArtPic> g_art;
extern SDL_AtomicInt g_artState;
void art_start(const std::vector<std::string> &dirs);
bool art_ready();
extern int g_bgShow;
extern const char *k_bgShowName[BG_SHOW_COUNT];
void list_pictures(const std::string &dir, std::vector<std::string> &out, size_t maxCount);
int SDLCALL bg_worker(void *p);
void backgrounds_next(Backgrounds &b);
void backgrounds_poll(Backgrounds &b, SDL_Renderer *ren);
void backgrounds_rebuild(Backgrounds &b, int group, const std::string &launcherDir, const std::string &ttDir);
void bg_show_load();
void bg_show_save();
void draw_background(ImDrawList *dl, ImVec2 size, Backgrounds &b);

// pc_settings.cpp
void effective_res(const App &a, int &w, int &h);
int effective_refresh(const App &a);
void refresh_controllers(App &a);
extern bool g_removeSelf;
extern bool g_fixRemoved;
extern bool g_xboxPage;
bool save(App &a);
void tab_display(App &a);
void tab_graphics(App &a);
int label_style(const App &a);
const char *fullscreen_hint(int ls);
const char *phys_label(int ph, int style);
void capture_event(App &a, const SDL_Event &e);
bool kb_capture_event(App &a, const SDL_Event &e);
bool keyboard_section(App &a, bool &enabled, int &player, std::string *keys, const std::string *labels, const std::string *defaults, int n);
void tab_controls(App &a);
void tab_audio(App &a);
bool own_game(const App &a);
bool load_pc_settings(App &a, const std::string &dir);
bool fix_installed(const GameInfo &g);
void use_pc_game(App &a, const std::string &dir);
void add_game_from(App &a, const std::string &path);
void achievements_section(const std::string &path, bool header = true);
void tab_gameplay(App &a);
void tab_about(App &a);
void setup_here(App &a, const std::string &dst);
void admin_setup_prompt(App &a);
void draw_setup(App &a);

// shortcuts.cpp
void shortcuts_section(App &a);

// launcher_settings.cpp
bool launcher_ini_get(const char *k, int def);
void launcher_ini_put(const char *k, int v);
extern bool g_borderless;
extern SDL_Window *g_mainWin;
void set_fullscreen(App &a, bool on);
void games_section(App &a);

// updater_ui.cpp
bool updates_on();
extern bool g_askUpdate;
void update_popup(App &a);
void update_section(App &a);

// mods.cpp
void tab_mods(App &a);
void pc_texture_packs(App &a);
void about_credits(App &a);
void games_apply_pending(App &a);   // About: one line, the licences and launcher options behind buttons

// saves.cpp
std::string logs_dir();
void saves_section(App &a, const std::string &game, const std::string &saves, const std::string &shaders, const std::string &gameDir = "");

// ports.cpp
std::string port_data_dir(const char *dir, const char *oldDir);
extern int g_ttWho;
const TomlPortId &tp();
int tp_of(GameKind k);
bool tp_kind(GameKind k);
void tp_use(App &a, int who);
std::string tt_data_dir();
std::string tt_settings_path();
std::string tt_log_path();
std::string ports_ini();
bool native_arm64();
bool arm_files_current();
void tt_find(App &a);
bool tt_ready(const App &a);
std::string tt_val(const App &a, const PortSetting &ps);
const PortSetting *tt_setting(const App &a, const char *sec, const char *key);
void tt_load_settings(App &a);
bool tt_save(App &a);
void tt_play(App &a, const LibGame &g);
void port_finished(App &a, DWORD code);
std::string rx_data_dir();

// release_report.cpp
std::string exe_machine(const std::string &path);
std::string url_encode(const std::string &s);
extern std::vector<std::string> g_untested;
bool release_confirmed(const char *dir, const LibGame &g);
bool release_untested(const char *dir, const LibGame &g);
void report_version(App &a, const char *dir, const char *game, const LibGame &g, bool worked);

// problem_report.cpp
void report_section(App &a);
void crash_box(App &a, const std::string &text, const std::string &title);

// xbox_pages.cpp
void draw_tt(App &a, const LibGame &g);
bool rec_button(const char *id, const std::vector<std::string> &changes, bool canUndo, bool *undo);
void tt_put(App &a, const char *sec, const char *key, const std::string &v);
void tt_apply_refresh(App &a);
int tt_button_count(App &a);
bool tt_assign_button(App &a, int idx, int ph);
void tt_tabs(App &a, const LibGame &);
std::string tt_summary(const App &a);

// pages.cpp
void draw_ui(App &a, ImVec2 size, SDL_Renderer *ren);
void play_time_add(const std::string &key, unsigned long long secs);
void shortcut_play(App &a, const std::string &key);
void front_drop(App &a, const char *utf8);

// main.cpp
void language_combo();

inline float S(float v) { return v * g_scale; }
