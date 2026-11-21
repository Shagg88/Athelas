// XAPI functions that talk to hardware: controller ports and the hard disk cache partition
#include <chrono>
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "core/input.h"
#include "gpu/backend.h"
#include "kernel/lockstep.h"
#include "xdk/netplay.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;

namespace {
// XBE_PAD_SCRIPT=<file>: test runs press the first controller's buttons from a script instead of the keyboard, one
// press a line: "<seconds from start> <button> [held seconds]", buttons A B X Y BLACK WHITE LT RT START BACK UP DOWN
// LEFT RIGHT, SHOT (a screenshot), and the left stick pushed fully: LX+ LX- LY+ LY- (held 0.15 s unless given). The first controller then
// counts as connected.
struct ScriptPress {
    double at, until;
    int analog;       // 0..7, or -1
    uint16_t digital;
    int lx = 0, ly = 0;  // -1, 0 or 1: the left stick
    bool shot = false;   // "SHOT": a screenshot at that moment (test runs)
    mutable bool done = false;
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
            if (b == "LX+") p.lx = 1;
            if (b == "LX-") p.lx = -1;
            if (b == "LY+") p.ly = 1;
            if (b == "LY-") p.ly = -1;
            if (b == "SHOT") p.shot = true;
            if (p.analog >= 0 || p.digital || p.lx || p.ly || p.shot) out.push_back(p);
        }
        LOGR("xapi: controller script %s, %zu presses", f, out.size());
        return out;
    }();
    return s;
}
void ApplyPadScript(int port, input::Pad& p) {
    if (port != 0 || PadScript().empty()) return;
    static const auto t0 = std::chrono::steady_clock::now();
    static const double per_step = [] {
        const char* e = std::getenv("XBE_PAD_STEPS");
        double n = e ? std::atof(e) : 0;
        if (n > 0) LOGR("test: pad script timed in game steps, %.0f a second (XBE_PAD_STEPS)", n);
        return n > 0 ? 1.0 / n : 0.0;
    }();
    double t = per_step > 0 ? double(app::g_game_steps.load()) * per_step
                            : std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    p.connected = true;
    for (const ScriptPress& sp : PadScript()) {
        if (sp.shot) {
            if (t >= sp.at && !sp.done) { sp.done = true; gpu::g_screenshot = true; }
            continue;
        }
        if (t < sp.at || t >= sp.until) continue;
        if (sp.analog >= 0) p.analog[sp.analog] = 255;
        p.buttons |= sp.digital;
        if (sp.lx) p.lx = sp.lx * 32767;
        if (sp.ly) p.ly = sp.ly * 32767;
    }
}


// Replays for bug reports. XBE_PAD_RECORD=<file> writes the first controller as the game reads it; XBE_PAD_REPLAY=<file>
// plays it back instead of the controller. Both run the game in lockstep (kernel/lockstep.h), so with the same build,
// settings and saves the game does the same thing again, and the bug happens again on another PC. Calls are counted
// (the game's reads of controller 1, the same on every run in lockstep), and every 60th read the record holds a hash of
// the game's memory: a replay that goes its own way says where, which tells a missed difference (saves, settings,
// build) from a bug. Text file:
//   athelas-replay 1 <build hash>
//   <read> P <buttons> <8 analog> <lx> <ly> <rx> <ry>   the pad from this read on (only when it changes)
//   <read> H <hash>                                     the memory hash at this read
struct Replay {
    FILE* rec = nullptr;
    std::map<uint64_t, input::Pad> pads;  // replay: from which read on
    std::map<uint64_t, uint64_t> hashes;  // replay: expected
    bool playing = false, diverged = false;
    uint64_t reads = 0, hash = 1469598103934665603ull;
    input::Pad last{};
    bool have_last = false;
};
Replay& Rp() {
    static Replay r = [] {
        Replay r;
        if ((std::getenv("XBE_PAD_REPLAY") || std::getenv("XBE_PAD_RECORD")) && !kernel::ls::kSafepoints) {
            LOGR("replay: this game isn't built for lockstep (only The Return of the King's is, for online co-op), so "
                 "it can't record or replay the same way twice; ignored");
            return r;
        }
        if (const char* f = std::getenv("XBE_PAD_REPLAY")) {
            FILE* in = std::fopen(f, "r");
            char line[256];
            unsigned ver = 0;
            uint64_t build = 0;
            if (!in || !std::fgets(line, sizeof line, in) || std::sscanf(line, "athelas-replay %u %" SCNx64, &ver, &build) != 2 || ver != 1) {
                LOGR("replay: %s can't be read, playing without it", f);
                if (in) std::fclose(in);
                return r;
            }
            if (build != netplay::BuildHash())
                LOGR("replay: recorded with another build (%016" PRIx64 ", this is %016" PRIx64 "); it may go its own way", build, netplay::BuildHash());
            while (std::fgets(line, sizeof line, in)) {
                uint64_t at = 0, h = 0;
                char kind = 0;
                unsigned b = 0, an[8] = {};
                int lx = 0, ly = 0, rx = 0, ry = 0;
                if (std::sscanf(line, "%" SCNu64 " H %" SCNx64, &at, &h) == 2 && std::strstr(line, " H ")) {
                    r.hashes[at] = h;
                } else if (std::sscanf(line, "%" SCNu64 " %c %x %u %u %u %u %u %u %u %u %d %d %d %d", &at, &kind, &b, &an[0], &an[1],
                                       &an[2], &an[3], &an[4], &an[5], &an[6], &an[7], &lx, &ly, &rx, &ry) == 15 && kind == 'P') {
                    input::Pad p{};
                    p.connected = true;
                    p.buttons = uint16_t(b);
                    for (int i = 0; i < 8; i++) p.analog[i] = uint8_t(an[i]);
                    p.lx = int16_t(lx), p.ly = int16_t(ly), p.rx = int16_t(rx), p.ry = int16_t(ry);
                    r.pads[at] = p;
                }
            }
            std::fclose(in);
            r.playing = true;
            LOGR("replay: playing %s, %zu pad changes, %zu checks", f, r.pads.size(), r.hashes.size());
        } else if (const char* f = std::getenv("XBE_PAD_RECORD")) {
            r.rec = std::fopen(f, "w");
            if (r.rec) {
                std::fprintf(r.rec, "athelas-replay 1 %016" PRIx64 "\n", netplay::BuildHash());
                LOGR("replay: recording the controller to %s", f);
            } else {
                LOGR("replay: can't write %s, not recording", f);
            }
        }
        return r;
    }();
    return r;
}
// the first controller's read: recorded, or replaced by the replay's; then the memory check
void ReplayPad(input::Pad& p) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    Replay& r = Rp();
    if (!r.rec && !r.playing) return;
    const uint64_t n = r.reads++;
    if (r.playing) {
        auto it = r.pads.upper_bound(n);
        p = it == r.pads.begin() ? input::Pad{} : std::prev(it)->second;
        if (it == r.pads.end() && !r.pads.empty() && n == r.pads.rbegin()->first + 1) LOGR("replay: last recorded press reached (read %" PRIu64 ")", n);
    } else {
        p.connected = true;  // recording: the first port counts as plugged in (UpdatePorts), so the replay's always is
        auto same = [](const input::Pad& x, const input::Pad& y) {
            return x.connected == y.connected && x.buttons == y.buttons && !std::memcmp(x.analog, y.analog, 8) && x.lx == y.lx &&
                   x.ly == y.ly && x.rx == y.rx && x.ry == y.ry;
        };
        if (!r.have_last || !same(p, r.last)) {
            const input::Pad& q = p;
            std::fprintf(r.rec, "%" PRIu64 " P %x %u %u %u %u %u %u %u %u %d %d %d %d\n", n, q.buttons, q.analog[0], q.analog[1],
                         q.analog[2], q.analog[3], q.analog[4], q.analog[5], q.analog[6], q.analog[7], q.lx, q.ly, q.rx, q.ry);
            r.last = p, r.have_last = true;
        }
    }
    // a slice of memory a read, the whole every 60 reads (as co-op does once a second)
    r.hash = netplay::MemoryHashSlice(uint32_t(n % 60), r.hash);
    if (n % 60 == 59) {
        if (r.rec) {
            std::fprintf(r.rec, "%" PRIu64 " H %016" PRIx64 "\n", n, r.hash);
            std::fflush(r.rec);
        } else if (!r.diverged) {
            auto it = r.hashes.find(n);
            if (it != r.hashes.end() && it->second != r.hash) {
                r.diverged = true;
                LOGR("replay: the game went its own way by read %" PRIu64 " (about %.0f s in): not the same build, settings or saves", n, n / 60.0);
            }
        }
        r.hash = 1469598103934665603ull;
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
        if (input::Connected(p) || (p == 0 && (!PadScript().empty() || Rp().playing || Rp().rec))) cur |= 1u << p;
    if (netplay::Active()) cur = 3;  // online co-op: both players' ports, whatever is plugged in here
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

namespace netplay {
// this PC's first controller as its player's input (a test script presses it in test runs)
input::Pad LocalSample() {
    input::Pad p = input::Poll(0);
    ApplyPadScript(0, p);
    // this PC's second port too (the keyboard set to player 2, a second controller): whichever is used, it is this PC's
    // player; buttons together, the stronger of the sticks and triggers
    input::Pad q = input::Poll(1);
    if (q.connected) {
        p.connected = true;
        p.buttons |= q.buttons;
        for (int i = 0; i < 8; i++) p.analog[i] = std::max(p.analog[i], q.analog[i]);
        auto stronger = [](int16_t a, int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
        p.lx = stronger(p.lx, q.lx), p.ly = stronger(p.ly, q.ly), p.rx = stronger(p.rx, q.rx), p.ry = stronger(p.ry, q.ry);
    }
    return p;
}
}  // namespace netplay

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
    if (!g_poller && !kernel::ls::g_on) {  // lockstep: what is plugged in stays as it was at the start
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
    input::Pad p;
    if (netplay::Active()) {
        p = netplay::Pad(int(port));  // online co-op: the frame's input of both players, the same on both PCs
    } else {
        p = input::Poll(int(port));
        ApplyPadScript(int(port), p);
        if (port == 0) ReplayPad(p);
    }
    if (!p.connected) return 1167;  // ERROR_DEVICE_NOT_CONNECTED
    std::lock_guard<std::mutex> lock(g_ports_lock);
    Handle& h = g_handles[a[0]];
    // field by field: a memcmp would also compare the padding byte after 'connected', whose contents are whatever the
    // stack held (a packet counted that never was; in online co-op the two PCs counted differently)
    auto same = [](const input::Pad& x, const input::Pad& y) {
        return x.connected == y.connected && x.buttons == y.buttons && !std::memcmp(x.analog, y.analog, 8) &&
               x.lx == y.lx && x.ly == y.ly && x.rx == y.rx && x.ry == y.ry;
    };
    if (!same(p, h.last)) h.packet++;
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
    // online co-op: only this PC's player feels their rumble, on this PC's first controller
    if (netplay::Active()) port = int(port) == netplay::LocalPort() ? 0 : 99;
    // XINPUT_FEEDBACK_HEADER is dwStatus, hEvent and 58 reserved bytes, the rumble follows
    if (port < 4) input::Rumble(int(port), R16(fb + 66), R16(fb + 68));
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
