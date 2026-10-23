// FOTR Xbox: [input] invert_camera_y / invert_camera_x, the controller's right stick reversed for the camera (#14).
// The right stick moves the game's camera around Frodo: up raises it to look down on him, right swings it round
// (measured in Bree with the harness's RSU / RSR). The game has no invert option of its own. Applied in
// GameInfo::pad, before the keyboard and mouse are merged in (input::Poll), so it is the controller only.
#include <cstdint>

#include "core/input.h"

namespace fotr {
bool g_invert_camera_y = false;  // [input] invert_camera_y
bool g_invert_camera_x = false;  // [input] invert_camera_x

// not plain negation: -32768 has no positive
static int16_t Flip(int16_t v) { return int16_t(-1 - v); }

void Pad(int, input::Pad& p) {
    if (g_invert_camera_y) p.ry = Flip(p.ry);
    if (g_invert_camera_x) p.rx = Flip(p.rx);
}
}  // namespace fotr
