// Testing only, in the Linux test build (never in the Windows exe): ROTK_WATCH=<hex address> logs a character's
// position (+0x30) after each pass of the character list (0x2EAF0, once a time step), with its move that step, so
// walks at different time steps (ROTK_DT) can be compared (notes 09). In Path of the Wizard the player is at
// 13D08A0 (type 30, moved by 0x5E310 -> 0x255D0 -> state 0x4E6C0 -> 0x59210 -> 0x5F340 -> 0x5EBD0).
#ifndef _WIN32
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
uint32_t WatchObj() {
    static const uint32_t w = [] {
        const char* e = std::getenv("ROTK_WATCH");
        return e ? uint32_t(std::strtoul(e, nullptr, 16)) : 0u;
    }();
    return w;
}
void Pos(uint32_t o, float* p) {
    for (int i = 0; i < 3; i++) {
        uint32_t u = ::kernel::R32(o + 0x30 + i * 4);
        std::memcpy(&p[i], &u, 4);
    }
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_0002EAF0);
X86_FUNC(sub_0002EAF0) {
    uint32_t w = WatchObj();
    float b[3] = {}, a[3] = {};
    if (w) Pos(w, b);
    __imp__sub_0002EAF0(ctx, base);
    if (!w) return;
    Pos(w, a);
    static uint32_t n = 0;
    LOGR("watch frame %u pos %.3f %.3f %.3f d %.4f %.4f %.4f", n++, a[0], a[1], a[2], a[0] - b[0], a[1] - b[1],
         a[2] - b[2]);
}
namespace {
float Fl(uint32_t a) {
    uint32_t u = kernel::R32(a);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
}  // namespace
// ROTK_AUDIO_TRACE: each sound command (0x7FD40: the character's sounds, command, channel, sound and two more).
// "audio: play" events (0x7C5B0) are command 1; a hero's channel 2 is his voice, 0 footsteps, 3 swings
X86_EXTERN_FUNC(__imp__sub_0007FD40);
X86_FUNC(sub_0007FD40) {
    static const bool on = std::getenv("ROTK_AUDIO_TRACE") != nullptr;
    if (on)
        LOGR("audio cmd p%u c%u s%d v %g %g %g", kernel::R32(ctx.esp + 4) & 0xFF, kernel::R32(ctx.esp + 8),
             int32_t(kernel::R32(ctx.esp + 12)), Fl(ctx.esp + 16), Fl(ctx.esp + 20), Fl(ctx.esp + 24));
    __imp__sub_0007FD40(ctx, base);
}
#endif
