#include "app/app.h"
#ifndef GAME_VERSION
#define GAME_VERSION "1.6.0"  // CMakeLists.txt sets it (XBE_VERSION); the headless harness does not
#endif

namespace rotk {
extern bool g_skip_all_videos, g_skip_logos;    // src/game/rotk_movies.cpp
extern uint32_t g_fps;                          // src/game/rotk_framerate.cpp
extern uint32_t g_fov_percent, g_draw_percent;  // src/game/rotk_widescreen.cpp
extern bool g_alpha_edges, g_hud_edges, g_safe_area;  // src/game/rotk_widescreen.cpp
extern bool g_sharp_text;                       // src/game/rotk_fonts.cpp
void Pad(int port, input::Pad& pad);            // src/game/rotk_controls.cpp
extern uint32_t g_button_pictures;              // src/game/rotk_prompts.cpp
void Loaded();                                  // rotk_movies.cpp
void Init();                                    // rotk_settings.cpp: ROTKFix.ini, then the frame rate
void LoadTable(const void* table);              // rotk_settings.cpp: 1.0.0's defaults for settings the file lacks
void SettingChanged(const cfg::Desc& d);        // rotk_settings.cpp: the in-game menu changed one
void FileOpened(const char* name);              // rotk_controls.cpp: the front end or a level
bool InMenus();
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);  // rotk_widescreen.cpp: F1
extern bool g_second_controller;                // rotk_controls.cpp
extern bool g_unlock_all;                       // rotk_secrets.cpp
extern uint32_t g_damage_percent, g_enemy_damage_percent;  // rotk_secrets.cpp
extern bool g_all_heroes;                       // rotk_playas.cpp
extern bool g_eowyn;                            // rotk_eowyn.cpp
extern uint32_t g_subtitles;                    // rotk_lang.cpp
extern bool g_cheat_invulnerable, g_cheat_missiles, g_cheat_devastating, g_cheat_perfect;  // rotk_cheats.cpp
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
    .version = "rotkx-port " GAME_VERSION,        // --version
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"The game files couldn't be found. Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .title_id = 0x4541003E,
    .images = {{"default.xbe", "ROTKXbox"}},  // each recompiled xbe and its exe, for title launches
    // the game's own settings, in the settings file's [graphics]; defaults as on the console
    .options = {{"graphics", "skip_logos", cfg::Type::Bool, "Skip opening logos",
                 "Go straight to the title screen at startup. (The very first start, without a saved game, opens with "
                 "the Helm's Deep film, whose first seconds are the logos: Skip any video lets you skip it.)",
                 &rotk::g_skip_logos},
                {"graphics", "skip_all_videos", cfg::Type::Bool, "Skip any video",
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
                 nullptr, {"Auto", "Xbox", "PlayStation", "Nintendo", "Keyboard keys"}},
                {"input", "second_controller", cfg::Type::Bool, "Second controller",
                 "A second controller plays as player 2 in co-op. Off: only the first controller is read, so a "
                 "second one (or a program that adds a pretend one) can't take over player 2.",
                 &rotk::g_second_controller},
                {"gameplay", "damage_taken", cfg::Type::Int, "Damage you take",
                 "How hard enemies hit you, as a share of the chosen difficulty's own (100). Lower is easier.",
                 nullptr, &rotk::g_damage_percent, nullptr, 10, 500},
                {"gameplay", "enemy_damage", cfg::Type::Int, "Damage enemies take",
                 "How hard your hits are on enemies, as a share of the chosen difficulty's own (100). Fights between "
                 "other characters, such as your allies and their enemies, stay as they are. Higher is easier.",
                 nullptr, &rotk::g_enemy_damage_percent, nullptr, 10, 500},
                {"gameplay", "unlock_all", cfg::Type::Bool, "Unlock all levels and characters",
                 "Every level, the secret one included, and every character can be chosen from the start, as in the "
                 "game's developer builds. Your saved progress isn't changed: turning it off again shows only what "
                 "you've earned.",
                 &rotk::g_unlock_all},
                {"gameplay", "all_heroes", cfg::Type::Bool, "All heroes in completed levels",
                 "In a level you've already completed, the character select offers every hero, as the game does once "
                 "it's beaten. A level not completed yet keeps the game's choice, and Helm's Deep stays Gandalf's.",
                 &rotk::g_all_heroes},
                {"gameplay", "eowyn", cfg::Type::Bool, "\u00c9owyn instead of Faramir",
                 "\u00c9owyn takes Faramir's place: her figure and name on the character select, her face on the HUD, "
                 "and her in every level he can play, with his moves, combos, bow and upgrades. Made from her own "
                 "model when the game starts (a few seconds more). She has no voice of her own, so she fights "
                 "quietly.",
                 &rotk::g_eowyn},
                {"language", "subtitles", cfg::Type::Choice, "Translation subtitles",
                 "The game's menus, prompts and spoken lines translated at the bottom of the screen, in place of the "
                 "game's English subtitles. The game's own text stays as it is.",
                 nullptr, &rotk::g_subtitles, nullptr, 0, 0, {"off", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "ru"},
                 nullptr, nullptr,
                 {"Off", "Deutsch", "Fran\u00e7ais", "Espa\u00f1ol", "Italiano", "Portugu\u00eas (Brasil)", "Polski",
                  "Nederlands", "Svenska", "\u0420\u0443\u0441\u0441\u043a\u0438\u0439"}},
                {"cheats", "invulnerable", cfg::Type::Bool, "Invulnerable",
                 "The game's own secret code: enemies can't hurt you.", &rotk::g_cheat_invulnerable},
                {"cheats", "always_devastating", cfg::Type::Bool, "Always devastating",
                 "The game's own secret code: every attack is a devastating one.", &rotk::g_cheat_devastating},
                {"cheats", "infinite_missiles", cfg::Type::Bool, "Infinite missiles",
                 "The game's own secret code: arrows and throwing weapons never run out.", &rotk::g_cheat_missiles},
                {"cheats", "perfect_mode", cfg::Type::Bool, "Perfect mode",
                 "The game's own secret code: its Perfect Mode.", &rotk::g_cheat_perfect}},
    // the launcher's preset menu, as The Two Towers' (keys left out stay the player's own); frame rate only in
    // Original (the console's 30). Smooth menus stay the player's own: Sharper text redraws the menus here
    .presets = {{"Original", {{"graphics.sharpening", "0"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "false"},
                              {"graphics.anisotropic_filtering", "\"0\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"0\""},
                              {"graphics.fps", "\"30\""}, {"graphics.console_textures", "true"}}},
                {"Balanced", {{"graphics.sharpening", "30"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                              {"graphics.anisotropic_filtering", "\"8\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"2\""}, {"graphics.console_textures", "false"}}},
                {"High", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                          {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"4\""}, {"graphics.console_textures", "false"}}},
                {"Ultra", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"150\""}, {"graphics.widescreen", "true"},
                           {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"8\""}, {"graphics.console_textures", "false"}}}},
    // what each Xbox button does (ROTK Fix's names for the slots), for the keyboard's labels; Black is RB, White LB
    .button_actions = {"Speed Attack", "Physical Attack", "Parry", "Fierce Attack", "Action", "Jump Back",
                       "Ranged Attack", "Killing Move"},
    .load = rotk::LoadTable,              // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = rotk::Init,                   // after the settings load, before the window and boot
    .loaded = rotk::Loaded,               // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = rotk::Message,             // window messages first; true takes one (F1 hides the HUD)
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = rotk::Pad,                     // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .remap_controls = true,               // [controls], as in the other ports (ROTKFix.ini's layout is brought over once)
    // the in-game menu: these take effect at once; its changes go into settings.toml, as in the other ports
    .live_settings = {"graphics.fov", "graphics.draw_distance", "graphics.hud_edges", "gameplay.damage_taken",
                      "gameplay.enemy_damage", "cheats.invulnerable", "cheats.always_devastating",
                      "cheats.infinite_missiles", "cheats.perfect_mode"},
    .setting_changed = rotk::SettingChanged,
    .file_opened = rotk::FileOpened,
    .in_menus = rotk::InMenus,
    .menu_accept_start = true,  // the front end goes on with Start; "press start" takes nothing else
    .achievements = rotk::Achievements(),
    .discord_app_id = ROTKX_DISCORD_APP_ID,
    .activity = rotk::Activity,
    .activity_state = rotk::ActivityState,
};
