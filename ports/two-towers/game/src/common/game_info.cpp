#include <cstring>
#include "app/app.h"

#ifndef GAME_VERSION
#define GAME_VERSION "1.5.0"  // CMakeLists.txt sets it (XBE_VERSION); the headless harness does not
#endif

namespace tt {
extern uint32_t g_fps_mode;  // src/game/tt_framerate.cpp
extern bool g_hud_edges;  // src/game/tt_widescreen.cpp
extern uint32_t g_fov_percent;  // src/game/tt_extras.cpp
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);  // tt_extras.cpp: F1 hides the HUD
extern bool g_skip_logos;  // tt_movies.cpp
extern bool g_skip_all_videos;  // tt_movies.cpp
extern bool g_kill_points;      // tt_killpoints.cpp
extern bool g_all_heroes;       // tt_playas.cpp
extern uint32_t g_subtitles;    // tt_lang.cpp
extern bool g_cheat_invulnerable, g_cheat_devastating, g_cheat_missiles, g_cheat_slow;  // tt_cheats.cpp
void InitRenderer();  // tt_renderer.cpp: the water simulation's targets at the console's size
extern uint32_t g_damage_taken, g_enemy_damage;  // tt_devsettings.cpp
void FileOpened(const char* name);  // tt_extras.cpp: the front end or a level
bool InMenus();
bool TitleScreen();  // tt_extras.cpp: the title screen is up
std::vector<ach::Def> Achievements();  // tt_achievements.cpp
std::string Activity();
}

// the Discord application whose name Discord shows as the game ("The Two Towers"); null: no Discord status
#ifndef TT_DISCORD_APP_ID
#define TT_DISCORD_APP_ID nullptr
#endif

// the stream sound banks (D:\Sound\S_WB_<level>.xwb): the game crashes without one (an incomplete copy), so its
// absence ends the run with a message that names it
static bool RequiredFile(const char* name) {
    size_t n = std::strlen(name);
    auto lower = [](char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); };
    if (n < 8 || lower(name[0]) != 'd' || name[1] != ':') return false;
    const char* ext = name + n - 4;
    return lower(ext[0]) == '.' && lower(ext[1]) == 'x' && lower(ext[2]) == 'w' && lower(ext[3]) == 'b';
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"The Two Towers (PC port)",               // window and splash
    .short_title = L"Two Towers",         // message boxes
    .name = L"Game",                      // window classes, crash dumps, handover event
    .log = "tt",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe, unless --config names another
    .version = "tt-port " GAME_VERSION,    // --version
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"The game files couldn't be found. Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .title_id = 0x45410010,
    .images = {{"default.xbe", "TwoTowers"}},  // each recompiled xbe and its exe, for title launches
    // the game's own settings, in the settings file's [graphics]; defaults as on the console
    .options = {{"graphics", "fps", cfg::Type::Choice, "Frame rate",
                 "The game was made for 30 on the Xbox. Higher rates play at normal speed; movies keep their own pace. "
                 "In fullscreen the screen switches to the matching refresh rate. In borderless and windowed, rates that "
                 "divide evenly into your screen's refresh rate are the smoothest.",
                 nullptr, &tt::g_fps_mode, nullptr, 0, 0, {"30", "60", "90", "120", "144", "165", "180", "240"}, nullptr,
                 nullptr, {"30 fps", "60 fps", "90 fps", "120 fps", "144 fps", "165 fps", "180 fps", "240 fps"}},
                {"graphics", "hud_edges", cfg::Type::Bool, "HUD at the screen edges",
                 "With widescreen correction on, the health bar and counters sit at the edges of the wide picture "
                 "instead of in the 4:3 middle.",
                 &tt::g_hud_edges, nullptr, nullptr, 0, 0, {}, "hud_edges"},
                {"graphics", "fov", cfg::Type::Int, "Field of view",
                 "How wide the camera sees, as a share of the game's own (100). Wider shows more of the battle but "
                 "makes characters look further away.",
                 nullptr, &tt::g_fov_percent, nullptr, 70, 150, {}, "fov_percent"},
                {"gameplay", "damage_taken", cfg::Type::Int, "Damage you take",
                 "How hard enemies hit you and your allies, as a share of the chosen difficulty's own (100). Lower is "
                 "easier.",
                 nullptr, &tt::g_damage_taken, nullptr, 25, 300},
                {"gameplay", "enemy_damage", cfg::Type::Int, "Damage enemies take",
                 "How hard you and your allies hit enemies, as a share of the chosen difficulty's own (100). Higher "
                 "is easier.",
                 nullptr, &tt::g_enemy_damage, nullptr, 25, 300},
                {"gameplay", "kill_points", cfg::Type::Bool, "Kill points",
                 "Shows the points each killing blow earns after its rating (\"Excellent +150\"), as The Return of "
                 "the King does. Off: as the game shows it.",
                 &tt::g_kill_points},
                {"gameplay", "all_heroes", cfg::Type::Bool, "All heroes in completed levels",
                 "In a level you've already completed, the level select offers every hero, Isildur too, as it does "
                 "once the game is beaten. A level not completed yet keeps the game's choice; the Prologue and "
                 "Weathertop keep the hero the game gives them.",
                 &tt::g_all_heroes},
                {"language", "subtitles", cfg::Type::Choice, "Translation subtitles",
                 "The game's menus, prompts and descriptions translated at the bottom of the screen, as the game shows "
                 "them. The game's own text stays as it is.",
                 nullptr, &tt::g_subtitles, nullptr, 0, 0, {"off", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "ru"},
                 nullptr, nullptr,
                 {"Off", "Deutsch", "Fran\u00e7ais", "Espa\u00f1ol", "Italiano", "Portugu\u00eas (Brasil)", "Polski",
                  "Nederlands", "Svenska", "\u0420\u0443\u0441\u0441\u043a\u0438\u0439"}},
                {"cheats", "invulnerable", cfg::Type::Bool, "Invulnerable",
                 "The game's own secret code: enemies can't hurt you.", &tt::g_cheat_invulnerable},
                {"cheats", "always_devastating", cfg::Type::Bool, "Always devastating",
                 "The game's own secret code: every attack is a devastating one.", &tt::g_cheat_devastating},
                {"cheats", "infinite_missiles", cfg::Type::Bool, "Infinite missiles",
                 "The game's own secret code: arrows and throwing weapons never run out.", &tt::g_cheat_missiles},
                {"cheats", "slow_motion", cfg::Type::Bool, "Slow motion",
                 "The game's own secret code: the game runs in slow motion.", &tt::g_cheat_slow},
                {"graphics", "skip_logos", cfg::Type::Bool, "Skip opening logos",
                 "Go straight to the title screen at startup.", &tt::g_skip_logos},
                {"graphics", "skip_all_videos", cfg::Type::Bool, "Skip any video",
                 "Every movie can be skipped with the controller, including the opening logos.", &tt::g_skip_all_videos}},
    // the launcher's preset menu; keys left out (brightness, field of view, logos) stay the player's own. Frame rate is
    // only in Original (the console's 30): the others are picture quality, as ROTK Fix's
    .presets = {{"Original", {{"graphics.sharpening", "0"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "false"},
                              {"graphics.anisotropic_filtering", "\"0\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"0\""},
                              {"graphics.fps", "\"30\""}, {"graphics.smooth_2d", "false"}, {"graphics.console_textures", "true"}}},
                {"Balanced", {{"graphics.sharpening", "30"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                              {"graphics.anisotropic_filtering", "\"8\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"2\""}, {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}},
                {"High", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                          {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"4\""}, {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}},
                {"Ultra", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"150\""}, {"graphics.widescreen", "true"},
                           {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"8\""}, {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}}},
    // [controls]: what each Xbox button does, as the game's own controls screen names it; White does nothing, so it
    // keeps its Xbox name
    .button_actions = {"Speed Attack / Fire", "Knock Back", "Parry", "Fierce Attack", "Jump Back", nullptr, "Ranged Weapon",
                       "Killing Move"},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = tt::InitRenderer,             // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = tt::Message,               // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = nullptr,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .remap_controls = true,
    // [keyboard]: Black is Jump Back here (ROTK's White); White does nothing
    .key_defaults = {{keys::kBlack, "Tab"}, {keys::kWhite, ""}},
    // the in-game menu: these take effect at once
    .live_settings = {"graphics.fov", "graphics.hud_edges", "gameplay.damage_taken", "gameplay.enemy_damage",
                      "cheats.invulnerable", "cheats.always_devastating", "cheats.infinite_missiles",
                      "cheats.slow_motion"},
    .file_opened = tt::FileOpened,
    .required_file = RequiredFile,
    .in_menus = tt::InMenus,  // Esc goes back in the front end's menus; Enter (A) goes on there as it is
    .menu_start_with_a = true,  // and presses Start too: the title's "press start button" takes nothing else (#8)
    .start_with_a_now = tt::TitleScreen,  // on the title screen only: the Yes/No pop-ups between levels take A (#17)
    .achievements = tt::Achievements(),
    .discord_app_id = TT_DISCORD_APP_ID,
    .activity = tt::Activity,
};
