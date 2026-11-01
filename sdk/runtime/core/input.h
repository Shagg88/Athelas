#pragma once
#include <cstdint>

namespace input {

// the console controller's report, analog buttons are 0-255
struct Pad {
    bool connected = false;
    uint16_t buttons = 0;  // dpad up/down/left/right, start, back, left thumb, right thumb
    uint8_t analog[8] = {};  // A B X Y black white left trigger right trigger
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

// [controls]: the controller buttons an Xbox button can be set to, in Settings::buttons' numbering
constexpr const char* kSources[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "none"};
constexpr const char* kSourceNames[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Left stick click", "Right stick click", "Nothing"};
constexpr uint32_t kSourceCount = 11;

void Init();  // window thread, before boot
// the controller on that port, then GameInfo::pad
Pad Poll(int port);
// the controller alone, its buttons as on the Xbox: no [controls] layout, game changes or keys (the in-game menu)
Pad Raw(int port);
// a controller is on the port, or the keyboard plays there (XAPI's device list)
bool Connected(int port);
void Rumble(int port, uint16_t left, uint16_t right);
// the kind of controller on that port, for button pictures: 0 none or Xbox-like, 1 PlayStation, 2 Nintendo
int Style(int port);

}  // namespace input
