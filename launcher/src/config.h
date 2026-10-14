// ROTK Fix Launcher - settings, game files and system information
#pragma once
#include <string>
#include <vector>

struct Settings {
    // [Frame]
    int  targetFps = 60;
    // [Gamepad]
    bool padEnabled = true;
    bool hideRealPads = true;
    int  deadzone = 12;
    int  triggerThreshold = 30;
    int  vibration = 100;         // controller rumble strength %, 0 = off
    int  player2 = 1;             // 0 keyboard and mouse, 1 second controller (co-op)
    int  buttonNames = 0;         // 0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 original ("Button 7")
    int  padMap[2][9] = { { 0, 1, 2, 3, 4, 5, 8, 10, 11 },   // per player: physical button for each PadAction (PadPhys ids)
                          { 0, 1, 2, 3, 4, 5, 8, 10, 11 } };
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
    bool alphaEdges = true;
    int  aniso = 16;              // 0 2 4 8 16
    bool widescreenFix = true;
    int  renderScale = 100;            // [Graphics] RenderScale: % of the chosen size the game draws at (50-200)
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
};

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
bool save_settings(const std::string &dir, const Settings &s, std::string &err);
bool save_rotkfix_ini(const std::string &dir, const Settings &s, std::string &err);   // ROTKFix.ini only
bool install_input_profile(const std::string &dir, std::string &err);
bool launch_game(const std::string &dir, std::string &err);
std::string exe_dir();

// First-run help when the launcher is not in the game folder
struct FoundGame { std::string dir; GameInfo info; };
std::vector<FoundGame> find_game_folders();                 // installed-programs list, EA registry keys, usual folders
bool pick_folder(std::string &out, const wchar_t *title = L"Choose the folder with rotk.exe");   // Windows' "choose a folder" box
// Copy ROTK Fix from 'src' into the game folder 'dst' (another mod's d3d8/dinput8 kept as *.before-rotkfix).
// accessDenied is set when Windows refused to write there (Program Files without administrator rights).
bool install_to(const std::string &src, const std::string &dst, std::string &err, bool &accessDenied);
bool start_launcher(const std::string &dir, bool elevated, const std::string &args);
bool folder_writable(const std::string &dir);   // false = Windows needs administrator rights there
