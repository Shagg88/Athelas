#pragma once
// Keyboard and mouse play: what each key does, named as the settings file and a launcher write them. No platform
// code, so the lists can be checked anywhere; app/keyboard.cpp reads the keys on Windows.
#include <atomic>
#include <cstdint>
#include <string>

namespace keys {

// the things a key can do: move the left stick (and walk, half way), or press a button of the console controller
enum Action : uint32_t {
    kMoveUp, kMoveDown, kMoveLeft, kMoveRight, kWalk,
    kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger,
    kStart, kBack, kLeftStick, kRightStick,
    kDpadUp, kDpadDown, kDpadLeft, kDpadRight,
    kCount
};

// [keyboard] keys, in Action's order
constexpr const char* kSettingKeys[kCount] = {
    "move_up", "move_down", "move_left", "move_right", "walk",
    "a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
    "start", "back", "left_stick", "right_stick",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right"};

// a launcher's labels when the game does not name the button's job (GameInfo::button_actions names A to RT)
constexpr const char* kLabels[kCount] = {
    "Move forward", "Move back", "Move left", "Move right", "Walk (hold)",
    "Xbox A", "Xbox B", "Xbox X", "Xbox Y", "Xbox Black", "Xbox White", "Xbox left trigger", "Xbox right trigger",
    "Start (pause)", "Back", "Left stick click", "Right stick click",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right"};

// as ROTK on PC sets its keyboard (WASD, mouse buttons, Space, Q, E, Ctrl, Shift, Tab, Esc); one key or several,
// comma separated. GameInfo::key_defaults changes some for a game.
constexpr const char* kDefaults[kCount] = {
    "W", "S", "A", "D", "LAlt",
    "Mouse1, Enter", "LCtrl, Backspace", "Q", "Mouse2", "E", "Tab", "LShift", "Space, Mouse3",
    "Esc", "Grave", "F5", "",
    "Up", "Down", "Left", "Right"};

// the key names the settings take, and the Windows virtual-key code of each. The ports' own hotkeys ([ ] \ F10)
// are left out.
struct Key {
    const char* name;
    uint8_t vk;
};
constexpr Key kKeys[] = {
    {"A", 'A'}, {"B", 'B'}, {"C", 'C'}, {"D", 'D'}, {"E", 'E'}, {"F", 'F'}, {"G", 'G'}, {"H", 'H'}, {"I", 'I'},
    {"J", 'J'}, {"K", 'K'}, {"L", 'L'}, {"M", 'M'}, {"N", 'N'}, {"O", 'O'}, {"P", 'P'}, {"Q", 'Q'}, {"R", 'R'},
    {"S", 'S'}, {"T", 'T'}, {"U", 'U'}, {"V", 'V'}, {"W", 'W'}, {"X", 'X'}, {"Y", 'Y'}, {"Z", 'Z'},
    {"0", '0'}, {"1", '1'}, {"2", '2'}, {"3", '3'}, {"4", '4'}, {"5", '5'}, {"6", '6'}, {"7", '7'}, {"8", '8'}, {"9", '9'},
    {"F1", 0x70}, {"F2", 0x71}, {"F4", 0x73}, {"F5", 0x74}, {"F6", 0x75}, {"F7", 0x76}, {"F8", 0x77}, {"F9", 0x78},
    {"F11", 0x7A}, {"F12", 0x7B},
    {"Space", 0x20}, {"Enter", 0x0D}, {"Esc", 0x1B}, {"Tab", 0x09}, {"Backspace", 0x08}, {"CapsLock", 0x14},
    {"LShift", 0xA0}, {"RShift", 0xA1}, {"LCtrl", 0xA2}, {"RCtrl", 0xA3}, {"LAlt", 0xA4}, {"RAlt", 0xA5},
    {"Up", 0x26}, {"Down", 0x28}, {"Left", 0x25}, {"Right", 0x27},
    {"Insert", 0x2D}, {"Delete", 0x2E}, {"Home", 0x24}, {"End", 0x23}, {"PageUp", 0x21}, {"PageDown", 0x22},
    {"Numpad0", 0x60}, {"Numpad1", 0x61}, {"Numpad2", 0x62}, {"Numpad3", 0x63}, {"Numpad4", 0x64},
    {"Numpad5", 0x65}, {"Numpad6", 0x66}, {"Numpad7", 0x67}, {"Numpad8", 0x68}, {"Numpad9", 0x69},
    {"NumpadStar", 0x6A}, {"NumpadPlus", 0x6B}, {"NumpadMinus", 0x6D}, {"NumpadDot", 0x6E}, {"NumpadSlash", 0x6F},
    {"Minus", 0xBD}, {"Equals", 0xBB}, {"Comma", 0xBC}, {"Period", 0xBE}, {"Slash", 0xBF}, {"Semicolon", 0xBA},
    {"Apostrophe", 0xDE}, {"Grave", 0xC0},
    {"Mouse1", 0x01}, {"Mouse2", 0x02}, {"Mouse3", 0x04}, {"Mouse4", 0x05}, {"Mouse5", 0x06},
};
constexpr uint32_t kKeyCount = uint32_t(sizeof kKeys / sizeof kKeys[0]);

// a short name for a key in the game's button prompts and the overlay ("LMB", "Ctrl")
inline const char* ShortName(const char* name) {
    struct S {
        const char *from, *to;
    };
    static const S k[] = {{"Mouse1", "LMB"},     {"Mouse2", "RMB"},     {"Mouse3", "MMB"},    {"Mouse4", "M4"},
                          {"Mouse5", "M5"},      {"LCtrl", "Ctrl"},     {"RCtrl", "Ctrl"},    {"LShift", "Shift"},
                          {"RShift", "Shift"},   {"LAlt", "Alt"},       {"RAlt", "Alt"},      {"Backspace", "Bksp"},
                          {"CapsLock", "Caps"},  {"Grave", "`"},        {"Minus", "-"},       {"Equals", "="},
                          {"Comma", ","},        {"Period", "."},       {"Slash", "/"},       {"Semicolon", ";"},
                          {"Apostrophe", "'"},   {"PageUp", "PgUp"},    {"PageDown", "PgDn"}, {"Insert", "Ins"},
                          {"Delete", "Del"},     {"Numpad0", "Num0"},   {"Numpad1", "Num1"},  {"Numpad2", "Num2"},
                          {"Numpad3", "Num3"},   {"Numpad4", "Num4"},   {"Numpad5", "Num5"},  {"Numpad6", "Num6"},
                          {"Numpad7", "Num7"},   {"Numpad8", "Num8"},   {"Numpad9", "Num9"},  {"NumpadStar", "Num*"},
                          {"NumpadPlus", "Num+"}, {"NumpadMinus", "Num-"}, {"NumpadDot", "Num."}, {"NumpadSlash", "Num/"}};
    for (const S& s : k) {
        const char *a = s.from, *b = name;
        while (*a && (*a | 0x20) == (*b | 0x20)) a++, b++;
        if (!*a && !*b) return s.to;
    }
    return name;
}

}  // namespace keys

namespace input {
struct Pad;
}

// app/keyboard.cpp (Windows)
namespace keys {
void Merge(int port, input::Pad& pad);  // input::Poll: the keys onto the keyboard player's pad
void Latch(uint32_t vk);                 // window thread: a key or mouse button went down (WM_KEYDOWN...)
bool Claims(int port);                   // the keyboard plays on this port
bool Over();                             // the game window has the focus and the pointer is over its picture
std::string FirstKey(Action a);          // the first key bound to it ("Mouse1"), empty for none
extern std::atomic<bool> g_used;         // a key has pressed something since the start
}  // namespace keys
