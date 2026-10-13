#include "app/app.h"

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"ROTK Xbox (PC port)",      // window and splash; a neutral name until release
    .short_title = L"ROTK Xbox",          // message boxes
    .name = L"ROTKXbox",                  // window classes, crash dumps, handover event
    .log = "rotkx",                       // log file prefix
    .settings = "settings.toml",          // settings file beside the exe, unless --config names another
    .version = "rotkx-port 0.1.0",        // --version
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"The game files couldn't be found. Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .title_id = 0x4541003E,
    .images = {{"default.xbe", "ROTKXbox"}},  // each recompiled xbe and its exe, for title launches
    .options = {},                        // the game's own settings come once the faithful game runs
    .presets = {},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = nullptr,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
