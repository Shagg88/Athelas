// ROTK Xbox: temporary diagnostics for bring-up (removed once the game runs).
#include <cstdint>

#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

using kernel::R32;

// loader completion callback (cdecl: bytes, error, ...)
X86_EXTERN_FUNC(__imp__sub_000F29D0);
X86_FUNC(sub_000F29D0) {
    uint32_t esp = ctx.esp;
    LOGI("diag: F29D0 cb bytes %08X err %08X cur %08X slot %u", R32(esp + 4), R32(esp + 8), R32(0x1d93c8), R32(0x1d9964));
    __imp__sub_000F29D0(ctx, base);
}
// queue a read (cdecl: handle, buffer, bytes, offset, callback)
X86_EXTERN_FUNC(__imp__sub_000BFA30);
X86_FUNC(sub_000BFA30) {
    uint32_t esp = ctx.esp;
    LOGI("diag: BFA30 read h %08X buf %08X n %08X off %08X cb %08X", R32(esp + 4), R32(esp + 8), R32(esp + 12),
         R32(esp + 16), R32(esp + 20));
    __imp__sub_000BFA30(ctx, base);
    LOGI("diag: BFA30 -> %08X", ctx.eax);
}
