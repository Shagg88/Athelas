// every setting the settings file holds, described once: the host's here, the game's in GameInfo::options. No
// platform code, so the list can be checked anywhere.
#include <algorithm>
#include <string>

#include "app/app.h"
#include "core/input.h"

namespace app {

namespace {
std::vector<const char*> ScreenChoices() {
    std::vector<const char*> c;
    for (const std::string& n : ScreenNames()) c.push_back(n.c_str());
    return c;
}
}  // namespace

// the host's settings, then the game's; the defaults are the values here before Load
std::vector<cfg::Desc> SettingsList() {
    using cfg::Type;
    Settings& s = Get();
    static_assert(sizeof(DisplayMode) == sizeof(uint32_t));
    uint32_t* mode = reinterpret_cast<uint32_t*>(&s.display_mode);
    // labels and help as ROTK Fix words them, so the launcher reads the same for every game
    std::vector<cfg::Desc> l = {
        {"display", "mode", Type::Choice, "Display mode",
         "Borderless: fills the screen and switches instantly with Alt-Tab (recommended).\n"
         "Fullscreen: the screen switches to the chosen resolution and refresh rate.\n"
         "Windowed: a normal window you can move around.",
         nullptr, mode, nullptr, 0, 0, {"fullscreen", "borderless", "windowed"}, "display_mode", nullptr,
         {"Fullscreen", "Borderless", "Windowed"}},
        {"display", "screen", Type::Choice, "Monitor", "Which screen the game runs on.", nullptr, nullptr, &s.screen, 0,
         0, ScreenChoices()},
        {"display", "refresh", Type::Int, "Refresh rate",
         "Only used in fullscreen. Borderless and windowed use the desktop's refresh rate. 0 is the highest the screen "
         "offers.",
         nullptr, &s.refresh, nullptr, 0, 500},
        {"display", "width", Type::Int, "Width",
         "The size of the picture on screen, with height. 0 uses the screen's own size.", nullptr, &s.width, nullptr, 0,
         16384, {}, "width"},
        {"display", "height", Type::Int, "Height",
         "The size of the picture on screen, with width. 0 uses the screen's own size.", nullptr, &s.height, nullptr, 0,
         16384, {}, "height"},
        {"display", "vsync", Type::Bool, "V-Sync",
         "Prevents tearing. The frame rate can't go above the screen's refresh rate.", &s.vsync, nullptr, nullptr, 0, 0,
         {}, "vsync"},
        {"display", "show_fps", Type::Bool, "Frame rate counter",
         "Shows the frames per second in the top right corner of the screen.", &s.show_fps},
        {"display", "render_percent", Type::Choice, "Render resolution",
         "The game is drawn at this share of the screen's size, then fitted to it. Above 100% gives a cleaner, sharper "
         "picture (supersampling) and costs speed; below 100% is faster but softer. 100% is the screen's own size.",
         nullptr, &s.render_percent, nullptr, 0, 0, {"50", "75", "100", "125", "150", "200", "250", "300"},
         "render_percent", nullptr,
         {"50%", "75%", "100% (screen's own)", "125%", "150%", "200% (supersampling)", "250% (supersampling)",
          "300% (supersampling)"}},
        {"graphics", "anti_aliasing", Type::Choice, "Anti-aliasing",
         "Smooths jagged edges (MSAA). Higher costs speed, most of all with a high render resolution.", nullptr,
         &s.msaa, nullptr, 0, 0, {"0", "2", "4", "8"}, nullptr, nullptr, {"Off", "2x", "4x", "8x"}},
        {"graphics", "anisotropic_filtering", Type::Choice, "Texture filtering",
         "Anisotropic filtering: keeps ground and walls sharp at a distance.", nullptr, &s.aniso, nullptr, 0, 0,
         {"0", "2", "4", "8", "16"}, "anisotropic_filtering", nullptr, {"Off", "2x", "4x", "8x", "16x"}},
        {"graphics", "lod_bias", Type::Float, "Texture sharpness",
         "Shows textures at higher detail sooner. Too high can shimmer. 0.00 is the game's own.", nullptr, nullptr,
         nullptr, -100, 0, {}, nullptr, &s.lod_bias},
        {"graphics", "console_textures", Type::Bool, "Original texture softness",
         "At high resolutions the game's textures show their full, raw detail (lava, rock and water look grainier "
         "than on a TV). This draws them at the detail the Xbox used at its own resolution: softer, as you remember "
         "them. Menus and the HUD are not changed.",
         &s.console_textures},
        {"graphics", "sharpening", Type::Int, "Sharpening",
         "Crisper picture. 0 turns it off. While playing, [ switches it on and off to compare.", nullptr, &s.sharpening,
         nullptr, 0, 100},
        {"graphics", "smooth_2d", Type::Bool, "Smooth menus and HUD",
         "Menu pictures, icons and text that the game draws unfiltered are upscaled smoothly instead of turning into "
         "blocks at higher render resolutions. Off: as the game draws them.",
         &s.smooth_2d},
        {"graphics", "colour_look", Type::Choice, "Colour look",
         "Original: the game's own colours.\nFilm: richer contrast, cool shadows and warm highlights.\nVivid: stronger "
         "colour.\nDark fantasy: cooler and moodier, with less colour.\nWhile playing, ] switches between them.",
         nullptr, &s.colour_look, nullptr, 0, 0, {"original", "film", "vivid", "dark"}, nullptr, nullptr,
         {"Original", "Film", "Vivid", "Dark fantasy"}},
        {"graphics", "look_strength", Type::Int, "Look strength", "How strongly the colour look is applied.", nullptr,
         &s.look_strength, nullptr, 0, 100},
        {"graphics", "brightness", Type::Float, "Brightness",
         "1.00 is the game's own picture. Higher lifts dark scenes and caves without washing out white.", nullptr,
         nullptr, nullptr, 50, 200, {}, nullptr, &s.brightness},
        {"graphics", "hdr", Type::Bool, "HDR",
         "For HDR screens with Windows HDR on: fire, sky and lights shine brighter. The game was made for normal "
         "screens, so this brightens what it drew rather than adding detail. On other screens the picture stays as it is.",
         &s.hdr},
        {"graphics", "widescreen", Type::Bool, "Widescreen correction",
         "Shows more at the sides. Off keeps the original 4:3 picture, with bars at the sides.", &s.widescreen, nullptr,
         nullptr, 0, 0, {}, "widescreen"},
        {"graphics", "movie_display", Type::Choice, "Movie display",
         "Fit: the movies fill a wide screen with correct proportions (the black bars above and below the film are "
         "cut off).\nStretch: the whole movie frame is stretched to the screen.\n4:3: the movie as on the console, with "
         "bars at the sides.",
         nullptr, &s.movie_display, nullptr, 0, 0, {"fit", "stretch", "4:3"}, nullptr, nullptr, {"Fit", "Stretch", "4:3"}},
        {"graphics", "movie_sharpening", Type::Int, "Sharper cutscenes",
         "The movies are only 640 x 480. This sharpens them at their own pixel size before they are stretched to your "
         "screen, so faces and edges look crisper. 0 shows them as the game always did.",
         nullptr, &s.movie_sharpening, nullptr, 0, 100},
        {"graphics", "cutscene_audio_delay", Type::Int, "Cutscene sound delay",
         "Bluetooth speakers and headphones play sound late. This holds the movie picture back to match (150-300 ms is "
         "typical). 0 = off.",
         nullptr, &s.movie_delay, nullptr, 0, 500},
        {"textures", "replace", Type::Bool, "Use texture packs",
         "Shows sharper or remade pictures from texture packs. A pack goes in its own folder inside textures\\replace; "
         "remove the folder to go back to the original pictures.",
         &s.texture_replace},
        {"textures", "dump", Type::Bool, "Save game textures",
         "For making texture packs: every picture the game uses is saved as a PNG in textures\\dump as you play. Levels "
         "load a little slower the first time. Turn it off when you have what you need.",
         &s.texture_dump, nullptr, nullptr, 0, 0, {}, "texture_dump"},
        {"input", "vibration", Type::Int, "Vibration",
         "Controller rumble from the original Xbox version. 0 turns it off.", nullptr, &s.vibration, nullptr, 0, 100},
        {"input", "stick_deadzone", Type::Int, "Stick deadzone",
         "How far a stick must move before it counts, in % of its travel. 0 leaves it to the game, as on the Xbox; "
         "raise it if your character drifts.",
         nullptr, &s.stick_deadzone, nullptr, 0, 50},
        {"input", "trigger_threshold", Type::Int, "Trigger sensitivity",
         "How far the triggers must be pulled to count as pressed, in % of their travel. 0 leaves it to the game, as "
         "on the Xbox.",
         nullptr, &s.trigger_threshold, nullptr, 0, 90},
        {"network", "join_ip", Type::String, "Co-op host address",
         "Also looks for a co-op host at this address, past the local network. Empty searches the local network only.",
         nullptr, nullptr, &s.join_ip, 0, 0, {}, "join_ip"},
    };
    // [controls]: which controller button presses each Xbox button, named by what the game does with it
    static const char* const kKeys[8] = {"a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger"};
    static const char* const kXbox[8] = {"Xbox A", "Xbox B", "Xbox X", "Xbox Y", "Xbox Black", "Xbox White",
                                         "Xbox left trigger", "Xbox right trigger"};
    std::vector<const char*> sources(std::begin(input::kSources), std::end(input::kSources));
    std::vector<const char*> names(std::begin(input::kSourceNames), std::end(input::kSourceNames));
    for (int i = 0; i < (kGame.remap_controls ? 8 : 0); i++) {
        const char* action = size_t(i) < kGame.button_actions.size() ? kGame.button_actions[size_t(i)] : nullptr;
        cfg::Desc d{"controls", kKeys[i], Type::Choice, action ? action : kXbox[i],
                    "The button on your controller that does this. As on the Xbox by default (the Xbox's Black and "
                    "White buttons are the bumpers).",
                    nullptr, &s.buttons[i], nullptr, 0, 0, sources, nullptr, nullptr, names};
        l.push_back(d);
    }
    l.push_back({"window", "pause_unfocused", Type::Bool, "Pause in the background",
                 "The game stops, picture and sound, while another window is in front (Alt-Tab), and carries on "
                 "where it was when you come back.",
                 &s.pause_unfocused});
    l.push_back({"window", "mute_unfocused", Type::Bool, "Mute in the background",
                 "No sound while another window is in front; the game keeps running.", &s.mute_unfocused});
    l.push_back({"window", "background_input", Type::Bool, "Controller in the background",
                 "Controllers still play the game while another window is in front. Off: only the window in front "
                 "gets them, so pressing buttons in another program does nothing here.",
                 &s.background_input});
    l.push_back({"system", "one_core", Type::Bool, "One processor core",
                 "The game's own threads take turns on one processor core, as they did on the Xbox, instead of "
                 "running side by side. A little slower. Try it if the game crashes at random on a Windows on ARM "
                 "PC; read at start.",
                 &s.one_core});
    if (!kGame.achievements.empty())
        l.push_back({"achievements", "notify", Type::Bool, "Achievement notes",
                     "A note in the corner of the screen when you earn an achievement. The list is on the in-game "
                     "menu (F10) and in the launcher.",
                     &s.achievement_notes});
    if (kGame.discord_app_id) {
        l.push_back({"discord", "enabled", Type::Bool, "Discord status",
                     "Shows on your Discord profile that you're playing, and which level, while Discord is running "
                     "on this PC. Nothing is sent anywhere else.",
                     &s.discord});
        l.push_back({"discord", "app_id", Type::String, "Discord application",
                     "Another Discord application id to show instead of the game's own; empty uses the game's.",
                     nullptr, nullptr, &s.discord_app_id});
    }
    // [keyboard]: on by default, it changes nothing for a controller
    l.push_back({"keyboard", "enabled", Type::Bool, "Keyboard and mouse",
                 "Play with the keyboard and mouse as well as a controller. The keys work like the controller's "
                 "buttons; the game's hints still show the controller's.",
                 &s.keyboard});
    l.push_back({"keyboard", "player", Type::Choice, "Keyboard player",
                 "Which player the keyboard and mouse play as. Player 2 lets one person use the keyboard in co-op "
                 "while the other uses a controller.",
                 nullptr, &s.keyboard_player, nullptr, 0, 0, {"1", "2"}, nullptr, nullptr, {"Player 1", "Player 2"}});  // stores 1 or 2
    l.push_back({"keyboard", "mouse_aim", Type::Bool, "Mouse aiming",
                 "While the ranged attack is held, moving the mouse aims, as the left stick does on a controller.",
                 &s.mouse_aim});
    l.push_back({"keyboard", "mouse_sensitivity", Type::Int, "Mouse sensitivity",
                 "How far a mouse movement turns the aim, in % (100 is the default).", nullptr, &s.mouse_sensitivity,
                 nullptr, 10, 400});
    l.push_back({"keyboard", "mouse_menus", Type::Bool, "Mouse in the menus",
                 "In the game's menus the mouse wheel moves up and down, a left click chooses and a right click goes "
                 "back.",
                 &s.mouse_menus});
    for (uint32_t i = 0; i < keys::kCount; i++) {
        const char* label = keys::kLabels[i];
        if (i >= keys::kA && i <= keys::kRightTrigger && size_t(i - keys::kA) < kGame.button_actions.size() &&
            kGame.button_actions[i - keys::kA])
            label = kGame.button_actions[i - keys::kA];
        l.push_back({"keyboard", keys::kSettingKeys[i], Type::String, label,
                     "The keys that do this, comma separated (\"Mouse1, Enter\"); empty for none.", nullptr, nullptr,
                     &s.keys[i]});
    }
    // the game's own settings go with the graphics ones, before textures
    auto at = std::find_if(l.begin(), l.end(), [](const cfg::Desc& d) { return std::string(d.section) == "textures"; });
    l.insert(at, kGame.options.begin(), kGame.options.end());
    return l;
}

}  // namespace app
