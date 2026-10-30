// The Lord of the Rings: The Third Age (original Xbox, NTSC-U). What the runtime asks of the game.
// A first, bare description: the game's own options (frame rate, field of view, and so on) are added as their hooks
// are found, as the other three ports have them.
#include "app/app.h"
#include "gpu/backend.h"
namespace ta {
void Loaded();          // src/game/ta_harness_level.cpp
void InitWidescreen();  // src/game/ta_widescreen.cpp
void InitFrameRate();   // src/game/ta_framerate.cpp
extern uint32_t g_fps;  // src/game/ta_framerate.cpp
void FileOpened(const char* name);  // src/game/ta_state.cpp: the scene opened
bool InMenus();                     // src/game/ta_state.cpp: the front end
void FileRead(const char* name);    // src/game/ta_state.cpp: movies streaming
std::string Activity();             // src/game/ta_state.cpp: the area, for the Discord status
std::vector<ach::Def> Achievements();  // src/game/ta_achievements.cpp
void InitMovies();                  // src/game/ta_state.cpp
void InitMovieOptions();
void InitMovieShape();             // src/game/ta_movies.cpp
extern bool g_skip_logos, g_skip_all_videos, g_sharp_text, g_unlock_all, g_invulnerable, g_infinite_ap;
extern uint32_t g_damage_percent, g_enemy_damage_percent;  // src/game/ta_gameplay.cpp
void InitSharpText();
void InitHud();                    // src/game/ta_hud.cpp
extern bool g_hud_edges;              // src/game/ta_sharptext.cpp
extern bool g_safe_area;            // src/game/ta_widescreen.cpp           // src/game/ta_movies.cpp
extern uint32_t g_draw_percent;     // src/game/ta_widescreen.cpp
extern uint32_t g_fov_percent;      // src/game/ta_widescreen.cpp
extern uint32_t g_movie_zoom;        // src/game/ta_movies.cpp
extern bool g_alpha_edges;          // src/game/ta_widescreen.cpp
bool g_second_core = true;          // [graphics] second_core: the drawing on a core of its own
void ApplyCameraSettings();         // src/game/ta_widescreen.cpp
// GameInfo::setting_changed: the in-game menu changed one, at once
void SettingChanged(const cfg::Desc&) { ApplyCameraSettings(); }
// GameInfo::init: after the settings load, before the window and boot
void Init() {
    InitWidescreen();
    InitFrameRate();
    InitMovies();
    InitMovieShape();
    InitSharpText();
    InitHud();
    gpu::g_render_thread = g_second_core ? 1 : 0;
}
}

#ifndef GAME_VERSION
#define GAME_VERSION "0.1.0"  // CMakeLists.txt sets it (XBE_VERSION); the headless harness does not
#endif

// the Discord application whose name Discord shows as the game; null: no Discord status
#ifndef TAX_DISCORD_APP_ID
#define TAX_DISCORD_APP_ID nullptr
#endif

const app::GameInfo app::kGame = {
    .title = L"The Third Age (PC port)",  // window and splash
    .short_title = L"Third Age Xbox",     // message boxes
    .name = L"TAXbox",                    // window classes, crash dumps, handover event
    .log = "tax",                         // log file prefix
    .settings = "settings.toml",
    .version = "tax-port " GAME_VERSION,
    .splash = L"splash",
    .missing = L"The game files couldn't be found. Put the game in the assets folder: its disc image (.iso), or Default.xbe with its folders beside it.",
    .title_id = 0x4541005F,  // EA-095, version 8 (the player's default.xbe, checked)
    .images = {{"default.xbe", "TAXbox"}},
    // the game's own settings, in the settings file's [graphics]; defaults as on the console
    .options = {{"graphics", "skip_logos", cfg::Type::Bool, "Skip opening logos",
                 "Go straight to the title screen at startup.", &ta::g_skip_logos},
                {"graphics", "skip_all_videos", cfg::Type::Bool, "Skip all videos",
                 "Every movie ends as it starts, as if skipped with A, the opening logos included.",
                 &ta::g_skip_all_videos},
                {"graphics", "fps", cfg::Type::Choice, "Frame rate",
                 "The game was made for 30 on the Xbox. Higher rates play at normal speed.",
                 nullptr, &ta::g_fps, nullptr, 0, 0, {"30", "60", "90", "120", "144", "165", "180", "240", "0"}, nullptr,
                 nullptr, {"30 fps", "60 fps", "90 fps", "120 fps", "144 fps", "165 fps", "180 fps", "240 fps", "Unlocked"}},
                {"graphics", "fov", cfg::Type::Int, "Field of view",
                 "How wide the camera sees, as a share of the game's own (100). Wider shows more of the battle but "
                 "makes characters look further away.",
                 nullptr, &ta::g_fov_percent, nullptr, 70, 150, {}},
                {"graphics", "draw_distance", cfg::Type::Int, "Draw distance",
                 "How far the camera sees, as a share of the game's own (100); the fog moves out with it.",
                 nullptr, &ta::g_draw_percent, nullptr, 50, 800, {}},
                {"graphics", "movie_zoom", cfg::Type::Int, "Movie zoom",
                 "With Movie display on Fit, zooms the story movies in to cut down their black bars (100: none). "
                 "The sides of the picture are cut off a little.",
                 nullptr, &ta::g_movie_zoom, nullptr, 100, 140, {}},
                {"graphics", "safe_area", cfg::Type::Bool, "TV-safe border",
                 "The console kept a black border round the picture for old TVs, which cut off the edges. Off, the "
                 "picture fills the screen.",
                 &ta::g_safe_area},
                {"graphics", "hud_edges", cfg::Type::Bool, "HUD at the screen edges",
                 "With widescreen, the battle HUD keeps its shape and its pieces move out to the sides of the screen.",
                 &ta::g_hud_edges},
                {"graphics", "sharp_text", cfg::Type::Bool, "Sharper text",
                 "The game's letters redrawn at four times their size with crisp edges, for large screens.",
                 &ta::g_sharp_text},
                {"graphics", "second_core", cfg::Type::Bool, "Draw on a second core",
                 "Faster: the drawing runs on a processor core of its own, beside the game. Turn off if characters "
                 "show a ghost image or the picture flickers. Takes effect at the next start.",
                 &ta::g_second_core},
                {"graphics", "alpha_edges", cfg::Type::Bool, "Smooth leaf edges",
                 "With anti-aliasing on, smooths the edges of leaves, grass and fences.", &ta::g_alpha_edges},
                {"gameplay", "damage_taken", cfg::Type::Int, "Damage you take",
                 "How hard enemies hit your party, as a share of the game's own (100). Lower is easier.",
                 nullptr, &ta::g_damage_percent, nullptr, 10, 500},
                {"gameplay", "enemy_damage", cfg::Type::Int, "Damage enemies take",
                 "How hard your party's hits are on enemies, as a share of the game's own (100). Higher is easier.",
                 nullptr, &ta::g_enemy_damage_percent, nullptr, 10, 500},
                {"gameplay", "unlock_all", cfg::Type::Bool, "Unlock Evil Mode",
                 "Evil Mode can be started from any save. Your saved progress isn't changed: turning it off again "
                 "shows only what you've earned.",
                 &ta::g_unlock_all},
                {"cheats", "invulnerable", cfg::Type::Bool, "Invulnerable",
                 "Enemies can't hurt your party.", &ta::g_invulnerable},
                {"cheats", "infinite_ap", cfg::Type::Bool, "Infinite AP",
                 "Your party's skills and magic cost no AP.", &ta::g_infinite_ap}},
    // the same presets as the other ports, without the frame rate (no hook for it yet)
    .presets = {{"Original", {{"graphics.sharpening", "0"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "false"},
                              {"graphics.anisotropic_filtering", "\"0\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"0\""},
                              {"graphics.smooth_2d", "false"}, {"graphics.console_textures", "true"}}},
                {"Balanced", {{"graphics.sharpening", "30"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                              {"graphics.anisotropic_filtering", "\"8\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"2\""},
                              {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}},
                {"High", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                          {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"4\""},
                          {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}},
                {"Ultra", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"150\""}, {"graphics.widescreen", "true"},
                           {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"8\""},
                           {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}}},
    // [controls]: what A and B do (the menus and the battles' commands); the others show the button's name until
    // they are checked against the game
    .button_actions = {"Select", "Back", "", "", "", "", "", ""},
    .init = ta::Init,  // after the settings load: widescreen and frame rate
    .loaded = ta::Loaded,  // image in memory: TA_LEVEL (testing)
    .live_settings = {"graphics.fov", "graphics.draw_distance", "graphics.alpha_edges"},
    .setting_changed = ta::SettingChanged,  // draw distance and smooth edges take effect at once
    .file_opened = ta::FileOpened,  // the scene, from the .scx the game opens
    .file_read = ta::FileRead,      // a movie playing, from its .vid being read
    .in_menus = ta::InMenus,        // the front end: Esc goes back
    .achievements = ta::Achievements(),  // areas, the kill deeds and Fate (ta_achievements.cpp)
    .discord_app_id = TAX_DISCORD_APP_ID,
    .activity = ta::Activity,  // "Eregion"
};
