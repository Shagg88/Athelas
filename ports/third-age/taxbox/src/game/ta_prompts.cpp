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
#include <iterator>
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
const app::PromptTexture kKnown[] = {
    {0xaca5eacd437fa203ull, 32, "A", 0, 0, 1},  // green disc
};
const app::PromptSet kSet{"ta", kKnown, std::size(kKnown), &g_button_pictures};

// the shared hook (app/prompt_style.cpp): Kenney's pictures, or the keyboard's key caps, for the game's own
bool Builtin(uint64_t hash, uint32_t width, uint32_t height, gpu::pack::Image& out) {
    return app::PromptBuiltin(kSet, hash, width, height, out);
}
}  // namespace

// after the settings load (ta::Init)
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace ta
