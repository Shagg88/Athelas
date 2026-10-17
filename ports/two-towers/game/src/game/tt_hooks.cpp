#include <cstdio>
// The Two Towers: instruction hooks ([[hook]] entries in config/hooks.toml).
#include <chrono>
#include <cstdint>

#include "cpu/x86_context.h"

// XACT's clock 0x137B19 reads the MCPX APU sample counter (0xFE80200C, 48 kHz) at 0x137B30 and turns the
// difference since its last read into 100 ns units. The host gives it the 48 kHz samples since start and the
// hardware read is skipped (on_true jumps to 0x137B35).
bool TT_ApuSampleCounter(X86Context& ctx, uint8_t* base) {
    (void)base;
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    uint64_t us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start).count());
    ctx.eax = uint32_t(us * 48000 / 1000000);
    return true;
}

// XapiInitProcess 0xFE45D: the Xbox drive authentication query (0xFE36E) fails on PC, which on the console
// ends in a reboot to the dashboard (0xFE4C4 -> 0xFE309). Startup continues at 0xFE4C9 instead, so XAPI maps
// T:/U:, mounts Z: and finishes its own initialisation.
#include "core/log.h"
bool TT_DiscCheckFailed(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    LOGI("tt: no Xbox drive, continuing XAPI startup");
    return true;
}

// The game's frame profiler (notes/09-startup-crash.md): two 0x158-byte slots at 0x6B18B0, each with its current
// section (0 to 19) at +0x10. Section start 0xE60B0 and section end 0xE6545 index the slot's section table with it
// unchecked. Rarely, at startup, the field held a stray value and the read crashed the game. The profiler only
// measures time, so a section out of range is taken as section 0 before it is used; nothing else changes.
#include <atomic>
static void ProfilerSection(uint8_t* base, uint32_t slot) {
    if (slot != 0x6B18B0 && slot != 0x6B18B0 + 0x158) return;
    uint32_t* section = reinterpret_cast<uint32_t*>(base + slot + 0x10);
    uint32_t v = *section;
    if (v < 20) return;
    *section = 0;
    static std::atomic<int> told{0};
    if (told++ < 3) LOGI("tt: profiler section %08X out of range, taken as 0", v);
    if (told == 1) {
        // what overwrote it: the slot's first 0x40 bytes and the 0x40 before, as text where printable
        for (uint32_t at = slot - 0x40; at < slot + 0x40; at += 16) {
            char hex[64], txt[17];
            for (int i = 0; i < 16; i++) {
                uint8_t b = base[at + i];
                std::snprintf(hex + i * 3, 4, "%02X ", b);
                txt[i] = b >= 32 && b < 127 ? char(b) : '.';
            }
            txt[16] = 0;
            LOGI("tt:   %08X  %s %s", at, hex, txt);
        }
    }
}
void TT_ProfilerStart(X86Context& ctx, uint8_t* base) { ProfilerSection(base, ctx.eax); }  // 0xE60FC, slot in eax
void TT_ProfilerEnd(X86Context& ctx, uint8_t* base) { ProfilerSection(base, ctx.edi); }    // 0xE65B1, slot in edi

// The cause (notes/09-startup-crash.md, "The cause"): the profiler keeps a stack of section numbers, the count at
// 0x6B181C and up to 32 entries from 0x6B1820. 0xE6080 pushes the current section, 0xE6AC0 pops one and starts it
// again. Neither is atomic: on the Xbox's one core two threads rarely met inside them, but here guest threads run
// side by side, so the count could be lost or go below 0, and the pop then read a "section" from the data before
// the stack (a time stamp, or text). The profiler's four routines now run one thread at a time.
#include <mutex>
#include "cpu/x86_ops.h"

X86_EXTERN_FUNC(__imp__sub_000E6080);
X86_EXTERN_FUNC(__imp__sub_000E60B0);
X86_EXTERN_FUNC(__imp__sub_000E6540);
X86_EXTERN_FUNC(__imp__sub_000E6AC0);

namespace {
std::recursive_mutex g_profiler;  // recursive: the pop calls the section start
}

X86_FUNC(sub_000E6080) {  // push the current section
    std::lock_guard<std::recursive_mutex> lock(g_profiler);
    uint32_t& count = *reinterpret_cast<uint32_t*>(base + 0x6B181C);
    if (count >= 32) {  // a full stack would write over the next variables
        static std::atomic<int> told{0};
        if (told++ < 3) LOGI("tt: profiler stack full (%u), push skipped", count);
        return;
    }
    __imp__sub_000E6080(ctx, base);
}
X86_FUNC(sub_000E6AC0) {  // pop a section and start it
    std::lock_guard<std::recursive_mutex> lock(g_profiler);
    uint32_t& count = *reinterpret_cast<uint32_t*>(base + 0x6B181C);
    if (count == 0 || count > 32) {  // nothing pushed: section 0, as the guard above would take it
        static std::atomic<int> told{0};
        if (told++ < 3) LOGI("tt: profiler stack empty (%u), section 0 started", count);
        count = 1;
        *reinterpret_cast<uint32_t*>(base + 0x6B1820) = 0;
    }
    __imp__sub_000E6AC0(ctx, base);
}
X86_FUNC(sub_000E60B0) {  // start a section
    std::lock_guard<std::recursive_mutex> lock(g_profiler);
    __imp__sub_000E60B0(ctx, base);
}
X86_FUNC(sub_000E6540) {  // end a section, and at the frame's end swap the slots
    std::lock_guard<std::recursive_mutex> lock(g_profiler);
    __imp__sub_000E6540(ctx, base);
}
