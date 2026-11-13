// The in-game menu's look (menu_look.cpp): the launcher's Gondor stone, gold and elvish lines, drawn with Dear ImGui's
// shapes, so the menu over the game looks like Athelas itself. Only ImGui: overlay.cpp decides what is on it, and a
// preview program can draw it without a game (sdk/harness/menu_preview.cpp).
#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace app::look {

struct Fonts {
    ImFont* title = nullptr;  // Uncial Antiqua: "Athelas"
    ImFont* head = nullptr;   // Cinzel Decorative: headings, tabs, buttons
    ImFont* body = nullptr;   // IM Fell English: the text
};
// the three fonts, added to the atlas (once); the text font gets fallback for letters it lacks
Fonts AddFonts(ImFontAtlas* atlas, ImFont* fallback);

// the frame's measures: u is one pixel of a 1080-line screen; c0..c1 the 16:9 area in the middle (ultrawide: the sides
// are dimmed only)
struct Look {
    ImVec2 display{0, 0};
    float u = 1;
    ImVec2 c0{0, 0}, c1{0, 0};
    bool pad = false;  // a controller is in use: its buttons in the hints, else the keys
    Fonts fonts;
    float px(float v) const { return v * u; }
};
Look MakeLook(ImVec2 display, const Fonts& fonts, bool pad);

// the colours (the launcher's, app.h)
ImU32 Gold(float a = 1);
ImU32 GoldLight(float a = 1);
ImU32 Text(float a = 1);
ImU32 Dim(float a = 1);

// Dear ImGui's own widgets (sliders, lists) in the same colours, for as long as the menu draws
void PushStyle(const Look& L);
void PopStyle();

void Backdrop(ImDrawList* dl, const Look& L);
// "Athelas" and the game's name, the words on the right; returns the y under the line beneath them
float Header(ImDrawList* dl, const Look& L, const char* game, const char* right);
// a panel of dark stone with a gold edge, notched at the corners
void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, const Look& L);
// the elvish line: tapering gold ending in a scroll
void ElfRule(ImDrawList* dl, const Look& L, float x0, float x1, float y);
// a heading: a gold diamond and the words in Cinzel; returns its height
float Heading(ImDrawList* dl, const Look& L, ImVec2 p, const char* text, float size = 24, float max_w = 0);

// the tabs between a and b; the clicked one, or -1. prev / next: the buttons or keys that step through them
int Tabs(const Look& L, ImVec2 a, ImVec2 b, const std::vector<std::string>& names, int current, const char* prev,
         const char* next);
// a button: primary is the gold one (Back to the game)
bool Button(const Look& L, const char* label, ImVec2 size, bool primary);
// an on / off switch, right-aligned in size, with its word beside it
bool Toggle(const Look& L, const char* id, bool* v, ImVec2 size);

// the hints along the bottom: each a button or key picture and what it does
struct Hint {
    std::string glyph;  // "A", "B", "LB", "Back+Start", "Esc", "F10"...
    std::string label;
};
void Hints(ImDrawList* dl, const Look& L, const std::vector<Hint>& hints);

}  // namespace app::look
