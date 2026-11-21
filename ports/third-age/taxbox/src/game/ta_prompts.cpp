// The Third Age: [graphics] button_pictures, the controller buttons the game's menus and battle commands show, as
// the other three ports have it (same name, same choices). auto follows the controller on port 1 (the keyboard's keys
// when the keyboard plays there and no controller is plugged in), or always xbox, playstation, nintendo or keyboard.
// Kenney's Input Prompts (CC0; the ports' shared pictures, sdk/tools/mkprompts.py) through the SDK's
// built-in texture hook, found by the textures' pack hashes. A texture pack file of the same name still wins.
//
// Seen so far (the texture dump of the front end and the first battles): A at 32x32, the disc filling the texture.
// B, X and Y are added as dumps show them; a picture not in the list keeps the game's own.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // std::min and std::max, not Windows' macros
#endif
#include <windows.h>
#pragma comment(lib, "gdi32")  // the key caps' text
#endif

#include "app/app.h"
#include "app/prompt_style.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/texture_pack.h"

namespace ta {
uint32_t g_button_pictures = 0;  // [graphics] button_pictures: 0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 keyboard

namespace {
// the game's button textures, by pack hash, and where their disc is (a share of the texture: left, top, across)
struct Known {
    uint64_t hash;
    uint32_t size;
    const char* button;
    float x, y, d;
};
const Known kKnown[] = {
    {0xaca5eacd437fa203ull, 32, "A", 0, 0, 1},  // green disc
};
constexpr uint32_t kOut = 256;  // the pictures given: 256x256 (4x or 8x the game's)

// a picture made for a full square put where the game's disc is (app/prompt_style.cpp, shared by the ports)
void Place(const gpu::pack::Image& src, const Known& k, gpu::pack::Image& out) { app::PlacePicture(src, k.x, k.y, k.d, kOut, out); }

// the key a button's job is on, as a key cap (app/prompt_style.cpp, shared by the ports)
bool KeyCap(const char* button, gpu::pack::Image& out) { return app::KeyCapPicture(button, int(kOut), int(kOut), 0.05f, out); }

bool Builtin(uint64_t hash, uint32_t width, uint32_t height, gpu::pack::Image& out) {
    const Known* k = nullptr;
    for (const Known& c : kKnown)
        if (c.hash == hash && c.size == width && c.size == height) k = &c;
    if (!k) return false;
    // auto: the controller on port 1; the keyboard's keys when it plays there and no controller is plugged in
    uint32_t style = app::PromptStyle(g_button_pictures);
    gpu::pack::Image full;
    if (style == 4) {
        if (!KeyCap(k->button, full)) return false;
        static std::atomic<uint32_t> logged{0};
        if (!logged++) LOGR("ta: keyboard key pictures in the hints");
    } else {
        const char* name = app::PromptStyleName(style);  // playstation, nintendo, steamdeck, steamcontroller
        if (!name) return false;
        const app::PromptPicture* pic = app::FindPromptPicture(name, k->button);
        if (!pic || !gpu::pack::DecodePng(pic->png, pic->size, full) || full.levels.empty()) return false;
        static std::atomic<uint32_t> logged{0};
        uint32_t bit = 1u << (k - kKnown);
        if (!(logged.fetch_or(bit) & bit)) LOGR("ta: %s button picture for %s (%ux%u)", name, k->button, width, height);
    }
    Place(full, *k, out);
    return true;
}
}  // namespace

// after the settings load (ta::Init)
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace ta
