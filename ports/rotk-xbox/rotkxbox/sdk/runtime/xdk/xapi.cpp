// XAPI functions that talk to hardware: controller ports and the hard disk cache partition
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/input.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;

namespace {
// XBE_PAD_SCRIPT=<file>: test runs press the first controller's buttons from a script instead of the keyboard, one
// press a line: "<seconds from start> <button> [held seconds]", buttons A B X Y BLACK WHITE LT RT START BACK UP DOWN
// LEFT RIGHT (held 0.15 s unless given). The first controller then counts as connected.
struct ScriptPress {
    double at, until;
    int analog;       // 0..7, or -1
    uint16_t digital;
};
const std::vector<ScriptPress>& PadScript() {
    static std::vector<ScriptPress> s = [] {
        std::vector<ScriptPress> out;
        const char* f = std::getenv("XBE_PAD_SCRIPT");
        if (!f) return out;
        std::ifstream in(f);
        std::string line;
        const char* names[] = {"A", "B", "X", "Y", "BLACK", "WHITE", "LT", "RT"};
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            double at, held = 0.15;
            std::string b;
            if (!(ls >> at >> b)) continue;
            ls >> held;
            ScriptPress p{at, at + held, -1, 0};
            for (int i = 0; i < 8; i++)
                if (b == names[i]) p.analog = i;
            if (b == "UP") p.digital = 1;
            if (b == "DOWN") p.digital = 2;
            if (b == "LEFT") p.digital = 4;
            if (b == "RIGHT") p.digital = 8;
            if (b == "START") p.digital = 0x10;
            if (b == "BACK") p.digital = 0x20;
            if (p.analog >= 0 || p.digital) out.push_back(p);
        }
        LOGR("xapi: controller script %s, %zu presses", f, out.size());
        return out;
    }();
    return s;
}
void ApplyPadScript(int port, input::Pad& p) {
    if (port != 0 || PadScript().empty()) return;
    static const auto t0 = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    p.connected = true;
    for (const ScriptPress& sp : PadScript()) {
        if (t < sp.at || t >= sp.until) continue;
        if (sp.analog >= 0) p.analog[sp.analog] = 255;
        p.buttons |= sp.digital;
    }
}


// XPP device type records: CurrentConnected, ChangeConnected, PreviousConnected
constexpr uint32_t kGamepadType = SYM(XPP_GamepadType);
constexpr uint32_t kMemoryUnitType = SYM(XPP_MemoryUnitType);

std::mutex g_ports_lock;
bool g_poller = false;

void UpdatePorts() {
    std::lock_guard<std::mutex> lock(g_ports_lock);
    uint32_t cur = 0;
    for (int p = 0; p < 4; p++)
        if (input::Connected(p) || (p == 0 && !PadScript().empty())) cur |= 1u << p;
    uint32_t old = R32(kGamepadType + XDK_XPP_Current);
    if (cur != old) {
        W32(kGamepadType + XDK_XPP_Changed, R32(kGamepadType + XDK_XPP_Changed) | (cur ^ old));
        W32(kGamepadType + XDK_XPP_Current, cur);
        LOGI("xapi: controller ports now %X", cur);
    }
}

struct Handle {
    uint32_t port;
    uint32_t packet;
    input::Pad last;
};
std::unordered_map<uint32_t, Handle> g_handles;

}  // namespace

HLE_STDCALL(XMountUtilityDrive, 1) {
    CreateSymbolicLink("\\??\\Z:", "\\Device\\Harddisk0\\Partition5");
    return 1;
}

HLE_STDCALL(XInitDevices, 2) {
    XDK_REQUIRE("xapi", XDK_KEY(XPP_Current), XDK_KEY(XPP_Changed), XDK_KEY(XPP_Previous), XDK_DATA(XPP_GamepadType));
    W32(kGamepadType + XDK_XPP_Current, 0);
    W32(kGamepadType + XDK_XPP_Changed, 0);
    W32(kGamepadType + XDK_XPP_Previous, 0);
    if constexpr (HAS_XPP_MemoryUnitType != 0) {  // titles without memory unit support have no record
        W32(kMemoryUnitType + XDK_XPP_Current, 0);
        W32(kMemoryUnitType + XDK_XPP_Changed, 0);
        W32(kMemoryUnitType + XDK_XPP_Previous, 0);
    }
    UpdatePorts();
    if (!g_poller) {
        g_poller = true;
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                UpdatePorts();
            }
        }).detach();
    }
    return 0;
}

HLE_STDCALL(XInputOpen, 4) {
    uint32_t type = a[0], port = a[1];
    LOG_FIRST(8, "xapi: XInputOpen type %08X port %u", type, port);
    if (type != kGamepadType || port > 3) return 0;
    uint32_t h = mem::AllocSystem(16);
    std::lock_guard<std::mutex> lock(g_ports_lock);
    g_handles[h] = {port, 0, {}};
    return h;
}

HLE_STDCALL(XInputClose, 1) {
    std::lock_guard<std::mutex> lock(g_ports_lock);
    g_handles.erase(a[0]);
    mem::FreeSystem(a[0]);
    return 0;
}

HLE_STDCALL(XInputGetState, 2) {
    uint32_t port;
    {
        std::lock_guard<std::mutex> lock(g_ports_lock);
        auto it = g_handles.find(a[0]);
        if (it == g_handles.end()) return 6;  // ERROR_INVALID_HANDLE
        port = it->second.port;
    }
    input::Pad p = input::Poll(int(port));
    ApplyPadScript(int(port), p);
    if (!p.connected) return 1167;  // ERROR_DEVICE_NOT_CONNECTED
    std::lock_guard<std::mutex> lock(g_ports_lock);
    Handle& h = g_handles[a[0]];
    if (std::memcmp(&p, &h.last, sizeof(p))) h.packet++;
    h.last = p;
    uint32_t s = a[1];
    W32(s, h.packet);  // XINPUT_STATE: packet, buttons, 8 analog buttons, thumbs
    W16(s + 4, p.buttons);
    std::memcpy(HostPtr(s + 6), p.analog, 8);
    W16(s + 14, uint16_t(p.lx));
    W16(s + 16, uint16_t(p.ly));
    W16(s + 18, uint16_t(p.rx));
    W16(s + 20, uint16_t(p.ry));
    return 0;
}

HLE_STDCALL(XInputSetState, 2) {
    uint32_t port;
    {
        std::lock_guard<std::mutex> lock(g_ports_lock);
        auto it = g_handles.find(a[0]);
        if (it == g_handles.end()) return 6;
        port = it->second.port;
    }
    uint32_t fb = a[1];
    // XINPUT_FEEDBACK_HEADER is dwStatus, hEvent and 58 reserved bytes, the rumble follows
    input::Rumble(int(port), R16(fb + 66), R16(fb + 68));
    W32(fb, 0);
    if (uint32_t ev = R32(fb + 4))
        if (ObjectRef o = FromHandle(ev)) SetEvent(*o);
    return 997;  // ERROR_IO_PENDING, already complete
}

// A recompiled image launches through the XAPI, then a process restart into its executable; any other
// (the dashboard, updates) is refused and the title stays in its menu.
X86_EXTERN_FUNC(GUEST_ORIG(XLaunchNewImageA));
X86_FUNC(GUEST(XLaunchNewImageA)) {
    std::string path = ReadCString(R32(ctx.esp + 4));
    if (ImageExe(path)) {
        GUEST_ORIG(XLaunchNewImageA)(ctx, base);
        return;
    }
    LOGW("xapi: launch of %s refused, it is not recompiled", path.c_str());
    Return(ctx, 2, 2);  // ERROR_FILE_NOT_FOUND
}
