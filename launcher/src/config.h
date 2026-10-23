// Athelas (the launcher) - settings, game files and system information
#pragma once

// The launcher is called Athelas; it runs ROTK Fix (PC) and the Xbox ports.
// ROTKFixLauncher.exe was its name before (removed when a new copy is installed).
#define LAUNCHER_EXE "Athelas.exe"
#define LAUNCHER_EXE_OLD "ROTKFixLauncher.exe"
// The release's folders beside Athelas.exe: each game's own (the Xbox ports keep their settings, saves and logs in
// theirs), and Athelas.ini with the list of games. The older names are still read once, to carry settings over.
#define DIR_ROTK_PC   "ROTK-PC"
#define DIR_ROTK_XBOX "ROTK-Xbox"
#define DIR_TT_XBOX   "TT-Xbox"
#define DIR_FOTR_XBOX "FOTR-Xbox"
#define ATHELAS_INI   "Athelas.ini"
#include <string>
#include <vector>

extern const char *const k_subtitleCodes[10];   // [Language] Subtitles: off, de, fr, es, it, pt, pl, nl, sv, ru
extern const char *const k_subtitleNames[10];   // as each language names itself
struct Settings {
    // [Frame]
    int  targetFps = 60;
    bool showFps = false;         // [Frame] ShowFPS: a frame rate counter in the game
    // [Gamepad]
    bool padEnabled = true;
    bool hideRealPads = true;
    int  deadzone = 12;
    int  triggerThreshold = 30;
    int  vibration = 100;         // controller rumble strength %, 0 = off
    int  player2 = 1;             // 0 keyboard and mouse, 1 second controller (co-op)
    int  buttonNames = 0;         // 0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 original ("Button 7"), 5 keyboard (Xbox version)
    int  padMap[2][9] = { { 0, 1, 2, 3, 4, 5, 8, 10, 11 },   // per player: physical button for each PadAction (PadPhys ids)
                          { 0, 1, 2, 3, 4, 5, 8, 10, 11 } };
    // [Gameplay] (both ROTK versions): damage taken and dealt, percent of the game's; every level and character open
    int  damageTaken = 100;
    int  enemyDamage = 100;
    bool unlockAll = false;
    bool allHeroes = false;       // [Gameplay] AllHeroes (Xbox version): every hero in a level already completed
    bool eowyn = false;           // [Gameplay] Eowyn (Xbox version): Éowyn in Faramir's place
    int subLang = 0;              // [Language] Subtitles (Xbox version): an index into k_subtitleCodes, 0 off
    bool achNotify = true;        // [Achievements] Notify: a note in the corner when one is earned
    bool discord = false;         // [Discord] Enabled: the Discord status
    // [Video]
    bool skipAllVideos = true;
    bool texReplace = true;       // [Textures] Replace: use texture packs
    bool texDump = false;         // [Textures] Dump: save the game's textures as PNGs
    int  movieDisplay = 0;        // 0 auto, 1 stretch, 2 4:3
    bool skipLogos = false;
    int  audioDelayMs = 0;
    int  sharperMovies = 70;      // 0-100
    // [Graphics]
    int  renderer = 0;            // 0 dxvk, 1 native
    int  width = 0, height = 0;   // 0 = desktop
    int  displayMode = 0;         // 0 borderless, 1 fullscreen, 2 windowed
    int  refresh = 0;             // fullscreen only, 0 = best
    bool vsync = true;
    int  msaa = 4;                // 0 2 4 8
    int  aniso = 16;              // 0 2 4 8 16
    bool widescreenFix = true;
    int  renderScale = 100;            // [Graphics] RenderScale: % of the chosen size the game draws at (50-200)
    bool oneCore = false;              // [System] OneCore: Xbox version only, the game's threads on one core (Windows on ARM)
    bool hudEdges = false;             // [Graphics] HUDEdges: Xbox version only, HUD pieces at the 16:9 edges
    bool sharperText = false;          // [Graphics] SharperText: Xbox version only, the PC version's larger fonts and menu pictures
    int  fovPct = 100;            // 70-150
    float lodBias = -0.5f;        // -1 .. 0
    float brightness = 1.0f;      // 0.5 - 2
    int  sharpening = 60;         // 0-100
    int  colourLook = 0;          // 0 original, 1 film, 2 vivid, 3 dark
    int  lookStrength = 100;      // 0-100
    // [Launcher]
    int  preset = 3;              // 0 original 1 balanced 2 high 3 ultra 4 custom
    int  monitor = 0;
    std::string screen = "main";   // [Graphics] Screen: main, or DISPLAY2 etc.

    // the game's own ROTK.ini (Documents)
    int  shadows = 2;             // 0 off 1 low 2 high
    bool charDetailHigh = true;
    bool altCameras = false;      // game's AlternateCameras option
    int  sfx = 2;                 // 0 low 1 medium 2 high
    bool subtitles = true;
    int  sound3d = 1;             // 0 off (game's own), 1 speakers, 2 headphones (HRTF)
    int  volMusic = 100, volVoice = 100, volSfx = 100;

    // the Xbox version's own (ROTK on PC has the game's own keyboard controls and slows itself in the background)
    bool pauseBg = false;         // [Window] PauseInBackground
    bool muteBg = false;          // [Window] MuteInBackground
    bool bgController = true;     // [Window] BackgroundController (Xbox version)
    bool kbEnabled = true;        // [Keyboard] Enabled
    bool mouseAim = true;         // [Keyboard] MouseAim (Xbox version): the mouse aims while the ranged attack is held
    int  mouseSens = 100;         // [Keyboard] MouseSensitivity, percent
    bool mouseInvertY = false;    // [Keyboard] MouseInvertY (Xbox version): mouse aiming up and down reversed
    bool mouseMenus = true;       // [Keyboard] MouseMenus: the wheel and right click in the game's menus
    bool cheatInvulnerable = false, cheatMissiles = false, cheatDevastating = false, cheatPerfect = false;  // [Cheats] (Xbox version)
    int  kbPlayer = 1;            // [Keyboard] Player: 1 or 2
    std::string kbKeys[21];       // [Keyboard] each KB_ action's keys ("Mouse1, Enter"); k_kbDefault when not set
};

// Keyboard and mouse play in the Xbox ports, in the ports' order (keys.h in their SDK); ini key, default keys and
// label for ROTK Xbox. The Two Towers' come from the port itself (its [keyboard] settings).
enum { KB_ACTION_COUNT = 21 };
extern const char *const k_kbKey[KB_ACTION_COUNT];
extern const char *const k_kbDefault[KB_ACTION_COUNT];
extern const char *const k_kbLabel[KB_ACTION_COUNT];

// Controller buttons, named by Xbox position (A = bottom face button on every brand)
enum PadPhys { PH_A, PH_B, PH_X, PH_Y, PH_LB, PH_RB, PH_BACK, PH_START, PH_LS, PH_RS, PH_LT, PH_RT, PAD_PHYS_COUNT };
extern const char *const k_padPhysId[PAD_PHYS_COUNT];          // ini names: A B X Y LB RB Back Start LS RS LT RT
// Remappable moves, in display order
enum PadAction { PA_SPEED, PA_PHYSICAL, PA_PARRY, PA_FIERCE, PA_JUMPBACK, PA_ACTION, PA_SPECIAL, PA_RANGED, PA_KILLING, PAD_ACTION_COUNT };
extern const char *const k_padActionKey[PAD_ACTION_COUNT];     // ini keys in [GamepadButtons]
extern const int k_padActionDefault[PAD_ACTION_COUNT];

enum { PRESET_ORIGINAL, PRESET_BALANCED, PRESET_HIGH, PRESET_ULTRA, PRESET_CUSTOM };
void apply_preset(Settings &s, int preset);
int  detect_preset(const Settings &s);

struct Mode { int w, h; std::vector<int> hz; };
struct Monitor {
    std::string device;           // \\.\DISPLAY1
    std::string name;             // friendly name
    int deskW = 0, deskH = 0, deskHz = 0;
    bool primary = false;
    std::vector<Mode> modes;      // sorted, 32-bit only
    int maxHz() const;
    const Mode *find(int w, int h) const;
};
std::vector<Monitor> enum_monitors();

struct GameInfo {
    std::string dir;              // folder with rotk.exe
    bool exeFound = false;
    bool versionOk = false;       // signatures match the supported release
    bool discProtected = false;   // the original disc exe (SafeDisc: needs SafeDiscShim on Windows 10/11)
    std::string versionText;
    bool hasDll = false, hasProxyD3D8 = false, hasProxyDinput = false, hasSdl = false, hasDxvk = false;
    std::string rotkIniDir;       // Documents\LOTR The Return of the King (tm) Data
    std::string rotkIniPath;      // ...\ROTK.ini
    bool rotkIniExists = false;
    bool inputProfileInstalled = false;
    bool otherD3D8 = false, otherDinput = false;   // a d3d8.dll / dinput8.dll from another mod is in the folder
};
GameInfo inspect_game(const std::string &dir);
// Remove ROTK Fix from the game folder and put the game's files back. 'done' lists what happened.
bool uninstall(const std::string &dir, std::vector<std::string> &done, std::string &err);
// After uninstalling: delete the launcher's own exe once this process has exited (call just before exiting)
void remove_launcher_after_exit();

int  pick_refresh(const std::vector<int> &hz, int fps);
bool load_settings(const std::string &dir, Settings &s);   // false = no ROTKFix.ini yet (defaults used)
// controllers: how many are connected now (-1: unknown). Player 1 gets the ROTK Fix Gamepad layout only when one
// is, as that layout has no keyboard controls for playing; otherwise the game's own keyboard and mouse layout
bool save_settings(const std::string &dir, const Settings &s, std::string &err, int controllers = -1);
bool save_rotkfix_ini(const std::string &dir, const Settings &s, std::string &err);   // ROTKFix.ini only
bool install_input_profile(const std::string &dir, std::string &err);
bool launch_game(const std::string &dir, std::string &err);
std::string exe_dir();
// A wide (Unicode) path as the 2003 game and this launcher's ANSI calls can use it: as is when every
// character fits the system's code page, otherwise its short 8.3 form (all ASCII); "" if neither works.
std::string ansi_path(const wchar_t *w);

// First-run help when the launcher is not in the game folder
struct FoundGame { std::string dir; GameInfo info; };
std::vector<FoundGame> find_game_folders();                 // installed-programs list, EA registry keys, usual folders
bool pick_folder(std::string &out, const wchar_t *title = L"Choose the folder with rotk.exe");   // Windows' "choose a folder" box
// Copy ROTK Fix from 'src' into the game folder 'dst' (another mod's d3d8/dinput8 kept as *.before-rotkfix).
// accessDenied is set when Windows refused to write there (Program Files without administrator rights).
std::string pc_fix_source();
bool fix_outdated(const std::string &dir);
std::string fix_dll_version(const std::string &dll);   // "1.1.2" from a ROTKFix.dll, "" when unreadable   // the game folder has other ROTK Fix files than the launcher carries
bool install_to(const std::string &src, const std::string &dst, std::string &err, bool &accessDenied);
bool start_launcher(const std::string &dir, bool elevated, const std::string &args);
bool folder_writable(const std::string &dir);   // false = Windows needs administrator rights there

// Copies a folder's contents (and subfolders) into another; files of the same name are replaced. False on an error.
bool copy_folder(const std::string &from, const std::string &to);
