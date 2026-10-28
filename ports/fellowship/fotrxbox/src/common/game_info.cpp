#include "app/app.h"
#ifndef GAME_VERSION
#define GAME_VERSION "1.6.0"  // CMakeLists.txt sets it (XBE_VERSION); the headless harness does not
#endif

namespace fotr {
extern uint32_t g_fps;       // src/game/fotr_framerate.cpp
extern bool g_skip_logos;    // src/game/fotr_movies.cpp
void FileOpened(const char* name);
extern bool g_cheat_ring, g_cheat_health, g_cheat_spirit, g_cheat_ammo;  // src/game/fotr_cheats.cpp
extern uint32_t g_text_language;  // src/game/fotr_lang.cpp
extern uint32_t g_fov_percent;    // src/game/fotr_camera.cpp
extern uint32_t g_button_pictures;  // src/game/fotr_prompts.cpp
extern bool g_sharp_text;           // src/game/fotr_text.cpp
extern bool g_invert_camera_y, g_invert_camera_x;  // src/game/fotr_input.cpp
void Pad(int port, input::Pad& pad);
extern uint32_t g_damage_taken, g_enemy_damage;  // src/game/fotr_difficulty.cpp
extern bool g_combat_test;      // src/game/fotr_extras.cpp
extern uint32_t g_start_chapter;  // src/game/fotr_chapters.cpp
void Init();
std::vector<ach::Def> Achievements();  // src/game/fotr_achievements.cpp
std::string Activity();                // src/game/fotr_achievements.cpp: the region, for the Discord status
void FrameShown(double fps);           // src/game/fotr_camera.cpp
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);  // fotr_camera.cpp: F1
}  // namespace fotr

// the Discord application whose name Discord shows as the game ("The Fellowship of the Ring"); null: no Discord status
#ifndef FOTRX_DISCORD_APP_ID
#define FOTRX_DISCORD_APP_ID nullptr
#endif

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
                {"graphics", "sharp_text", cfg::Type::Bool, "Sharper text",
                 "The game's text (menus, hints, dialogue, the quest journal) drawn at three times its size, so it "
                 "stays sharp at high resolutions. Read at start.",
                 &fotr::g_sharp_text},
                {"graphics", "button_pictures", cfg::Type::Choice, "Button pictures",
                 "Which controller's buttons the game's hints and menus show. Auto follows the controller you play "
                 "with when the game starts.",
                 nullptr, &fotr::g_button_pictures, nullptr, 0, 0, {"auto", "xbox", "playstation", "nintendo", "keyboard"},
                 nullptr, nullptr, {"Auto", "Xbox", "PlayStation", "Nintendo", "Keyboard keys"}},
                {"input", "invert_camera_y", cfg::Type::Bool, "Invert camera up/down",
                 "Reverses up and down on the controller's right stick, which moves the camera. For players used to "
                 "inverted camera controls.", &fotr::g_invert_camera_y},
                {"input", "invert_camera_x", cfg::Type::Bool, "Invert camera left/right",
                 "Reverses left and right on the controller's right stick, which moves the camera.",
                 &fotr::g_invert_camera_x},
                {"gameplay", "damage_taken", cfg::Type::Int, "Damage you take",
                 "How hard enemies hit you, as a share of the game's own (100). Lower is easier.",
                 nullptr, &fotr::g_damage_taken, nullptr, 10, 500},
                {"gameplay", "enemy_damage", cfg::Type::Int, "Damage enemies take",
                 "How hard your hits and shots are on enemies, as a share of the game's own (100). Higher is easier.",
                 nullptr, &fotr::g_enemy_damage, nullptr, 10, 500},
                {"gameplay", "combat_test", cfg::Type::Bool, "Combat Test (unused developer arenas)",
                 "Left in the game but never switched on: in Bilbo's room in Rivendell, a choice of Frodo, Aragorn or "
                 "Gandalf takes you to one of three test arenas with their own enemies. Their exits lead back to "
                 "Bilbo's room.",
                 &fotr::g_combat_test},
                {"gameplay", "start_chapter", cfg::Type::Choice, "New game starts at",
                 "The game has no chapter select: this starts a new game at the beginning of a later chapter instead "
                 "of Bag End, to replay it. Items and quest steps from the chapters skipped are not given. Saved games "
                 "are not affected.",
                 nullptr, &fotr::g_start_chapter, nullptr, 0, 0,
                 {"shire", "old_forest", "bree", "weathertop", "rivendell", "moria", "lothlorien", "anduin"}, nullptr,
                 nullptr,
                 {"The Shire (the game's own start)", "The Old Forest", "Bree", "Weathertop", "Rivendell", "Moria",
                  "Lothlórien", "The River Anduin"}},
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
    .message = fotr::Message,  // window messages first: F1 hides the HUD
    .ui = fotr::FrameShown,  // each frame shown: wider screens tell menus from fades by it
    .pad = fotr::Pad,        // the camera stick reversed when asked ([input] invert_camera_*)
    // the in-game menu (F10): these take effect at once
    .live_settings = {"graphics.fov", "input.invert_camera_y", "input.invert_camera_x", "gameplay.damage_taken", "gameplay.enemy_damage", "cheats.infinite_health",
                      "cheats.infinite_spirit", "cheats.infinite_ammo", "cheats.infinite_ring"},
    .file_opened = fotr::FileOpened,  // the movies, to show each by the movie settings; achievements
    .achievements = fotr::Achievements(),
    .discord_app_id = FOTRX_DISCORD_APP_ID,
    .activity = fotr::Activity,
};
