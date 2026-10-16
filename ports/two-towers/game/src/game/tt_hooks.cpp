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
}
void TT_ProfilerStart(X86Context& ctx, uint8_t* base) { ProfilerSection(base, ctx.eax); }  // 0xE60FC, slot in eax
void TT_ProfilerEnd(X86Context& ctx, uint8_t* base) { ProfilerSection(base, ctx.edi); }    // 0xE65B1, slot in edi
