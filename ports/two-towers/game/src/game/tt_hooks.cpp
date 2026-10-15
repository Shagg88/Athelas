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
