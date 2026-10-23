#include "app/app.h"
#ifndef GAME_VERSION
#define GAME_VERSION "1.5.0"  // CMakeLists.txt sets it (XBE_VERSION); the headless harness does not
#endif

namespace fotr {
extern uint32_t g_fps;       // src/game/fotr_framerate.cpp
extern bool g_skip_logos;    // src/game/fotr_movies.cpp
void FileOpened(const char* name);
extern bool g_cheat_ring, g_cheat_health, g_cheat_spirit, g_cheat_ammo;  // src/game/fotr_cheats.cpp
extern uint32_t g_text_language;  // src/game/fotr_lang.cpp
extern uint32_t g_fov_percent;    // src/game/fotr_camera.cpp
extern bool g_invert_camera_y, g_invert_camera_x;  // src/game/fotr_input.cpp
void Pad(int port, input::Pad& pad);
extern uint32_t g_damage_taken, g_enemy_damage;  // src/game/fotr_difficulty.cpp
void Init();
std::vector<ach::Def> Achievements();  // src/game/fotr_achievements.cpp
void FrameShown(double fps);           // src/game/fotr_camera.cpp
}  // namespace fotr

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"The Fellowship of the Ring (PC port)",  // window and splash
    .short_title = L"FOTR Xbox",                       // message boxes
    .name = L"FOTRXbox",                               // window classes, crash dumps, handover event
    .log = "fotrx",                                    // log file prefix
    .settings = "settings.toml",                       // settings file beside the exe, unless --config names another
    .version = "fotrx-port " GAME_VERSION,                // --version
    .splash = L"splash",
    .missing = L"The game files couldn't be found. Put the game in the assets folder: its disc image (.iso), or Default.xbe with its folders beside it.",
    .title_id = 0x56550004,
    .images = {{"Default.xbe", "FOTRXbox"}},
    // the game's own settings, in the settings file's [graphics]; defaults as on the console
    .options = {{"graphics", "fps", cfg::Type::Choice, "Frame rate",
                 "The game was made for 30 on the Xbox. Higher rates play at normal speed; movies keep their own pace.",
                 nullptr, &fotr::g_fps, nullptr, 0, 0, {"30", "60", "90", "120", "144", "165", "180", "240"}, nullptr,
                 nullptr, {"30 fps", "60 fps", "90 fps", "120 fps", "144 fps", "165 fps", "180 fps", "240 fps"}},
                {"graphics", "skip_logos", cfg::Type::Bool, "Skip opening logos",
                 "Go straight to the title screen at startup.", &fotr::g_skip_logos},
                {"graphics", "fov", cfg::Type::Int, "Field of view",
                 "How wide the camera sees while you play, as a share of the game's own (100). Wider shows more "
                 "around Frodo but makes everything look further away. Cutscenes keep their own.",
                 nullptr, &fotr::g_fov_percent, nullptr, 70, 150},
                {"input", "invert_camera_y", cfg::Type::Bool, "Invert camera up/down",
                 "Reverses up and down on the controller's right stick, which moves the camera. For players used to "
                 "inverted camera controls.", &fotr::g_invert_camera_y},
                {"input", "invert_camera_x", cfg::Type::Bool, "Invert camera left/right",
                 "Reverses left and right on the controller's right stick, which moves the camera.",
                 &fotr::g_invert_camera_x},
                {"gameplay", "damage_taken", cfg::Type::Int, "Damage you take",
                 "How hard enemies hit you, as a share of the game's own (100). Lower is easier.",
                 nullptr, &fotr::g_damage_taken, nullptr, 25, 300},
                {"gameplay", "enemy_damage", cfg::Type::Int, "Damage enemies take",
                 "How hard your hits and shots are on enemies, as a share of the game's own (100). Higher is easier.",
                 nullptr, &fotr::g_enemy_damage, nullptr, 25, 300},
                {"cheats", "infinite_health", cfg::Type::Bool, "Infinite health",
                 "The game's own secret code (Y A X B A Y): health never runs out.", &fotr::g_cheat_health},
                {"cheats", "infinite_spirit", cfg::Type::Bool, "Infinite spirit",
                 "The game's own secret code (X Y A X B X): spirit never runs out.", &fotr::g_cheat_spirit},
                {"cheats", "infinite_ammo", cfg::Type::Bool, "Infinite ammunition",
                 "The game's own secret code (X B Y A X B): arrows, stones and other ammunition never run out.", &fotr::g_cheat_ammo},
                {"cheats", "infinite_ring", cfg::Type::Bool, "Infinite Ring",
                 "The game's own secret code (Y B A B Y X): Frodo can wear the Ring without limit.", &fotr::g_cheat_ring},
                {"language", "text", cfg::Type::Choice, "Game text language",
                 "The game's menus, hints, item descriptions, quest journal and dialogue text in another language (our "
                 "own translation). The voices stay English.",
                 nullptr, &fotr::g_text_language, nullptr, 0, 0, {"off", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "ru"},
                 nullptr, nullptr,
                 {"English (the game's own)", "Deutsch", "Fran\u00e7ais", "Espa\u00f1ol", "Italiano", "Portugu\u00eas (Brasil)",
                  "Polski", "Nederlands", "Svenska", "\u0420\u0443\u0441\u0441\u043a\u0438\u0439"}}},
    // the launcher's preset menu; keys left out (brightness, logos) stay the player's own. Frame rate is only in
    // Original (the console's 30): the others are picture quality. The game draws widescreen itself (the console's
    // 16:9 setting: a wider view and its HUD laid out for it), so the others have it on.
    .presets = {{"Original", {{"graphics.sharpening", "0"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "false"},
                              {"graphics.anisotropic_filtering", "\"0\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"0\""},
                              {"graphics.fps", "\"30\""}, {"graphics.smooth_2d", "false"}, {"graphics.console_textures", "true"}}},
                {"Balanced", {{"graphics.sharpening", "30"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                              {"graphics.anisotropic_filtering", "\"8\""}, {"graphics.lod_bias", "0.00"}, {"graphics.anti_aliasing", "\"2\""},
                              {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}},
                {"High", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"100\""}, {"graphics.widescreen", "true"},
                          {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"4\""},
                          {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}},
                {"Ultra", {{"graphics.sharpening", "60"}, {"display.render_percent", "\"150\""}, {"graphics.widescreen", "true"},
                           {"graphics.anisotropic_filtering", "\"16\""}, {"graphics.lod_bias", "-0.50"}, {"graphics.anti_aliasing", "\"8\""},
                           {"graphics.smooth_2d", "true"}, {"graphics.console_textures", "false"}}}},
    // [controls]: what each Xbox button does, as the game's own Control\default.map names it
    .button_actions = {"Attack", "Crouch", "Action", "Jump", "Use Belt Item", "Switch Weapons / Tools", "Target Left",
                       "Target Right"},
    .init = fotr::Init,  // after the settings load, before the window and boot
    .ui = fotr::FrameShown,  // each frame shown: wider screens tell menus from fades by it
    .pad = fotr::Pad,        // the camera stick reversed when asked ([input] invert_camera_*)
    // the in-game menu (F10): these take effect at once
    .live_settings = {"graphics.fov", "input.invert_camera_y", "input.invert_camera_x", "gameplay.damage_taken", "gameplay.enemy_damage", "cheats.infinite_health",
                      "cheats.infinite_spirit", "cheats.infinite_ammo", "cheats.infinite_ring"},
    .file_opened = fotr::FileOpened,  // the movies, to show each by the movie settings; achievements
    .achievements = fotr::Achievements(),
};
