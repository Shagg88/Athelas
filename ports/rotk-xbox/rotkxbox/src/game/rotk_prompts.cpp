// ROTK Xbox: button pictures in the game's on-screen prompts ("A Speed", "Y Fierce").
//
// The Xbox game shows its controller's buttons as small textures (a coloured disc with the letter, top left of a
// 64x64 texture). [graphics] button_pictures (ROTKFix.ini [Gamepad] ButtonNames): auto follows the controller on
// port 1, or always xbox, playstation or nintendo. For PlayStation and Nintendo the port draws its own pictures in
// their place (tools/mkprompts.py, original artwork) through the SDK's built-in texture hook, found by the textures'
// pack hashes. A texture pack file of the same name still wins. "original" is the game's own, as xbox.
// The style is fixed the first time each picture loads.
#include <atomic>
#include <cstdint>
#include <cstring>

#include "core/input.h"
#include "core/log.h"
#include "gpu/texture_pack.h"
#include "rotk_prompts_data.h"

namespace rotk {
uint32_t g_button_pictures = 0;  // 0 auto, 1 xbox, 2 playstation, 3 nintendo

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

bool Builtin(uint64_t hash, uint32_t width, uint32_t height, gpu::pack::Image& out) {
    if (width != 64 || height != 64) return false;
    const char* button = nullptr;
    for (const Known& k : kKnown)
        if (k.hash == hash) button = k.button;
    if (!button) return false;
    uint32_t style = g_button_pictures;
    if (style == 0) style = uint32_t(input::Style(0)) + 1;  // 1 xbox, 2 playstation, 3 nintendo
    const char* name = style == 2 ? "playstation" : style == 3 ? "nintendo" : nullptr;
    if (!name) return false;
    for (const PromptPicture& p : kPromptPictures) {
        if (std::strcmp(p.style, name) || std::strcmp(p.button, button)) continue;
        if (!gpu::pack::DecodePng(p.png, p.size, out)) return false;
        static std::atomic<uint32_t> logged{0};
        uint32_t bit = 1u << (&p - kPromptPictures);
        if (!(logged.fetch_or(bit) & bit)) LOGR("rotk: %s button picture for %s", name, button);
        return true;
    }
    return false;
}
}  // namespace

// after the settings load
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace rotk
