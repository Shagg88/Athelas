#include "app/app.h"

namespace rotk {
extern bool g_skip_all_videos;                  // src/game/rotk_movies.cpp
extern uint32_t g_fps;                          // src/game/rotk_framerate.cpp
extern uint32_t g_fov_percent, g_draw_percent;  // src/game/rotk_widescreen.cpp
extern bool g_alpha_edges, g_hud_edges, g_safe_area;  // src/game/rotk_widescreen.cpp
extern bool g_sharp_text;                       // src/game/rotk_fonts.cpp
void Pad(int port, input::Pad& pad);            // src/game/rotk_controls.cpp
extern uint32_t g_button_pictures;              // src/game/rotk_prompts.cpp
void Loaded();                                  // rotk_movies.cpp
void Init();                                    // rotk_settings.cpp: ROTKFix.ini, then the frame rate
void LoadTable(const void* table);              // rotk_settings.cpp: 1.0.0's defaults for settings the file lacks
void SaveIni();                                 // rotk_settings.cpp: the in-game menu's changes into ROTKFix.ini
void SettingChanged(const cfg::Desc& d);        // rotk_settings.cpp: the in-game menu changed one
void FileOpened(const char* name);              // rotk_controls.cpp: the front end or a level
bool InMenus();
std::vector<ach::Def> Achievements();           // rotk_achievements.cpp
std::string Activity();
std::string ActivityState();
}

// the Discord application whose name Discord shows as the game ("The Return of the King"); null: no Discord status
#ifndef ROTKX_DISCORD_APP_ID
#define ROTKX_DISCORD_APP_ID nullptr
#endif

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"The Return of the King (PC port)",       // window and splash
    .short_title = L"ROTK Xbox",          // message boxes
    .name = L"ROTKXbox",                  // window classes, crash dumps, handover event
    .log = "rotkx",                       // log file prefix
    .settings = "settings.toml",          // settings file beside the exe, unless --config names another
    .version = "rotkx-port 1.0.1",        // --version
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"The game files couldn't be found. Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .title_id = 0x4541003E,
    .images = {{"default.xbe", "ROTKXbox"}},  // each recompiled xbe and its exe, for title launches
    // the game's own settings, in the settings file's [graphics]; defaults as on the console
    .options = {{"graphics", "skip_all_videos", cfg::Type::Bool, "Allow skipping all videos",
                 "On the console the opening logos can't be skipped and each story movie only after you have seen it "
                 "once. This lets a button press skip any of them.",
                 &rotk::g_skip_all_videos},
                {"graphics", "fps", cfg::Type::Choice, "Frame rate",
                 "The game was made for 30 on the Xbox. Higher rates play at normal speed; movies keep their own pace.",
                 nullptr, &rotk::g_fps, nullptr, 0, 0, {"30", "60", "90", "120", "144", "165", "180", "240"}, nullptr,
                 nullptr, {"30 fps", "60 fps", "90 fps", "120 fps", "144 fps", "165 fps", "180 fps", "240 fps"}},
                {"graphics", "fov", cfg::Type::Int, "Field of view",
                 "How wide the camera sees, as a share of the game's own (100). Wider shows more of the battle but "
                 "makes characters look further away.",
                 nullptr, &rotk::g_fov_percent, nullptr, 70, 150, {}},
                {"graphics", "draw_distance", cfg::Type::Int, "Draw distance",
                 "How far the camera sees, as a share of the game's own (100); the fog moves out with it.",
                 nullptr, &rotk::g_draw_percent, nullptr, 50, 800, {}},
                {"graphics", "alpha_edges", cfg::Type::Bool, "Smooth leaf edges",
                 "With anti-aliasing on, smooths the edges of leaves, grass and fences.", &rotk::g_alpha_edges},
                {"graphics", "hud_edges", cfg::Type::Bool, "HUD at the screen edges",
                 "With widescreen on, the HUD keeps its shape and the health bar and counters sit at the edges of the "
                 "wide picture. Off, the HUD is stretched to fill it, as on PC.",
                 &rotk::g_hud_edges},
                {"graphics", "sharp_text", cfg::Type::Bool, "Sharper text and menus",
                 "Text drawn from the PC version's larger fonts, and the title and main menu pictures from its "
                 "larger copies, so they stay sharp at high resolutions. Needs the PC version's Data folder beside "
                 "ROTKFix.ini.",
                 &rotk::g_sharp_text},
                {"graphics", "safe_area", cfg::Type::Bool, "TV-safe border",
                 "The console kept a black border round the picture for old TVs, which cut off the edges. Off, the "
                 "picture fills the screen.",
                 &rotk::g_safe_area},
                {"graphics", "button_pictures", cfg::Type::Choice, "Button pictures",
                 "Which controller's buttons the game's hints show. Auto follows the controller you play with.",
                 nullptr, &rotk::g_button_pictures, nullptr, 0, 0, {"auto", "xbox", "playstation", "nintendo", "keyboard"}, nullptr,
                 nullptr, {"Auto", "Xbox", "PlayStation", "Nintendo", "Keyboard keys"}}},
    .presets = {},
    // what each Xbox button does (ROTK Fix's names for the slots), for the keyboard's labels; Black is RB, White LB
    .button_actions = {"Speed Attack", "Physical Attack", "Parry", "Fierce Attack", "Action", "Jump Back",
                       "Ranged Attack", "Killing Move"},
    .load = rotk::LoadTable,              // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = rotk::Init,                   // after the settings load, before the window and boot
    .loaded = rotk::Loaded,               // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = rotk::Pad,                     // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .remap_controls = false,              // buttons are remapped through ROTKFix.ini ([Gamepad], rotk_controls.cpp)
    // the in-game menu: these take effect at once; it saves into ROTKFix.ini, which the launcher shares with ROTK PC
    .live_settings = {"graphics.fov", "graphics.draw_distance", "graphics.hud_edges"},
    .save_settings = rotk::SaveIni,
    .setting_changed = rotk::SettingChanged,
    .file_opened = rotk::FileOpened,
    .in_menus = rotk::InMenus,
    .menu_accept_start = true,  // the front end goes on with Start; "press start" takes nothing else
    .achievements = rotk::Achievements(),
    .discord_app_id = ROTKX_DISCORD_APP_ID,
    .activity = rotk::Activity,
    .activity_state = rotk::ActivityState,
};
