// The Two Towers: XInputGetCapabilities, which the SDK does not replace. The game asks right after XInputOpen
// (0xE7D89, 0xE7E48) and keeps the answer at 0x6B2882; the recompiled XAPI version reads USB device state the
// host does not have and answers ERROR_DEVICE_NOT_CONNECTED.
#include <cstring>

#include "kernel/kernel.h"
#include "xdk/hle.h"

// (handle, XINPUT_CAPABILITIES*): SubType at 0, In (XINPUT_GAMEPAD, 18 bytes) at 4, Out (XINPUT_RUMBLE) at 22
HLE_STDCALL(XInputGetCapabilities, 2) {
    if (!a[0] || !a[1]) return 6;  // ERROR_INVALID_HANDLE
    uint8_t* c = static_cast<uint8_t*>(::kernel::HostPtr(a[1]));
    std::memset(c, 0, 26);
    c[0] = 1;                  // XINPUT_DEVSUBTYPE_GC_GAMEPAD
    std::memset(c + 4, 0xFF, 18);  // every button, analog button and thumb axis reported
    std::memset(c + 22, 0xFF, 4);  // both rumble motors
    return 0;
}
