// The Two Towers: [graphics] button_pictures, the controller buttons the game's hints, menus and pause screens show,
// as The Return of the King's port has it (same name, same choices).
//
// The game shows the Xbox controller's buttons as coloured discs with the letter, in two sets of textures: 64x64 in
// the front end and the Prologue's lessons (the disc 51 pixels across, at 5, 7), and 32x32 on the pause screens and
// in levels (the disc filling the texture). auto follows the controller on port 1 (the keyboard's keys when the
// keyboard plays there and no controller is plugged in), or always xbox, playstation, nintendo or keyboard. For
// PlayStation, Nintendo, Steam Deck and Steam Controller the port shows Kenney's Input Prompts in their place (CC0;
// the ports' shared pictures, sdk/tools/mkprompts.py), for the keyboard a key cap with the key's name; through the SDK's built-in texture hook, found by
// the textures' pack hashes. A texture pack file of the same name still wins. The style is fixed the first time each
// picture loads.
//
// Seen so far (texture dumps of every level, the front end and the pause screens): A, B, X and Y at 32x32, and A, X
// and Y at 64x64; a picture not in the list keeps the game's own.
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

namespace tt {
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
    {0xde68b21786d495c1ull, 32, "A", 0, 0, 1},  // green disc
    {0xce41150702d5dd62ull, 32, "B", 0, 0, 1},  // red
    {0xa1bf0dc4c267bbe0ull, 32, "X", 0, 0, 1},  // blue
    {0x548332f9b3df50b1ull, 64, "A", 5 / 64.0f, 7 / 64.0f, 51 / 64.0f},
    {0x73a802aade5d5690ull, 64, "X", 5 / 64.0f, 7 / 64.0f, 51 / 64.0f},
    {0x1ff51bff42dd7ff7ull, 64, "Y", 5 / 64.0f, 7 / 64.0f, 51 / 64.0f},
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
        if (!logged++) LOGR("tt: keyboard key pictures in the hints");
    } else {
        const char* name = app::PromptStyleName(style);  // playstation, nintendo, steamdeck, steamcontroller
        if (!name) return false;
        const app::PromptPicture* pic = app::FindPromptPicture(name, k->button);
        if (!pic || !gpu::pack::DecodePng(pic->png, pic->size, full) || full.levels.empty()) return false;
        static std::atomic<uint32_t> logged{0};
        uint32_t bit = 1u << (k - kKnown);
        if (!(logged.fetch_or(bit) & bit)) LOGR("tt: %s button picture for %s (%ux%u)", name, k->button, width, height);
    }
    Place(full, *k, out);
    return true;
}
}  // namespace

// after the settings load (tt::Init)
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace tt
