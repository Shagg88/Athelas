// every setting the settings file holds, described once: the host's here, the game's in GameInfo::options. No
// platform code, so the list can be checked anywhere.
#include <algorithm>
#include <string>

#include "app/app.h"

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
        {"graphics", "sharpening", Type::Int, "Sharpening",
         "Crisper picture. 0 turns it off. While playing, [ switches it on and off to compare.", nullptr, &s.sharpening,
         nullptr, 0, 100},
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
    // the game's own settings go with the graphics ones, before textures
    auto at = std::find_if(l.begin(), l.end(), [](const cfg::Desc& d) { return std::string(d.section) == "textures"; });
    l.insert(at, kGame.options.begin(), kGame.options.end());
    return l;
}

}  // namespace app
