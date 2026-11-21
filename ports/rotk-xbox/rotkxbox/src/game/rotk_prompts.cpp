// ROTK Xbox: button pictures in the game's on-screen prompts ("A Speed", "Y Fierce").
//
// The Xbox game shows its controller's buttons as small textures (a coloured disc with the letter, top left of a
// 64x64 texture). [graphics] button_pictures (ROTKFix.ini [Gamepad] ButtonNames): auto follows the controller on
// port 1, or always xbox, playstation or nintendo. For PlayStation and Nintendo the port draws its own pictures in
// their place (Kenney's Input Prompts, CC0: sdk/tools/mkprompts.py) through the SDK's built-in texture hook, found by the textures'
// pack hashes. A texture pack file of the same name still wins. "original" is the game's own, as xbox.
// The style is fixed the first time each picture loads.
#include <algorithm>
#include <atomic>
#include <cmath>
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
#include <cstdint>
#include <cstring>

#include "app/app.h"
#include "app/prompt_style.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/texture_pack.h"

namespace rotk {
uint32_t g_button_pictures = 0;  // 0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 keyboard

namespace {
// the game's prompt textures, by pack hash (64x64)
struct Known {
    uint64_t hash;
    const char* button;
};
const Known kKnown[] = {
    {0x0d3c98d63170de0dull, "A"},      // green disc
    {0x1893c32b5190d3fbull, "B"},      // red disc
    {0x2accbbd50631e250ull, "X"},      // blue disc
    {0x8fc49efe4a49539aull, "Y"},      // yellow disc
    {0x3ada26e2ce4fdef3ull, "WHITE"},  // white disc
    {0x5f36f0391f521310ull, "BLACK"},  // black disc
    {0x495a8fcbed4702f9ull, "LT"},     // grey disc with L
};

// the key a button's job is on, as a key cap (app/prompt_style.cpp, shared by the ports)
bool KeyCap(const char* button, gpu::pack::Image& out) { return app::KeyCapPicture(button, 256, 256 * 43 / 64, 0.07f, out); }

bool Builtin(uint64_t hash, uint32_t width, uint32_t height, gpu::pack::Image& out) {
    if (width != 64 || height != 64) return false;
    const char* button = nullptr;
    for (const Known& k : kKnown)
        if (k.hash == hash) button = k.button;
    if (!button) return false;
    // auto: the controller on port 1; the keyboard's keys when it plays there and no controller is plugged in
    uint32_t style = app::PromptStyle(g_button_pictures);
    if (style == 4) {
        if (!KeyCap(button, out)) return false;
        static std::atomic<uint32_t> logged{0};
        if (!logged++) LOGR("rotk: keyboard key pictures in the hints");
        return true;
    }
    const char* name = app::PromptStyleName(style);  // playstation, nintendo, steamdeck, steamcontroller
    if (!name) return false;
    const app::PromptPicture* p = app::FindPromptPicture(name, button);
    gpu::pack::Image pic;
    if (!p || !gpu::pack::DecodePng(p->png, p->size, pic) || pic.levels.empty()) return false;
    // where the game's disc is: the top left of its 64x64 texture, 43 across; given at four times that
    app::PlacePicture(pic, 0, 0, 43.0f / 64.0f, 256, out);
    LOG_FIRST_R(12, "rotk: %s button picture for %s", name, button);
    return true;
}
}  // namespace

// after the settings load
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace rotk
