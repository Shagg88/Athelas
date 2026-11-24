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
#include <iterator>
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
// the game's prompt textures, by pack hash (64x64): the disc at the top left, 43 across
constexpr float kDisc = 43.0f / 64.0f;
const app::PromptTexture kKnown[] = {
    {0x0d3c98d63170de0dull, 64, "A", 0, 0, kDisc},      // green disc
    {0x1893c32b5190d3fbull, 64, "B", 0, 0, kDisc},      // red disc
    {0x2accbbd50631e250ull, 64, "X", 0, 0, kDisc},      // blue disc
    {0x8fc49efe4a49539aull, 64, "Y", 0, 0, kDisc},      // yellow disc
    {0x3ada26e2ce4fdef3ull, 64, "WHITE", 0, 0, kDisc},  // white disc
    {0x5f36f0391f521310ull, 64, "BLACK", 0, 0, kDisc},  // black disc
    {0x495a8fcbed4702f9ull, 64, "LT", 0, 0, kDisc},     // grey disc with L
};
const app::PromptSet kSet{"rotk", kKnown, std::size(kKnown), &g_button_pictures, 0.07f, false};

// the shared hook (app/prompt_style.cpp): Kenney's pictures, or the keyboard's key caps, for the game's own
bool Builtin(uint64_t hash, uint32_t width, uint32_t height, gpu::pack::Image& out) {
    return app::PromptBuiltin(kSet, hash, width, height, out);
}
}  // namespace

// after the settings load
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace rotk
