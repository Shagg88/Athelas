// ROTK Xbox: instruction hooks ([[hook]] entries in config/hooks.toml).
#include <cstdint>

#include "core/log.h"
#include "cpu/x86_context.h"

// XapiInitProcess: the Xbox drive authentication (0x121C84, SCSI pass-through 0x4D014) fails on PC, which on the
// console ends in XapiBootToDash (0x121EBE -> 0x121EE4). Startup continues at 0x121EC3 instead, so XAPI maps
// T:/U:, mounts Z: and finishes its own initialisation. Same as The Two Towers' port (XDK 4928, 0xFE4C4).
bool ROTK_DiscCheckFailed(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    LOGI("rotk: no Xbox drive, continuing XAPI startup");
    return true;
}
