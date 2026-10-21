// FOTR Xbox: instruction hooks ([[hook]] entries in config/hooks.toml).
#include <cstdint>

#include "core/log.h"
#include "cpu/x86_context.h"

// XapiInitProcess: the Xbox drive authentication (0x75A28, SCSI pass-through 0x4D014) fails on PC, which on the
// console ends in XapiBootToDash (0x75B7E -> 0x75BC9). Startup continues at 0x75B83 instead, so XAPI maps T:/U:,
// mounts Z: and finishes its own initialisation. As in The Two Towers' and The Return of the King's ports.
bool FOTR_DiscCheckFailed(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    LOGI("fotr: no Xbox drive, continuing XAPI startup");
    return true;
}
