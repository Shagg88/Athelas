// The button pictures' choice and the keyboard's key caps, shared by the ports' [graphics] button_pictures
// (tt_prompts.cpp, rotk_prompts.cpp, fotr_prompts.cpp, ta_prompts.cpp: each finds its game's own button textures and
// puts these in their place). The PlayStation and Nintendo discs are in app/prompt_pictures.h.
#pragma once
#include <cstddef>
#include <cstdint>

#include "gpu/texture_pack.h"

namespace app {
// the style to show for the setting (0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 keyboard, 5 steamdeck,
// 6 steamcontroller): auto follows the controller on port 1 (on a Steam Deck, whose own controls read as an Xbox
// controller, the Deck's), or the keyboard's keys when the keyboard plays there and no controller is plugged in
uint32_t PromptStyle(uint32_t setting);
// the shared pictures' name for a style (app/prompt_pictures.h): 2 playstation, 3 nintendo, 5 steamdeck,
// 6 steamcontroller; null for the styles that have none there (xbox: the game's own; keyboard: key caps)
const char* PromptStyleName(uint32_t style);
// the key a button's job is on (A B X Y WHITE BLACK LT RT): Kenney's picture of that key or mouse button, in a
// size_px square picture, cap_px across in its top left. A key the pack has no picture of (the number pad's) is
// drawn as a key cap: a dark rounded square with a light edge and the key's short name ("Num0"), Windows' Arial
// Bold through GDI, its shadow shadow_dy of the cap lower. False off Windows, or when no key is set.
bool KeyCapPicture(const char* button, int size_px, int cap_px, float shadow_dy, gpu::pack::Image& out);
// one of the shared pictures (app/prompt_pictures.h, Kenney's Input Prompts): a PNG file's bytes
struct PromptPicture {
    const char* style;   // xbox, playstation, nintendo, steamdeck, steamcontroller, keyboard
    const char* button;  // A B X Y WHITE BLACK LT RT START BACK; keyboard: the key's name in the settings ("Q", "Mouse1")
    const uint8_t* png;
    size_t size;
};
// the picture of a button in a style, null when there is none (names compared without regard to case)
const PromptPicture* FindPromptPicture(const char* style, const char* button);
// a picture made for a full square (the disc filling it) put where a game's own disc is in its texture: the box at
// (x, y), d across, in fractions of the out_px square picture; box-filtered, transparent around it
void PlacePicture(const gpu::pack::Image& src, float x, float y, float d, uint32_t out_px, gpu::pack::Image& out);
}  // namespace app
